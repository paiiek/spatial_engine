// test_convergence_log_sweep.cpp
// Dreamscape convergence — Phase 4.2b log-sweep calibration signal.
//
// Verifies spe::dsp::LogSweep (byte-faithful to reference AudioEngine.cpp:985-996):
//   1. Instantaneous frequency f0·(f1/f0)^pos: 20 Hz at pos 0, 20 kHz at pos 1,
//      632.46 Hz at pos 0.5 (geometric mean) — the exponential law.
//   2. Output stays in [-1, 1] across a full 1-second sweep.
//   3. Frequency genuinely RISES: zero-crossings in the last 10 ms ≫ the first
//      10 ms (≈20 Hz start vs ≈20 kHz end).
//   4. reset() is deterministic.

#include "dsp/LogSweep.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; \
    } } while (0)
#define CHECK_NEAR(a, b, tol) \
    do { double _a=(a),_b=(b); if (std::fabs(_a-_b) > (tol)) { \
        std::printf("FAIL %s:%d  |%.4f - %.4f| > %.2e\n", __FILE__, __LINE__, _a, _b, (double)(tol)); \
        ++failures; } } while (0)

using spe::dsp::LogSweep;

static int zero_crossings(const std::vector<float>& x, int lo, int hi) {
    int z = 0;
    for (int i = lo + 1; i < hi; ++i)
        if ((x[(size_t) i] >= 0.f) != (x[(size_t) (i - 1)] >= 0.f)) ++z;
    return z;
}

// ---- 1: exponential instantaneous frequency ----
static void test_inst_freq_law() {
    CHECK_NEAR(LogSweep::instFreq(0.0f),  20.0,    1e-3);
    CHECK_NEAR(LogSweep::instFreq(1.0f),  20000.0, 1e-1);
    CHECK_NEAR(LogSweep::instFreq(0.5f),  20.0 * std::sqrt(1000.0), 1e-1);  // 632.46
    // strictly increasing
    CHECK(LogSweep::instFreq(0.25f) < LogSweep::instFreq(0.75f));
    std::printf("[PASS] instantaneous frequency law 20 Hz -> 20 kHz exponential\n");
}

// ---- 2 + 3: bounded output, rising frequency over 1 s ----
static void test_sweep_rises() {
    const double sr = 48000.0;
    const int N = 48000;                 // exactly one 1-second period
    LogSweep s;
    std::vector<float> y((size_t) N);
    float maxabs = 0.f;
    for (int i = 0; i < N; ++i) {
        y[(size_t) i] = s.processSample(sr);
        maxabs = std::max(maxabs, std::fabs(y[(size_t) i]));
    }
    CHECK(maxabs <= 1.0001f);            // sine output bounded
    const int w = 480;                  // 10 ms windows
    const int zc_early = zero_crossings(y, 0, w);
    const int zc_late  = zero_crossings(y, N - w, N);
    // ~20 Hz start → ≤ a couple crossings; ~20 kHz end → hundreds.
    CHECK(zc_early < 5);
    CHECK(zc_late > 100);
    CHECK(zc_late > 50 * (zc_early + 1));
    std::printf("[%s] sweep rises: zc(first 10 ms)=%d  zc(last 10 ms)=%d, |y|max=%.3f\n",
                (zc_late > 100 && zc_early < 5) ? "PASS" : "FAIL", zc_early, zc_late, maxabs);
}

// ---- 4: reset determinism ----
static void test_reset_determinism() {
    const double sr = 48000.0;
    LogSweep s;
    double a = 0.0; for (int i = 0; i < 2000; ++i) a += s.processSample(sr);
    s.reset();
    double b = 0.0; for (int i = 0; i < 2000; ++i) b += s.processSample(sr);
    CHECK(a == b);
    std::printf("[PASS] reset deterministic\n");
}

int main() {
    test_inst_freq_law();
    test_sweep_rises();
    test_reset_determinism();
    if (failures) { std::printf("[RESULT] FAIL (%d)\n", failures); return 1; }
    std::printf("[RESULT] PASS\n");
    return 0;
}
