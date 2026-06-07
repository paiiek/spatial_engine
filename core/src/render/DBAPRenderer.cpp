// core/src/render/DBAPRenderer.cpp

#include "render/DBAPRenderer.h"
#include "core/Constants.h"
#include <cassert>
#include <cstring>
#include <cmath>

namespace spe::render {

void DBAPRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                  double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());
    // Match VBAP/VAP: fail loudly on the control thread if a layout exceeds the
    // fixed MAX_SPEAKERS scratch (ramps_ / final_gains/gain_acc/g_v[MAX_SPEAKERS]).
    // The clamp is defense-in-depth under NDEBUG. (LayoutLoader already bounds
    // YAML layouts to kMaxYamlChannel==MAX_SPEAKERS; this guards programmatic
    // callers too.) Phase 0.5 (128 lift): cap is the compile-time MAX_SPEAKERS.
    assert(num_speakers_ <= spe::MAX_SPEAKERS
           && "DBAPRenderer: layout exceeds MAX_SPEAKERS cap (scratch fixed)");
    if (num_speakers_ > spe::MAX_SPEAKERS) num_speakers_ = spe::MAX_SPEAKERS;
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

        float final_gains[spe::MAX_SPEAKERS] = {};

        if (objects[obj].width_rad > 1e-3f) {
            // Multi-virtual-source model (N=3): az offsets -half_w, 0, +half_w
            constexpr int VSRC = 3;
            const float half_w = objects[obj].width_rad * 0.5f;
            const float az_offsets[VSRC] = { -half_w, 0.f, +half_w };

            float gain_acc[spe::MAX_SPEAKERS] = {};  // accumulated power (gain^2)
            float g_v[spe::MAX_SPEAKERS]      = {};

            for (int v = 0; v < VSRC; ++v) {
                const float az_v = objects[obj].az_rad + az_offsets[v];
                dbapForPosition(az_v, objects[obj].el_rad, objects[obj].dist_m, g_v);
                for (int s = 0; s < S; ++s)
                    gain_acc[s] += g_v[s] * g_v[s];
            }

            // sqrt of average power, then L2 normalise
            float sum_sq = 0.f;
            for (int s = 0; s < S; ++s) {
                gain_acc[s] = std::sqrt(gain_acc[s] / VSRC);
                sum_sq += gain_acc[s] * gain_acc[s];
            }
            const float norm = (sum_sq > 1e-9f) ? (1.f / std::sqrt(sum_sq)) : 1.f;
            for (int s = 0; s < S; ++s)
                final_gains[s] = gain_acc[s] * norm;
        } else {
            // Single-source DBAP (width=0 baseline preserved)
            dbapForPosition(objects[obj].az_rad, objects[obj].el_rad,
                            objects[obj].dist_m, final_gains);
        }

        for (int s = 0; s < S; ++s)
            ramps_[obj][s].setTarget(final_gains[s], num_samples);

        for (int n = 0; n < num_samples; ++n) {
            float x = src[n];
            for (int s = 0; s < S; ++s)
                out[n * S + s] += x * ramps_[obj][s].next();
        }
    }
}

void DBAPRenderer::dbapForPosition(float az_rad, float el_rad, float dist_m,
                                    float* out_gains) const noexcept
{
    const float d    = dist_m;
    const float sx   = d * std::sin(az_rad) * std::cos(el_rad);
    const float sy   = d * std::sin(el_rad);
    const float sz   = d * std::cos(az_rad) * std::cos(el_rad);

    // RT-safe (Phase 1.3): write straight into the caller's fixed buffer
    // (final_gains/g_v are float[MAX_SPEAKERS]); no per-block std::vector alloc.
    const int n = AlgorithmAnalyticReference::dbap_gain_into(
        layout_, sx, sy, sz, rolloff_a_, out_gains, spe::MAX_SPEAKERS);
    for (int s = n; s < num_speakers_; ++s)
        out_gains[s] = 0.f;  // capacity/empty fallback (n==num_speakers_ normally)
}

} // namespace spe::render
