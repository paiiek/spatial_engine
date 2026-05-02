// core/src/render/AmbisonicRenderer.cpp

#include "render/AmbisonicRenderer.h"
#include "ambi/AmbisonicEncoder.h"
#include <cstring>

namespace spe::render {

void AmbisonicRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                       double /*sample_rate*/)
{
    decoder_.prepare(layout);
    num_speakers_ = decoder_.numSpeakers();

    // Pre-allocate B-format accumulation buffers (MAX_BLOCK samples each).
    buf_W_.assign(spe::MAX_BLOCK, 0.f);
    buf_Y_.assign(spe::MAX_BLOCK, 0.f);
    buf_Z_.assign(spe::MAX_BLOCK, 0.f);
    buf_X_.assign(spe::MAX_BLOCK, 0.f);
}

void AmbisonicRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    // Zero B-format accumulation buffers for this block.
    std::fill(buf_W_.begin(), buf_W_.begin() + num_samples, 0.f);
    std::fill(buf_Y_.begin(), buf_Y_.begin() + num_samples, 0.f);
    std::fill(buf_Z_.begin(), buf_Z_.begin() + num_samples, 0.f);
    std::fill(buf_X_.begin(), buf_X_.begin() + num_samples, 0.f);

    const int N = static_cast<int>(objects.size());
    for (int obj = 0; obj < N; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[static_cast<size_t>(obj)];
        if (!src) continue;

        auto coeffs = ambi::AmbisonicEncoder::encode_1st_order(
            objects[obj].az_rad, objects[obj].el_rad);

        // Width: max-rE weighting — attenuate X/Y/Z relative to W.
        // width=0 → rE_scale=1 (point source); width=π → rE_scale=0 (omnidirectional W only).
        const float rE_scale = 1.0f - (objects[obj].width_rad / 3.14159265f);
        coeffs.X *= rE_scale;
        coeffs.Y *= rE_scale;
        coeffs.Z *= rE_scale;

        for (int n = 0; n < num_samples; ++n) {
            const float s = src[n];
            buf_W_[static_cast<size_t>(n)] += s * coeffs.W;
            buf_Y_[static_cast<size_t>(n)] += s * coeffs.Y;
            buf_Z_[static_cast<size_t>(n)] += s * coeffs.Z;
            buf_X_[static_cast<size_t>(n)] += s * coeffs.X;
        }
    }

    // Decode B-format → speaker outputs.
    decoder_.decode(buf_W_.data(), buf_Y_.data(), buf_Z_.data(), buf_X_.data(),
                    num_samples, out);
}

} // namespace spe::render
