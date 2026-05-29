// bench_cpumeter_record_latency.cpp
//
// v0.9 Lane A (A-M1, AC2b) — RT-latency microbench for the CpuMeter hot path.
//
// The alloc=0 sentinel (RtAssertNoAlloc.h) only counts operator new; it cannot
// catch a latency regression. This bench measures the median wall cost of one
// recordBlockStart()+recordBlockEnd() pair over N>=50 samples and asserts it
// stays under a small budget. CpuMeter is a scalar O(1) estimator (5 P²
// markers, EWMA, peak — no array of samples, no sort over stored data), so the
// per-call cost is a handful of arithmetic ops plus two steady_clock reads.
//
// BUDGET RATIONALE: the dominant cost is the two steady_clock::now() reads
// (vDSO, ~20-40 ns each on Linux) plus the O(1) estimator math. The measured
// O(1) baseline on CI-class hardware is well under 1 µs. We pin the budget at
// 5.0 µs/call — generous margin (≈10-100×) over the steady-state cost so the
// gate is robust to scheduler jitter / loaded CI while still catching a real
// regression (e.g. someone reintroducing a sort or a reservoir copy, which
// would push the per-call cost into the tens of µs).

#include "util/CpuMeter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    using Clock = std::chrono::steady_clock;

    constexpr int    kN          = 200;     // samples (>= 50 required by AC2b)
    constexpr int    kWarm       = 50;      // warm-up the P² markers first
    constexpr double kBudgetUs   = 5.0;     // per-call budget, see header note
    constexpr int    kNumFrames  = 512;
    constexpr double kSampleRate = 48000.0;

    spe::util::CpuMeter meter;
    meter.reset();

    // Warm-up so the P² estimator is in its steady (warm) branch during the
    // measured window — that is the more expensive code path.
    for (int i = 0; i < kWarm; ++i) {
        meter.recordBlockStart();
        meter.recordBlockEnd(kNumFrames, kSampleRate);
    }

    std::vector<double> per_call_us;
    per_call_us.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        const Clock::time_point t0 = Clock::now();
        meter.recordBlockStart();
        meter.recordBlockEnd(kNumFrames, kSampleRate);
        const Clock::time_point t1 = Clock::now();
        const double ns =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        per_call_us.push_back(ns * 1e-3);
    }

    std::sort(per_call_us.begin(), per_call_us.end());
    const double median_us = per_call_us[per_call_us.size() / 2];
    const double min_us     = per_call_us.front();
    const double max_us     = per_call_us.back();

    std::printf("  cpumeter record latency: median=%.4f µs (min=%.4f max=%.4f) "
                "over N=%d, budget=%.1f µs\n",
                median_us, min_us, max_us, kN, kBudgetUs);

    if (median_us > kBudgetUs) {
        std::fprintf(stderr,
                     "FAIL: median %.4f µs exceeds budget %.1f µs\n",
                     median_us, kBudgetUs);
        return 1;
    }

    std::printf("PASS bench_cpumeter_record_latency\n");
    return 0;
}
