// core/src/output_backend/BinauralMonitor.h
// BinauralMonitor — multi-object binaural rendering via per-object HRTF convolution.
//
// v0.5 refactor (commercial-grade):
//   * Each of MAX_OBJECTS objects owns a dual-slot OlaConvolver pair (L/R).
//   * setDirection(obj_id, az, el) is CONTROL-thread only: looks up the
//     nearest HRIR via the KdTree3D, calls loadInto() on the idle slot, then
//     atomically promotes it via front_idx_.store(release). A 2-block
//     crossfade ramps the old slot down and new slot up.
//   * processBlockForObject(obj_id, monoIn, leftOut, rightOut, n) is RT-safe.
//   * Pass-through fallback retained for when no .speh has been loaded.
//   * Chained-crossfade preempt-with-current-gain handoff per A5/r2 amendment.
//
// v0.5 P4 (B2 AmbiVS path):
//   * Second AmbiDecoder + SpeakerLayout from kTDesign24 + 24x2 OlaConvolvers,
//     all owned by BinauralMonitor (A4: prevents SpatialEngine from growing a
//     second decoder graph). vs_* HRIR cache is independent of physical layout.
//   * processBlockB2() consumes K-channel ACN-ordered SH planar input, decodes
//     onto 24 t-design virtual speakers, convolves each VS through its HRIR
//     pair, sums to L/R. RT-safe; alloc-free.
//   * runThroughputProbe() is the CPU-headroom probe (A6 — control thread,
//     intended for VST3 setActive(true)). When throughput < kMinB2Throughput
//     (1.5x RT), effective_mode_ clamps to Direct (B1 fallback) and surfaces
//     a `probeWarningCode()` of "ambivs_disabled_cpu" with the measured
//     throughput in probeThroughput().
//
// JUCE binaural path is removed in v0.5 (architect decision, deferred to v0.6).
//
// Coordinate convention:
//   Engine/pipeline frame: az = atan2(x, z); x=right → az=+90 deg = RIGHT.
//   .speh storage:         SOFA az values are pre-flipped to pipeline convention
//                          (RIGHT=+az) during SOFA→SPEH conversion, so no sign
//                          flip is needed at lookup time.
//   lookupHrtfFromTree():  accepts engine az (RIGHT=+az) and passes it directly
//                          to KdTree3D::nearest() — both sides use the same
//                          azElToXYZ formula, so nearest-neighbor is correct.
//   Worked example:        pt.x=+1, pt.z=0 → az=atan2(1,0)=+π/2 (RIGHT ear)
//                          → KD-tree finds RIGHT-side HRIR. Correct.
//   ITD: source at az_engine=+30 deg (right) → right ear first → ITD > 0.

#pragma once

#include "ambi/AmbiDecoder.h"
#include "core/Constants.h"
#include "dsp/GainRamp.h"
#include "geometry/SpeakerLayout.h"
#include "hrtf/KdTree3D.h"
#include "hrtf/OlaConvolver.h"
#include "hrtf/SofaBinReader.h"

#include <array>
#include <atomic>
#include <string>

namespace spe::output {

// v0.5 P4: binaural rendering mode. Control thread sets it; audio thread reads
// `effectiveMode()` which may differ from the user-requested mode when the
// throughput probe clamps B2→B1 on insufficient CPU headroom.
enum class BinauralMode : int {
    Direct = 0,   // B1: per-object direct HRTF (default).
    AmbiVS = 1,   // B2: 3rd-order ambisonics → 24-pt t-design VS → HRTF.
};

// Minimum acceptable throughput (multiples of real-time) for B2 to remain
// enabled. Probe measures B2's 24-convolver fan-out and clamps to B1 below
// this threshold to preserve audio integrity on slow hosts. 1.5x = 50% headroom.
inline constexpr float kMinB2Throughput = 1.5f;

class BinauralMonitor {
public:
    struct Config {
        std::string sofaPath;             // .speh binary path, or "" for pass-through
        float       sampleRate = 48000.f;
        int         blockSize  = 64;
    };

