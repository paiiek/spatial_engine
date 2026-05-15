// test_ola_convolver_loadinto_rt_safe.cpp
//
// v0.5 P0: assert OlaConvolver::loadInto() is alloc-free after prepareForReload().
//
// Strategy:
//   1) Prime the convolver with prepareForReload(kOlaMaxIRLength, blockSize=256).
//   2) Under SPE_RT_NO_ALLOC_SCOPE, call loadInto() 1000 times with varying
//      ir_len from {64, 128, 256, 384, 512, 1024} and varying IR contents.
//   3) Assert rt_alloc_violations() == 0 and loadIntoFailures() == 0.
//
// Notes:
//   * When SPE_RT_ASSERTS is OFF this test still exercises the path but
//     can't observe allocations via the override; we still validate the
//     failure-counter is zero and that loadInto() leaves the convolver
//     in a process()-able state.

#include "hrtf/OlaConvolver.h"
#include "util/RtAssertNoAlloc.h"

#include <array>
#include <cstdio>
#include <vector>

int main()
{
    spe::util::rt_alloc_violations_reset();

    constexpr int blockSize = 256;
    spe::hrtf::OlaConvolver conv;
    conv.prepareForReload(spe::hrtf::kOlaMaxIRLength, blockSize);

    // Sanity: post-priming the convolver is "ready" (ir_.size() == max).
    if (!conv.isReady()) {
        std::fprintf(stderr, "FAIL: convolver not ready after prepareForReload\n");
        return 1;
    }

    const int ir_lens[] = {64, 128, 256, 384, 512, 1024};
    constexpr int kNumIterations = 1000;

    // Pre-fill a worst-case-length IR buffer; loadInto() reads only ir_len bytes.
    std::array<float, spe::hrtf::kOlaMaxIRLength> ir_buffer{};
    for (int i = 0; i < spe::hrtf::kOlaMaxIRLength; ++i) {
        ir_buffer[static_cast<std::size_t>(i)] =
            0.01f * static_cast<float>(i % 100);
    }

    // Drive loadInto under no-alloc scope.
    {
        SPE_RT_NO_ALLOC_SCOPE();
        for (int it = 0; it < kNumIterations; ++it) {
            const int ir_len = ir_lens[static_cast<std::size_t>(it) % 6u];
            // Vary content slightly across iterations.
            ir_buffer[0] = static_cast<float>(it) * 1e-4f;
            conv.loadInto(ir_buffer.data(), ir_len);
        }
    }

    // Verify counters.
    const auto failures = conv.loadIntoFailures();
    if (failures != 0u) {
        std::fprintf(stderr,
            "FAIL: loadIntoFailures()=%llu (expected 0)\n",
            static_cast<unsigned long long>(failures));
        return 1;
    }

    const auto violations = spe::util::rt_alloc_violations();
    if (violations != 0u) {
        std::fprintf(stderr,
            "FAIL: rt_alloc_violations()=%llu (expected 0)\n",
            static_cast<unsigned long long>(violations));
        return 1;
    }

    // Sanity: process() one block to confirm the convolver is functional after reloads.
    std::vector<float> input(blockSize, 0.f);
    std::vector<float> output(blockSize, 0.f);
    input[0] = 1.f;
    conv.process(input.data(), blockSize, output.data());

    std::puts("PASS test_ola_convolver_loadinto_rt_safe");
    return 0;
}
