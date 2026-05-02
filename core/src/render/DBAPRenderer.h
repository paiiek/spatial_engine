// core/src/render/DBAPRenderer.h
// DBAP renderer: g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))
// Rolloff a ∈ [1.5, 2.0], config-tunable.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "render/AlgorithmAnalyticReference.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <array>

namespace spe::render {

class DBAPRenderer : public RenderingAlgorithm {
public:
    explicit DBAPRenderer(float rolloff_a = 2.0f) : rolloff_a_(rolloff_a) {}

    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

    void setRolloff(float a) noexcept { rolloff_a_ = a; }

private:
    float rolloff_a_ = 2.0f;
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;
    std::array<std::array<spe::dsp::GainRamp, 64>, spe::MAX_OBJECTS> ramps_;

    // Compute single-position DBAP gains into out_gains[0..num_speakers_-1].
    // RT-safe: no allocation.
    void dbapForPosition(float az_rad, float el_rad, float dist_m,
                         float* out_gains) const noexcept;
};

} // namespace spe::render
