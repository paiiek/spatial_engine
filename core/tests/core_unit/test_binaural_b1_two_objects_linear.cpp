// test_binaural_b1_two_objects_linear.cpp
//
// v0.5 P3: assert two-object per-object HRTF rendering is linear, i.e.,
// the sum of two single-object renders equals the two-object accumulated
// render.

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

    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = sr;
    cfg.blockSize  = blockSize;

    // Helper: warm-up function — quiet 3 blocks after setDirection so any
    // crossfade left over from initialize()'s auto-prime is settled, then
    // run the impulse block.
    auto run_one = [&](spe::output::BinauralMonitor& mon, int obj_id,
                       float az, float gain,
                       std::vector<float>& outL, std::vector<float>& outR) {
        mon.setDirection(obj_id, az, 0.f);
        std::vector<float> quiet(blockSize, 0.f);
        std::vector<float> tmpL(blockSize, 0.f), tmpR(blockSize, 0.f);
        for (int b = 0; b < 3; ++b) {
            mon.processBlockForObject(obj_id, quiet.data(), blockSize, tmpL.data(), tmpR.data());
        }
        std::vector<float> in(blockSize, 0.f); in[0] = gain;
        mon.processBlockForObject(obj_id, in.data(), blockSize, outL.data(), outR.data());
    };

    // Single object 0 at az=0. Object 0 is auto-primed at (0,0) so
    // setDirection(0, 0) is a no-op fade target → no crossfade engaged.
    std::vector<float> sumL(blockSize, 0.f), sumR(blockSize, 0.f);
    {
        spe::output::BinauralMonitor mon;
        if (mon.initialize(cfg) != spe::output::BinauralMonitor::InitResult::Ok) return 1;
        std::vector<float> outL(blockSize, 0.f), outR(blockSize, 0.f);
        run_one(mon, 0, 0.f, 0.7f, outL, outR);
        for (int n = 0; n < blockSize; ++n) {
            sumL[static_cast<std::size_t>(n)] += outL[static_cast<std::size_t>(n)];
            sumR[static_cast<std::size_t>(n)] += outR[static_cast<std::size_t>(n)];
        }
    }
    // Single object 0 at az=π (different IR — R delay=3).
    {
        spe::output::BinauralMonitor mon;
        if (mon.initialize(cfg) != spe::output::BinauralMonitor::InitResult::Ok) return 1;
        std::vector<float> outL(blockSize, 0.f), outR(blockSize, 0.f);
        run_one(mon, 0, static_cast<float>(M_PI), 0.3f, outL, outR);
        for (int n = 0; n < blockSize; ++n) {
            sumL[static_cast<std::size_t>(n)] += outL[static_cast<std::size_t>(n)];
            sumR[static_cast<std::size_t>(n)] += outR[static_cast<std::size_t>(n)];
        }
    }

    // Combined: two objects in one monitor instance.
    spe::output::BinauralMonitor mon;
    if (mon.initialize(cfg) != spe::output::BinauralMonitor::InitResult::Ok) return 1;
    // obj 0 stays at az=0 (auto-primed). obj 1 is fresh — its first
    // setDirection snaps without a crossfade.
    mon.setDirection(1, static_cast<float>(M_PI), 0.f);

    std::vector<float> in0(blockSize, 0.f); in0[0] = 0.7f;
    std::vector<float> in1(blockSize, 0.f); in1[0] = 0.3f;

    std::vector<float> out0L(blockSize, 0.f), out0R(blockSize, 0.f);
    std::vector<float> out1L(blockSize, 0.f), out1R(blockSize, 0.f);
    mon.processBlockForObject(0, in0.data(), blockSize, out0L.data(), out0R.data());
    mon.processBlockForObject(1, in1.data(), blockSize, out1L.data(), out1R.data());

    std::vector<float> combL(blockSize, 0.f), combR(blockSize, 0.f);
    for (int n = 0; n < blockSize; ++n) {
        combL[static_cast<std::size_t>(n)] = out0L[static_cast<std::size_t>(n)] +
                                              out1L[static_cast<std::size_t>(n)];
        combR[static_cast<std::size_t>(n)] = out0R[static_cast<std::size_t>(n)] +
                                              out1R[static_cast<std::size_t>(n)];
    }

    // Compare sum-of-monos vs combined (must match to numeric tolerance).
    float max_err = 0.f;
    for (int n = 0; n < blockSize; ++n) {
        max_err = std::max(max_err, std::abs(sumL[static_cast<std::size_t>(n)] - combL[static_cast<std::size_t>(n)]));
        max_err = std::max(max_err, std::abs(sumR[static_cast<std::size_t>(n)] - combR[static_cast<std::size_t>(n)]));
    }
    if (max_err > 1e-5f) {
        std::fprintf(stderr, "FAIL: linearity check max_err=%g\n", max_err);
        return 1;
    }

    std::puts("PASS test_binaural_b1_two_objects_linear");
    return 0;
}
