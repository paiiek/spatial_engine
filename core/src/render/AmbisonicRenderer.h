// core/src/render/AmbisonicRenderer.h
// Ambisonic encoder + decoder chain renderer (orders 1, 2, 3).
// Encodes each active object into K-channel B-format (K = (order+1)²),
// sums into per-channel buses, then decodes via AmbiDecoder.
// Order and DecoderType are switchable at runtime (atomic, RT-safe setters).
// RT-safe: all SH buses pre-allocated for the maximum order at prepareToPlay.
// Decoder rebuild on type change is control-thread only via applyPendingDecoderTypeChange().

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

    // Set decoder algorithm: 0=PINV,1=MAX_RE,2=ALLRAD,3=EPAD,4=IN_PHASE.
    // Out-of-range clamps to PINV. Safe to call from any thread (atomic write).
    // The actual decoder_.prepare() rebuild is deferred to the control thread —
    // call applyPendingDecoderTypeChange() from a non-RT context between blocks.
    // processBlock() performs zero allocation on a pending change (AC-S4.5.4).
    void setDecoderType(int type) noexcept;

    // Control-thread only: rebuild decode matrices if decoder_type_ changed.
    // Must be called outside processBlock. Mirrors algo-swap crossfade pattern.
    void applyPendingDecoderTypeChange();

private:
    ambi::AmbiDecoder decoder_;
    std::atomic<int>  order_{1};

    // Atomic decoder type (written by any thread, applied by control thread).
    std::atomic<int>  decoder_type_{0};
    int               applied_decoder_type_{0}; // last applied (control thread)

    // Cached layout for rebuild on type change.
    geometry::SpeakerLayout layout_;

    // K-channel SH accumulation buses, one per ACN channel up to 3rd order (16).
    // Each MAX_BLOCK samples long.
    std::array<std::vector<float>, ambi::AmbiDecoder::MAX_K> sh_bufs_;
    // Pointer table for AmbiDecoder::decode(int, const float* const*, …).
    std::array<const float*, ambi::AmbiDecoder::MAX_K> sh_ptrs_{};
};

} // namespace spe::render
