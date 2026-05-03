// core/src/render/AmbisonicRenderer.cpp

#include "render/AmbisonicRenderer.h"
#include "ambi/AmbisonicEncoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spe::render {

namespace {
constexpr int kK[3] = {4, 9, 16};
} // namespace

void AmbisonicRenderer::prepareToPlay(const geometry::SpeakerLayout& layout,
                                       double /*sample_rate*/)
{
    decoder_.prepare(layout);
    num_speakers_ = decoder_.numSpeakers();

    // Pre-allocate the full 16-channel SH bus (3rd-order). Lower orders
    // simply ignore the higher-K slots — they remain zeroed each block.
    for (int k = 0; k < ambi::AmbiDecoder::MAX_K; ++k) {
        sh_bufs_[static_cast<size_t>(k)].assign(spe::MAX_BLOCK, 0.f);
        sh_ptrs_[static_cast<size_t>(k)] = sh_bufs_[static_cast<size_t>(k)].data();
    }
}

void AmbisonicRenderer::setOrder(int order) noexcept {
    if (order < 1) order = 1;
    if (order > ambi::AmbiDecoder::MAX_ORDER) order = ambi::AmbiDecoder::MAX_ORDER;
    order_.store(order, std::memory_order_relaxed);
}

void AmbisonicRenderer::processBlock(
    std::span<const ObjectState> objects,
    std::span<const float* const> dry_mono,
    float* out,
    int    num_samples)
{
    const int active_order = order_.load(std::memory_order_relaxed);
    const int K = kK[active_order - 1];

    // Zero the in-use SH buses for this block. Higher-K buses stay zero
    // from prepareToPlay so the lower-order decode is unaffected.
    for (int k = 0; k < K; ++k) {
        std::fill_n(sh_bufs_[static_cast<size_t>(k)].data(), num_samples, 0.f);
    }

    const int N = static_cast<int>(objects.size());
    for (int obj = 0; obj < N; ++obj) {
        if (!objects[obj].active) continue;
        const float* src = dry_mono[static_cast<size_t>(obj)];
        if (!src) continue;

        const float az    = objects[obj].az_rad;
        const float el    = objects[obj].el_rad;

        // Width: max-rE-style attenuation of higher harmonics.
        // width=0 → no attenuation (point source); width=π → only W (omni).
        const float width_norm = std::clamp(objects[obj].width_rad / 3.14159265f, 0.f, 1.f);
        const float rE_scale   = 1.0f - width_norm;
        // Linear attenuation per order (each higher order shrinks further).
        // order-1 multiplier = rE_scale, order-2 = rE_scale², order-3 = rE_scale³.
        const float scale1 = rE_scale;
        const float scale2 = rE_scale * rE_scale;
        const float scale3 = scale2 * rE_scale;

        if (active_order == 1) {
            const auto c = ambi::AmbisonicEncoder::encode_1st_order(az, el);
            const float w = c.W;
            const float y = c.Y * scale1; // ACN 1
            const float z = c.Z * scale1; // ACN 2
            const float x = c.X * scale1; // ACN 3
            float* W = sh_bufs_[0].data();
            float* Y = sh_bufs_[1].data();
            float* Z = sh_bufs_[2].data();
            float* X = sh_bufs_[3].data();
            for (int n = 0; n < num_samples; ++n) {
                const float s = src[n];
                W[n] += s * w;
                Y[n] += s * y;
                Z[n] += s * z;
                X[n] += s * x;
            }
        } else if (active_order == 2) {
            auto c = ambi::AmbisonicEncoder::encode_2nd_order(az, el);
            // Order-1 channels (1..3) get scale1; order-2 channels (4..8) get scale2.
            for (int k = 1; k < 4;  ++k) c[static_cast<size_t>(k)] *= scale1;
            for (int k = 4; k < 9;  ++k) c[static_cast<size_t>(k)] *= scale2;
            for (int k = 0; k < 9; ++k) {
                float* B = sh_bufs_[static_cast<size_t>(k)].data();
                const float ck = c[static_cast<size_t>(k)];
                for (int n = 0; n < num_samples; ++n) B[n] += src[n] * ck;
            }
        } else { // active_order == 3
            auto c = ambi::AmbisonicEncoder::encode_3rd_order(az, el);
            for (int k = 1;  k < 4;  ++k) c[static_cast<size_t>(k)] *= scale1;
            for (int k = 4;  k < 9;  ++k) c[static_cast<size_t>(k)] *= scale2;
            for (int k = 9;  k < 16; ++k) c[static_cast<size_t>(k)] *= scale3;
            for (int k = 0; k < 16; ++k) {
                float* B = sh_bufs_[static_cast<size_t>(k)].data();
                const float ck = c[static_cast<size_t>(k)];
                for (int n = 0; n < num_samples; ++n) B[n] += src[n] * ck;
            }
        }
    }

    // Decode K-channel SH bus → speaker outputs.
    decoder_.decode(active_order, sh_ptrs_.data(), num_samples, out);
}

} // namespace spe::render
