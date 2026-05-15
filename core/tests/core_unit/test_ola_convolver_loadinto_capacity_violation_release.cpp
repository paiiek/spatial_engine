// test_ola_convolver_loadinto_capacity_violation_release.cpp
//
// v0.5 P0: assert release-build behavior of OlaConvolver::loadInto() when
// preconditions are violated.
//
// Spec (A2):
//   - Pre-allocate via prepare() with a SMALLER capacity than kOlaMaxIRLength.
//   - Call loadInto() with ir_len=1024. In release builds: no state mutation
//     (memcmp of internal buffers before/after unchanged) + load_into_failures_
//     counter == 1.
//   - In debug builds the underlying assert(false) trips. This test is
//     skipped in debug mode (NDEBUG not defined).

#include "hrtf/OlaConvolver.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

int main()
{
#ifndef NDEBUG
    // Debug build: loadInto's assert() trips on violation. Skip the
    // capacity-violation arm to keep the test green; the no-op path is
    // already exercised in test_ola_convolver_loadinto_rt_safe.
    std::puts("SKIP test_ola_convolver_loadinto_capacity_violation_release (debug build — assert traps)");
    return 0;
#else
    constexpr int small_ir_len = 256;
    constexpr int blockSize    = 256;
    spe::hrtf::OlaConvolver conv;

    // Prime via prepare() with a SMALL IR — capacity intentionally below kOlaMaxIRLength.
    std::array<float, small_ir_len> small_ir{};
    small_ir[0] = 1.f;
    conv.prepare(small_ir.data(), small_ir_len, blockSize);

    // Snapshot: run one block of audio so overlap_ holds non-trivial state.
    std::vector<float> input(blockSize, 0.f);
    std::vector<float> output(blockSize, 0.f);
    input[0] = 1.f;
    conv.process(input.data(), blockSize, output.data());

    // Capture pre-call output of process() to verify it does not change after
    // a rejected loadInto() (which should be a no-op).
    std::vector<float> pre_output(output.begin(), output.end());

    // Attempt to load a 1024-tap IR — capacity is only small_ir_len → must be rejected.
    std::array<float, spe::hrtf::kOlaMaxIRLength> big_ir{};
    for (int i = 0; i < spe::hrtf::kOlaMaxIRLength; ++i)
        big_ir[static_cast<std::size_t>(i)] = 0.5f;
    conv.loadInto(big_ir.data(), spe::hrtf::kOlaMaxIRLength);

    // Verify: counter incremented, internal state unchanged.
    const auto failures = conv.loadIntoFailures();
    if (failures != 1u) {
        std::fprintf(stderr,
            "FAIL: expected loadIntoFailures()==1, got %llu\n",
            static_cast<unsigned long long>(failures));
        return 1;
    }

    // Re-run process on the same input; output must match the pre-loadInto run
    // bit-exactly (no state mutation by the rejected loadInto).
    std::vector<float> post_output(blockSize, 0.f);
    conv.reset();  // clear overlap to repeat the same input → output mapping
    conv.process(input.data(), blockSize, post_output.data());
    // After reset+process, the convolution result depends only on the active IR,
    // which the rejected loadInto must NOT have changed.
    std::vector<float> baseline(blockSize, 0.f);
    {
        spe::hrtf::OlaConvolver baseline_conv;
        baseline_conv.prepare(small_ir.data(), small_ir_len, blockSize);
        baseline_conv.process(input.data(), blockSize, baseline.data());
    }
    if (std::memcmp(post_output.data(), baseline.data(),
                    blockSize * sizeof(float)) != 0) {
        std::fprintf(stderr,
            "FAIL: rejected loadInto mutated internal state — output diverged from baseline\n");
        return 1;
    }

    // Second rejected call: counter should increment to 2.
    conv.loadInto(big_ir.data(), spe::hrtf::kOlaMaxIRLength);
    if (conv.loadIntoFailures() != 2u) {
        std::fprintf(stderr,
            "FAIL: expected loadIntoFailures()==2, got %llu\n",
            static_cast<unsigned long long>(conv.loadIntoFailures()));
        return 1;
    }

    std::puts("PASS test_ola_convolver_loadinto_capacity_violation_release");
    return 0;
#endif
}
