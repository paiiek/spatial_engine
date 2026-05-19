// core/tests/core_unit/test_shared_memory_region.cpp
//
// ADR 0019 PR1 — SharedMemoryRegion attach/detach tests.
// Requires /dev/shm (Linux) or POSIX shm (macOS).
//
// Tests:
//   6. attach_create_or_open_creates_region
//   7. attach_open_existing_finds_prior_creation
//   8. detach_releases_mapping
//   9. name_too_long_returns_error
//
// Cleanup: each test calls shm_unlink() directly — the class deliberately
// does not unlink (producer owns lifecycle per ADR §3 note).
//
// Unique shm names use PID + a counter to avoid collision across parallel
// ctest runs.

#include "audio_io/shm/SharedMemoryRegion.h"
#include "audio_io/shm/RingHeader.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(__linux__) || defined(__APPLE__)

#include <sys/mman.h>   // shm_unlink
#include <unistd.h>     // getpid

using namespace spe::audio_io::shm;

static int g_counter = 0;

static std::string unique_shm_name() {
    return "/spe-test-" + std::to_string(static_cast<long>(::getpid()))
           + "-" + std::to_string(++g_counter);
}

// ── Test 6 ─────────────────────────────────────────────────────────────────

static int test_attach_create_or_open_creates_region() {
    const std::size_t sz = total_region_bytes(2, 1024);
    const std::string name = unique_shm_name();

    SharedMemoryRegion r;
    RegionError err = r.attach(name.c_str(), AttachMode::CreateOrOpen, sz);
    assert(err == RegionError::Ok);
    assert(r.is_attached());
    assert(r.size() == sz);
    assert(r.base() != nullptr);
    assert(r.header() != nullptr);

    r.detach();
    ::shm_unlink(name.c_str());
    std::printf("  PASS  attach_create_or_open_creates_region\n");
    return 0;
}

// ── Test 7 ─────────────────────────────────────────────────────────────────

static int test_attach_open_existing_finds_prior_creation() {
    const std::size_t sz = total_region_bytes(2, 1024);
    const std::string name = unique_shm_name();

    // First attach: create and write the magic sentinel.
    SharedMemoryRegion r1;
    RegionError err1 = r1.attach(name.c_str(), AttachMode::CreateOrOpen, sz);
    assert(err1 == RegionError::Ok);
    r1.header()->magic = kSpeRingMagic;

    // Second attach: open existing and verify the same magic is visible.
    SharedMemoryRegion r2;
    RegionError err2 = r2.attach(name.c_str(), AttachMode::OpenExisting, sz);
    assert(err2 == RegionError::Ok);
    assert(r2.header()->magic == kSpeRingMagic);

    r1.detach();
    r2.detach();
    ::shm_unlink(name.c_str());
    std::printf("  PASS  attach_open_existing_finds_prior_creation\n");
    return 0;
}

// ── Test 8 ─────────────────────────────────────────────────────────────────

static int test_detach_releases_mapping() {
    const std::size_t sz = total_region_bytes(1, 512);
    const std::string name = unique_shm_name();

    SharedMemoryRegion r;
    RegionError err = r.attach(name.c_str(), AttachMode::CreateOrOpen, sz);
    assert(err == RegionError::Ok);
    assert(r.is_attached());

    r.detach();
    assert(!r.is_attached());
    assert(r.base()   == nullptr);
    assert(r.header() == nullptr);
    assert(r.size()   == 0u);

    ::shm_unlink(name.c_str());
    std::printf("  PASS  detach_releases_mapping\n");
    return 0;
}

// ── Test 9 ─────────────────────────────────────────────────────────────────

static int test_name_too_long_returns_error() {
    // Build a 300-character name (well over POSIX NAME_MAX = 255).
    std::string long_name(300, 'x');
    long_name[0] = '/';  // make it look like a POSIX shm name

    SharedMemoryRegion r;
    RegionError err = r.attach(long_name.c_str(), AttachMode::CreateOrOpen, 4096);
    assert(err == RegionError::NameTooLong);
    assert(!r.is_attached());

    std::printf("  PASS  name_too_long_returns_error\n");
    return 0;
}

// ── main ───────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== test_shared_memory_region ===\n");
    int rc = 0;
    rc |= test_attach_create_or_open_creates_region();
    rc |= test_attach_open_existing_finds_prior_creation();
    rc |= test_detach_releases_mapping();
    rc |= test_name_too_long_returns_error();
    if (rc == 0) std::printf("All shared_memory_region tests PASSED.\n");
    return rc;
}

#else

int main() {
    std::printf("SKIP: shared memory region tests require Linux or macOS.\n");
    return 0;
}

#endif
