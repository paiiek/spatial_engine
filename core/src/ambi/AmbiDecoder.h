// core/src/ambi/AmbiDecoder.h
// Multi-order Ambisonics mode-matching decoder (1st/2nd/3rd order).
// Converts K-channel B-format (ACN, K=(order+1)²) to N-speaker output.
// Decoding matrix is the Tikhonov-regularised pseudo-inverse of the
// per-speaker encoding matrix — works for non-uniform and degenerate layouts
// (e.g. horizontal-only layout where Y_1^0 row is all zero).
// RT-safe: no allocation in decode(). All matrices pre-allocated in prepare().

#pragma once
#include "geometry/SpeakerLayout.h"
#include <array>
#include <vector>

namespace spe::ambi {

class AmbiDecoder {
public:
    static constexpr int MAX_ORDER = 3;
    static constexpr int MAX_K     = (MAX_ORDER + 1) * (MAX_ORDER + 1); // 16

    // Build pseudo-inverse decoders for orders 1, 2, 3. Allocates.
    void prepare(const geometry::SpeakerLayout& layout);

    // Multi-order decode: K-channel ACN-ordered SH input → N-speaker output.
    // K = (order+1)² ∈ {4, 9, 16}. order ∈ {1, 2, 3}.
    // sh_planar[k]: planar buffer for ACN channel k, num_samples long.
    // out_interleaved: output buffer sized num_samples * numSpeakers().
    //   out[sample * num_spk + spk_idx]
    // RT-safe: no allocation. Out-of-range order silently clamps to 1.
    void decode(int order, const float* const* sh_planar,
                int num_samples, float* out_interleaved) const noexcept;

    // Legacy 1st-order signature (W, Y, Z, X planar) — kept for compatibility.
    void decode(const float* W, const float* Y, const float* Z, const float* X,
                int num_samples, float* out_interleaved) const noexcept;

    int numSpeakers() const noexcept { return num_speakers_; }

private:
    int num_speakers_ = 0;
    // decode_matrices_[order-1][s * K + k]; K = (order+1)².
    std::array<std::vector<float>, MAX_ORDER> decode_matrices_;

    void buildDecoderForOrder(const geometry::SpeakerLayout& layout, int order);
};

} // namespace spe::ambi