    enum class InitResult {
        Ok,
        SofaNotFound,
        SofaSampleRateMismatch,
        SofaIRLengthUnsupported,
        SofaInvalidFormat,
    };

    BinauralMonitor() = default;

    InitResult initialize(const Config& cfg);

    // Legacy single-object pass-through API. When no .speh is loaded this
    // performs the v0.4 pass-through (monoIn → leftOut/rightOut). When a
    // .speh IS loaded, it uses object 0 as the legacy single-source slot.
    void processBlock(const float* monoIn, int numSamples,
                      float* leftOut, float* rightOut);

    // v0.5 P1: per-object processing. Reads the front slot under acquire,
    // runs convolution; if a crossfade is active, also runs the back slot
    // and mixes via ramp_old/ramp_new gains. After 2*block_size_ samples,
    // crossfade_active flips to false. Alloc-free.
    void processBlockForObject(int obj_id,
                               const float* monoIn,
                               int          numSamples,
                               float*       leftOut,
                               float*       rightOut);

    void reset();

    bool isInitialized() const { return initialized_; }
    bool hasHrtf()       const { return hrtf_loaded_; }

    // v0.5 P1: control-thread only. Look up nearest HRIR via KD-tree, load
    // into the idle slot, kick off a 2-block crossfade. Preempt-with-
    // current-gain handoff applies if a crossfade is already in flight.
    void setDirection(int obj_id, float az_rad, float el_rad = 0.f);

    // Legacy single-object setDirection (preserves obj_id=0 semantics).
    void setDirection(float az_rad, float el_rad = 0.f) {
        setDirection(0, az_rad, el_rad);
    }

    // Telemetry — sum of per-slot loadInto failures across all objects.
    std::uint64_t loadIntoFailures() const noexcept;

    // Block size (samples) the convolvers are primed for.
    int blockSize() const noexcept { return block_size_; }

    // ─────────────────────────────────────────────────────────────────────
    // v0.5 P4 — B2 AmbiVS path (24-point t-design virtual speakers).
    // ─────────────────────────────────────────────────────────────────────

    // True iff the B2 chain (vs_decoder_, vs_layout_, 48 OlaConvolvers, 24
    // HRIR pairs) was initialised. False when no .speh is loaded.
    bool hasB2() const noexcept { return b2_initialized_; }

    // Control-thread: request a binaural rendering mode. Audio thread will
    // observe the change on the next block via effectiveMode().
    // After requesting AmbiVS, callers should invoke runThroughputProbe()
    // to validate CPU headroom; on insufficient throughput, effectiveMode()
    // clamps to Direct (B1) while requestedMode() preserves the user intent.
    void setRequestedMode(BinauralMode m) noexcept;

    // Audio-thread reader. Returns whichever of {Direct, AmbiVS} the audio
    // path should actually run this block.
    BinauralMode effectiveMode() const noexcept {
        return static_cast<BinauralMode>(
            effective_mode_.load(std::memory_order_acquire));
    }

    // Control-thread reader. The user-requested mode survives B2→B1 fallback
    // so that a later prepareToPlay() on a faster system can restore B2.
    BinauralMode requestedMode() const noexcept {
        return static_cast<BinauralMode>(
            requested_mode_.load(std::memory_order_acquire));
    }

    // Run the CPU throughput probe synchronously on the calling thread (must
    // be a control thread; allocates a small stack-backed sacrificial IR).
    // Sets effective_mode_ to {requestedMode if throughput >= kMinB2Throughput,
    // else Direct} and returns the measured throughput in multiples of RT.
    // No-op (returns 0.f) when hasB2()==false.
    float runThroughputProbe();

