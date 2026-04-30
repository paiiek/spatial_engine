// core/src/render/WFSRenderer.cpp

#include "render/WFSRenderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace spe::render {

void WFSRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                 double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());

    // Compute spatial aliasing fade factor
    float spacing = layout.max_spacing_m();
    if (spacing < 1e-6f) spacing = 1e-6f;
    float alias_limit = spe::SOUND_C / (2.f * F_MAX); // ~21.4 mm
    alias_fade_gain_ = std::min(1.f, alias_limit / spacing);

    // Pre-allocate delay lines and ramps on heap (non-RT, allowed here)
    const int total = spe::MAX_OBJECTS * num_speakers_;
    delays_.resize(total);
    ramps_.resize(total);
    for (auto& d : delays_) d.prepareToPlay(sample_rate);
    for (int obj = 0; obj < spe::MAX_OBJECTS; ++obj)
        for (int s = 0; s < num_speakers_; ++s)
            ramps_[flat_idx(obj, s)].reset(0.f);
}

void WFSRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int N = static_cast<int>(objects.size());
    const int S = num_speakers_;

    std::memset(out, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    for (int obj = 0; obj < N && obj < spe::MAX_OBJECTS; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[obj];
        if (!src) continue;

        float az  = objects[obj].az_rad;
        float el  = objects[obj].el_rad;
        float d   = std::max(objects[obj].dist_m, 0.01f);

        float sx = d * std::sin(az) * std::cos(el);
        float sy = d * std::sin(el);
        float sz = d * std::cos(az) * std::cos(el);

        for (int s = 0; s < S; ++s) {
            const auto& spk = layout_.speakers[s];
            float dx = spk.x - sx;
            float dy = spk.y - sy;
            float dz = spk.z - sz;
            float r  = std::sqrt(dx*dx + dy*dy + dz*dz);
            r = std::max(r, 0.01f);

            float delay_samples = r / spe::SOUND_C * static_cast<float>(sr_);
            float gain = alias_fade_gain_ / std::sqrt(r);

            auto& ramp  = ramps_[flat_idx(obj, s)];
            auto& delay = delays_[flat_idx(obj, s)];
            ramp.setTarget(gain, num_samples);

            for (int n = 0; n < num_samples; ++n) {
                float g       = ramp.next();
                float delayed = delay.processSample(src[n], delay_samples);
                out[n * S + s] += delayed * g;
            }
        }
    }
}

} // namespace spe::render
