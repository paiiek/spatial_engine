// test_shm_telemetry_emitter.cpp
// ADR 0019 Phase C PCM IPC — PR4 (telemetry).
//
// Drives spe::bin::ShmTelemetryEmitter against a RingHeader-backed fixture +
// a real (listen_port_==0, no-drain) OSCBackend, asserting the emitted
// /sys/warning + /sys/state wire bytes via the outboundPending()/outboundPeek()
// accessors (the test scaffold from test_osc_outbound_reply_smoke.cpp + the
// injected-clock pattern from test_phase_b_sync_handlers.cpp case 11).
//
// Cases: AC-1 (4-code positive) · AC-2 (no-double-emit + underrun coalesce) ·
// AC-2b (underrun 1/s latch at 10 Hz) · AC-3 (clock-domain stale) · AC-4
// (3 state fields on-change + consumerLockWord) · AC-4b (alive/stale co-emit) ·
// AC-4c (detached guard) · AC-5 (state no-peer latch) · AC-5b (warning no-peer
// retry).

#include <new>
#include "audio_io/SharedRingBackend.h"
#include "audio_io/shm/RingHeader.h"
#include "audio_io/shm/SharedMemoryRegion.h"
#include "audio_io/AudioCallback.h"
#include "bin/ShmTelemetryEmitter.h"
#include "ipc/OSCBackend.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>    // shm_unlink
#include <sys/socket.h>
#include <unistd.h>      // getpid

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace spe::audio_io;
using namespace spe::audio_io::shm;
using spe::bin::ShmTelemetryEmitter;
using spe::ipc::OSCBackend;

namespace {

// ── A minimal capture callback (mirror test_shared_ring_backend.cpp) ────────
struct CaptureCallback final : spe::audio_io::AudioCallback {
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void audioBlock(const spe::audio_io::AudioBlock&) override { ++calls; }
    int calls = 0;
};

// ── Unique shm name per fixture ─────────────────────────────────────────────
static std::string unique_shm_name() {
    static std::atomic<int> ctr{0};
    return "/spe_pr4_tel_" + std::to_string(::getpid()) + "_" +
           std::to_string(ctr.fetch_add(1));
}

// Owns a created shm region + writes a RingHeader. Cleans up on destruction.
struct RingFixture {
    std::string        name;
    SharedMemoryRegion region;
    std::uint32_t      channels;
    std::uint32_t      capacity;

