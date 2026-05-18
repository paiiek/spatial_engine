// core/src/ipc/OSCBackend.cpp
// JUCE-free stub path active when SPE_HAVE_JUCE is not defined or 0.

#include "ipc/OSCBackend.h"

#include <chrono>
#include <cstring>

#if defined(SPE_HAVE_JUCE) && SPE_HAVE_JUCE
// ---- JUCE path (compiled when JUCE submodule is present) -------------------
// Full UDP receive/send via juce::OSCReceiver / juce::OSCSender.
// OSC bytes are decoded on the JUCE message thread, then forwarded to sink_.
// The audio thread never touches this class.
//
// v0.5.1 Q1: the JUCE wire path is still deferred to v1+. The outbound
// reply ring and last_peer_endpoint_ members compile in (they live in the
// header, allocated on construction) but neither dispatch() nor the
// reply drain thread exercises any UDP I/O here. This keeps the JUCE
// build symbol-compatible with the stub path without committing to a
// JUCE OSC sender pipeline that the v0.5.1 hotfix scope does not need.

#include <juce_osc/juce_osc.h>

namespace spe::ipc {

void OSCBackend::start() {
    running_.store(true, std::memory_order_release);
    // JUCE receiver would be started here when listener port is set.
    // Implementation deferred to v1+ (requires JUCE message loop).
}

void OSCBackend::stop() {
    running_.store(false, std::memory_order_release);
}

void OSCBackend::dispatch(Command const& cmd) {
    // Encode to bytes and send via JUCE sender.
    // Deferred to v1+: requires juce::OSCSender::send().
    (void)cmd;
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet) noexcept {
    Command cmd = decoder_.decode(packet);
    if (cmd.tag != CommandTag::Unknown && sink_) {
        sink_(cmd);
    }
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet,
                              const struct sockaddr*   peer,
                              socklen_t                peer_len) noexcept {
    if (peer && peer_len > 0
        && peer_len <= static_cast<socklen_t>(sizeof(last_peer_endpoint_))) {
        std::memcpy(&last_peer_endpoint_, peer, peer_len);
        last_peer_len_.store(peer_len, std::memory_order_release);
    }
    injectPacket(packet);
}

void OSCBackend::overridePeerPort(uint16_t /*new_port*/) noexcept {}

bool OSCBackend::sendReply(const char*, const char*, const char*) noexcept {
    // JUCE-path sender deferred to v1+. Always reports drop so callers
    // observe the (current) inability to talk back over UDP.
    outbound_drops_.fetch_add(1, std::memory_order_relaxed);
    return false;
}
bool OSCBackend::sendReply(const char*, const char*, const char*, float) noexcept {
    outbound_drops_.fetch_add(1, std::memory_order_relaxed);
    return false;
}
bool OSCBackend::sendReply(const char*, const char*, int32_t) noexcept {
    outbound_drops_.fetch_add(1, std::memory_order_relaxed);
    return false;
}
std::size_t OSCBackend::encodeOscReply(uint8_t*, std::size_t, const char*,
                                       const char*, const char*, bool, float,
                                       bool, int32_t) noexcept {
    return 0;
}
void OSCBackend::outboundDrainLoop() {}

} // namespace spe::ipc

#else
// ---- JUCE-free stub path ---------------------------------------------------
// POSIX UDP listener when listen_port_ > 0; in-process dispatch for tests.

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <array>
#include <thread>
#include <cstdio>
#include <mutex>

namespace spe::ipc {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void OSCBackend::start() {
    running_.store(true, std::memory_order_release);
    if (listen_port_ <= 0) return; // no UDP in test mode

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    // 100ms receive timeout for clean shutdown
    struct timeval tv{0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(listen_port_));

    // v0.5.1 Sec H2 — bind to the configured address (default loopback).
    // Invalid strings fall back to loopback with a stderr warning so the
    // listener still comes up safely; misconfigured operators do not silently
    // get exposed via INADDR_ANY.
    if (::inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr,
            "[OSCBackend] WARNING: invalid bind address '%s', falling back to 127.0.0.1\n",
            bind_addr_.c_str());
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return;
    }
    udp_fd_ = fd;