    // ─────────────────────────────────────────────────────────────────────
    // v0.5.1 Q2 — A3 mode-transition crossfade.
    //
    // Threading model: the audio thread owns the crossfade state (all
    // non-atomic ints). The control-thread setRequestedMode() path writes
    // requested_mode_ + effective_mode_ atomics; the audio thread snapshots
    // effective_mode_ once per block via observeAndArmXfade() and decides
    // whether to arm a B1↔B2 ramp. While a ramp is in flight, a second
    // mode flip is deferred until xfade_blocks_remaining_ reaches 0.
    //
    // Crossfade duration:
    //   * Default kXfadeBlocks = 2 (two-block linear ramp).
    //   * When probe_warning_set_ is true at arm time, kXfadeBlocks = 1
    //     (CPU headroom safety on borderline hardware — trades residual
    //     click for halving the dual-branch render cost). Emits the
    //     /sys/binaural_warning ,s "xfade_truncated_cpu" one-shot via the
    //     xfade_truncated_pending_ atomic, drained by the 1-Hz IO heartbeat.
    // ─────────────────────────────────────────────────────────────────────
    struct XfadeStep {
        bool          active;            // true while a ramp is in flight (this block).
        int           blocks_remaining; // value AFTER this block's decrement.
        BinauralMode  outgoing;          // mode being faded out.
        BinauralMode  incoming;          // mode being faded in (== effectiveMode()).
        BinauralMode  steady;            // single mode to render when !active.
        int           total_blocks;      // ramp length: 1 or kXfadeBlocksDefault.
        int           block_index;       // 0-based index of this block within the ramp.
    };

    // Audio-thread: snapshot effectiveMode(), arm a new ramp if needed, return
    // the step descriptor for this block (callers use it to decide whether to
    // render one or both branches). Idempotent within a block.
    XfadeStep observeAndArmXfade() noexcept;

    // Audio-thread: complete a block under the ramp — decrements counter and
    // promotes `incoming` to steady-state when the counter reaches zero.
    // Must be called once per block AFTER observeAndArmXfade() (whether or not
    // the step was active — the call is a no-op in steady state).
    void finalizeXfadeBlock() noexcept;

    // Pre-computed linear ramp envelopes for kXfadeBlocks ∈ {1, 2}. Sized
    // [total_blocks * blockSize_]. Accessor returns nullptr when total_blocks
    // is invalid. Index = (block_index * block_size_) + sample_index.
    //   incoming envelope: 0 → 1 linearly over (total_blocks * block_size_)
    //   outgoing envelope: 1 → 0 linearly (complement of incoming)
    const float* xfadeIncomingEnvelope(int total_blocks) const noexcept;
    const float* xfadeOutgoingEnvelope(int total_blocks) const noexcept;

    // Audio-thread: true iff a ramp is currently in flight. Atomic mirror of
    // xfade_blocks_remaining_ — provided for tests + heartbeat-loop sidecar.
    bool xfadeActive() const noexcept {
        return xfade_blocks_remaining_atomic_.load(std::memory_order_acquire) > 0;
    }

    // Q2 MAJOR 1-iter3: one-shot flag set by the audio thread when arming a
    // ramp with kXfadeBlocks=1 (probe-clamped CPU truncation). Read+exchange
    // by the 1-Hz IO heartbeat in vst3/SpatialEngineProcessor; on a true
    // exchange, sends /sys/binaural_warning ,s "xfade_truncated_cpu".
    // Returns true when a pending truncation event was observed and cleared.
    bool drainXfadeTruncatedPending() noexcept {
        return xfade_truncated_pending_.exchange(false,
                                                 std::memory_order_acq_rel);
    }

    // ──── v0.6 #5 — runtime sticky-underrun auto-demote ────
    //
    // When the B2 fan-out's actual wall-clock processing time exceeds
    // (kDemoteBudgetFraction × block deadline) for kDemoteStrikes
    // consecutive blocks, BinauralMonitor auto-demotes the effective mode
    // to Direct (B1) and arms a one-shot warning latch for the IO drain.
    // The demote is sticky for the lifetime of the BinauralMonitor (it is
    // cleared by initialize()), so transient spikes won't flap the mode.
    //
    // The plan (v0.5.1 §Q5, deferred to v0.6) explicitly preferred option
    // (a): a wall-clock detector inside BinauralMonitor — because the VST3
    // plugin process has no XrunCounter feed (only the standalone Null/
    // Dante backends do). We measure via std::chrono::steady_clock; on
    // modern Linux this is a vDSO call (~30 ns, no syscall, no alloc).
    //
    // Tuning:
    //   kDemoteStrikes        = 8 consecutive blocks (≈ 21 ms at 48 kHz/128)
    //   kDemoteBudgetFraction = 0.9  (90% of deadline)
    static constexpr int   kRuntimeDemoteStrikes        = 8;
    static constexpr float kRuntimeDemoteBudgetFraction = 0.9f;

