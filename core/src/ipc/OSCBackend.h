// core/src/ipc/OSCBackend.h
// OSC transport backend. Two compile paths:
//   SPE_HAVE_JUCE=1 : juce::OSCReceiver + juce::OSCSender, SPSC FIFO crossing.
//   SPE_HAVE_JUCE=0 : In-memory dispatch stub (wire I/O deferred to v1+).
// Audio thread never crosses this boundary.

#pragma once
#include "ExternalControl.h"
#include "CommandDecoder.h"

#include <sys/socket.h>      // sockaddr_storage, socklen_t (POSIX only — JUCE
                             // path uses the same struct as an opaque blob).
#include <netinet/in.h>      // sockaddr_in (testPeerEndpointPort accessor).
#include <arpa/inet.h>       // ntohs (testPeerEndpointPort accessor).

#include <functional>
#include <span>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace spe::ipc {

// ─────────────────────────────────────────────────────────────────────────
// v0.5.1 final polish — Sec H2 (network exposure default).
//
// Threat model: OSCBackend exposes the engine's entire command surface
// (/sys/load_layout, /sys/binaural_sofa with filesystem paths, etc) over
// unauthenticated UDP. Prior to this hotfix the inbound socket bound to
// INADDR_ANY (0.0.0.0), making the command surface reachable from any LAN
// interface. Any host on the same broadcast domain could:
//   * Drive arbitrary layout / SOFA path loads (path traversal via the
//     filesystem APIs that consume those paths).
//   * Steer telemetry once H1's loopback-required override gate gets
//     paired with a forged sender IP.
//
// Mitigation: bind defaults to 127.0.0.1 (loopback). Operators opting into
// LAN-wide exposure must pass --osc-bind 0.0.0.0 (or a specific NIC
// address) on the standalone CLI and acknowledge the warning. Configure
// via setBindAddr() before start().
// ─────────────────────────────────────────────────────────────────────────

class OSCBackend final : public ExternalControl {
public:
    // Callback invoked on control thread with each decoded Command.
    using CommandSink = std::function<void(const Command&)>;

    explicit OSCBackend(CommandSink sink, int listen_port = 0)
        : sink_(std::move(sink)), listen_port_(listen_port),
          bind_addr_("127.0.0.1") {
        std::memset(&last_peer_endpoint_, 0, sizeof(last_peer_endpoint_));
        last_peer_len_.store(0, std::memory_order_relaxed);
    }
    ~OSCBackend() override { stop(); }

    // v0.5.1 Sec H2 — configure the inbound UDP bind address. Default is
    // "127.0.0.1" (loopback only). Pass "0.0.0.0" to bind every interface
    // (required for cross-machine deployments — OPERATOR ACKNOWLEDGES OSC
    // IS UNAUTHENTICATED). Must be called before start(); a call after
    // start() takes effect on the next start() invocation. Invalid IPv4
    // strings fall back to loopback with a stderr warning at start().
    void setBindAddr(const std::string& addr) { bind_addr_ = addr; }
    const std::string& bindAddr() const noexcept { return bind_addr_; }

    // ExternalControl interface.
    // dispatch(): encode cmd to OSC bytes and (JUCE path) send via UDP;
    //             (stub path) route directly to sink_ for in-process testing.
    void dispatch(Command const& cmd) override;
    void start() override;
    void stop()  override;

    // Inject raw OSC bytes — for tests or internal loopback. The peer-addr
    // overload lets tests simulate a recvfrom() capture so the outbound
    // reply path can be exercised in-process.
    void injectPacket(std::span<const uint8_t> packet) noexcept;
    void injectPacket(std::span<const uint8_t> packet,
                      const struct sockaddr* peer,
                      socklen_t               peer_len) noexcept;

    // In-process Command 주입 (encoder 우회) — Phase C C2 §15.A.
    // Critic R3 권고: cmd.tag != Unknown guard 로 malformed Command 차단.
    void injectCommand(Command const& cmd) noexcept {
        if (cmd.tag != CommandTag::Unknown && sink_) sink_(cmd);
    }

    CommandDecoder& decoder() noexcept { return decoder_; }

    // Set wire dialect for encode() (--osc-dialect CLI flag).
    // Default is Legacy; call setDialect(WireDialect::AdmV1) for ADM-OSC output.
    void setDialect(WireDialect d) noexcept { dialect_ = d; }
    WireDialect dialect() const noexcept { return dialect_; }

