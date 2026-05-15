// test_binaural_b1_one_object.cpp
//
// v0.5 P3: assert per-object B1 path emits non-zero binaural output
// for one active object at a known direction.

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
    if (mon.initialize(cfg) != spe::output::BinauralMonitor::InitResult::Ok) {
        std::fprintf(stderr, "FAIL: initialize\n");
        return 1;
    }

    // Set object 0 to az=π/2 (fixture: L delay=0, R delay=2). The auto-prime
    // in initialize() targets (az=0, el=0) — a subsequent setDirection
    // triggers a 2-block crossfade. Render 3 quiet blocks to clear the fade,
    // then send the impulse.
    mon.setDirection(0, static_cast<float>(M_PI_2), 0.f);

    std::vector<float> in(blockSize, 0.f);
    std::vector<float> outL(blockSize, 0.f);
    std::vector<float> outR(blockSize, 0.f);
    for (int b = 0; b < 3; ++b) {
        mon.processBlockForObject(0, in.data(), blockSize, outL.data(), outR.data());
    }
    // Now fire the impulse on a fresh block at steady state.
    in[0] = 1.f;
    std::fill(outL.begin(), outL.end(), 0.f);
    std::fill(outR.begin(), outR.end(), 0.f);
    mon.processBlockForObject(0, in.data(), blockSize, outL.data(), outR.data());

    if (std::abs(outL[0] - 1.f) > 1e-5f) {
        std::fprintf(stderr, "FAIL: outL[0]=%f expected 1.0\n", outL[0]);
        return 1;
    }
    if (std::abs(outR[2] - 1.f) > 1e-5f) {
        std::fprintf(stderr, "FAIL: outR[2]=%f expected 1.0\n", outR[2]);
        return 1;
    }
    // Sanity: outR[0] should be zero.
    if (std::abs(outR[0]) > 1e-5f) {
        std::fprintf(stderr, "FAIL: outR[0]=%f expected ~0\n", outR[0]);
        return 1;
    }

    std::puts("PASS test_binaural_b1_one_object");
    return 0;
}
