// core/src/audio_io/SharedRingBackend.h
//
// ADR 0019 Phase C PCM IPC — PR2 (backend).
//
// Input-only AudioBackend over the PR1 POSIX shared-memory primitives
// (SharedMemoryRegion + RingHeader). A single producer (Python adm_player
// sidecar, PR5) publishes planar f32 PCM into the ring; this backend is the
// single consumer (SPSC), polling the ring at block_size/sample_rate cadence
// and handing planar blocks to the engine's AudioCallback — exactly the way
// NullBackend::thread_loop drives the callback, but reading the ring instead
// of a fixture.
//
// ── Thread model ─────────────────────────────────────────────────────────
//   • Audio thread  : pump_block() — the RT body. ZERO clock reads, ZERO
//                     syscalls, ZERO allocation. Only loads/stores/memcpy/
//                     memset against the mapped pages + pre-allocated staging.
//                     The hw timestamp is a PARAMETER (the worker thread reads
//                     steady_clock BEFORE entering pump_block; the test passes
//                     a synthetic monotonic value) so the body has no clock
//                     read. SPSC ordering contract (load-acquire write_idx at
//                     the top, store-release read_idx at the end) is documented
//                     on pump_block below and is load-bearing — see PM2.
//   • Control thread: poll_diagnostics() — reads header atomics (load-acquire)
//                     + private state, writes ONLY the four warning counters,
//                     the previous-pts cache, and the per-code last-warning
//                     timestamps. NEVER advances read_idx, NEVER touches the
//                     staging buffers, NEVER writes a header atomic.
//   • Exception (PR3, ADR 0019): the consumer-attach-lock word at
//                     kConsumerLockOffset is CAS'd 0→pid in start() (gate 7) and
//                     stored→0 in stop(). This is the ONLY header write outside
//                     read_idx, is CONTROL-THREAD-ONLY (start/stop), and is NEVER
//                     touched on the RT path (pump_block/thread_loop). The
//                     Decision-1 resync store-back to read_idx in prepare() is
//                     likewise control-thread-only (before the worker spawns).
//
// ── attach()/start() contract (ADR 0019 §3.1, plan iter-2 C2) ────────────
//   attach() does ONLY the OS-level mmap (SharedMemoryRegion::attach). It
//   returns nullptr on OS failure and does NOT validate header semantics.
//   ALL header validation lives in start(), which returns a BackendError and
//   performs every gate IN ORDER (see start()). All allocation also happens
//   in start(); on any gate failure start() allocates nothing and leaves
//   isRunning() == false.
//
// ── Diagnostic counters → PR4 OSC mapping (plan Decision 1 / ADR §6) ──────
//   The four control-thread warning counters map 1:1 onto the PR4
//   /sys/warning shm_* codes. PR2 owns the detection + rate-limit POLICY;
//   PR4 owns TRANSPORT (it hooks the same counter edges to emit OSC):
//     underrunWarningCount()       → shm_underrun
//     staleWarningCount()          → shm_producer_stale
//     pacingWarningCount()         → shm_producer_pacing
//     attachedNoDataWarningCount() → shm_attached_no_data
//   This backend does NOT emit OSC.
//
// Linux + macOS (POSIX shm) only — same guard as RingHeader/SharedMemoryRegion.

#pragma once

#include "audio_io/AudioBackend.h"
#include "audio_io/shm/RingHeader.h"
#include "audio_io/shm/SharedMemoryRegion.h"
#include "util/XrunCounter.h"

