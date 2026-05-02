// core/src/hrtf/OlaConvolver.h
// Overlap-add convolution for a fixed-length IR and fixed block size.
// Pure C++, no FFT library dependency. Direct convolution with internal
// overlap buffer. Suitable for monitor-path IR lengths up to ~1024.

#pragma once

#include <vector>

namespace spe::hrtf {

class OlaConvolver {
public:
    // Set IR and block size. ir_len must be >= 1.
    void prepare(const float* ir, int ir_len, int block_size);

    // Convolve one block of mono input, accumulate into output.
    // output must be at least block_size floats (zeroed or pre-filled by caller).
    void process(const float* input, int num_samples, float* output);

    // Clear overlap buffer (call on flush/reset).
    void reset();

    bool isReady() const { return !ir_.empty(); }

private:
    std::vector<float> ir_;
    std::vector<float> overlap_;  // (ir_len - 1) tail from previous block
    int                block_size_ = 0;
};

} // namespace spe::hrtf
