#pragma once

#include <atomic>
#include <cstdint>

namespace spe::util {

/// Shared observability counters exposed via /sys/metrics OSC address.
///
/// All fields are std::atomic so they may be written from the audio thread
/// and read from the control/metrics thread without locks.
/// Layout: counters are grouped by size to minimise padding.
///
/// Usage:
///   static ObservabilityCounters g_counters;
///   g_counters.audio_underrun_count.fetch_add(1, std::memory_order_relaxed);
///
/// Metric names match the /sys/metrics OSC bundle fields documented in
/// docs/3u_rack_constraints.md and the soak harness.
struct ObservabilityCounters {
    // -----------------------------------------------------------------------
    // 64-bit event counters (written rarely, monotonically increasing)
    // -----------------------------------------------------------------------

    /// Number of audio buffer underruns (xruns) since engine start.
    std::atomic<uint64_t> audio_underrun_count{0};

    /// OSC packets processed per second (rolling 1-second window).
    std::atomic<uint64_t> osc_packet_rate{0};

    /// OSC packets rejected (malformed, unknown address, auth fail).
    std::atomic<uint64_t> osc_reject_count{0};

    /// OSC packets dropped due to reorder buffer overflow.
    std::atomic<uint64_t> osc_reordered_drops{0};

    /// Number of 1-second windows where OSC reorder burst was detected.
    /// Non-zero is a yellow flag (warning), not a hard failure gate.
    std::atomic<uint64_t> osc_reorder_burst_count_1s{0};

    /// Number of heartbeat misses (gap > 300 ms) since engine start.
    /// Any non-zero value fails the heartbeat_miss gate.
    std::atomic<uint64_t> heartbeat_miss_count{0};

    // -----------------------------------------------------------------------
    // 32-bit live-state counters (overwritten, not monotonic)
    // -----------------------------------------------------------------------

    /// Current IPC command queue depth (number of pending messages).
    std::atomic<uint32_t> ipc_queue_depth{0};

    /// Monotonically increasing version number bumped on every geometry update.
    std::atomic<uint32_t> geometry_cache_version{0};

    /// p99 per-block processing time in microseconds.
    /// Updated by the audio thread each block using a lightweight estimator.
    /// Gate threshold: 933 µs (= floor(64/48000 * 0.7 * 1e6)).
    std::atomic<uint32_t> per_block_time_p99_us{0};

    /// Audio thread CPU usage percentage (0–100), sampled each block.
    std::atomic<uint32_t> cpu_pct_audio_thread{0};

    // -----------------------------------------------------------------------
    // v1.0 Phase 1.4b — per-stage audio-thread timing (microseconds, last
    // block). The audio thread times each stage with steady_clock (vDSO, cheap)
    // and stores the last-block value here; the 1 Hz /sys/metrics tick samples
    // a representative block. Diagnostic only (not a gate). 0 when the stage did
    // not run that block (render not ready / reverb off / decorr off / binaural
    // off).
    std::atomic<uint32_t> stage_render_us{0};    ///< 5-renderer dispatch + mix sum.
    /// Spatial-room reverb fan-out only (active_reverb==2: late FDN + early
    /// reflections + cluster, distributed across the speaker bus). Does NOT
    /// include the per-object reverb-send chains or the global mono FDN/IR
    /// reverb, which run earlier in the block (outside this timing region).
    std::atomic<uint32_t> stage_room_us{0};
    std::atomic<uint32_t> stage_decorr_us{0};    ///< per-speaker decorrelation bank.
    std::atomic<uint32_t> stage_binaural_us{0};  ///< B1/B2 binaural side-output.
};

}  // namespace spe::util