    // True when runtime auto-demote has fired (sticky until next initialize()).
    bool isRuntimeDemoted() const noexcept {
        return runtime_demoted_.load(std::memory_order_acquire);
    }

    // IO-thread drain: returns true once per demote event. Heartbeat emits
    // /sys/binaural_warning ,s "ambivs_demoted_runtime" on a true result.
    bool drainRuntimeDemotePending() noexcept {
        return runtime_demote_warning_pending_.exchange(
            false, std::memory_order_acq_rel);
    }

    // Audio-thread entry: report the wall-clock duration of a just-finished
    // B2 block. Bumps the strike counter when over budget, resets to 0
    // otherwise. When strikes reach kRuntimeDemoteStrikes for the first
    // time, sets runtime_demoted_, clamps effective_mode_ to Direct, and
    // arms the warning latch. Subsequent calls are no-ops once demoted.
    // Safe to call with block_size <= 0 or sample_rate <= 0 (no-op).
    void recordB2BlockTiming(int block_size, float sample_rate,
                             long long elapsed_ns) noexcept;

    // Test-only hook: drive the strike counter to (kRuntimeDemoteStrikes - 1)
    // so the next over-budget recordB2BlockTiming() call triggers the demote
    // deterministically. Use with care; do NOT invoke from production code.
    void injectRuntimeUnderrunStrikesForTest() noexcept;

    // Test-only hook: clear the runtime demote state. The strike counter,
    // demoted flag, and warning latch all reset to fresh-start values.
    // Useful for re-running scenarios in a single test process.
    void clearRuntimeDemoteForTest() noexcept;

    // ──── v0.6 D-M2 — steady_clock vDSO availability probe ────
    //
    // On platforms where std::chrono::steady_clock::now() falls back to
    // a syscall (old Linux kernels without the arch_timer vDSO, some
    // older macOS Intel hosts, exotic non-arch-timer ARM SoCs), the
    // per-block wall-clock brackets added in v0.6 #5 at
    // SpatialEngine.cpp:709-741 would burn several µs per audio block
    // on actual syscalls and become RT-unsafe themselves — exactly the
    // kind of self-fulfilling demote prophecy the Architect retroactive
    // review §A.2 called out.
    //
    // initialize() now times kSteadyClockProbeSamples calls to
    // steady_clock::now(); if the average exceeds kSteadyClockFastThresholdNs,
    // we declare the platform "slow" and:
    //   (a) SpatialEngine.cpp gates the wall-clock brackets behind
    //       isSteadyClockFast() so the demote detector silently
    //       no-ops on a slow host (B2 will still run, just without
    //       runtime self-monitoring).
    //   (b) the heartbeat IO thread drains drainRtTimingUnavailablePending()
    //       and emits /sys/binaural_warning ,s "rt_timing_unavailable"
    //       exactly once per BinauralMonitor lifetime so the host
    //       knows demote detection is disabled.
    //
    // The probe cost is bounded — at the threshold (200 ns/call ×
    // 10 000 samples) the worst-case is ~2 ms on a slow host, paid
    // once per prepareToPlay on the control thread.
    static constexpr long long kSteadyClockFastThresholdNs = 200;
    static constexpr int       kSteadyClockProbeSamples    = 10000;

    // True iff the most recent initialize() probe found steady_clock::now()
    // fast enough to bracket every B2 block. Sticky for the BinauralMonitor
    // lifetime (re-probed on the next initialize()).
    bool isSteadyClockFast() const noexcept {
        return steady_clock_fast_.load(std::memory_order_acquire);
    }

