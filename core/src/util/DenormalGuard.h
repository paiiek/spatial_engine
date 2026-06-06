// core/src/util/DenormalGuard.h
//
// JUCE-free flush-to-zero / denormals-are-zero (FTZ/DAZ) control for the
// calling thread. Equivalent to juce::ScopedNoDenormals but header-only and
// framework-neutral so the NO_JUCE core can use it.
//
// IMPORTANT: the FTZ/DAZ mode lives in a *per-thread* control register
// (x86-64 MXCSR, AArch64 FPCR). Setting it on the control thread (e.g. in a
// prepareToPlay) does NOT affect the audio thread. To suppress denormal stalls
// in reverb / allpass / FDN tails, enableDenormalFlush() MUST be called from
// the audio thread itself — call it at the top of the audio callback.
//
// The call is a couple of register writes; doing it once per block is cheap and
// idempotent (the bits are sticky for the thread, so re-setting is a no-op cost
// beyond the writes themselves).

#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
  #define SPE_DENORMAL_FLUSH_X86 1
#elif defined(__aarch64__)
  #include <cstdint>
  #define SPE_DENORMAL_FLUSH_AARCH64 1
#endif

namespace spe::util {

// Enable flush-to-zero / denormals-are-zero for the calling thread.
// No-op on architectures without a known control register.
inline void enableDenormalFlush() noexcept {
#if defined(SPE_DENORMAL_FLUSH_X86)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(SPE_DENORMAL_FLUSH_AARCH64)
    // FPCR bit 24 (FZ) flushes denormal results to zero.
    std::uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (std::uint64_t{1} << 24);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
}

}  // namespace spe::util

// Keep the arch-detect macros local to this header — it is included broadly, so
// do not leak object-like macros into every translation unit.
#undef SPE_DENORMAL_FLUSH_X86
#undef SPE_DENORMAL_FLUSH_AARCH64
