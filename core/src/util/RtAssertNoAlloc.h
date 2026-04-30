// core/src/util/RtAssertNoAlloc.h
//
// Test-build-only sentinel that aborts on any heap allocation while a scoped
// guard is active on the calling thread. Used to harden Principle 1
// (audio thread is allocation-free) against silent regressions.
//
// Usage (audio thread):
//   void audioCallback(...) {
//       SPE_RT_NO_ALLOC_SCOPE();   // declare guard for this scope
//       ...                        // any new/malloc here aborts (test build)
//   }
//
// Globally, the program-wide override is engaged only when SPE_RT_ASSERTS=1
// is defined at build time. See core/CMakeLists.txt
// (option SPATIAL_ENGINE_RT_ASSERTS).

#pragma once

#include <atomic>

namespace spe::util {

// Per-thread flag toggled by RtAssertNoAllocScope. Cheap to read; only the
// custom global operator new branches on it.
inline thread_local int g_rt_no_alloc_depth = 0;

// Global counter of allocation violations seen by the override. Drained by
// tests; never inspected on the audio thread.
inline std::atomic<unsigned long long> g_rt_alloc_violations{0};

class RtAssertNoAllocScope {
public:
    RtAssertNoAllocScope() noexcept { ++g_rt_no_alloc_depth; }
    ~RtAssertNoAllocScope() noexcept { --g_rt_no_alloc_depth; }
    RtAssertNoAllocScope(const RtAssertNoAllocScope&) = delete;
    RtAssertNoAllocScope& operator=(const RtAssertNoAllocScope&) = delete;
};

inline bool rt_no_alloc_active() noexcept {
    return g_rt_no_alloc_depth > 0;
}

inline unsigned long long rt_alloc_violations() noexcept {
    return g_rt_alloc_violations.load(std::memory_order_relaxed);
}

inline void rt_alloc_violations_reset() noexcept {
    g_rt_alloc_violations.store(0, std::memory_order_relaxed);
}

}  // namespace spe::util

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
    #define SPE_RT_NO_ALLOC_SCOPE() ::spe::util::RtAssertNoAllocScope _spe_rt_no_alloc_scope
#else
    #define SPE_RT_NO_ALLOC_SCOPE() do {} while (0)
#endif