    // IO-thread drain: returns true once per slow-probe event. Heartbeat
    // emits /sys/binaural_warning ,s "rt_timing_unavailable" on a true
    // result. Single-fire — subsequent calls return false until the next
    // initialize() re-arms the latch (assuming the next platform is also
    // slow; on a re-probe that returns fast the latch stays unarmed).
    bool drainRtTimingUnavailablePending() noexcept {
        return rt_timing_unavailable_pending_.exchange(
            false, std::memory_order_acq_rel);
    }

    // Test-only hook: force the slow-clock path so the warning drain and
    // the SpatialEngine.cpp gate are both exercised even on a fast CI
    // runner. Marks the platform "slow" and arms the warning latch.
    void injectSteadyClockSlowForTest() noexcept;

    // Test-only hook to inject a synthetic throughput value without running
    // the real probe (useful for slow-CPU fallback unit tests). Suffixed to
    // discourage production callers; do NOT invoke from VST3/UI code.
    void injectProbeThroughputForTest(float throughput_rt) noexcept;

    // Last probe result (multiples of real-time). 0.f before the first probe.
    float probeThroughput() const noexcept {
        return probe_throughput_.load(std::memory_order_acquire);
    }

    // "" when probe passed or has not run; "ambivs_disabled_cpu" when probe
    // forced a B2→B1 fallback. Read by the host to surface OSC
    // /sys/binaural_warning telemetry.
    const char* probeWarningCode() const noexcept {
        return probe_warning_set_.load(std::memory_order_acquire)
                   ? "ambivs_disabled_cpu"
                   : "";
    }

    // RT-safe entry for the B2 path. Decodes K-channel ACN-ordered SH planar
    // input onto 24 virtual speakers (t-design 24-point), convolves each VS
    // through its cached HRIR pair, sums to leftOut/rightOut.
    //   order ∈ {1, 2, 3} (clamped). K = (order+1)².
    //   sh_planar[k] is a [num_samples]-long block for ACN channel k.
    //   leftOut / rightOut are overwritten (not accumulated) by this call.
    // No-op (writes silence) when hasB2()==false or num_samples invalid
    // or sh_planar==nullptr. The mode gate is the CALLER's responsibility:
    // SpatialEngine snapshots effectiveMode() once per block and dispatches.
    // (C1 fix — eliminates the dual-read race that produced silent blocks on
    //  B1↔B2 transitions when an OSC mode flip arrived mid-block.)
    void processBlockB2(const float* const* sh_planar,
                        int                 order,
                        int                 num_samples,
                        float*              leftOut,
                        float*              rightOut);

    // Number of virtual speakers in the B2 chain (always 24 for v0.5).
    static constexpr int kNumVirtualSpeakers = 24;

    // Read-only access to the cached B2 HRIR length for VS index i; -1 OOB.
    // Used by tests to verify HRIR cache invariance across physical-layout
    // changes (test_b2_layout_change_does_not_reinit_vs.cpp).
    int b2HrirLength(int vs_idx) const noexcept;

    // Pointer to the cached B2 left-ear HRIR for VS index i. Stable across
    // physical-layout changes; only changes on .speh reload. Returns nullptr
    // when OOB or B2 not initialised. Audio-thread-safe read; control thread
    // is responsible for not overlapping with a hypothetical re-initialise.
    const float* b2HrirLeft(int vs_idx) const noexcept;
    const float* b2HrirRight(int vs_idx) const noexcept;

private:
    bool  initialized_  = false;
    bool  hrtf_loaded_  = false;
    int   block_size_   = 64;
    float sample_rate_  = 48000.f;

    // HRTF table + spatial index. Built once at initialize().
    hrtf::HrtfTable table_;
    hrtf::KdTree3D  tree_;

    // Per-object dual-slot state.
    struct ObjectSlots {
        // Two slots per ear; front_idx_ selects the "active" slot for the
        // audio thread. The idle slot (1-front) is written from the control
        // thread under no concurrent reader.
        std::array<hrtf::OlaConvolver, 2> conv_L{};
        std::array<hrtf::OlaConvolver, 2> conv_R{};

