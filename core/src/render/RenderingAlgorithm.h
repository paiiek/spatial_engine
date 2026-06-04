// core/src/render/RenderingAlgorithm.h
// Abstract base for VBAP / WFS / DBAP renderers.
// RT-safe contract: no allocation in processBlock().

#pragma once
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include <array>
#include <span>

namespace spe::render {

struct ObjectState {
    float az_rad   = 0.f;
    float el_rad   = 0.f;
    float dist_m   = 1.f;
    bool  active   = false;
    float width_rad = 0.f;  // source spread (0 = point source, π = omnidirectional)
};

// Per-algorithm scratch for one object → per-speaker gains (SoA).
// Pre-allocated at prepareToPlay.
struct AlgoScratch {
    // gains[speaker_idx] for MAX_OBJECTS objects.
    std::array<std::array<float, spe::MAX_OBJECTS>, spe::MAX_SPEAKERS> gains{};
    int num_speakers = 0;
};

class RenderingAlgorithm {
public:
    virtual ~RenderingAlgorithm() = default;

    // Called once before audio thread starts. May allocate.
    virtual void prepareToPlay(const geometry::SpeakerLayout& layout,
                               double sample_rate) = 0;

    // Called on audio thread. Must not allocate.
    // objects: span of ObjectState[MAX_OBJECTS].
    // out: interleaved output buffer [sample][speaker], size = num_samples * num_speakers.
    // dry_mono: per-object dry mono input [MAX_OBJECTS][num_samples].
    virtual void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,  // [obj_idx] -> samples
        float* out,                               // [sample * num_speakers + spk]
        int    num_samples) = 0;

    int numSpeakers() const noexcept { return num_speakers_; }

protected:
    int num_speakers_ = 0;
};

} // namespace spe::render
