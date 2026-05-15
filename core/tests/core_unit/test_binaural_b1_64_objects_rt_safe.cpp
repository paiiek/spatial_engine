// test_binaural_b1_64_objects_rt_safe.cpp
//
// v0.5 P3: assert per-object B1 path is RT-safe with all MAX_OBJECTS=64
// objects active. Each object gets a setDirection() at a random
// position before the block, then processBlockForObject() runs under
// SPE_RT_NO_ALLOC_SCOPE.

#include "core/Constants.h"
#include "output_backend/BinauralMonitor.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

int main()
{
    spe::util::rt_alloc_violations_reset();

    constexpr int blockSize = 256;
    constexpr float sr      = 48000.f;

    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = sr;
    cfg.blockSize  = blockSize;
    if (mon.initialize(cfg) != spe::output::BinauralMonitor::InitResult::Ok) {
        std::fprintf(stderr, "FAIL: initialize\n");
        return 1;
    }

    // Pre-prime all 64 objects with directions.
    for (int i = 0; i < spe::MAX_OBJECTS; ++i) {
        const float az = (static_cast<float>(i) / spe::MAX_OBJECTS) * 2.f * static_cast<float>(M_PI) - static_cast<float>(M_PI);
        mon.setDirection(i, az, 0.f);
    }

    std::vector<float> in(blockSize, 0.f);
    for (int n = 0; n < blockSize; ++n)
        in[static_cast<std::size_t>(n)] = 0.1f * std::sin(2.f * static_cast<float>(M_PI) * 220.f * static_cast<float>(n) / sr);
    std::vector<float> outL(blockSize, 0.f), outR(blockSize, 0.f);
    std::vector<float> sumL(blockSize, 0.f), sumR(blockSize, 0.f);

    {
        SPE_RT_NO_ALLOC_SCOPE();
        // 100 audio blocks with 64 objects each.
        for (int b = 0; b < 100; ++b) {
            for (int n = 0; n < blockSize; ++n) {
                sumL[static_cast<std::size_t>(n)] = 0.f;
                sumR[static_cast<std::size_t>(n)] = 0.f;
            }
            for (int i = 0; i < spe::MAX_OBJECTS; ++i) {
                mon.processBlockForObject(i, in.data(), blockSize, outL.data(), outR.data());
                for (int n = 0; n < blockSize; ++n) {
                    sumL[static_cast<std::size_t>(n)] += outL[static_cast<std::size_t>(n)];
                    sumR[static_cast<std::size_t>(n)] += outR[static_cast<std::size_t>(n)];
                }
            }
        }
    }

    const auto fails = mon.loadIntoFailures();
    if (fails != 0u) {
        std::fprintf(stderr, "FAIL: loadIntoFailures()=%llu\n",
                     static_cast<unsigned long long>(fails));
        return 1;
    }
    const auto v = spe::util::rt_alloc_violations();
    if (v != 0u) {
        std::fprintf(stderr, "FAIL: rt_alloc_violations()=%llu\n",
                     static_cast<unsigned long long>(v));
        return 1;
    }

    std::puts("PASS test_binaural_b1_64_objects_rt_safe");
    return 0;
}
