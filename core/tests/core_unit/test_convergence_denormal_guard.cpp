// test_convergence_denormal_guard.cpp
// Dreamscape Convergence v1.0 — Phase 1.1: audio-thread denormal flush.
//
// The fix moves FTZ/DAZ enabling out of FdnReverb::prepareToPlay (control
// thread only) into a shared header (spe::util::enableDenormalFlush) that the
// audio callback (SpatialEngine::audioBlock) calls on the AUDIO thread. The
// MXCSR/FPCR control register is per-thread, so the prior setup never reached
// the audio thread and reverb/allpass/FDN tails could stall on denormals.
//
// This test proves the guard actually engages FTZ/DAZ on the calling thread:
//   1. (all arches) after enableDenormalFlush(), denormal arithmetic flushes to
//      zero — the behavioral guarantee the audio thread relies on.
//   2. (x86 only) the MXCSR FTZ (bit 15) and DAZ (bit 6) bits are set.
//   3. enableDenormalFlush() performs no heap allocation (RT contract).
//
// Cap-agnostic; no #if on speaker/object caps. Runs at both builds.

#include "util/DenormalGuard.h"
#include "util/RtAssertNoAlloc.h"

#include <cstdio>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
  #include <xmmintrin.h>
  #define DG_X86 1
#endif

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// volatile sinks defeat compile-time constant folding so the denormal actually
// flows through the FPU at runtime under the active rounding/flush mode.
volatile float g_denormal_in = 1e-40f;  // representable IEEE-754 denormal
volatile float g_zero = 0.0f;

}  // namespace

int main() {
    // Engage the guard on THIS thread (stands in for the audio thread).
    spe::util::enableDenormalFlush();

    // (1) Behavioral: a denormal operand / denormal result must flush to zero.
    // Without FTZ/DAZ, g_denormal_in (1e-40) is a nonzero denormal and the sum
    // stays denormal-nonzero; with the guard, the result is exactly 0.
    {
        const float y = g_denormal_in + g_zero;
        check(y == 0.0f, "denormal arithmetic did not flush to zero (FTZ/DAZ off)");
    }

#ifdef DG_X86
    // (2) Direct register evidence on x86: FTZ (bit 15) and DAZ (bit 6).
    const unsigned int csr = _mm_getcsr();
    check((csr & (1u << 15)) != 0u, "MXCSR FTZ (flush-to-zero) bit not set");
    check((csr & (1u << 6)) != 0u, "MXCSR DAZ (denormals-are-zero) bit not set");
#endif

    // (3) No-alloc: the guard must not allocate (it runs on the audio thread).
    // SPE_RT_NO_ALLOC_SCOPE() only instruments allocations when SPE_RT_ASSERTS=1;
    // otherwise it is a no-op and this assertion would pass vacuously. Guard the
    // check so a non-instrumented build reports a skip instead of a false pass.
#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
    {
        const auto before = spe::util::g_rt_alloc_violations.load();
        {
            SPE_RT_NO_ALLOC_SCOPE();
            spe::util::enableDenormalFlush();
        }
        const auto after = spe::util::g_rt_alloc_violations.load();
        check(after == before, "enableDenormalFlush allocated under no-alloc scope");
    }
#else
    std::printf("test_convergence_denormal_guard: no-alloc check SKIPPED "
                "(build without SPE_RT_ASSERTS)\n");
#endif

    if (g_failures == 0) {
        std::printf("test_convergence_denormal_guard: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_convergence_denormal_guard: %d FAILURE(S)\n", g_failures);
    return 1;
}
