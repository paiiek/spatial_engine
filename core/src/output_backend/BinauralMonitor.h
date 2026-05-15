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
// JUCE binaural path is removed in v0.5 (architect decision, deferred to v0.6).
//
// Coordinate convention:
//   Engine frame: az=+90 deg = LEFT  (AmbiX/B-format)
//   SOFA frame:   az=+90 deg = LEFT  (AES69) — no sign flip needed.
//   ITD: source at az_engine=+30 deg (left) → left ear first → ITD > 0.

#pragma once

#include "core/Constants.h"
#include "dsp/GainRamp.h"
#include "hrtf/KdTree3D.h"
#include "hrtf/OlaConvolver.h"
#include "hrtf/SofaBinReader.h"

#include <array>
#include <atomic>
#include <string>

namespace spe::output {

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
};

} // namespace spe::output
