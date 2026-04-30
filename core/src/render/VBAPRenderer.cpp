// core/src/render/VBAPRenderer.cpp

#include "render/VBAPRenderer.h"
#include <cstring>

namespace spe::render {

void VBAPRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                  double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());
    // Reset all gain ramps to 0
    for (auto& obj_ramps : ramps_)
        for (int s = 0; s < num_speakers_; ++s)
            obj_ramps[s].reset(0.f);
}

void VBAPRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int N = static_cast<int>(objects.size());
    const int S = num_speakers_;

    // Clear output
    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    for (int obj = 0; obj < N; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        // Compute target VBAP gains via analytic reference
        auto gains = AlgorithmAnalyticReference::vbap_gain(
            layout_, objects[obj].az_rad, objects[obj].el_rad);

        // Update ramp targets
        for (int s = 0; s < S; ++s)
            ramps_[obj][s].setTarget(gains[s], num_samples);

        // Mix per sample
        for (int n = 0; n < num_samples; ++n) {
            float x = src[n];
            for (int s = 0; s < S; ++s)
                out[n * S + s] += x * ramps_[obj][s].next();
        }
    }
}

} // namespace spe::render
