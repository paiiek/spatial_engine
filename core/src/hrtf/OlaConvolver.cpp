// core/src/hrtf/OlaConvolver.cpp

#include "hrtf/OlaConvolver.h"
#include <cstring>
#include <algorithm>

namespace spe::hrtf {

void OlaConvolver::prepare(const float* ir, int ir_len, int block_size)
{
    ir_.assign(ir, ir + ir_len);
    overlap_.assign(static_cast<std::size_t>(ir_len - 1), 0.f);
    block_size_ = block_size;
}

void OlaConvolver::process(const float* input, int num_samples, float* output)
{
    if (ir_.empty()) {
        // pass-through
        std::memcpy(output, input, static_cast<std::size_t>(num_samples) * sizeof(float));
        return;
    }

    const int ir_len   = static_cast<int>(ir_.size());
    const int tail_len = ir_len - 1;

    // Output length for this block = num_samples + ir_len - 1
    // We add the overlap tail from previous block, then store new tail.

    // Temporary output buffer: num_samples + tail_len
    const int out_len = num_samples + tail_len;
    std::vector<float> buf(static_cast<std::size_t>(out_len), 0.f);

    // Direct convolution
    for (int n = 0; n < num_samples; ++n) {
        for (int k = 0; k < ir_len; ++k) {
            buf[static_cast<std::size_t>(n + k)] += input[n] * ir_[static_cast<std::size_t>(k)];
        }
    }

    // Add saved overlap to first tail_len samples of buf
    for (int i = 0; i < tail_len; ++i)
        buf[static_cast<std::size_t>(i)] += overlap_[static_cast<std::size_t>(i)];

    // Copy first num_samples to output
    for (int i = 0; i < num_samples; ++i)
        output[i] = buf[static_cast<std::size_t>(i)];

    // Save new overlap (last tail_len samples)
    for (int i = 0; i < tail_len; ++i)
        overlap_[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(num_samples + i)];
}

void OlaConvolver::reset()
{
    std::fill(overlap_.begin(), overlap_.end(), 0.f);
}

} // namespace spe::hrtf
