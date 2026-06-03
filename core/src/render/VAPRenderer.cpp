// core/src/render/VAPRenderer.cpp

#include "render/VAPRenderer.h"
#include "render/ported/Vap.h"
#include "coords/Coords.h"
#include <cstring>
#include <cmath>

namespace spe::render {

void VAPRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                double sample_rate)
{
    layout_       = layout;
    sr_           = sample_rate;
    num_speakers_ = static_cast<int>(layout.speakers.size());
    if (num_speakers_ > 64) num_speakers_ = 64;

    // Pre-convert speaker positions (mmhoa frame: x=right,y=up,z=front) to the
    // ported frame (x=right,y=front,z=up) via the Y<->Z swap adapter. Unit
    // directions feed the VBAP component; positions feed the volumetric one.
    for (int s = 0; s < num_speakers_; ++s) {
        const auto& spk = layout.speakers[static_cast<size_t>(s)];
        const auto p = spe::coords::mmhoa_to_ported(spk.x, spk.y, spk.z);
        spk_pos_ported_[static_cast<size_t>(s)] = iae::Vec3{p[0], p[1], p[2]};
        spk_dir_ported_[static_cast<size_t>(s)] =
            iae::normalized(spk_pos_ported_[static_cast<size_t>(s)]);
    }

    for (auto& obj_ramps : ramps_)
        for (int s = 0; s < num_speakers_; ++s)
            obj_ramps[static_cast<size_t>(s)].reset(0.f);
}

void VAPRenderer::processBlock(
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
        const float* src = dry_mono[static_cast<size_t>(obj)];
        if (!src) continue;

        // Source position: mmhoa (az,el,dist) -> ported-frame metres via the
        // canonical adapter (pipeline_dir_to_ported is the unit dir).
        const float d = objects[obj].dist_m;
        const auto u  = spe::coords::pipeline_dir_to_ported(objects[obj].az_rad,
                                                            objects[obj].el_rad);
        const iae::Vec3 obj_pos{u[0] * d, u[1] * d, u[2] * d};

        float gains[64] = {};
        iae::computeVolumetricAmplitudePanning(
            spk_pos_ported_.data(), spk_dir_ported_.data(),
            /*participateInVbap=*/nullptr, static_cast<size_t>(S),
            obj_pos, room_center_ported_, wall_radius_m_,
            curve_power_, dist_exponent_, gains);

        for (int s = 0; s < S; ++s)
            ramps_[static_cast<size_t>(obj)][static_cast<size_t>(s)]
                .setTarget(gains[s], num_samples);

        for (int n = 0; n < num_samples; ++n) {
            const float x = src[n];
            for (int s = 0; s < S; ++s)
                out[n * S + s] +=
                    x * ramps_[static_cast<size_t>(obj)][static_cast<size_t>(s)].next();
        }
    }
}

} // namespace spe::render
