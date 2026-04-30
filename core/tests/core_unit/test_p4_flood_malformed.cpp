// test_p4_flood_malformed.cpp
// P4-b: Flood with malformed packets. Short version (default): 10,000.
//       Long version (LABEL "long"): SPE_FLOOD_LONG=1 → 1,000,000.
// Verifies: rejectCount() == N, no crashes, no memory corruption.

#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstdio>

using namespace spe::ipc;

int main() {
#if defined(SPE_FLOOD_LONG) && SPE_FLOOD_LONG
    const int N = 1'000'000;
    const char* label = "long";
#else
    const int N = 10'000;
    const char* label = "short";
#endif

    CommandDecoder dec;

    // Various malformed packet patterns.
    const uint8_t patterns[][8] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // no leading '/'
        {0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x00, 0x00, 0x00}, // garbage
        {0x01},                                             // too short (1 byte)
        {'/',  'a',  'b',  0x00, 'X',  0x00, 0x00, 0x00}, // no ',' in type-tag
    };
    const int NUM_PATTERNS = 4;

    int total_rejected = 0;
    for (int i = 0; i < N; ++i) {
        const auto& pat = patterns[i % NUM_PATTERNS];
        // pat[2] is 1-byte for the short case; we use actual lengths.
        std::span<const uint8_t> sp;
        if (i % NUM_PATTERNS == 2) {
            sp = std::span<const uint8_t>(pat, 1);
        } else {
            sp = std::span<const uint8_t>(pat, 8);
        }
        Command rt = dec.decode(sp);
        assert(rt.tag == CommandTag::Unknown);
        ++total_rejected;
    }

    assert(dec.rejectCount() == static_cast<uint32_t>(total_rejected));
    (void)label;
    std::printf("PASS test_p4_flood_malformed [%s, N=%d, rejected=%d]\n",
                label, N, total_rejected);
    return 0;
}