        // Atomic — flipped by setDirection() (release) and read by
        // processBlockForObject() (acquire).
        std::atomic<int> front_idx{0};

        // Crossfade ramps. ramp_new ramps 0 → 1 (incoming), ramp_old ramps
        // current → 0 (outgoing). When ramp_new is no longer ramping,
        // crossfade_active flips false.
        dsp::GainRamp    ramp_new;
        dsp::GainRamp    ramp_old;
        std::atomic<bool> crossfade_active{false};

        // Scratch — block-deep temporary buffers for old/new slot convolution.
        std::array<float, MAX_BLOCK> tmp_new_L{};
        std::array<float, MAX_BLOCK> tmp_new_R{};
        std::array<float, MAX_BLOCK> tmp_old_L{};
        std::array<float, MAX_BLOCK> tmp_old_R{};

        // True once setDirection() has loaded at least one IR pair into a
        // slot; the audio thread otherwise produces silence to avoid
        // convolving against zero IRs.
        std::atomic<bool> primed{false};
    };
    std::array<ObjectSlots, MAX_OBJECTS> obj_slots_{};

    // Prime all OlaConvolvers in obj_slots_ to MAX_IR_LEN capacity.
    // Called once from initialize() — allocates; control thread.
    void primeAllSlots();

    // ──── v0.5 P4 — B2 AmbiVS chain (24-pt t-design VS) ────
    bool                                       b2_initialized_ = false;
    spe::ambi::AmbiDecoder                     vs_decoder_;
    spe::geometry::SpeakerLayout               vs_layout_;
    std::array<hrtf::OlaConvolver,
               kNumVirtualSpeakers>            vs_conv_L_{};
    std::array<hrtf::OlaConvolver,
               kNumVirtualSpeakers>            vs_conv_R_{};
    // Per-VS HRIR cache keyed by t-design point. Populated once at
    // initialize() and invariant across physical-layout swaps (A4 clarif).
    std::array<std::array<float, hrtf::kOlaMaxIRLength>,
               kNumVirtualSpeakers>            vs_hrir_L_{};
    std::array<std::array<float, hrtf::kOlaMaxIRLength>,
               kNumVirtualSpeakers>            vs_hrir_R_{};
    std::array<int, kNumVirtualSpeakers>       vs_hrir_len_{};

    // Scratch for AmbiDecoder.decode() output: interleaved
    // [sample * kNumVirtualSpeakers + vs_idx]. Sized MAX_BLOCK * 24.
    std::array<float, MAX_BLOCK * kNumVirtualSpeakers> vs_decode_scratch_{};
    // Per-VS de-interleaved input scratch and per-ear convolution output.
    std::array<std::array<float, MAX_BLOCK>,
               kNumVirtualSpeakers>            vs_buf_{};
    std::array<float, MAX_BLOCK>               vs_conv_L_scratch_{};
    std::array<float, MAX_BLOCK>               vs_conv_R_scratch_{};

    // Mode state. Three values:
    //   requested_mode_ — user intent (preserved across fallback).
    //   effective_mode_ — what the audio thread actually runs this block.
    //                     Equal to requested_mode_ unless probe forced fallback.
    //   mode_            — backward-compat alias (kept as a copy of effective).
    std::atomic<int>   requested_mode_{static_cast<int>(BinauralMode::Direct)};
    std::atomic<int>   effective_mode_{static_cast<int>(BinauralMode::Direct)};

    // Last throughput probe result + fallback flag for telemetry.
    std::atomic<float> probe_throughput_{0.f};
    std::atomic<bool>  probe_warning_set_{false};

    // ──── v0.5.1 Q2 — A3 mode-transition crossfade state ────
    // Audio-thread-local non-atomic ints (read+written only by the audio
    // thread inside observeAndArmXfade() / finalizeXfadeBlock()).
    int  prev_effective_mode_   = static_cast<int>(BinauralMode::Direct);
    int  xfade_blocks_remaining_ = 0;
    int  xfade_total_blocks_     = 0;
    int  outgoing_mode_          = static_cast<int>(BinauralMode::Direct);
    int  incoming_mode_          = static_cast<int>(BinauralMode::Direct);

