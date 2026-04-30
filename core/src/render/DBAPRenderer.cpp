// core/src/render/DBAPRenderer.cpp

#include "render/DBAPRenderer.h"
#include <cstring>
#include <cmath>

namespace spe::render {

void DBAPRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                  double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());
    for (auto& obj_ramps : ramps_)
        for (int s = 0; s < num_speakers_; ++s)
            obj_ramps[s].reset(0.f);
}

void DBAPRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int N = static_cast<int>(objects.size());
    const int S = num_speakers_;

    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    for (int obj = 0; obj < N; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        // Convert spherical to Cartesian for DBAP distance calculation
        float az  = objects[obj].az_rad;
        float el  = objects[obj].el_rad;
        float d   = objects[obj].dist_m;
        float src_x = d * std::sin(az) * std::cos(el);
        float src_y = d * std::sin(el);
        float src_z = d * std::cos(az) * std::cos(el);

        auto gains = AlgorithmAnalyticReference::dbap_gain(
            layout_, src_x, src_y, src_z, rolloff_a_);

        for (int s = 0; s < S; ++s)
            ramps_[obj][s].setTarget(gains[s], num_samples);

        for (int n = 0; n < num_samples; ++n) {
            float x = src[n];
            for (int s = 0; s < S; ++s)
                out[n * S + s] += x * ramps_[obj][s].next();
        }
    }
}

} // namespace spe::render
