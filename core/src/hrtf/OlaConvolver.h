// core/src/hrtf/OlaConvolver.h
// Overlap-add convolution for a fixed-length IR and fixed block size.
// Pure C++, no FFT dependency. Direct convolution with pre-allocated work buffer.
//
// RT-safety contract:
//   process() is alloc-free after prepare(). prepare() allocates; call it from
//   a non-RT thread (e.g., control thread) before audio starts.
//   num_samples MUST equal block_size passed to prepare() — not validated at runtime.

#pragma once

#include <vector>

namespace spe::hrtf {

class OlaConvolver {
public:
    // Prepare with IR of length ir_len >= 1 and fixed block size.
    // Allocates internal buffers. NOT RT-safe — call from control thread.
    void prepare(const float* ir, int ir_len, int block_size);

    // Convolve one block. num_samples must equal block_size from prepare().
    // output is overwritten (not accumulated). Alloc-free after prepare().
    void process(const float* input, int num_samples, float* output);

    // Clear overlap state (flush). Alloc-free.
    void reset();

    bool isReady() const { return !ir_.empty(); }

private:
    std::vector<float> ir_;
    std::vector<float> overlap_;  // (ir_len - 1) tail carried across blocks
    std::vector<float> work_;     // pre-allocated: block_size + ir_len - 1
    int                block_size_ = 0;
};

} // namespace spe::hrtf