    // Atomic mirror of xfade_blocks_remaining_ for control-thread tests +
    // heartbeat sidecar visibility. Written ONLY by the audio thread inside
    // observeAndArmXfade() / finalizeXfadeBlock(). Strict relaxed semantics
    // would suffice; we use release/acquire for ordering with the truncation
    // pending flag below.
    std::atomic<int>  xfade_blocks_remaining_atomic_{0};

    // Q2 MAJOR 1-iter3: one-shot truncation flag. Set by the audio thread
    // when a ramp is armed with total_blocks = 1 because probe_warning_set_
    // was true at arm time. The IO-thread heartbeat exchanges this to false
    // and emits /sys/binaural_warning ,s "xfade_truncated_cpu" exactly once
    // per arm event (no per-block flood).
    std::atomic<bool> xfade_truncated_pending_{false};

    // v0.6 #5 — runtime sticky-underrun auto-demote state.
    // strikes      — audio-thread consecutive over-budget counter (resets on
    //                a good block; saturates at kRuntimeDemoteStrikes).
    // demoted      — sticky atomic; once true the B2 dispatch path clamps
    //                effective_mode_ to Direct. Cleared only by initialize().
    // warning_pending — IO-thread one-shot drain (exchange-to-false), gates
    //                /sys/binaural_warning ,s "ambivs_demoted_runtime" emission.
    std::atomic<int>  runtime_demote_strikes_{0};
    std::atomic<bool> runtime_demoted_{false};
    std::atomic<bool> runtime_demote_warning_pending_{false};

    // v0.6 D-M2 — steady_clock::now() vDSO availability flag, set by
    // initialize() based on a 10 000-sample timing probe. When false,
    // SpatialEngine.cpp's wall-clock brackets are skipped (the demote
    // detector silently no-ops), and the heartbeat IO thread emits
    // /sys/binaural_warning ,s "rt_timing_unavailable" exactly once.
    // Default true (assume fast); the probe demotes to false only on
    // measurably-slow platforms.
    std::atomic<bool> steady_clock_fast_{true};
    std::atomic<bool> rt_timing_unavailable_pending_{false};

    // v0.6 D-M2 — internal helper that times kSteadyClockProbeSamples
    // calls to steady_clock::now() and writes the result to
    // steady_clock_fast_ (and arms rt_timing_unavailable_pending_ on
    // slow result). Called from initialize() only; not RT-safe.
    void probeSteadyClockSpeed() noexcept;

    // Pre-computed linear envelopes for both supported ramp lengths. Sized
    // [N * MAX_BLOCK] so callers can index by (block_index * block_size_ +
    // sample_index). Recomputed in initialize() once block_size_ is known.
    //   _1: kXfadeBlocks = 1 (probe-clamped truncation).
    //   _2: kXfadeBlocks = 2 (default).
    static constexpr int kXfadeBlocksDefault = 2;
    std::array<float, MAX_BLOCK * 1>  xfade_inc_env_1_{};
    std::array<float, MAX_BLOCK * 1>  xfade_out_env_1_{};
    std::array<float, MAX_BLOCK * kXfadeBlocksDefault>  xfade_inc_env_2_{};
    std::array<float, MAX_BLOCK * kXfadeBlocksDefault>  xfade_out_env_2_{};
    bool xfade_envelopes_built_ = false;

    // Helper — control thread; called from initialize() after block_size_
    // is final. Fills xfade_inc_env_{1,2}_ + xfade_out_env_{1,2}_.
    void buildXfadeEnvelopes() noexcept;

    // Build vs_layout_ from kTDesign24, prepare vs_decoder_, populate per-VS
    // HRIR cache via KD-tree lookup, prime 48 OlaConvolvers. Called once from
    // initialize() when sofa loaded. Control thread; allocates.
    void initializeB2();
};

} // namespace spe::output
