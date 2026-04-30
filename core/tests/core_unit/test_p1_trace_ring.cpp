// P1: TraceRing SPSC roundtrip + overflow drop counter.

#include "util/TraceRing.h"

#include <cstdio>

int main() {
    spe::util::TraceRing<16> ring;

    // Fill 15 entries (capacity-1 slots usable for SPSC).
    for (std::uint32_t i = 0; i < 15; ++i) {
        spe::util::TraceEvent e;
        e.kind = i;
        if (!ring.push(e)) {
            std::fprintf(stderr, "FAIL: push %u failed unexpectedly\n", i);
            return 1;
        }
    }
    // 16th push must overflow.
    if (ring.push({})) {
        std::fprintf(stderr, "FAIL: ring did not overflow at full capacity\n");
        return 1;
    }
    if (ring.drops() != 1) {
        std::fprintf(stderr, "FAIL: drops=%llu (expected 1)\n",
                     static_cast<unsigned long long>(ring.drops()));
        return 1;
    }

    // Drain and verify FIFO order.
    spe::util::TraceEvent out;
    for (std::uint32_t i = 0; i < 15; ++i) {
        if (!ring.pop(out)) return 1;
        if (out.kind != i) {
            std::fprintf(stderr, "FAIL: order mismatch at %u (got %u)\n", i, out.kind);
            return 1;
        }
    }
    if (ring.pop(out)) {
        std::fprintf(stderr, "FAIL: pop on empty ring should return false\n");
        return 1;
    }

    std::printf("p1_trace_ring OK\n");
    return 0;
}