#if defined(__linux__) || defined(__APPLE__)

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace spe::audio_io {

class SharedRingBackend final : public AudioBackend {
public:
    // Detection / rate-limit policy constants (ADR §6, plan Decision 1/3).
    static constexpr std::uint64_t kStaleThresholdMs        = 100;    // > 100 ms heartbeat age = stale
    static constexpr std::uint64_t kStaleRateLimitMs        = 30000;  // shm_producer_stale once / 30 s
    static constexpr std::uint64_t kPacingRateLimitMs       = 5000;   // shm_producer_pacing once / 5 s

    // Geometry bounds (FIX-2). Enforced in both attach() and prepare() to
    // prevent integer overflow in total_region_bytes() and pathological allocs.
    //   kMaxChannels       — ADR §2.3 "1..64" per-ring channel count.
    //   kMaxCapacityFrames — 2^20 = 1,048,576 frames ≈ 21.8 s @ 48 kHz;
    //     caps the region at ≤ 64 * 2^20 * 4 = 2^28 bytes — generous but well
    //     below the u64 overflow boundary for total_region_bytes().
    static constexpr std::uint32_t kMaxChannels        = 64u;
    static constexpr std::uint32_t kMaxCapacityFrames  = 1u << 20;  // 1,048,576

    // attach(): OS-level mmap ONLY. Returns nullptr on OS failure. Performs
    // NO header validation (that is start()'s job). The caller owns the
    // returned backend; the producer owns the shm lifecycle (we never unlink).
    static std::unique_ptr<SharedRingBackend> attach(const std::string& path,
                                                     shm::AttachMode mode);

    ~SharedRingBackend() override;

    // ── AudioBackend ───────────────────────────────────────────────────
    int          outputChannelCount() const noexcept override { return 0; }  // input-only
    int          inputChannelCount()  const noexcept override { return in_channels_; }
    double       sampleRate()         const noexcept override { return sample_rate_; }
    int          blockSize()          const noexcept override { return block_size_; }
    std::string  description()        const override;

    // start(): validates the header + allocates, IN ORDER:
    //   1. magic == kSpeRingMagic && version == kRingHeaderVersion
    //        else BackendError::DeviceOpenFailed
    //   2. header.block_size > MAX_BLOCK(512)        → BlockSizeExceedsMax
    //   3. header.block_size does not divide engine_block_size,
    //        OR header.block_size > header.capacity_frames → BlockConfigMismatch
    //   4. header.capacity_frames not a power of two  → DeviceOpenFailed
    // On any gate failure: allocate nothing, isRunning() stays false.
    //
    // engine_block_size is the engine's callback block; the header block_size
    // must divide it (ADR §2.3) and the AudioBlock delivered to the callback
    // carries header.block_size frames. The AudioBackend interface fixes the
    // start(callback) signature; the engine block is supplied via the
    // companion overload used in tests + PR3 wiring.
    BackendError start(AudioCallback* callback) override;
    BackendError start(AudioCallback* callback, int engine_block_size);
    // PR3: 3-arg form carries the engine's output channel count so the backend
    // can own the output staging the engine renders into (Decision 3(ii)).
    BackendError start(AudioCallback* callback, int engine_block_size, int out_channels);

    BackendError stop() noexcept override;
    bool         isRunning() const noexcept override { return running_.load(); }
    unsigned long long xrunCount() const noexcept override { return xruns_.total(); }

    // ── Diagnostics accessors (control thread only) ────────────────────
    std::uint64_t producer_heartbeat_ms() const noexcept;
    std::uint32_t producer_state()        const noexcept;

    unsigned long long underrunWarningCount()       const noexcept { return underrun_warnings_; }
    unsigned long long staleWarningCount()          const noexcept { return stale_warnings_; }
    unsigned long long pacingWarningCount()         const noexcept { return pacing_warnings_; }
    unsigned long long attachedNoDataWarningCount() const noexcept { return attached_no_data_warnings_; }

    // poll_diagnostics() — CONTROL THREAD ONLY. Hard signature contract:
    //   READS  : header atomics (producer_heartbeat_ms / producer_state /
    //            producer_meta_block_pts_ns) via load-acquire + private state.
    //   WRITES : ONLY the four warning counters, the previous-pts cache, and
    //            the per-code last-warning timestamps.
    //   NEVER  : advances read_idx, touches staging buffers, or writes any
    //            header atomic.
    // now_ms / now_ns are injected by the caller (mockable clock, mirroring
    // HeartbeatPublisher::tick) so cases 5/6 are deterministic.
    void poll_diagnostics(std::uint64_t now_ms, std::uint64_t now_ns) noexcept;

    // ── RT body + test hooks (plan Decision 2) ─────────────────────────
    // pump_block() — the RT audio body. CONTRACT (SPSC, plan §Decision 2 /
    // PM2): the SINGLE write_idx.load(acquire) at the very top is the
    // synchronizes-with edge that pairs with the producer's release store on
    // write_idx; NO sample memcpy may be hoisted above it (torn-read bug).
    // The read_idx.store(release) at the end publishes the consumed range only
    // after the copy completes. Body contains only loads/stores/memcpy/memset
    // — no clock read (hw_ts_ns is a parameter), no syscall, no allocation.
    void pump_block(AudioCallback* callback, std::uint64_t hw_ts_ns) noexcept;

    // pump_synchronous() — deterministic test hook: drive `blocks` pump_block
    // calls with synthetic monotonic hw timestamps (mirrors
    // NullBackend::pump_synchronous). No thread, no sleep. Does NOT acquire the
    // consumer lock (acquire_consumer_lock=false) so tests can double-attach via
    // this hook without tripping the SPSC reject. out_channels sizes the output
    // staging so the synchronous path can exercise output composition (M3).
    BackendError pump_synchronous(AudioCallback* callback, int blocks,
                                  std::uint64_t hw_ts_base, int engine_block_size,
                                  int out_channels);

    // ── Test accessors (mirror NullBackend's set_input_fixture / pump_*
    //    test-hook precedent) ─────────────────────────────────────────────
    // Effective masking capacity == header.capacity_frames verbatim (case 7);
    // never a rounded value.
    std::uint32_t maskingCapacityFrames() const noexcept { return masking_capacity_; }
    // Staging buffer pointer/capacity stability invariant (case 8 iv): a
    // realloc on the RT path would move .data() / change .capacity().
    const float*  stagingData()     const noexcept { return staging_flat_.data(); }
    std::size_t   stagingCapacity() const noexcept { return staging_flat_.capacity(); }
    // Output staging pointer/capacity (PR3 Decision 3(ii)): the engine renders
    // into this backend-owned scratch buffer. Same stability invariant as the
    // input staging (case 8 iv extended): a realloc on the RT path would move
    // .data() / change .capacity().
    const float*  outStagingData()     const noexcept { return out_staging_flat_.data(); }
    std::size_t   outStagingCapacity() const noexcept { return out_staging_flat_.capacity(); }

private:
    SharedRingBackend() = default;

    // Common gate+allocate path shared by start() and pump_synchronous().
    // acquire_consumer_lock: start() passes true (SPSC gate 7); pump_synchronous
    // passes false so the test hook never trips the single-consumer reject.
    BackendError prepare(AudioCallback* callback, int engine_block_size,
                         int out_channels, bool acquire_consumer_lock);
    // Unified start: spawn_worker is true ONLY for the 3-arg production/CLI/AC-5
    // path; the 1-arg/2-arg overloads (frozen PR2 manual-pump ctests) set
    // running_ without a worker so direct pump_block() calls are not raced.
    BackendError start(AudioCallback* callback, int engine_block_size,
                       int out_channels, bool spawn_worker);
    void         allocate_staging();
    void         thread_loop();

    shm::SharedMemoryRegion region_;

    double sample_rate_     = 0.0;
    int    in_channels_     = 0;
    int    out_channels_    = 0;   // engine output channel count (PR3 Decision 3(ii))
    int    block_size_      = 0;   // == header.block_size
    int    engine_block_    = 0;
    std::uint32_t masking_capacity_ = 0;  // == header.capacity_frames (always)

    // Consumer-owned read cursor mirror (single consumer; relaxed local read).
    std::uint64_t read_idx_local_ = 0;

    // Pre-allocated planar staging: in_channels_ * block_size_ floats.
    std::vector<float>        staging_flat_;
    std::vector<const float*> staging_channels_;

    // Pre-allocated planar OUTPUT staging the engine renders into (PR3): empty
    // when out_channels_ == 0 (the PR2 test path), so cases 3–8 are unaffected.
    std::vector<float>        out_staging_flat_;
    std::vector<float*>       out_staging_channels_;

    AudioCallback*    callback_  = nullptr;
    std::atomic<bool> running_{false};  // worker reads it; control thread writes it
    std::thread       worker_;          // production cadence (PR3 Decision 3(i))
    bool              holds_consumer_lock_ = false;  // true iff gate 7 acquired (PR3 Decision 2)
    util::XrunCounter xruns_;   // backend-private (NOT header xrun_count)

    // ── Control-thread diagnostic state ────────────────────────────────
    unsigned long long underrun_warnings_         = 0;
    unsigned long long stale_warnings_            = 0;
    unsigned long long pacing_warnings_           = 0;
    unsigned long long attached_no_data_warnings_ = 0;

    bool          attached_no_data_latched_ = false;  // once-on-attach latch
    bool          have_prev_pts_            = false;
    std::uint64_t prev_pts_ns_              = 0;
    bool          have_stale_warn_ts_       = false;
    std::uint64_t last_stale_warn_ms_       = 0;
    bool          have_pacing_warn_ts_      = false;
    std::uint64_t last_pacing_warn_ms_      = 0;
};

}  // namespace spe::audio_io

#endif  // defined(__linux__) || defined(__APPLE__)
