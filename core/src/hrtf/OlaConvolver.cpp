// core/src/hrtf/OlaConvolver.cpp

#include "hrtf/OlaConvolver.h"
#include <cassert>
#include <cstring>
#include <algorithm>

namespace spe::hrtf {

void OlaConvolver::prepare(const float* ir, int ir_len, int block_size)
{
    if (!ir || ir_len < 1 || block_size < 1) {
        ir_.clear();
        overlap_.clear();
        work_.clear();
        block_size_ = 0;
        return;
    }

    ir_.assign(ir, ir + ir_len);
    overlap_.assign(static_cast<std::size_t>(ir_len - 1), 0.f);
    work_.assign(static_cast<std::size_t>(block_size + ir_len - 1), 0.f);
    block_size_ = block_size;
}

void OlaConvolver::prepareForReload(int max_ir_len, int block_size)
{
    // Clamp + sanity: caller passes a value in (0, kOlaMaxIRLength]; default to
    // kOlaMaxIRLength on out-of-range.
    if (max_ir_len <= 0 || max_ir_len > kOlaMaxIRLength) {
        max_ir_len = kOlaMaxIRLength;
    }
    if (block_size < 1) block_size = 1;

    block_size_ = block_size;

    // Reserve + size to worst-case so loadInto() never reallocates.
    // After priming, ir_ holds kOlaMaxIRLength zeros (so isReady() returns true);
    // first loadInto() will resize to the real IR length and copy.
    ir_.reserve(static_cast<std::size_t>(max_ir_len));
    ir_.assign(static_cast<std::size_t>(max_ir_len), 0.f);

    overlap_.reserve(static_cast<std::size_t>(max_ir_len - 1));
    overlap_.assign(static_cast<std::size_t>(max_ir_len - 1), 0.f);

    work_.reserve(static_cast<std::size_t>(block_size + max_ir_len - 1));
    work_.assign(static_cast<std::size_t>(block_size + max_ir_len - 1), 0.f);
}

void OlaConvolver::loadInto(const float* ir, int ir_len)
{
    // Release-build: silent no-op on capacity violation; counter increments.
    // Note: the capacity-check uses the locked architectural max (kOlaMaxIRLength)
    // — callers must have primed via prepareForReload() with max_ir_len >= ir_len.
    const std::size_t need_ir      = static_cast<std::size_t>(kOlaMaxIRLength);
    const std::size_t need_overlap = static_cast<std::size_t>(kOlaMaxIRLength - 1);
    const std::size_t need_work    = static_cast<std::size_t>(block_size_ + kOlaMaxIRLength - 1);
    if (!ir || ir_len <= 0 || ir_len > kOlaMaxIRLength ||
        ir_.capacity()      < need_ir      ||
        overlap_.capacity() < need_overlap ||
        work_.capacity()    < need_work)
    {
        load_into_failures_.fetch_add(1, std::memory_order_relaxed);
        assert(false && "OlaConvolver::loadInto preconditions violated");
        return;  // no-op in release
    }

    ir_.resize(static_cast<std::size_t>(ir_len));        // no realloc (capacity preserved)
    std::copy(ir, ir + ir_len, ir_.begin());

    overlap_.resize(static_cast<std::size_t>(ir_len - 1));  // no realloc
    std::fill(overlap_.begin(), overlap_.end(), 0.f);       // flush tail on reload

    work_.resize(static_cast<std::size_t>(block_size_ + ir_len - 1));  // no realloc
    // work_ is fully overwritten at the start of each process(), so no fill here.
}

void OlaConvolver::process(const float* input, int num_samples, float* output)
{
    if (ir_.empty()) {
        std::memcpy(output, input, static_cast<std::size_t>(num_samples) * sizeof(float));
        return;
    }

    const int ir_len   = static_cast<int>(ir_.size());
    const int tail_len = ir_len - 1;

    // Zero the work buffer (reuse pre-allocated storage — no heap alloc).
    std::fill(work_.begin(), work_.end(), 0.f);

    // Direct convolution — outer loop on IR taps for stride-1 input access (SIMD-friendly).
    for (int k = 0; k < ir_len; ++k) {
        const float ir_k = ir_[static_cast<std::size_t>(k)];
        for (int n = 0; n < num_samples; ++n)
            work_[static_cast<std::size_t>(n + k)] += input[n] * ir_k;
    }

    // Add saved overlap to head of work buffer.
    for (int i = 0; i < tail_len; ++i)
        work_[static_cast<std::size_t>(i)] += overlap_[static_cast<std::size_t>(i)];

    // Copy first num_samples to output.
    for (int i = 0; i < num_samples; ++i)
        output[i] = work_[static_cast<std::size_t>(i)];

    // Save tail for next block.
    for (int i = 0; i < tail_len; ++i)
        overlap_[static_cast<std::size_t>(i)] = work_[static_cast<std::size_t>(num_samples + i)];
}

void OlaConvolver::reset()
{
    std::fill(overlap_.begin(), overlap_.end(), 0.f);
}

} // namespace spe::hrtf
