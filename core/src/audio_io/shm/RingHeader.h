// core/src/audio_io/shm/RingHeader.h — ADR 0019 §2.3 POD wire format.
//
// Self-contained header (no spe_core deps). The Python sidecar reads the
// same offsets via struct.unpack_from; keep them stable.
//
// All multi-byte fields: little-endian.
// Atomic fields use std::atomic<T> with #pragma pack(1) so the struct is
// byte-packed and every field sits at exactly the ADR-documented offset.
// On x86-64 and ARM64 std::atomic<uint64_t/uint32_t> are always lock-free
// even at 4-byte alignment (hardware guarantees atomic word ops).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(__linux__) || defined(__APPLE__)

namespace spe::audio_io::shm {

// ── Constants ──────────────────────────────────────────────────────────────

/// "SPECHMNG" as a little-endian u64.
constexpr uint64_t kSpeRingMagic     = 0x53504543484D4E47ULL;
constexpr uint32_t kRingHeaderSize   = 4096u;
constexpr uint32_t kRingHeaderVersion = 1u;

// ── ProducerState ──────────────────────────────────────────────────────────

enum class ProducerState : uint32_t {
    Idle      = 0,
    Streaming = 1,
    Draining  = 2,
    Closed    = 3,
};

// ── Lock-free assertions (compile-time) ────────────────────────────────────

static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "std::atomic<uint64_t> must be lock-free on this platform");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
    "std::atomic<uint32_t> must be lock-free on this platform");
static_assert(sizeof(std::atomic<uint64_t>) == 8,
    "sizeof(std::atomic<uint64_t>) must equal 8");
static_assert(sizeof(std::atomic<uint32_t>) == 4,
    "sizeof(std::atomic<uint32_t>) must equal 4");

// ── RingHeader ─────────────────────────────────────────────────────────────
//
// Byte layout (ADR 0019 §2.3). #pragma pack(1) suppresses all padding so
// every field lands at its documented offset.
//
// | Offset | Field                       | Type          |
// |--------|-----------------------------|---------------|
// | 0x0000 | magic                       | u64           |
// | 0x0008 | version                     | u32           |
// | 0x000C | header_size                 | u32           |
// | 0x0010 | sample_rate                 | u32           |
// | 0x0014 | block_size                  | u32           |
// | 0x0018 | channels                    | u32           |
// | 0x001C | capacity_frames             | u32           |
// | 0x0020 | write_idx                   | atomic<u64>   |
// | 0x0028 | read_idx                    | atomic<u64>   |
// | 0x0030 | producer_pid                | u32           |
// | 0x0034 | producer_heartbeat_ms       | atomic<u64>   |
// | 0x003C | xrun_count                  | atomic<u64>   |
// | 0x0044 | producer_meta_block_pts_ns  | atomic<u64>   |
// | 0x004C | producer_state              | atomic<u32>   |
// | 0x0050 | seq                         | atomic<u64>   |
// | 0x0058 | _reserved (zero-init)       | bytes[0xFA8]  |

#pragma pack(push, 1)

struct alignas(8) RingHeader {
    uint64_t                  magic;                      // 0x0000
    uint32_t                  version;                    // 0x0008
    uint32_t                  header_size;                // 0x000C
    uint32_t                  sample_rate;                // 0x0010
    uint32_t                  block_size;                 // 0x0014
    uint32_t                  channels;                   // 0x0018
    uint32_t                  capacity_frames;            // 0x001C
    std::atomic<uint64_t>     write_idx;                  // 0x0020
    std::atomic<uint64_t>     read_idx;                   // 0x0028
    uint32_t                  producer_pid;               // 0x0030
    std::atomic<uint64_t>     producer_heartbeat_ms;      // 0x0034
    std::atomic<uint64_t>     xrun_count;                 // 0x003C
    std::atomic<uint64_t>     producer_meta_block_pts_ns; // 0x0044
    std::atomic<uint32_t>     producer_state;             // 0x004C
    std::atomic<uint64_t>     seq;                        // 0x0050
    uint8_t                   _reserved[0x0FA8];          // 0x0058 → 0x1000
};

#pragma pack(pop)

// ── Offset assertions (ADR §2.3) ───────────────────────────────────────────

static_assert(offsetof(RingHeader, magic)                      == 0x0000, "magic offset");
static_assert(offsetof(RingHeader, version)                    == 0x0008, "version offset");
static_assert(offsetof(RingHeader, header_size)                == 0x000C, "header_size offset");
static_assert(offsetof(RingHeader, sample_rate)                == 0x0010, "sample_rate offset");
static_assert(offsetof(RingHeader, block_size)                 == 0x0014, "block_size offset");
static_assert(offsetof(RingHeader, channels)                   == 0x0018, "channels offset");
static_assert(offsetof(RingHeader, capacity_frames)            == 0x001C, "capacity_frames offset");
static_assert(offsetof(RingHeader, write_idx)                  == 0x0020, "write_idx offset");
static_assert(offsetof(RingHeader, read_idx)                   == 0x0028, "read_idx offset");
static_assert(offsetof(RingHeader, producer_pid)               == 0x0030, "producer_pid offset");
static_assert(offsetof(RingHeader, producer_heartbeat_ms)      == 0x0034, "producer_heartbeat_ms offset");
static_assert(offsetof(RingHeader, xrun_count)                 == 0x003C, "xrun_count offset");
static_assert(offsetof(RingHeader, producer_meta_block_pts_ns) == 0x0044, "producer_meta_block_pts_ns offset");
static_assert(offsetof(RingHeader, producer_state)             == 0x004C, "producer_state offset");
static_assert(offsetof(RingHeader, seq)                        == 0x0050, "seq offset");
static_assert(offsetof(RingHeader, _reserved)                  == 0x0058, "_reserved offset");

static_assert(sizeof(RingHeader) == 4096, "RingHeader must be exactly 4096 bytes");
static_assert(alignof(RingHeader) >= 8,   "RingHeader must have at least 8-byte alignment");

// ── Helpers ────────────────────────────────────────────────────────────────

/// Byte offset of channel c's ring data within the shared region.
/// Channels are planar: channel c starts at kRingHeaderSize + c * capacity_frames * 4.
inline std::size_t channel_byte_offset(uint32_t channel, uint32_t capacity_frames) noexcept {
    return static_cast<std::size_t>(kRingHeaderSize)
         + static_cast<std::size_t>(channel) * static_cast<std::size_t>(capacity_frames) * sizeof(float);
}

/// Total region size in bytes for the given channel/frame configuration.
inline std::size_t total_region_bytes(uint32_t channels, uint32_t capacity_frames) noexcept {
    return static_cast<std::size_t>(kRingHeaderSize)
         + static_cast<std::size_t>(channels) * static_cast<std::size_t>(capacity_frames) * sizeof(float);
}

}  // namespace spe::audio_io::shm

#endif  // defined(__linux__) || defined(__APPLE__)
