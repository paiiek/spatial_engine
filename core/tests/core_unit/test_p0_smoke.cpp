// P0 smoke test: link the core, read a Constants value, return 0.
// Real CTest suite begins at P1 (AbstractFifo SPSC roundtrip + RT_ASSERT_NO_ALLOC).

#include "core/Constants.h"

#include <cstdio>
#include <cstdlib>

int main() {
    static_assert(spe::MAX_OBJECTS == 64, "MAX_OBJECTS expanded to 64 by US-002");
    static_assert(spe::MAX_BLOCK == 512, "MAX_BLOCK pinned at 512 frames");
    static_assert(spe::ALGO_SWAP_K == 256, "Algorithm-swap crossfade per ADR 0006");

    if (spe::SOUND_C < 340.0f || spe::SOUND_C > 350.0f) {
        std::fprintf(stderr, "SOUND_C out of expected band: %f\n", spe::SOUND_C);
        return 1;
    }
    std::printf("p0_smoke OK: MAX_OBJECTS=%d MAX_BLOCK=%d ALGO_SWAP_K=%d SOUND_C=%.2f\n",
                spe::MAX_OBJECTS, spe::MAX_BLOCK, spe::ALGO_SWAP_K, spe::SOUND_C);
    return 0;
}
