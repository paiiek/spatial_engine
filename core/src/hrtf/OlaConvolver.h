// core/src/hrtf/OlaConvolver.h
// Overlap-add convolution for a fixed-length IR and fixed block size.
// Pure C++, no FFT dependency. Direct convolution with pre-allocated work buffer.
//
// RT-safety contract:
//   process() is alloc-free after prepare(). prepare() allocates; call it from
//   a non-RT thread (e.g., control thread) before audio starts.
//   num_samples MUST equal block_size passed to prepare() — not validated at runtime.
//
// v0.5 P0: added loadInto() + prepareForReload() for RT-safe IR swap.
//   - prepareForReload(max_ir_len, block_size) is the capacity-priming step
//     (control thread, allocates once).
//   - loadInto(ir, ir_len) reloads an IR into a pre-primed convolver without
//     allocating; in release builds, returns a no-op on capacity violation and
//     bumps the atomic `load_into_failures_` counter for telemetry.

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace spe::hrtf {

// Architecturally-locked maximum HRIR length for v0.5.
// Matches the cap enforced by SofaBinReader (kMaxIRLength=1024).
inline constexpr int kOlaMaxIRLength = 1024;

class OlaConvolver {
public:
    // Prepare with IR of length ir_len >= 1 and fixed block size.
    // Allocates internal buffers. NOT RT-safe — call from control thread.
    void prepare(const float* ir, int ir_len, int block_size);

    // v0.5 P0: capacity-priming step. Reserves ir_/overlap_/work_ to hold the
    // worst-case max_ir_len IR plus block_size + max_ir_len - 1 work samples.
    // Allocates; control thread only. After this, loadInto(ir, ir_len) is
    // alloc-free for any ir_len in (0, max_ir_len].
    //
    // The internal `block_size_` is set; ir_.size() == max_ir_len after call.
    // isReady() returns true after this call.
    void prepareForReload(int max_ir_len, int block_size);

    // v0.5 P0: RT-safe IR swap. Replaces the active IR without allocating.
    // Preconditions (debug-asserted):
    //   - ir != nullptr && ir_len > 0 && ir_len <= kOlaMaxIRLength.
    //   - ir_.capacity()      >= kOlaMaxIRLength.
    //   - overlap_.capacity() >= kOlaMaxIRLength - 1.
    //   - work_.capacity()    >= block_size_ + kOlaMaxIRLength - 1.
    // Release-build behavior on capacity violation: no-op + `load_into_failures_`
    // increments. Debug-build: assert. Never allocates in release.
    void loadInto(const float* ir, int ir_len);

    // Convolve one block. num_samples must equal block_size from prepare().
    // output is overwritten (not accumulated). Alloc-free after prepare().
    void process(const float* input, int num_samples, float* output);

    // Clear overlap state (flush). Alloc-free.
    void reset();

    bool isReady() const { return !ir_.empty(); }

    // v0.5 P0: telemetry — count of loadInto() no-ops due to capacity violations.
    // Atomic; readable from any thread.
    std::uint64_t loadIntoFailures() const noexcept {
        return load_into_failures_.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> ir_;
    std::vector<float> overlap_;  // (ir_len - 1) tail carried across blocks
    std::vector<float> work_;     // pre-allocated: block_size + ir_len - 1
    int                block_size_ = 0;

    // v0.5 P0: counts loadInto() rejections (capacity violation in release).
    std::atomic<std::uint64_t> load_into_failures_{0};
};

} // namespace spe::hrtf
