// test_p_binaural_setDirection_no_alloc.cpp
// P3: RT-no-alloc infra test for setBinauralEnabled.
//
// Spawns a SpatialEngine, runs 64 audioBlocks on the main thread under
// SPE_RT_NO_ALLOC_SCOPE, while a second thread toggles setBinauralEnabled
// 100 times. Asserts rt_alloc_violations() == 0.
//
// In v0.4 this passes trivially — no .speh is loaded, setBinauralEnabled
// only stores a flag. v0.5 P1 will extend this test with an actual SOFA load.

#include "audio_io/NullBackend.h"
#include "core/SpatialEngine.h"
#include "util/RtAssertNoAlloc.h"

#include <atomic>
#include <cstdio>
#include <thread>

int main() {
    spe::util::rt_alloc_violations_reset();

    spe::audio_io::NullBackend backend(48000.0, 8, 256);
    spe::core::SpatialEngine   engine;

    // Arm the toggle thread before pumping.
    std::atomic<bool> done{false};
    std::thread toggler([&engine, &done] {
        for (int i = 0; i < 100; ++i) {
            engine.setBinauralEnabled(true);
            engine.setBinauralEnabled(false);
        }
        done.store(true);
    });

    // 64 blocks under no-alloc scope.
    auto err = backend.pump_synchronous(&engine, 64);
    if (err != spe::audio_io::BackendError::Ok) {
        toggler.join();
        std::fprintf(stderr, "FAIL: pump_synchronous returned error\n");
        return 1;
    }

    toggler.join();

    auto v = spe::util::rt_alloc_violations();
    if (v != 0) {
        std::fprintf(stderr,
            "FAIL: rt_alloc_violations=%llu (expected 0)\n",
            static_cast<unsigned long long>(v));
        return 1;
    }

    std::puts("PASS test_p_binaural_setDirection_no_alloc");
    return 0;
}
