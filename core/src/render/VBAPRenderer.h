// core/src/render/VBAPRenderer.h
// 2D VBAP renderer. Pair-based amplitude panning.
// SoA scratch pre-allocated at prepareToPlay.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "render/AlgorithmAnalyticReference.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <array>
#include <vector>

namespace spe::render {

class VBAPRenderer : public RenderingAlgorithm {
public:
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

private:
    // Per-object, per-speaker GainRamp (SoA scratch, MAX_OBJECTS * 64 speakers).
    // Pre-allocated — no runtime allocation.
    std::array<std::array<spe::dsp::GainRamp, 64>, spe::MAX_OBJECTS> ramps_;
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;
};

} // namespace spe::render