    // ─────────────────────────────────────────────────────────────────────
    // v0.5.1 Q1 — outbound engine→client reply channel.
    //
    // sendReply() builds a minimal OSC packet (address + typetag + args) into
    // a pre-allocated SPSC ring slot and signals the IO drain thread. CALLABLE
    // FROM THE CONTROL THREAD ONLY (allocation-free up to the ring slot copy;
    // the IO thread performs the sendto()).
    //
    // Supported typetag forms (kept intentionally small for Q1):
    //   ",s"   const char*                          — e.g. "no_sofa_loaded"
    //   ",sf"  const char*, float                   — e.g. probe throughput
    //   ",i"   int32                                — e.g. failures counter
    //
    // v0.5.1 hotfix (code-MINOR): safe to call from multiple producer threads.
    // CAS-based head advance prevents enqueue races between control / audio /
    // IO heartbeat producers; a per-slot ready flag gates the consumer so a
    // partially-filled slot is never drained. The consumer (IO-thread drain)
    // remains single-threaded.
    //
    // Returns false when the ring is full (drop-on-full) or when the backend
    // has no last_peer_endpoint_ captured yet (no client has talked to us).
    bool sendReply(const char* addr, const char* types, const char* s) noexcept;
    bool sendReply(const char* addr, const char* types,
                   const char* s, float f) noexcept;
    bool sendReply(const char* addr, const char* types, int32_t i) noexcept;

    // Test-only accessor — returns true iff a peer endpoint has been captured
    // (either via recvfrom() or injectPacket(packet, peer, len)).
    bool hasPeerEndpoint() const noexcept {
        return last_peer_len_.load(std::memory_order_acquire) > 0;
    }

    // v0.5.1 hotfix (security tests) — read the captured peer endpoint's
    // port in host byte order. Returns 0 when no peer captured or family is
    // not AF_INET. Test-only; reads non-atomic sockaddr bytes so must not
    // race with the UDP listener thread (tests inject peer via
    // injectPacket() or wait until hasPeerEndpoint() is stable).
    uint16_t testPeerEndpointPort() const noexcept {
        if (last_peer_len_.load(std::memory_order_acquire) == 0) return 0;
        if (last_peer_endpoint_.ss_family != AF_INET) return 0;
        const auto* sa =
            reinterpret_cast<const struct sockaddr_in*>(&last_peer_endpoint_);
        return ntohs(sa->sin_port);
    }

    // v0.5.1 Q1 (WM-2) — override the captured peer endpoint's port. Used
    // when a handshake supplies an explicit reply_port; the engine wants
    // future sendReply() calls to target that explicit port on the same
    // host (instead of the sender's ephemeral source port). Control-thread
    // only. No-op when no peer endpoint has been captured yet, or when
    // new_port == 0.
    void overridePeerPort(uint16_t new_port) noexcept;

    // Number of outbound packets successfully sent so far (test/telemetry).
    std::uint64_t outboundSent() const noexcept {
        return outbound_sent_.load(std::memory_order_relaxed);
    }
    std::uint64_t outboundDrops() const noexcept {
        return outbound_drops_.load(std::memory_order_relaxed);
    }

    // v0.5.1 Q3 — test-only ring-depth accessor. Returns the number of slots
    // currently claimed in the outbound SPSC ring (producer-side count of
    // successfully enqueued replies that the IO drain thread has not yet
    // sent). Lets tests verify exact sendReply() success counts when no
    // drain thread is running (listen_port_ == 0). Lock-free.
    std::size_t outboundPending() const noexcept {
        const std::size_t h = out_head_.load(std::memory_order_acquire);
        const std::size_t t = out_tail_.load(std::memory_order_acquire);
        return (h + kOutboundRingCap - t) % kOutboundRingCap;
    }

    // v0.5.1 Q3 — test-only ring-slot peek. Returns a pointer to the OSC
    // bytes of the slot at logical index `i` (0 = oldest pending). Returns
    // nullptr / out_len=0 when `i` is out of range. Lock-free; callers must
    // not race with the drain thread (test usage assumes no drain).
    const uint8_t* outboundPeek(std::size_t i, std::size_t& out_len) const noexcept {
        const std::size_t pending = outboundPending();
        if (i >= pending) { out_len = 0; return nullptr; }
        const std::size_t t = out_tail_.load(std::memory_order_acquire);
        const std::size_t slot = (t + i) % kOutboundRingCap;
        out_len = outbound_ring_[slot].len;
        return outbound_ring_[slot].buf.data();
    }

    // v0.5.1 Q3 — test-only ring drain. Advances tail by `n` slots so a
    // second prepare cycle can refill without overflowing the 16-slot ring.
    // Lock-free; callers must not race with the drain thread.
    // v0.5.1 hotfix (multi-producer): also clears the per-slot ready flag
    // so the slot is reusable on the next producer wrap (the IO drain loop
    // does the same).
    void outboundDrainForTest(std::size_t n) noexcept {
        const std::size_t pending = outboundPending();
        const std::size_t take = (n < pending) ? n : pending;
        const std::size_t t = out_tail_.load(std::memory_order_acquire);
        for (std::size_t k = 0; k < take; ++k) {
            outbound_ring_[(t + k) % kOutboundRingCap]
                .ready.store(false, std::memory_order_relaxed);
        }
        out_tail_.store((t + take) % kOutboundRingCap, std::memory_order_release);
    }

