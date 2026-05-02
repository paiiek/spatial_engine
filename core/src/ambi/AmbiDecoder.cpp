// core/src/ambi/AmbiDecoder.cpp

#include "ambi/AmbiDecoder.h"
#include <cmath>
#include <cstring>

namespace spe::ambi {

void AmbiDecoder::prepare(const geometry::SpeakerLayout& layout) {
    num_speakers_ = static_cast<int>(layout.speakers.size());
    decode_matrix_.resize(static_cast<size_t>(num_speakers_));

    // Build mode-matching decode matrix using transpose normalisation.
    // For a uniform layout with N speakers the pseudo-inverse simplifies to:
    //   D = (1/N) * E^T
    // where E is the N×4 encoding matrix (one SH row per speaker position).
    //
    // SH row for speaker at (az, el) — matching AmbisonicEncoder convention:
    //   az: atan2(x,z), az=0 → +z front, az=π/2 → +x right
    //   SH: [W=1, Y=cos(el)*sin(az), Z=sin(el), X=cos(el)*cos(az)]
    //   channel order in decode_matrix_: 0=W, 1=Y, 2=Z, 3=X

    const float inv_N = 1.0f / static_cast<float>(num_speakers_);

    for (int s = 0; s < num_speakers_; ++s) {
        // Speaker stores Cartesian (x=right, y=up, z=front).
        // Engine convention: az = atan2(x, z), el = atan2(y, sqrt(x²+z²)).
        const float sx = layout.speakers[static_cast<size_t>(s)].x;
        const float sy = layout.speakers[static_cast<size_t>(s)].y;
        const float sz = layout.speakers[static_cast<size_t>(s)].z;
        const float horiz = std::sqrt(sx * sx + sz * sz);
        const float az    = std::atan2(sx, sz);
        const float el    = std::atan2(sy, horiz);
        const float cos_el = std::cos(el);
        decode_matrix_[static_cast<size_t>(s)] = {
            inv_N * 1.0f,                            // W
            inv_N * cos_el * std::sin(az),           // Y
            inv_N * std::sin(el),                    // Z
            inv_N * cos_el * std::cos(az)            // X
        };
    }
}

void AmbiDecoder::decode(const float* W, const float* Y, const float* Z, const float* X,
                          int num_samples, float* out_interleaved) const noexcept {
    const int S = num_speakers_;
    std::memset(out_interleaved, 0, sizeof(float) * static_cast<size_t>(num_samples * S));

    for (int n = 0; n < num_samples; ++n) {
        const float w = W[n], y = Y[n], z = Z[n], x = X[n];
        float* out_frame = out_interleaved + n * S;
        for (int s = 0; s < S; ++s) {
            const auto& d = decode_matrix_[static_cast<size_t>(s)];
            out_frame[s] = d[0] * w + d[1] * y + d[2] * z + d[3] * x;
        }
    }
}

} // namespace spe::ambi
