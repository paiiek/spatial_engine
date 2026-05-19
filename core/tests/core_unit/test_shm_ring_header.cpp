// core/tests/core_unit/test_shm_ring_header.cpp
//
// ADR 0019 PR1 — header-layout unit tests. No system dependencies; these
// run on any platform that compiles the header.
//
// Tests:
//   1. ring_header_size_is_4096
//   2. ring_header_offsets_match_adr
//   3. magic_constant_matches_adr
//   4. channel_byte_offset_first_channel_at_4096
//   5. total_region_bytes_8ch_8192frames

#include "audio_io/shm/RingHeader.h"

#include <cassert>
#include <cstdio>

#if defined(__linux__) || defined(__APPLE__)

using namespace spe::audio_io::shm;

static int test_ring_header_size_is_4096() {
    static_assert(sizeof(RingHeader) == 4096, "RingHeader size");
    assert(sizeof(RingHeader) == 4096u);
    std::printf("  PASS  ring_header_size_is_4096\n");
    return 0;
}

static int test_ring_header_offsets_match_adr() {
    static_assert(offsetof(RingHeader, magic)                      == 0x0000);
    static_assert(offsetof(RingHeader, version)                    == 0x0008);
    static_assert(offsetof(RingHeader, header_size)                == 0x000C);
    static_assert(offsetof(RingHeader, sample_rate)                == 0x0010);
    static_assert(offsetof(RingHeader, block_size)                 == 0x0014);
    static_assert(offsetof(RingHeader, channels)                   == 0x0018);
    static_assert(offsetof(RingHeader, capacity_frames)            == 0x001C);
    static_assert(offsetof(RingHeader, write_idx)                  == 0x0020);
    static_assert(offsetof(RingHeader, read_idx)                   == 0x0028);
    static_assert(offsetof(RingHeader, producer_pid)               == 0x0030);
    static_assert(offsetof(RingHeader, producer_heartbeat_ms)      == 0x0034);
    static_assert(offsetof(RingHeader, xrun_count)                 == 0x003C);
    static_assert(offsetof(RingHeader, producer_meta_block_pts_ns) == 0x0044);
    static_assert(offsetof(RingHeader, producer_state)             == 0x004C);
    static_assert(offsetof(RingHeader, seq)                        == 0x0050);
    static_assert(offsetof(RingHeader, _reserved)                  == 0x0058);
    std::printf("  PASS  ring_header_offsets_match_adr\n");
    return 0;
}

static int test_magic_constant_matches_adr() {
    static_assert(kSpeRingMagic == 0x53504543484D4E47ULL, "magic constant");
    assert(kSpeRingMagic == 0x53504543484D4E47ULL);
    std::printf("  PASS  magic_constant_matches_adr\n");
    return 0;
}

static int test_channel_byte_offset_first_channel_at_4096() {
    // Channel 0 must start immediately after the header.
    assert(channel_byte_offset(0, 8192) == 4096u);
    // Channel 1 starts 8192 * 4 bytes later.
    assert(channel_byte_offset(1, 8192) == 4096u + 8192u * 4u);
    std::printf("  PASS  channel_byte_offset_first_channel_at_4096\n");
    return 0;
}

static int test_total_region_bytes_8ch_8192frames() {
    // 4096 + 8 * 8192 * 4 = 4096 + 262144 = 266240
    const std::size_t expected = 4096u + 8u * 8192u * 4u;
    assert(expected == 266240u);
    assert(total_region_bytes(8, 8192) == expected);
    std::printf("  PASS  total_region_bytes_8ch_8192frames\n");
    return 0;
}

int main() {
    std::printf("=== test_shm_ring_header ===\n");
    int rc = 0;
    rc |= test_ring_header_size_is_4096();
    rc |= test_ring_header_offsets_match_adr();
    rc |= test_magic_constant_matches_adr();
    rc |= test_channel_byte_offset_first_channel_at_4096();
    rc |= test_total_region_bytes_8ch_8192frames();
    if (rc == 0) std::printf("All shm_ring_header tests PASSED.\n");
    return rc;
}

#else

int main() {
    std::printf("SKIP: shm ring header tests require Linux or macOS.\n");
    return 0;
}

#endif