    // v0.5.1 Sec H2 — test/telemetry accessor for the actual bound address
    // as returned by getsockname() AFTER start(). Stored in dotted-quad
    // form. Empty when no UDP socket is currently open.
    std::string boundAddrForTest() const {
        std::lock_guard<std::mutex> lk(bound_mutex_);
        return bound_addr_;
    }
    uint16_t boundPortForTest() const {
        std::lock_guard<std::mutex> lk(bound_mutex_);
        return bound_port_;
    }

private:
    CommandSink    sink_;
    CommandDecoder decoder_;
    WireDialect    dialect_     = WireDialect::Legacy;
    std::atomic<bool> running_{false};
    int            listen_port_ = 0;
    int            udp_fd_      = -1;
    std::thread    udp_thread_;
    std::string    bind_addr_;  // v0.5.1 Sec H2 — default "127.0.0.1"

    // v0.5.1 Sec H2 — actual bound address/port (post-bind), used by tests
    // and operators to confirm the listener landed where it should.
    mutable std::mutex bound_mutex_;
    std::string        bound_addr_;
    uint16_t           bound_port_{0};

    // ─────────────────────────────────────────────────────────────────────
    // v0.5.1 Q1 — peer-endpoint capture + outbound reply ring.
    // ─────────────────────────────────────────────────────────────────────

    // Last sender captured by recvfrom() (or injectPacket with peer). The
    // sockaddr_storage / len pair is written by the IO thread and read by
    // sendReply()'s ring-producer side. Stored as bytes + atomic length so
    // the reader sees a complete blob (acquire on length gates load of bytes).
    struct sockaddr_storage   last_peer_endpoint_{};
    std::atomic<socklen_t>    last_peer_len_{0};

    // Outbound ring entry. Fixed 256-byte slot covers every Q1 wire form.
    // v0.5.1 hotfix (multi-producer): `ready` is true iff a producer has fully
    // written buf/len/dest/dest_len for this slot. The drain consumer must
    // ACQUIRE-load ready before reading any payload field; the producer sets
    // ready last via RELEASE. The consumer clears ready (relaxed) when it
    // advances tail past this slot, so the next ring wrap reuses cleanly.
    struct OutboundPacket {
        std::array<uint8_t, 256> buf;
        uint16_t                 len; // bytes used in buf
        struct sockaddr_storage  dest;
        socklen_t                dest_len;
        std::atomic<bool>        ready{false};
    };
    // SPSC ring (control-thread producer → IO-thread drainer). 16 slots ×
    // 256 bytes ≈ 4 KB; comfortable headroom for 1 Hz status + bursty warns.
    static constexpr std::size_t kOutboundRingCap = 16;
    std::array<OutboundPacket, kOutboundRingCap> outbound_ring_{};
    std::atomic<std::size_t>  out_head_{0}; // producer (sendReply)
    std::atomic<std::size_t>  out_tail_{0}; // consumer (drain thread)
    // outbound_sent_: number of slots the drain loop SUCCESSFULLY pushed
    //   to sendto() with a non-negative return value. EINVAL / EAGAIN /
    //   ECONNREFUSED on sendto() count as drops instead.
    // outbound_drops_: enqueue-time drops (ring full / no peer / encode
    //   failure) PLUS send-time drops (sendto < 0, AF_UNSPEC guard hit).
    std::atomic<std::uint64_t> outbound_sent_{0};
    std::atomic<std::uint64_t> outbound_drops_{0};
    std::thread               drain_thread_;

    // v0.5.1 final polish (perf) — replace the drain-thread 5 ms busy-poll
    // with a condvar wake-up driven by sendReply() (and stop()). 1-second
    // timeout is the safety belt against missed wake-ups; under producer
    // load the cv is notified per enqueue so latency stays in the µs band.
    mutable std::mutex      out_cv_mutex_;
    std::condition_variable out_cv_;

    // Build an OSC packet (address + types + args) into dst. Returns bytes
    // written, or 0 on overflow. Shared by all sendReply() overloads.
    static std::size_t encodeOscReply(uint8_t* dst, std::size_t cap,
                                      const char* addr, const char* types,
                                      const char* s, bool have_f, float f,
                                      bool have_i, int32_t i) noexcept;

    // v0.6 #8 — unified sendReply implementation. The 3 public overloads
    // are now thin forwarders that pass have_s/have_f/have_i flags here.
    // Saves ~70 LOC of near-identical bodies and removes drift risk.
    bool sendReplyImpl(const char* addr, const char* types,
                       const char* s, bool have_f, float f,
                       bool have_i, int32_t i) noexcept;

    // IO-thread drain loop: blocks for a tiny interval, drains outbound_ring_
    // via sendto(). Runs while running_ is true.
    void outboundDrainLoop();
};

} // namespace spe::ipc
