// core/src/util/RtAssertNoAlloc.cpp
//
// Global operator new / delete override that records a violation whenever
// the calling thread is inside an RtAssertNoAllocScope. Linked only when
// SPATIAL_ENGINE_RT_ASSERTS=ON (test build). In Release builds this TU is
// not compiled, leaving libstdc++'s default allocators untouched.

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS

#include "util/RtAssertNoAlloc.h"

#include <cstdlib>
#include <new>

namespace {

void record_violation() noexcept {
    if (spe::util::g_rt_no_alloc_depth > 0) {
        spe::util::g_rt_alloc_violations.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

void* operator new(std::size_t n) {
    record_violation();
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t n) {
    record_violation();
    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    record_violation();
    return std::malloc(n == 0 ? 1 : n);
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    record_violation();
    return std::malloc(n == 0 ? 1 : n);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#endif  // SPE_RT_ASSERTS
