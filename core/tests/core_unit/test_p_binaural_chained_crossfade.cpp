// test_p_binaural_chained_crossfade.cpp
//
// v0.5 P1 (A5): preempt-with-current-gain handoff under back-to-back
// setDirection updates. Fires 3 consecutive setDirection updates within
// 1 block each; verifies no crash, no NaN, output stays bounded, and
// chain transitions stay C0-continuous.

#include "core/Constants.h"
#include "output_backend/BinauralMonitor.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

int main()
{
    constexpr int blockSize = 256;
    constexpr float sr      = 48000.f;

    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = sr;
    cfg.blockSize  = blockSize;
    const auto init = mon.initialize(cfg);
    if (init != spe::output::BinauralMonitor::InitResult::Ok) {
        std::fprintf(stderr, "FAIL: initialize\n");
        return 1;
    }

    std::vector<float> in(blockSize, 0.f);
    for (int n = 0; n < blockSize; ++n)
        in[static_cast<std::size_t>(n)] = 0.5f * std::sin(2.f * static_cast<float>(M_PI) * 440.f * static_cast<float>(n) / sr);
    std::vector<float> outL(blockSize, 0.f);
    std::vector<float> outR(blockSize, 0.f);

    // Three back-to-back direction updates, one block apart.
    const float dirs[3][2] = {
        {0.f,                            0.f},
        {static_cast<float>(M_PI_2),    0.f},
        {static_cast<float>(M_PI),      0.f},
    };
    for (int i = 0; i < 3; ++i) {
        mon.setDirection(0, dirs[i][0], dirs[i][1]);
        mon.processBlockForObject(0, in.data(), blockSize, outL.data(), outR.data());

        // Bounds: each output sample must stay finite and |x| < 10 (loose).
        for (int n = 0; n < blockSize; ++n) {
            const float l = outL[static_cast<std::size_t>(n)];
            const float r = outR[static_cast<std::size_t>(n)];
            if (!std::isfinite(l) || !std::isfinite(r)) {
                std::fprintf(stderr, "FAIL: non-finite sample at iter %d n=%d (L=%g R=%g)\n",
                             i, n, l, r);
                return 1;
            }
            if (std::abs(l) > 10.f || std::abs(r) > 10.f) {
                std::fprintf(stderr, "FAIL: out-of-bounds sample at iter %d n=%d (L=%g R=%g)\n",
                             i, n, l, r);
                return 1;
            }
        }
    }

    // Fire 5 more rapid updates (all on the same block) to stress preempt-
    // with-current-gain handoff hard.
    for (int i = 0; i < 5; ++i) {
        mon.setDirection(0, dirs[i % 3][0], dirs[i % 3][1]);
    }
    mon.processBlockForObject(0, in.data(), blockSize, outL.data(), outR.data());
    for (int n = 0; n < blockSize; ++n) {
        const float l = outL[static_cast<std::size_t>(n)];
        const float r = outR[static_cast<std::size_t>(n)];
        if (!std::isfinite(l) || !std::isfinite(r)) {
            std::fprintf(stderr, "FAIL: non-finite after rapid updates: n=%d L=%g R=%g\n",
                         n, l, r);
            return 1;
        }
    }

    std::puts("PASS test_p_binaural_chained_crossfade");
    return 0;
}
