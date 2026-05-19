// core/src/audio_io/shm/SharedMemoryRegion.h — POSIX shm_open + mmap wrapper.
//
// Hides the OS primitive. The class is "attach + detach" only; it never
// calls shm_unlink(). Lifecycle (create/destroy the named object) is the
// producer's responsibility. This matches the producer-is-owner contract
// described in ADR 0019 §3.
//
// Name routing:
//   - POSIX shm name  : string starts with '/' and contains NO further '/'.
//     Example: "/spe-session-1"   → shm_open() path.
//   - Filesystem path : string contains a second '/' (i.e. an embedded slash
//     after the first character).
//     Example: "/tmp/spe-1.shm"   → open() path.
//
// Linux + macOS (POSIX shm) only. Windows is stretch (ADR §5).

#pragma once

#include "audio_io/shm/RingHeader.h"

#include <cstddef>

#if defined(__linux__) || defined(__APPLE__)

namespace spe::audio_io::shm {

enum class AttachMode {
    OpenExisting,   ///< shm/file must already exist; fails if absent.
    CreateOrOpen,   ///< creates if absent, opens if present.
};

enum class RegionError {
    Ok = 0,
    ShmOpenFailed,
    FtruncateFailed,
    MmapFailed,
    InvalidSize,
    NameTooLong,
    AlreadyAttached,
};

class SharedMemoryRegion {
public:
    SharedMemoryRegion() = default;
    ~SharedMemoryRegion();

    SharedMemoryRegion(const SharedMemoryRegion&)            = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

    SharedMemoryRegion(SharedMemoryRegion&&) noexcept;
    SharedMemoryRegion& operator=(SharedMemoryRegion&&) noexcept;

    /// Attach to a named shared-memory region.
    ///
    /// @param name        POSIX shm name ("/spe-1") or filesystem path
    ///                    ("/tmp/spe-1.shm"). See routing rules in file header.
    /// @param mode        Whether to create-or-open or open-existing.
    /// @param size_bytes  Required size. On CreateOrOpen, the region is
    ///                    ftruncate'd to this size if it was just created
    ///                    (fstat shows size == 0). On OpenExisting, the mmap
    ///                    maps exactly size_bytes regardless of file size.
    RegionError attach(const char* name, AttachMode mode, std::size_t size_bytes) noexcept;

    /// Unmap the region. Safe to call on an unattached instance (no-op).
    /// Does NOT shm_unlink — lifecycle is the producer's responsibility.
    void detach() noexcept;

    bool        is_attached() const noexcept { return base_ != nullptr; }
    void*       base()        noexcept       { return base_; }
    const void* base()        const noexcept { return base_; }
    std::size_t size()        const noexcept { return size_; }

    /// Typed pointer to the RingHeader at offset 0.
    /// Precondition: is_attached() && size() >= sizeof(RingHeader).
    RingHeader*       header()       noexcept;
    const RingHeader* header() const noexcept;

private:
    void*       base_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace spe::audio_io::shm

#endif  // defined(__linux__) || defined(__APPLE__)
