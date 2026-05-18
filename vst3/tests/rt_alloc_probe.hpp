// vst3/tests/rt_alloc_probe.hpp
// C2B postmortem S5 — RT-safety malloc interposition guard.
//
// Strategy: strong-symbol override of malloc/free/calloc delegating to
// glibc-internal __libc_malloc/__libc_free/__libc_calloc. No dlsym, no
// libdl, no recursion risk (Decision B option-α, Round-2 A4).
//
// Requirements:
//   - GLIBC >= 2.30 (ubuntu-24.04 ships glibc 2.39 — CI is pinned)
//   - NOT portable to musl/BSD (documented, non-issue for this CI target)
//   - Test TUs MUST NOT enable -flto (R10 Layer 3 — LTO would inline
//     these overrides and defeat the counter; see CMakeLists.txt)
//   - realloc/aligned_alloc/posix_memalign NOT intercepted (zero hits in
//     core/src by grep, Round-2 A9). Re-evaluate if introduced. See R10.
//
// Usage: #include this header exactly once per test executable TU.
// The thread-local guard + counter are defined here (not just declared).
//
// To arm the guard:
//   g_rt_guard_active = true;
//   g_alloc_count     = 0;
//   /* ... call under test ... */
//   g_rt_guard_active = false;
//   assert(g_alloc_count == 0);
//
// Negative-control assertions (probe_observes_malloc, probe_observes_calloc)
// must fire BEFORE the main test loop to confirm interception is live.

#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

// Thread-local guard state — defined here, extern-declared by any TU that
// needs to arm/disarm without owning the definitions.
static thread_local bool   g_rt_guard_active = false;
static thread_local size_t g_alloc_count     = 0;

// ---------------------------------------------------------------------------
// v0.5.2 #2 — ASan compatibility guard.
//
// The strong-symbol malloc/free overrides below are FUNDAMENTALLY incompatible
// with AddressSanitizer: ASan also installs interceptors on malloc/free, and
// any new-thread startup path (e.g. ASan's pthread_getattr_np during
// asan_thread_start) calls free() on an ASan-tagged buffer. Our override
// routes that free to __libc_free, which sees an unknown size class and
// aborts with "free(): invalid size".
//
// This surfaced in v0.5.1 when the VST3 plugin started spawning a heartbeat
// std::thread inside setActive(true) — previously only the main thread ran
// and ASan never needed to spawn a new thread on the probed binaries.
//
// Under __SANITIZE_ADDRESS__: skip the overrides entirely. ASan owns malloc/
// free; the alloc counter stays at 0 (the RT-alloc gate becomes a no-op).
// The non-ASan ctest run remains the authoritative RT-alloc enforcement
// surface; the ASan run verifies behavioral correctness without RT counting.
// ---------------------------------------------------------------------------
#ifndef __SANITIZE_ADDRESS__

// ---------------------------------------------------------------------------
// glibc internal symbols — direct declaration, no <dlfcn.h>, no dlsym.
// Stable exported symbols since glibc 2.30 (GLIBC_PRIVATE / standard internal
// ABI). Verified present via readelf on ubuntu-24.04 glibc 2.39.
// ---------------------------------------------------------------------------
extern "C" void* __libc_malloc(size_t);
extern "C" void  __libc_free(void*);
extern "C" void* __libc_calloc(size_t, size_t);

// ---------------------------------------------------------------------------
// Strong-symbol overrides — resolved at link time, defeat glibc's weak malloc.
// ---------------------------------------------------------------------------
extern "C" void* malloc(size_t sz)
{
    if (g_rt_guard_active) ++g_alloc_count;
    return __libc_malloc(sz);
}

extern "C" void free(void* p)
{
    __libc_free(p);
}

extern "C" void* calloc(size_t n, size_t sz)
{
    if (g_rt_guard_active) ++g_alloc_count;
    return __libc_calloc(n, sz);
}

// ---------------------------------------------------------------------------
// operator new/new[] — delegate to our malloc (which we now own).
// operator delete/delete[] — delegate to our free.
// Kept for completeness; catches STL container allocations via new.
// ---------------------------------------------------------------------------
void* operator new(std::size_t sz)
{
    if (g_rt_guard_active) ++g_alloc_count;
    void* p = __libc_malloc(sz);
    if (!p) throw std::bad_alloc{};
    return p;
}

void* operator new[](std::size_t sz)
{
    if (g_rt_guard_active) ++g_alloc_count;
    void* p = __libc_malloc(sz);
    if (!p) throw std::bad_alloc{};
    return p;
}

void operator delete(void* p) noexcept              { __libc_free(p); }
void operator delete[](void* p) noexcept             { __libc_free(p); }
void operator delete(void* p, std::size_t) noexcept  { __libc_free(p); }
void operator delete[](void* p, std::size_t) noexcept { __libc_free(p); }

#endif // __SANITIZE_ADDRESS__
