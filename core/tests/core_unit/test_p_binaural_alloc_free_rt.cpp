// test_p_binaural_alloc_free_rt.cpp
//
// v0.5 P1: RT-safety regression for BinauralMonitor.
//
// Setup:
//   * Initialize BinauralMonitor with the synthetic 4-position .speh fixture.
//   * Prepare for 48 kHz / blockSize=256.
//
// Workload:
//   * Audio thread (main): SPE_RT_NO_ALLOC_SCOPE; process N blocks against
//     setDirection updates fired from a worker thread.
//   * Control thread: 60 Hz setDirection updates across 8 distinct directions.
//
// Assertion: rt_alloc_violations() == 0 AND loadIntoFailures() == 0.

#include "core/Constants.h"
#include "output_backend/BinauralMonitor.h"
#include "util/RtAssertNoAlloc.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
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
    const auto init = mon.initialize(cfg);
    if (init != spe::output::BinauralMonitor::InitResult::Ok) {
        std::fprintf(stderr, "FAIL: BinauralMonitor::initialize → %d (path=%s)\n",
                     static_cast<int>(init), cfg.sofaPath.c_str());
        return 1;
    }
    if (!mon.hasHrtf()) {
        std::fprintf(stderr, "FAIL: HRTF not loaded\n");
        return 1;
    }

    // Pre-seed object 0 with a known direction so processBlock has primed slot.
    mon.setDirection(0, 0.f, 0.f);

    // CI-bounded blocks: 1000 blocks @ blockSize=256 = ~5.3 s of audio.
    constexpr int kNumBlocks = 1000;

    std::atomic<bool> stop{false};
    std::thread updater([&mon, &stop] {
        const float dirs[8][2] = {
            { 0.f,           0.f},  {  static_cast<float>(M_PI_2), 0.f},
            { static_cast<float>(M_PI),  0.f}, { -static_cast<float>(M_PI_2), 0.f},
            { 0.f,           static_cast<float>(M_PI_4)},  { static_cast<float>(M_PI_2), -static_cast<float>(M_PI_4)},
            { static_cast<float>(M_PI),  static_cast<float>(M_PI_4)}, { -static_cast<float>(M_PI_2), 0.f},
        };
        int k = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            // ~60 Hz updates.
            mon.setDirection(0, dirs[k & 7][0], dirs[k & 7][1]);
            ++k;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });

    std::vector<float> in(blockSize, 0.f);
    std::vector<float> outL(blockSize, 0.f);
    std::vector<float> outR(blockSize, 0.f);
    for (int n = 0; n < blockSize; ++n)
        in[static_cast<std::size_t>(n)] = 0.1f * std::sin(2.f * static_cast<float>(M_PI) * 220.f * static_cast<float>(n) / sr);

    {
        SPE_RT_NO_ALLOC_SCOPE();
        for (int b = 0; b < kNumBlocks; ++b) {
            mon.processBlockForObject(0, in.data(), blockSize, outL.data(), outR.data());
        }
    }

    stop.store(true, std::memory_order_relaxed);
    updater.join();

    const auto fails = mon.loadIntoFailures();
    if (fails != 0u) {
        std::fprintf(stderr,
            "FAIL: loadIntoFailures()=%llu (expected 0)\n",
            static_cast<unsigned long long>(fails));
        return 1;
    }

    const auto v = spe::util::rt_alloc_violations();
    if (v != 0u) {
        std::fprintf(stderr,
            "FAIL: rt_alloc_violations()=%llu (expected 0)\n",
            static_cast<unsigned long long>(v));
        return 1;
    }

    std::puts("PASS test_p_binaural_alloc_free_rt");
    return 0;
}
