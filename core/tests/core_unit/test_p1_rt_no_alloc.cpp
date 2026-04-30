// P1 exit criterion (test build only, SPE_RT_ASSERTS=1):
// NullBackend driving SpatialEngine for N blocks under steady silence
// triggers zero allocation violations.

#include "audio_io/NullBackend.h"
#include "core/SpatialEngine.h"
#include "util/RtAssertNoAlloc.h"

#include <cstdio>

int main() {
    constexpr int kBlocks = 64;
    spe::util::rt_alloc_violations_reset();

    spe::audio_io::NullBackend backend(48000.0, 8, 64);
    spe::core::SpatialEngine engine;

    auto err = backend.pump_synchronous(&engine, kBlocks);
    if (err != spe::audio_io::BackendError::Ok) return 1;

    auto v = spe::util::rt_alloc_violations();
    if (v != 0) {
        std::fprintf(stderr, "FAIL: rt_alloc_violations=%llu (expected 0)\n",
                     static_cast<unsigned long long>(v));
        return 1;
    }
    std::printf("p1_rt_no_alloc OK: blocks=%d violations=0\n", kBlocks);
    return 0;
}