    // Record the post-bind address+port for test/telemetry consumers.
    {
        struct sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&bound),
                          &bound_len) == 0) {
            char ipbuf[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &bound.sin_addr, ipbuf, sizeof(ipbuf));
            std::lock_guard<std::mutex> lk(bound_mutex_);
            bound_addr_ = ipbuf;
            bound_port_ = ntohs(bound.sin_port);
        }
    }

    udp_thread_ = std::thread([this]() {
        std::array<uint8_t, 65536> buf{};
        while (running_.load(std::memory_order_acquire)) {
            // v0.5.1 Q1: use recvfrom() so the sender's (ip,port) is captured
            // into last_peer_endpoint_. The reply path (sendReply) targets
            // either that source port (legacy WM-2 sub-case) or, when the
            // client supplied reply_port in /sys/handshake, the explicit
            // reply socket on the same host.
            //
            // v0.5.1 hotfix (security-reviewer M1): the peer endpoint is now
            // captured ONLY when decode() recognises the packet (cmd.tag !=
            // Unknown). This prevents an on-path attacker from racing
            // legitimate clients with a 4-byte garbage datagram to hijack
            // the reply destination — only well-formed handshakes/commands
            // can promote a sender into last_peer_endpoint_.
            struct sockaddr_storage peer{};
            socklen_t peer_len = sizeof(peer);
            ssize_t n = recvfrom(udp_fd_, buf.data(), buf.size(), 0,
                                 reinterpret_cast<struct sockaddr*>(&peer),
                                 &peer_len);
            if (n > 0 && running_.load(std::memory_order_acquire)) {
                std::span<const uint8_t> packet(buf.data(),
                                                static_cast<size_t>(n));
                Command cmd = decoder_.decode(packet);
                if (cmd.tag != CommandTag::Unknown) {
                    if (peer_len > 0
                        && peer_len <= static_cast<socklen_t>(sizeof(last_peer_endpoint_))) {
                        std::memcpy(&last_peer_endpoint_, &peer, peer_len);
                        last_peer_len_.store(peer_len, std::memory_order_release);
                    }
                    if (sink_) sink_(cmd);
                }
                // Malformed / unknown packets are dropped silently without
                // mutating last_peer_endpoint_.
            }
        }
    });

    // Outbound drain thread (Q1) — drives sendto() from the SPSC ring.
    // Created unconditionally when start() opens the socket so sendReply()
    // is wired the moment the listener is up.
    drain_thread_ = std::thread([this]() { outboundDrainLoop(); });
}

void OSCBackend::stop() {
    running_.store(false, std::memory_order_release);
    if (udp_fd_ >= 0) {
        ::shutdown(udp_fd_, SHUT_RDWR); // unblock recvfrom() promptly
        close(udp_fd_);
        udp_fd_ = -1;
    }
    // v0.5.1 final polish — wake the drain thread out of its cv wait so
    // it can observe running_=false and exit promptly. Lock + notify so
    // the wake-up race-window between predicate check and wait is closed.
    {
        std::lock_guard<std::mutex> lk(out_cv_mutex_);
    }
    out_cv_.notify_all();
    if (udp_thread_.joinable()) udp_thread_.join();
    if (drain_thread_.joinable()) drain_thread_.join();
    {
        std::lock_guard<std::mutex> lk(bound_mutex_);
        bound_addr_.clear();
        bound_port_ = 0;
    }
}

// ---------------------------------------------------------------------------
// dispatch / inject
// ---------------------------------------------------------------------------

void OSCBackend::dispatch(Command const& cmd) {
    if (!running_.load(std::memory_order_acquire)) return;
    // Encode to OSC bytes using the active dialect, then decode back.
    std::vector<uint8_t> buf;
    if (decoder_.encode(cmd, buf, dialect_)) {
        Command decoded = decoder_.decode(std::span<const uint8_t>(buf));
        if (decoded.tag != CommandTag::Unknown && sink_) {
            sink_(decoded);
        }
    } else if (sink_) {
        // For tags that encode() doesn't support in this dialect, forward directly.
        sink_(cmd);
    }
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet) noexcept {
    Command cmd = decoder_.decode(packet);
    if (cmd.tag != CommandTag::Unknown && sink_) {
        sink_(cmd);
    }
}

void OSCBackend::injectPacket(std::span<const uint8_t> packet,
                              const struct sockaddr*   peer,
                              socklen_t                peer_len) noexcept {
    if (peer && peer_len > 0
        && peer_len <= static_cast<socklen_t>(sizeof(last_peer_endpoint_))) {
        std::memcpy(&last_peer_endpoint_, peer, peer_len);
        last_peer_len_.store(peer_len, std::memory_order_release);
    }
    injectPacket(packet);
}

