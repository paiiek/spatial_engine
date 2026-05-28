// core/src/audio_io/shm/SharedMemoryRegion.cpp — POSIX implementation.
//
// See SharedMemoryRegion.h for naming conventions and routing rules.
//
// This file is compiled only on Linux (and macOS when the v0.9 guard is
// lifted). The #if guard mirrors the one in the header.

#include "audio_io/shm/SharedMemoryRegion.h"

#if defined(__linux__) || defined(__APPLE__)

#include <cerrno>
#include <cstring>
#include <utility>   // std::swap

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// POSIX shm is in -lrt on Linux; nothing extra on macOS.
#if defined(__linux__)
#  include <sys/types.h>
#endif

namespace spe::audio_io::shm {

namespace {

/// Returns true if the name should use shm_open() semantics:
///   starts with '/' and has no further '/' after position 0.
bool is_posix_shm_name(const char* name) noexcept {
    if (!name || name[0] != '/') return false;
    // Scan for a second slash.
    for (const char* p = name + 1; *p; ++p) {
        if (*p == '/') return false;
    }
    return true;
}

constexpr std::size_t kMaxShmNameLen = 255;

} // namespace

// ── Destructor / move ──────────────────────────────────────────────────────

SharedMemoryRegion::~SharedMemoryRegion() {
    detach();
}

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept
    : base_(other.base_), size_(other.size_), backing_size_(other.backing_size_)
{
    other.base_         = nullptr;
    other.size_         = 0;
    other.backing_size_ = 0;
}

SharedMemoryRegion& SharedMemoryRegion::operator=(SharedMemoryRegion&& other) noexcept {
    if (this != &other) {
        detach();
        base_         = other.base_;
        size_         = other.size_;
        backing_size_ = other.backing_size_;
        other.base_         = nullptr;
        other.size_         = 0;
        other.backing_size_ = 0;
    }
    return *this;
}

// ── attach() ──────────────────────────────────────────────────────────────

RegionError SharedMemoryRegion::attach(const char* name,
                                       AttachMode  mode,
                                       std::size_t size_bytes) noexcept
{
    if (is_attached()) return RegionError::AlreadyAttached;
    if (size_bytes == 0) return RegionError::InvalidSize;

    // Name-length check (POSIX limit is typically NAME_MAX = 255).
    if (name && std::strlen(name) > kMaxShmNameLen) return RegionError::NameTooLong;

    const bool create = (mode == AttachMode::CreateOrOpen);
    int fd = -1;

    if (is_posix_shm_name(name)) {
        // ── POSIX shm_open() path ───────────────────────────────────────
        int oflag = O_RDWR;
        if (create) oflag |= O_CREAT;
        fd = ::shm_open(name, oflag, 0600);
        if (fd < 0) return RegionError::ShmOpenFailed;

        // ftruncate only if we just created (or the file has size 0).
        if (create) {
            struct stat st{};
            if (::fstat(fd, &st) == 0 && st.st_size == 0) {
                if (::ftruncate(fd, static_cast<off_t>(size_bytes)) != 0) {
                    ::close(fd);
                    return RegionError::FtruncateFailed;
                }
            }
        }
    } else {
        // ── Regular file path (open() + mmap) ──────────────────────────
        // O_NOFOLLOW: refuse to follow a symlinked operator-supplied path
        // (PR3-Q7 / ADR 0019 §10 defense-in-depth; mirrors the registry writer
        // PluginInstanceRegistry.cpp). On a symlinked final component open()
        // fails with ELOOP → ShmOpenFailed, which is the intended hardening.
        int oflag = O_RDWR | O_NOFOLLOW;
        if (create) oflag |= O_CREAT;
        fd = ::open(name, oflag, 0600);
        if (fd < 0) return RegionError::ShmOpenFailed;

        if (create) {
            struct stat st{};
            if (::fstat(fd, &st) == 0 && st.st_size == 0) {
                if (::ftruncate(fd, static_cast<off_t>(size_bytes)) != 0) {
                    ::close(fd);
                    return RegionError::FtruncateFailed;
                }
            }
        }
    }

    // Capture backing size before mmap so FIX-3 (PR2 hardening) can compare
    // the actual object size against the geometry the header claims.
    std::size_t backing = 0;
    {
        struct stat st{};
        if (::fstat(fd, &st) == 0 && st.st_size > 0) {
            backing = static_cast<std::size_t>(st.st_size);
        }
    }

    void* ptr = ::mmap(nullptr,
                       size_bytes,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       fd,
                       0);
    // fd can be closed after mmap; the mapping persists until munmap.
    ::close(fd);

    if (ptr == MAP_FAILED) return RegionError::MmapFailed;

    base_         = ptr;
    size_         = size_bytes;
    backing_size_ = backing;
    return RegionError::Ok;
}

// ── detach() ──────────────────────────────────────────────────────────────
//
// NOTE: This does NOT call shm_unlink(). The producer owns the lifecycle.
// Tests that want cleanup must call ::shm_unlink() themselves.

void SharedMemoryRegion::detach() noexcept {
    if (base_ != nullptr) {
        ::munmap(base_, size_);
        base_         = nullptr;
        size_         = 0;
        backing_size_ = 0;
    }
}

// ── header() ──────────────────────────────────────────────────────────────

RingHeader* SharedMemoryRegion::header() noexcept {
    return static_cast<RingHeader*>(base_);
}

const RingHeader* SharedMemoryRegion::header() const noexcept {
    return static_cast<const RingHeader*>(base_);
}

}  // namespace spe::audio_io::shm

#endif  // defined(__linux__) || defined(__APPLE__)