    RingFixture(std::uint32_t sample_rate, std::uint32_t block_size,
                std::uint32_t channels_, std::uint32_t capacity_)
        : name(unique_shm_name()), channels(channels_), capacity(capacity_) {
        const std::size_t bytes = total_region_bytes(channels, capacity);
        RegionError err = region.attach(name.c_str(), AttachMode::CreateOrOpen, bytes);
        assert(err == RegionError::Ok);
        RingHeader* h = region.header();
        new (h) RingHeader{};  // in-place value-init (RingHeader has atomics: no memset/assign)
        h->magic           = kSpeRingMagic;
        h->version         = kRingHeaderVersion;
        h->header_size     = kRingHeaderSize;
        h->sample_rate     = sample_rate;
        h->block_size      = block_size;
        h->channels        = channels;
        h->capacity_frames = capacity;
        h->write_idx.store(0, std::memory_order_relaxed);
        h->read_idx.store(0, std::memory_order_relaxed);
        h->producer_heartbeat_ms.store(0, std::memory_order_relaxed);
        h->xrun_count.store(0, std::memory_order_relaxed);
        h->producer_meta_block_pts_ns.store(0, std::memory_order_relaxed);
        h->producer_state.store(static_cast<std::uint32_t>(ProducerState::Streaming),
                                std::memory_order_relaxed);
        h->seq.store(0, std::memory_order_relaxed);
    }
    ~RingFixture() {
        region.detach();
        ::shm_unlink(name.c_str());
    }
    RingHeader* header() { return region.header(); }
};

// ── OSC wire-byte helpers ───────────────────────────────────────────────────
// A buffer "contains" the null-terminated needle as an OSC string segment
// somewhere inside the packet (address / typetag / string-arg are all padded
// null-terminated). memmem-style search is sufficient for the AC assertions.
static bool pkt_contains(const std::uint8_t* buf, std::size_t n, const char* needle) {
    const std::size_t m = std::strlen(needle);
    if (m == 0 || n < m) return false;
    for (std::size_t i = 0; i + m <= n; ++i) {
        if (std::memcmp(buf + i, needle, m) == 0) return true;
    }
    return false;
}

// pkt_contains_str(): search for needle as a null-terminated OSC string
// argument — i.e. the byte sequence needle + '\0' must appear in the packet.
// This prevents a bare single-digit string like "5" from spuriously matching
// a substring of "15", "50", or a random interior byte (MINOR-3).
static bool pkt_contains_str(const std::uint8_t* buf, std::size_t n, const char* needle) {
    const std::size_t m = std::strlen(needle);
    // Search for the (m+1)-byte sequence: needle bytes followed by '\0'.
    if (n < m + 1) return false;
    for (std::size_t i = 0; i + m + 1 <= n; ++i) {
        if (std::memcmp(buf + i, needle, m) == 0 &&
            buf[i + m] == 0) return true;
    }
    return false;
}

// Capture a loopback peer endpoint inside the backend (so sendReply succeeds)
// by injecting a bare `/hb/ping ,d` packet WITH a peer address. A `,d`
// heartbeat captures the endpoint on a successful decode but enqueues NO
// outbound packet (proven by test_phase_b_sync_handlers.cpp case 9), so the
// outbound ring stays clean for our assertions.
static void capturePeer(OSCBackend& osc) {
    // Build /hb/ping ,d <double> packet.
    std::vector<std::uint8_t> pkt;
    auto pushPadded = [&](const char* s) {
        for (const char* p = s; *p; ++p) pkt.push_back(static_cast<std::uint8_t>(*p));
        pkt.push_back(0);
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    pushPadded("/hb/ping");
    pushPadded(",d");
    double d = 1700000000.0;
    std::uint64_t u; std::memcpy(&u, &d, 8);
    for (int sh = 56; sh >= 0; sh -= 8) pkt.push_back(static_cast<std::uint8_t>(u >> sh));

    struct sockaddr_in peer{};
    peer.sin_family      = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer.sin_port        = htons(9999);
    osc.injectPacket(std::span<const std::uint8_t>(pkt),
                     reinterpret_cast<const struct sockaddr*>(&peer), sizeof(peer));
    assert(osc.hasPeerEndpoint());
}

// Collect all pending outbound packets' bytes (drain so the next batch is
// fresh). Returns a vector of byte-buffers; clears the ring afterward.
static std::vector<std::vector<std::uint8_t>> drainOutbound(OSCBackend& osc) {
    std::vector<std::vector<std::uint8_t>> out;
    const std::size_t pending = osc.outboundPending();
    for (std::size_t i = 0; i < pending; ++i) {
        std::size_t len = 0;
        const std::uint8_t* p = osc.outboundPeek(i, len);
        assert(p != nullptr);
        out.emplace_back(p, p + len);
    }
    osc.outboundDrainForTest(pending);
    return out;
}

// Count how many drained packets contain BOTH needles.
static int countWith(const std::vector<std::vector<std::uint8_t>>& pkts,
                     const char* n1, const char* n2 = nullptr) {
    int c = 0;
    for (const auto& pk : pkts) {
        if (pkt_contains(pk.data(), pk.size(), n1) &&
            (n2 == nullptr || pkt_contains(pk.data(), pk.size(), n2))) {
            ++c;
        }
    }
    return c;
}

// Build a started backend over a fixture (manual-pump 2-arg start: no worker).
// engine_block must be a multiple of header block_size.
static std::unique_ptr<SharedRingBackend> startedBackend(RingFixture& fx,
                                                          CaptureCallback& cb,
                                                          int engine_block) {
    auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
    assert(be != nullptr);
    BackendError err = be->start(&cb, engine_block);
    assert(err == BackendError::Ok);
    assert(be->inputChannelCount() >= 1);
    return be;
}

constexpr std::uint64_t kEpoch = 1748000000000ull;  // realistic large unix-ms

// ─────────────────────────────────────────────────────────────────────────
// AC-1 — Edge-emit positive, all 4 codes (D1).
// ─────────────────────────────────────────────────────────────────────────
static void ac1_edge_emit_positive() {
    // -- shm_producer_stale + shm_attached_no_data via poll_diagnostics --
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, /*listen_port=*/0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;

        // Seed tick (write_idx==read_idx==0 → attached_no_data edges in
        // poll_diagnostics; heartbeat fresh so no stale on this tick). First
        // tick seeds warnings SILENT but emits the state snapshot.
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        be->poll_diagnostics(kEpoch, 0);          // attached_no_data++ (latched)
        tel.tick(*be, osc, kEpoch);               // seed-silent warnings
        auto seed_pkts = drainOutbound(osc);
        // No /sys/warning on the seed tick (the attached_no_data count is the
        // baseline, not replayed). State snapshot IS emitted (seed-and-emit).
        assert(countWith(seed_pkts, "/sys/warning") == 0);
        assert(countWith(seed_pkts, "/sys/state") >= 1);

        // Now force a NEW attached_no_data edge is impossible (latched once);
        // instead drive a stale edge: make heartbeat 5 s old.
        be->poll_diagnostics(kEpoch + 5000, 0);   // stale++ (age 5000 ms)
        tel.tick(*be, osc, kEpoch + 5000);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "/sys/warning", "shm_producer_stale") == 1);
        assert(countWith(pkts, ",iis") == 1);
        assert(pkt_contains_str(pkts.front().data(), pkts.front().size(), "5"));  // age seconds, null-terminated
    }
    // -- shm_attached_no_data edge isolated (fresh emitter, first real edge) --
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        // Seed FIRST with no attached_no_data yet: set write_idx!=0 so the
        // once-on-attach latch does NOT fire on seed.
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);   // wi!=0 → no attached_no_data
        tel.tick(*be, osc, kEpoch);
        (void)drainOutbound(osc);
        // Now reset to wi==ri==0 so attached_no_data edges.
        fx.header()->write_idx.store(0, std::memory_order_relaxed);
        fx.header()->read_idx.store(0, std::memory_order_relaxed);
        be->poll_diagnostics(kEpoch + 1000, 0);  // attached_no_data++
        tel.tick(*be, osc, kEpoch + 1000);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "/sys/warning", "shm_attached_no_data") == 1);
        assert(countWith(pkts, "channels=2") == 1);
    }
    // -- shm_producer_pacing edge --
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        // pts0 seeds prev_pts.
        fx.header()->producer_meta_block_pts_ns.store(0, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);  // have_prev_pts seeded (pts==0)
        tel.tick(*be, osc, kEpoch);
        (void)drainOutbound(osc);
        // Big pts jump > one block period (64/48000 s ≈ 1.33 ms ≈ 1.33e6 ns).
        fx.header()->producer_meta_block_pts_ns.store(50000000ull, std::memory_order_relaxed);
        be->poll_diagnostics(kEpoch + 1000, 0);  // pacing++
        tel.tick(*be, osc, kEpoch + 1000);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "/sys/warning", "shm_producer_pacing") == 1);
        assert(countWith(pkts, "pacing_drift") == 1);
    }
    // -- shm_underrun edge (RT path via pump_synchronous, no producer data) --
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        // pump_synchronous starts the backend (acquire_consumer_lock=false).
        // No producer data → every block underruns → underrun_warnings_++.
        BackendError err = be->pump_synchronous(&cb, /*blocks=*/3, /*hw_ts_base=*/0,
                                                /*engine_block=*/64, /*out_channels=*/2);
        assert(err == BackendError::Ok);
        assert(be->underrunWarningCount() >= 1);
        tel.seed(*be);                      // seed-silent: capture the count
        (void)drainOutbound(osc);
        // Bump underrun again with a new pump.
        const unsigned long long before = be->underrunWarningCount();
        err = be->pump_synchronous(&cb, 2, 1000, 64, 2);
        assert(err == BackendError::Ok);
        assert(be->underrunWarningCount() > before);
        tel.tick(*be, osc, kEpoch);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "/sys/warning", "shm_underrun") == 1);
    }
    std::printf("AC-1 PASS: edge_emit_positive (4 codes)\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-2 — No-double-emit + underrun coalesce (PM1/PM4).
// ─────────────────────────────────────────────────────────────────────────
static void ac2_no_double_emit_and_coalesce() {
    // (a) hold all 4 counts constant across 3 ticks → zero warnings after seed.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);  // no attached_no_data
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        tel.seed(*be);
        for (int i = 0; i < 3; ++i) {
            be->poll_diagnostics(kEpoch + i * 10, 0);  // fresh heartbeat → no edges
            tel.tick(*be, osc, kEpoch + i * 10);
        }
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "/sys/warning") == 0);  // zero across all 3 ticks
    }
    // (b) advance stale once, then 2 idle ticks → exactly one packet.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);
        tel.tick(*be, osc, kEpoch);
        (void)drainOutbound(osc);
        // One stale edge (the 30 s source rate-limit means subsequent stale
        // ticks within 30 s won't re-increment the counter → naturally idle).
        be->poll_diagnostics(kEpoch + 5000, 0);   // stale++
        tel.tick(*be, osc, kEpoch + 5000);
        be->poll_diagnostics(kEpoch + 6000, 0);   // within 30 s → no new stale++
        tel.tick(*be, osc, kEpoch + 6000);
        be->poll_diagnostics(kEpoch + 7000, 0);
        tel.tick(*be, osc, kEpoch + 7000);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "shm_producer_stale") == 1);
    }
    // (c) underrun coalesce: +N in one tick → exactly one shm_underrun.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        BackendError err = be->pump_synchronous(&cb, 1, 0, 64, 2);
        assert(err == BackendError::Ok);
        tel.seed(*be);
        // Many underruns between seed and the next tick.
        err = be->pump_synchronous(&cb, 5, 1000, 64, 2);
        assert(err == BackendError::Ok);
        assert(be->underrunWarningCount() >= 6);
        tel.tick(*be, osc, kEpoch);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "shm_underrun") == 1);  // coalesced to one
        // detail = cumulative xrun total (a numeric string present in packet).
        char total[24];
        std::snprintf(total, sizeof(total), "%llu", be->xrunCount());
        assert(pkt_contains(pkts.front().data(), pkts.front().size(), total));
    }
    std::printf("AC-2 PASS: no_double_emit_and_coalesce\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-2b — Underrun 1/s latch, cadence-independent (MJ-1).
// ─────────────────────────────────────────────────────────────────────────
static void ac2b_underrun_1hz_latch() {
    RingFixture fx(48000, 64, 2, 8192);
    CaptureCallback cb;
    auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
    assert(be != nullptr);
    OSCBackend osc([](const spe::ipc::Command&){}, 0);
    capturePeer(osc);
    ShmTelemetryEmitter tel;
    be->pump_synchronous(&cb, 1, 0, 64, 2);
    tel.seed(*be);
    (void)drainOutbound(osc);

    // Drive 25 ticks at simulated 10 Hz (100 ms/tick), underrun++ every tick.
    int total = 0;
    for (int i = 0; i < 25; ++i) {
        be->pump_synchronous(&cb, 1, static_cast<std::uint64_t>(2000 + i * 100), 64, 2);
        const std::uint64_t now = kEpoch + static_cast<std::uint64_t>(i) * 100ull;
        tel.tick(*be, osc, now);
        total += countWith(drainOutbound(osc), "shm_underrun");
    }
    // 25 ticks span 0..2400 ms → windows open at ~0/1000/2000 ms → ≤ 3 emits.
    assert(total >= 1 && total <= 3);

    // Flush any held edge: the last burst emit was 1/s-gated, so prev_underrun_
    // may legitimately lag cur (CR-2 hold). One tick in a fresh open window
    // (≥1000 ms after the last successful send) drains the held edge and
    // advances prev == cur.
    tel.tick(*be, osc, kEpoch + 4000);
    (void)drainOutbound(osc);

    // Now with underrun STATIC (no increment): zero further emits regardless of
    // tick rate (prev_underrun_ == cur → diff is zero).
    const unsigned long long frozen = be->underrunWarningCount();
    int extra = 0;
    for (int i = 0; i < 30; ++i) {
        const std::uint64_t now = kEpoch + 5000ull + static_cast<std::uint64_t>(i) * 100ull;
        tel.tick(*be, osc, now);
        extra += countWith(drainOutbound(osc), "shm_underrun");
    }
    assert(be->underrunWarningCount() == frozen);
    assert(extra == 0);
    std::printf("AC-2b PASS: underrun_1hz_latch (emits=%d, cadence-independent)\n", total);
}

// ─────────────────────────────────────────────────────────────────────────
// AC-3 — Clock-domain stale correctness (D2/PM2).
// ─────────────────────────────────────────────────────────────────────────
static void ac3_clock_domain_stale() {
    // Stale: heartbeat 5 s behind now_unix_ms → edge fires, detail ≈ "5".
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch - 5, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);  // fresh-ish on seed (age 5 ms)
        tel.tick(*be, osc, kEpoch);
        (void)drainOutbound(osc);
        // Now make it 5 s stale.
        fx.header()->producer_heartbeat_ms.store(kEpoch + 0, std::memory_order_relaxed);
        be->poll_diagnostics(kEpoch + 5000, 0);
        tel.tick(*be, osc, kEpoch + 5000);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "shm_producer_stale") == 1);
        assert(pkt_contains_str(pkts.front().data(), pkts.front().size(), "5"));  // null-terminated "5\0"
    }
    // Fresh: heartbeat 10 ms behind → no stale edge.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch - 10, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);
        tel.tick(*be, osc, kEpoch);
        auto pkts = drainOutbound(osc);
        assert(countWith(pkts, "shm_producer_stale") == 0);
    }
    std::printf("AC-3 PASS: clock_domain_stale\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-4 — /sys/state three fields on-change (D3) + consumerLockWord (CR-1).
// ─────────────────────────────────────────────────────────────────────────
static void ac4_state_fields_and_lockword() {
    // consumerLockWord(): 0 on detached/never-attached, holder PID after set.
    {
        // Detached, default-constructed backend → consumerLockWord()==0.
        RingFixture fx(48000, 64, 2, 8192);
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        assert(be->consumerLockWord() == 0);  // attached but lock word unset
        // Set the lock word in the fixture header and assert the accessor reads it.
        consumer_lock_atomic(fx.header())->store(0x1234, std::memory_order_release);
        assert(be->consumerLockWord() == 0x1234);
    }
    // Three state fields on-change.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);
        capturePeer(osc);
        ShmTelemetryEmitter tel;
        // Seed: producer_state=Streaming(1), heartbeat fresh (alive=1), lock 0.
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_state.store(1, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        consumer_lock_atomic(fx.header())->store(0, std::memory_order_release);
        tel.seed(*be);
        tel.tick(*be, osc, kEpoch);
        auto seed_pkts = drainOutbound(osc);
        // Seed-and-emit: all three state keys delivered once.
        assert(countWith(seed_pkts, "/sys/state", "shm_producer_alive=1") == 1);
        assert(countWith(seed_pkts, "/sys/state", "shm_producer_state=1") == 1);
        assert(countWith(seed_pkts, "/sys/state", "shm_consumer_locked=0") == 1);

        // No change → no re-emit.
        tel.tick(*be, osc, kEpoch + 10);
        assert(drainOutbound(osc).empty());

        // producer_state 1→2.
        fx.header()->producer_state.store(2, std::memory_order_relaxed);
        tel.tick(*be, osc, kEpoch + 20);
        auto p1 = drainOutbound(osc);
        assert(countWith(p1, "/sys/state", "shm_producer_state=2") == 1);
        assert(countWith(p1, "shm_producer_alive") == 0);   // unchanged → no re-emit
        assert(countWith(p1, "shm_consumer_locked") == 0);

        // lock word set → shm_consumer_locked=1.
        consumer_lock_atomic(fx.header())->store(0xABCD, std::memory_order_release);
        tel.tick(*be, osc, kEpoch + 30);
        auto p2 = drainOutbound(osc);
        assert(countWith(p2, "/sys/state", "shm_consumer_locked=1") == 1);
    }
    std::printf("AC-4 PASS: state_fields_and_lockword\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-4b — alive/stale co-emit on one tick (fold-in 7a).
// ─────────────────────────────────────────────────────────────────────────
static void ac4b_alive_stale_co_emit() {
    RingFixture fx(48000, 64, 2, 8192);
    CaptureCallback cb;
    auto be = startedBackend(fx, cb, 64);
    OSCBackend osc([](const spe::ipc::Command&){}, 0);
    capturePeer(osc);
    ShmTelemetryEmitter tel;
    fx.header()->write_idx.store(64, std::memory_order_relaxed);
    fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
    tel.seed(*be);
    be->poll_diagnostics(kEpoch, 0);   // fresh on seed (alive=1)
    tel.tick(*be, osc, kEpoch);
    (void)drainOutbound(osc);
    // One stale-heartbeat tick: stale warning edge AND alive flips to 0.
    fx.header()->producer_heartbeat_ms.store(kEpoch + 0, std::memory_order_relaxed);
    be->poll_diagnostics(kEpoch + 5000, 0);  // stale++
    tel.tick(*be, osc, kEpoch + 5000);
    auto pkts = drainOutbound(osc);
    assert(countWith(pkts, "/sys/warning", "shm_producer_stale") == 1);
    assert(countWith(pkts, "/sys/state", "shm_producer_alive=0") == 1);
    // Fresh-heartbeat tick produces neither (alive flips back to 1, no stale++).
    (void)drainOutbound(osc);
    fx.header()->producer_heartbeat_ms.store(kEpoch + 6000, std::memory_order_relaxed);
    be->poll_diagnostics(kEpoch + 6000, 0);   // fresh → no stale++
    tel.tick(*be, osc, kEpoch + 6000);
    auto p2 = drainOutbound(osc);
    assert(countWith(p2, "shm_producer_stale") == 0);
    // alive flips 0→1 so a state packet DOES emit; assert it is alive=1 (not 0).
    assert(countWith(p2, "shm_producer_alive=1") == 1);
    std::printf("AC-4b PASS: alive_stale_co_emit\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-4c — detached-region guard (fold-in 7b).
// ─────────────────────────────────────────────────────────────────────────
static void ac4c_detached_guard() {
    // An attached-but-NOT-started backend has inputChannelCount()==0 and
    // producer_heartbeat_ms()==0 (start() sets in_channels_) — the same
    // observable state as a detached/never-attached region from the emitter's
    // view (the detached proxy, fold-in 7b). A tick with a real now_unix_ms
    // must early-return and emit NEITHER a false stale warning NOR alive=0.
    RingFixture fx(48000, 64, 2, 8192);
    auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
    assert(be != nullptr);
    assert(be->inputChannelCount() == 0);          // never started → detached proxy
    OSCBackend osc([](const spe::ipc::Command&){}, 0);
    capturePeer(osc);
    ShmTelemetryEmitter tel;
    tel.seed(*be);                                  // detached → seed no-ops
    tel.tick(*be, osc, kEpoch);                     // must early-return
    auto pkts = drainOutbound(osc);
    assert(pkts.empty());                           // no warning, no state
    assert(countWith(pkts, "shm_producer_stale") == 0);
    assert(countWith(pkts, "shm_producer_alive") == 0);
    std::printf("AC-4c PASS: detached_guard\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-5 — State retry-on-no-peer latch (PM3).
// ─────────────────────────────────────────────────────────────────────────
static void ac5_state_no_peer_latch() {
    RingFixture fx(48000, 64, 2, 8192);
    CaptureCallback cb;
    auto be = startedBackend(fx, cb, 64);
    OSCBackend osc([](const spe::ipc::Command&){}, 0);  // NO peer captured yet
    assert(!osc.hasPeerEndpoint());
    ShmTelemetryEmitter tel;
    fx.header()->write_idx.store(64, std::memory_order_relaxed);
    fx.header()->producer_state.store(1, std::memory_order_relaxed);
    fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
    // start() acquired the consumer lock (gate 7) → reset to 0 so the asserted
    // value is deterministic (independent of the test process PID).
    consumer_lock_atomic(fx.header())->store(0, std::memory_order_release);

    // Seed-and-emit arms all three latches, but there is NO peer → sendReply
    // returns false → nothing delivered, latches stay armed.
    tel.seed(*be);
    tel.tick(*be, osc, kEpoch);
    assert(osc.outboundPending() == 0);   // no peer → no packet enqueued
    assert(osc.outboundDrops() >= 3);     // three armed latches each dropped

    // Capture a peer; the NEXT tick must deliver the armed snapshot + clear.
    capturePeer(osc);
    tel.tick(*be, osc, kEpoch + 10);
    auto pkts = drainOutbound(osc);
    assert(countWith(pkts, "/sys/state", "shm_producer_state=1") == 1);
    assert(countWith(pkts, "/sys/state", "shm_producer_alive=1") == 1);
    assert(countWith(pkts, "/sys/state", "shm_consumer_locked=0") == 1);

    // A third tick (no change) re-sends nothing.
    tel.tick(*be, osc, kEpoch + 20);
    assert(drainOutbound(osc).empty());
    std::printf("AC-5 PASS: state_no_peer_latch\n");
}

// ─────────────────────────────────────────────────────────────────────────
// AC-5b — /sys/warning no-peer retry (CR-2/PM6).
// ─────────────────────────────────────────────────────────────────────────
static void ac5b_warning_no_peer_retry() {
    // stale edge held across a no-peer window, delivered after peer connects.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = startedBackend(fx, cb, 64);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);  // NO peer
        ShmTelemetryEmitter tel;
        fx.header()->write_idx.store(64, std::memory_order_relaxed);
        fx.header()->producer_heartbeat_ms.store(kEpoch, std::memory_order_relaxed);
        tel.seed(*be);
        be->poll_diagnostics(kEpoch, 0);
        tel.tick(*be, osc, kEpoch);   // seed-silent warnings; no-peer state drops

        // Advance the stale counter once (a warning edge), still no peer.
        be->poll_diagnostics(kEpoch + 5000, 0);  // stale++
        const unsigned long long stale_after = be->staleWarningCount();
        tel.tick(*be, osc, kEpoch + 5000);
        assert(countWith(drainOutbound(osc), "shm_producer_stale") == 0);  // held

        // Two more idle no-peer ticks (within 30 s → no new stale++); held.
        be->poll_diagnostics(kEpoch + 6000, 0);
        tel.tick(*be, osc, kEpoch + 6000);
        be->poll_diagnostics(kEpoch + 7000, 0);
        tel.tick(*be, osc, kEpoch + 7000);
        assert(be->staleWarningCount() == stale_after);  // counter idle
        assert(countWith(drainOutbound(osc), "shm_producer_stale") == 0);

        // Capture a peer → NEXT tick delivers exactly one stale edge.
        capturePeer(osc);
        be->poll_diagnostics(kEpoch + 8000, 0);
        tel.tick(*be, osc, kEpoch + 8000);
        assert(countWith(drainOutbound(osc), "shm_producer_stale") == 1);

        // A following idle tick sends nothing (prev advanced on the delivery).
        be->poll_diagnostics(kEpoch + 9000, 0);
        tel.tick(*be, osc, kEpoch + 9000);
        assert(countWith(drainOutbound(osc), "shm_producer_stale") == 0);
    }
    // underrun edge held + 1/s window resumes from the SUCCESSFUL send.
    {
        RingFixture fx(48000, 64, 2, 8192);
        CaptureCallback cb;
        auto be = SharedRingBackend::attach(fx.name, AttachMode::OpenExisting);
        assert(be != nullptr);
        OSCBackend osc([](const spe::ipc::Command&){}, 0);  // NO peer
        ShmTelemetryEmitter tel;
        be->pump_synchronous(&cb, 1, 0, 64, 2);
        tel.seed(*be);
        // underrun edge with no peer → held.
        be->pump_synchronous(&cb, 1, 1000, 64, 2);
        tel.tick(*be, osc, kEpoch);
        assert(countWith(drainOutbound(osc), "shm_underrun") == 0);  // held
        // Capture peer → delivered exactly once.
        capturePeer(osc);
        tel.tick(*be, osc, kEpoch + 10);
        assert(countWith(drainOutbound(osc), "shm_underrun") == 1);
        // 1/s window now resumes from the SUCCESSFUL send (kEpoch+10): an
        // underrun edge < 1000 ms later is gated.
        be->pump_synchronous(&cb, 1, 2000, 64, 2);
        tel.tick(*be, osc, kEpoch + 500);
        assert(countWith(drainOutbound(osc), "shm_underrun") == 0);  // window closed
        // ≥ 1000 ms after the send → delivered.
        tel.tick(*be, osc, kEpoch + 1100);
        assert(countWith(drainOutbound(osc), "shm_underrun") == 1);
    }
    std::printf("AC-5b PASS: warning_no_peer_retry\n");
}

} // namespace

int main() {
    ac1_edge_emit_positive();
    ac2_no_double_emit_and_coalesce();
    ac2b_underrun_1hz_latch();
    ac3_clock_domain_stale();
    ac4_state_fields_and_lockword();
    ac4b_alive_stale_co_emit();
    ac4c_detached_guard();
    ac5_state_no_peer_latch();
    ac5b_warning_no_peer_retry();
    std::printf("PASS test_shm_telemetry_emitter\n");
    return 0;
}
