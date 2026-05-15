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
//   Engine frame: az=+90 deg = LEFT  (AmbiX/B-format)
//   SOFA frame:   az=+90 deg = LEFT  (AES69) — no sign flip needed.
//   ITD: source at az_engine=+30 deg (left) → left ear first → ITD > 0.

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

    // Test-only hook to inject a synthetic throughput value without running
    // the real probe (useful for slow-CPU fallback unit tests).
    void injectProbeThroughput(float throughput_rt) noexcept;

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
    // No-op (writes silence) when hasB2()==false or effectiveMode()!=AmbiVS.
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

    // Build vs_layout_ from kTDesign24, prepare vs_decoder_, populate per-VS
    // HRIR cache via KD-tree lookup, prime 48 OlaConvolvers. Called once from
    // initialize() when sofa loaded. Control thread; allocates.
    void initializeB2();
};

} // namespace spe::output