// v0.5.1 hotfix (security-reviewer H1) — reply_port validation.
//
// Threat model: PayloadSysHandshake.reply_port is honoured by the engine to
// retarget /sys/binaural_warning + /sys/binaural_status emissions. Without
// validation, an off-host attacker who can spoof a source IP could send a
// forged handshake with reply_port=<victim_port> and turn the engine into a
// UDP reflection amplifier aimed at an arbitrary host:port victim (DNS
// resolvers on :53, telemetry collectors, etc).
//
// Mitigations enforced here:
//   1. Reject system / privileged ports (<1024) entirely. Telemetry has no
//      reason to target a privileged service port.
//   2. Require the captured peer IP to be loopback. Spatial Engine is a
//      single-host IPC surface (host DAW ↔ engine); a non-loopback peer
//      MUST NOT be permitted to redirect telemetry to a third party. If a
//      future networked deployment lifts this restriction, the override
//      gate needs to be paired with an authentication step.
//   3. Reject 0 (the legacy "use sender's source port" sentinel — callers
//      shouldn't be calling overridePeerPort() with 0 anyway).
void OSCBackend::overridePeerPort(uint16_t new_port) noexcept {
    if (new_port < 1024) return;  // reject system / privileged ports + 0
    const socklen_t len = last_peer_len_.load(std::memory_order_acquire);
    if (len < static_cast<socklen_t>(sizeof(sockaddr_in))) return;
    // Only IPv4 supported in the stub path's recvfrom() (AF_INET listener).
    if (last_peer_endpoint_.ss_family != AF_INET) return;
    auto* sa = reinterpret_cast<struct sockaddr_in*>(&last_peer_endpoint_);
    // Single-host IPC: require the existing peer IP to be loopback before
    // honouring a port override. Prevents off-host attacker from forging a
    // handshake that redirects telemetry to a third-party victim.
    if (sa->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return;
    sa->sin_port = htons(new_port);
}

// ---------------------------------------------------------------------------
// OSC reply encoding
// ---------------------------------------------------------------------------
//
// Minimal OSC 1.0 packet writer for the Q1 reply surface. Supports the three
// type tag forms documented in OSCBackend.h. All multi-byte words are big
// endian per OSC 1.0 §3.

namespace {

// Pad len up to next 4-byte boundary, returning the # padding bytes (1..4).
inline std::size_t padTo4(std::size_t len) noexcept {
    return 4 - (len % 4);
}

// Write str + null + zero padding (OSC string). Returns bytes written or 0
// on overflow.
std::size_t writeOscString(uint8_t* dst, std::size_t cap, const char* s) noexcept
{
    const std::size_t slen = std::strlen(s);
    const std::size_t total = slen + padTo4(slen); // includes >=1 null
    if (total > cap) return 0;
    std::memcpy(dst, s, slen);
    for (std::size_t i = slen; i < total; ++i) dst[i] = 0;
    return total;
}

// Big-endian float32 / int32 writers.
inline std::size_t writeBeFloat(uint8_t* dst, std::size_t cap, float v) noexcept {
    if (cap < 4) return 0;
    uint32_t u; std::memcpy(&u, &v, 4);
    dst[0] = static_cast<uint8_t>((u >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((u >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((u >>  8) & 0xFF);
    dst[3] = static_cast<uint8_t>((u      ) & 0xFF);
    return 4;
}
inline std::size_t writeBeI32(uint8_t* dst, std::size_t cap, int32_t v) noexcept {
    if (cap < 4) return 0;
    const uint32_t u = static_cast<uint32_t>(v);
    dst[0] = static_cast<uint8_t>((u >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((u >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((u >>  8) & 0xFF);
    dst[3] = static_cast<uint8_t>((u      ) & 0xFF);
    return 4;
}

} // namespace

std::size_t OSCBackend::encodeOscReply(uint8_t* dst, std::size_t cap,
                                       const char* addr, const char* types,
                                       const char* s, bool have_f, float f,
                                       bool have_i, int32_t i) noexcept
{
    if (!dst || !addr || !types) return 0;
    std::size_t off = 0;

    // 1) Address string.
    std::size_t n = writeOscString(dst + off, cap - off, addr);
    if (n == 0) return 0;
    off += n;

    // 2) Type tag string (must start with ',' per OSC 1.0).
    if (types[0] != ',') return 0;
    n = writeOscString(dst + off, cap - off, types);
    if (n == 0) return 0;
    off += n;

    // 3) Arguments — order matches OSC 1.0 type tag order.
    for (const char* t = types + 1; *t; ++t) {
        switch (*t) {
        case 's': {
            if (!s) return 0;
            n = writeOscString(dst + off, cap - off, s);
            if (n == 0) return 0;
            off += n;
            break;
        }
        case 'f': {
            if (!have_f) return 0;
            n = writeBeFloat(dst + off, cap - off, f);
            if (n == 0) return 0;
            off += n;
            break;
        }
        case 'i': {
            if (!have_i) return 0;
            n = writeBeI32(dst + off, cap - off, i);
            if (n == 0) return 0;
            off += n;
            break;
        }
        default:
            return 0; // unsupported type tag for Q1 surface
        }
    }
    return off;
}

// ---------------------------------------------------------------------------
// sendReply — control-thread enqueue → IO-thread drain.
// ---------------------------------------------------------------------------

namespace {

// v0.5.1 hotfix (code-MINOR — multi-producer safety):
//
// sendReply() is documented as control-thread-only but in practice is invoked
// from at least three producer threads in the VST3 build:
//   * Control thread        — probe + handshake telemetry
//   * Audio thread          — no-SOFA latch drain (process())
//   * IO / heartbeat thread — 1 Hz status heartbeat
//
// The original two-phase pattern (load head → fill slot → store head+1) is
// only safe under a single producer; with concurrent producers, two threads
// can read the same `head`, both write into slot[head], and both publish
// head+1, corrupting one packet and double-advancing nothing.
//
// Replace with a CAS-based reservation: each producer CAS-advances head to
// claim a unique slot index, then writes its payload, then publishes
// readiness through a per-slot atomic seq counter. The consumer (single IO
// drain thread) reads tail, awaits slot.ready==true with acquire ordering,
// drains, then advances tail. ready is reset to false in the same step so
// the slot can be reused on the next ring wrap.

// CAS-based head reservation. Returns slot index on success or SIZE_MAX on
// ring full. Safe across multiple concurrent producers.
std::size_t claimSlotCAS(std::atomic<std::size_t>& head,
                         const std::atomic<std::size_t>& tail,
                         std::size_t cap) noexcept {
    auto h = head.load(std::memory_order_relaxed);
    while (true) {
        const auto next = (h + 1) % cap;
        if (next == tail.load(std::memory_order_acquire)) {
            return static_cast<std::size_t>(-1); // full
        }
        if (head.compare_exchange_weak(h, next,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            return h;
        }
        // h was updated by another producer; loop with refreshed head.
    }
}

} // namespace

// v0.6 #8 — single source of truth for outbound enqueue. The 3 public
// sendReply() overloads are 3-line forwarders below.
bool OSCBackend::sendReplyImpl(const char* addr, const char* types,
                               const char* s, bool have_f, float f,
                               bool have_i, int32_t i) noexcept
{
    if (last_peer_len_.load(std::memory_order_acquire) == 0) {
        outbound_drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const std::size_t idx =
        claimSlotCAS(out_head_, out_tail_, kOutboundRingCap);
    if (idx == static_cast<std::size_t>(-1)) {
        outbound_drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    auto& slot = outbound_ring_[idx];
    const std::size_t n = encodeOscReply(slot.buf.data(), slot.buf.size(),
                                          addr, types, s, have_f, f,
                                          have_i, i);
    if (n == 0) {
        // Slot was reserved but payload encode failed — publish an empty
        // ready slot so the consumer can drain past it (a hole in the ring
        // is worse than a no-op send). Count as drop.
        slot.len      = 0;
        slot.dest_len = 0;
        slot.ready.store(true, std::memory_order_release);
        outbound_drops_.fetch_add(1, std::memory_order_relaxed);
        out_cv_.notify_one();
        return false;
    }
    slot.len      = static_cast<uint16_t>(n);
    slot.dest_len = last_peer_len_.load(std::memory_order_acquire);
    std::memcpy(&slot.dest, &last_peer_endpoint_, slot.dest_len);
    slot.ready.store(true, std::memory_order_release);
    // v0.5.1 final polish — wake the drain thread. The release-store on
    // ready happens-before notify_one (single-thread visibility); the
    // consumer's wait predicate acquire-loads head/tail and ready, so the
    // ordering is preserved without holding out_cv_mutex_ across the store.
    out_cv_.notify_one();
    return true;
}

bool OSCBackend::sendReply(const char* addr, const char* types,
                            const char* s) noexcept
{
    return sendReplyImpl(addr, types, s, false, 0.f, false, 0);
}

bool OSCBackend::sendReply(const char* addr, const char* types,
                            const char* s, float f) noexcept
{
    return sendReplyImpl(addr, types, s, true, f, false, 0);
}

bool OSCBackend::sendReply(const char* addr, const char* types,
                            int32_t i) noexcept
{
    return sendReplyImpl(addr, types, nullptr, false, 0.f, true, i);
}

// ---------------------------------------------------------------------------
// Outbound drain loop — IO thread, runs alongside udp_thread_.
// ---------------------------------------------------------------------------

void OSCBackend::outboundDrainLoop() {
    // Reuse the listening socket for sendto() — its source port is bound to
    // listen_port_ so peers see the engine's canonical port, which makes
    // recvfrom() routing trivial on the client side. Falls back to a fresh
    // socket if the listening fd was somehow closed (defensive).
    //
    // v0.5.1 hotfix (multi-producer): consumer must wait for slot.ready (the
    // per-slot RELEASE flag) before reading slot.{buf,len,dest,dest_len}.
    // Under multi-producer, head can be advanced ahead of the actual fill
    // because the CAS commits the reservation before the payload is written.
    //
    // v0.5.1 final polish (perf): block on out_cv_ instead of polling every
    // 5 ms. sendReply() notifies the cv after publishing slot.ready=true;
    // stop() notifies on shutdown. 1 s timeout is a safety belt against
    // missed wake-ups so a stuck producer can't strand a slot forever.
    while (running_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lk(out_cv_mutex_);
            out_cv_.wait_for(lk, std::chrono::seconds(1), [this] {
                if (!running_.load(std::memory_order_acquire)) return true;
                const std::size_t h = out_head_.load(std::memory_order_acquire);
                const std::size_t t = out_tail_.load(std::memory_order_acquire);
                return h != t;
            });
        }
        if (!running_.load(std::memory_order_acquire)) break;

        // Drain everything currently in the ring (in tail order).
        while (true) {
            const std::size_t t = out_tail_.load(std::memory_order_relaxed);
            const std::size_t h = out_head_.load(std::memory_order_acquire);
            if (t == h) break; // empty

            auto& slot = outbound_ring_[t];
            // Wait for the producer to publish a fully-written slot. If the
            // slot isn't ready yet (a producer claimed the slot but hasn't
            // finished encoding), break out of the inner drain and re-poll
            // after the sleep — preserves FIFO order across producers.
            if (!slot.ready.load(std::memory_order_acquire)) break;

            int send_fd = udp_fd_;
            bool local_fd = false;
            if (send_fd < 0) {
                send_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
                local_fd = true;
            }
            if (send_fd >= 0) {
                // v0.5.1 final polish (Fix 2 — AF_UNSPEC guard): validate
                // sockaddr family + length BEFORE sendto(). Production
                // can't reach this branch today (recvfrom only stores
                // AF_INET peers per M1 fix), but a test injectPacket()
                // with a non-AF_INET peer would otherwise EINVAL silently
                // and inflate outbound_sent_.
                const bool family_ok =
                    (slot.dest.ss_family == AF_INET) &&
                    (slot.dest_len >= static_cast<socklen_t>(sizeof(sockaddr_in)));
                if (slot.len > 0 && slot.dest_len > 0 && family_ok) {
                    const ssize_t sent = ::sendto(
                        send_fd, slot.buf.data(), slot.len, 0,
                        reinterpret_cast<const struct sockaddr*>(&slot.dest),
                        slot.dest_len);
                    if (sent < 0) {
                        // sendto failed (ECONNREFUSED, EAGAIN, …). Count
                        // as a drop rather than inflating outbound_sent_;
                        // outbound_sent_ now means "confirmed handed to
                        // the kernel".
                        outbound_drops_.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        outbound_sent_.fetch_add(1, std::memory_order_relaxed);
                    }
                } else if (slot.len > 0 && slot.dest_len > 0 && !family_ok) {
                    // Bad family / short sockaddr — refuse to call sendto.
                    outbound_drops_.fetch_add(1, std::memory_order_relaxed);
                }
                // (slot.len == 0 — producer recorded an encode-failure drop
                //  and published an empty slot; nothing to send. Drop already
                //  accounted at enqueue time.)
                if (local_fd) ::close(send_fd);
            } else {
                outbound_drops_.fetch_add(1, std::memory_order_relaxed);
            }
            // v0.6 #9 — tighten the ready/tail ordering for weakly-ordered
            // hardware (ARM/ppc). The clear MUST be release so that:
            //   (a) by the time the wrap-producer CAS-claims this slot and
            //       writes its own ready=true (release), our clear has
            //       fully propagated — no stale ready=false store can later
            //       overwrite the producer's published true.
            //   (b) the consumer's subsequent acquire-load on ready (next
            //       iteration when the slot is republished) sees the
            //       producer's data writes happens-before the producer's
            //       ready=true, not our stale slot contents.
            // The tail release-store still serializes the prior reads of
            // slot.{buf,dest_len} so the wrap-producer's CAS-acquire on
            // tail observes the consumer's drain complete.
            slot.ready.store(false, std::memory_order_release);
            out_tail_.store((t + 1) % kOutboundRingCap,
                             std::memory_order_release);
        }
    }
}

} // namespace spe::ipc
#endif
