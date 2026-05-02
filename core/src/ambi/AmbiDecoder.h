// core/src/ambi/AmbiDecoder.h
// 1st-order Ambisonics mode-matching decoder.
// Converts 4-channel B-format (ACN/SN3D: W, Y, Z, X) to N-speaker output.
// RT-safe: no allocation in decode(). All buffers pre-allocated in prepare().

#pragma once
#include "geometry/SpeakerLayout.h"
#include <array>
#include <vector>

namespace spe::ambi {

class AmbiDecoder {
public:
    // Must be called before decode(). May allocate.
    void prepare(const geometry::SpeakerLayout& layout);

    // Decode 4-channel B-format to interleaved speaker output.
    // W/Y/Z/X: planar input buffers, each num_samples long.
    // out_interleaved: output buffer sized num_samples * numSpeakers().
    //   out[sample * num_spk + spk_idx]
    // RT-safe: no allocation.
    void decode(const float* W, const float* Y, const float* Z, const float* X,
                int num_samples, float* out_interleaved) const noexcept;

    int numSpeakers() const noexcept { return num_speakers_; }

private:
    int num_speakers_ = 0;
    // decode_matrix_[speaker][channel] — channel: 0=W, 1=Y, 2=Z, 3=X
    // Using transpose-normalisation: D[spk][ch] = (1/N) * SH_at_spk[spk][ch]
    std::vector<std::array<float, 4>> decode_matrix_;
};

} // namespace spe::ambi
