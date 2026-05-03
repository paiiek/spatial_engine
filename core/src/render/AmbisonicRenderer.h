// core/src/render/AmbisonicRenderer.h
// Ambisonic encoder + decoder chain renderer (orders 1, 2, 3).
// Encodes each active object into K-channel B-format (K = (order+1)²),
// sums into per-channel buses, then decodes via AmbiDecoder pseudo-inverse.
// Order is switchable at runtime through setOrder() (atomic, RT-safe).
// RT-safe: all SH buses pre-allocated for the maximum order at prepareToPlay.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "ambi/AmbiDecoder.h"
#include "core/Constants.h"
#include <array>
#include <atomic>
#include <vector>

namespace spe::render {

class AmbisonicRenderer : public RenderingAlgorithm {
public:
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

    // Set decoding order: 1, 2, or 3. Out-of-range values clamp to 1.
    // Safe to call from any thread; the renderer picks up the change at the
    // start of the next audio block.
    void setOrder(int order) noexcept;

    int order() const noexcept { return order_.load(std::memory_order_relaxed); }

private:
    ambi::AmbiDecoder decoder_;
    std::atomic<int>  order_{1};

    // K-channel SH accumulation buses, one per ACN channel up to 3rd order
    // (16 channels). Each MAX_BLOCK samples long.
    std::array<std::vector<float>, ambi::AmbiDecoder::MAX_K> sh_bufs_;
    // Pointer table for AmbiDecoder::decode(int, const float* const*, …).
    std::array<const float*, ambi::AmbiDecoder::MAX_K> sh_ptrs_{};
};

} // namespace spe::render
