// test_convergence_pink_noise.cpp
// Dreamscape convergence — Phase 4.2a real pink noise (Paul Kellet 7-state).
//
// Verifies spe::dsp::PinkKellet:
//   1. Coefficients are the canonical Kellet "refined" gains (true −3 dB/oct),
//      NOT the reference engine's variant gains which measure ~−5.3 dB/oct.
//   2. The analytic transfer function H(z)=Σ g_i/(1-a_i z^-1)+0.5362 has a
//      −3 dB/octave magnitude slope across the audio mid-band (the defining
//      property of pink; the old single-pole LP placeholder was ~−6 dB/oct and
//      not log-flat). Deterministic — no RNG variance.
//   3. reset() zeroes state; processSample is deterministic + finite.
//
// (No FFT: the magnitude response is evaluated in closed form, so the slope
//  check has zero statistical noise.)

#include "dsp/PinkNoise.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <utility>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while (0)

using spe::dsp::PinkKellet;

// |H(f)|^2 of the Kellet filter (output scale folds out of a slope, kept for
// parity with the runtime path).
static double mag2_at(double f, double sr) {
    const double w = 2.0 * M_PI * f / sr;
    std::complex<double> zinv(std::cos(-w), std::sin(-w));   // e^{-jw}
    std::complex<double> H(static_cast<double>(PinkKellet::kWhiteFeed), 0.0);
    for (std::size_t i = 0; i < 7; ++i) {
        const std::complex<double> denom =
            1.0 - static_cast<double>(PinkKellet::kA[i]) * zinv;
        H += static_cast<double>(PinkKellet::kG[i]) / denom;
    }
    H *= static_cast<double>(PinkKellet::kOutScale);
    return std::norm(H);
}

// ---- 1: canonical Kellet "refined" coefficients (true pink) ----
static void test_canonical_coefficients() {
    const std::array<float, 7> a = {0.99886f, 0.99332f, 0.96900f,
                                    0.86650f, 0.55000f, -0.7616f, 0.0f};
    const std::array<float, 7> g = {0.0555179f, 0.0750759f, 0.1538520f,
                                    0.3104856f, 0.5329522f, -0.0168980f,
                                    0.1159260f};
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK(PinkKellet::kA[i] == a[i]);
        CHECK(PinkKellet::kG[i] == g[i]);
    }
    CHECK(PinkKellet::kWhiteFeed == 0.5362f);
    std::printf("[PASS] canonical Kellet refined coefficients (true -3 dB/oct)\n");
}

// ---- 2: −3 dB/octave slope across the mid-band ----
static void test_minus3db_per_octave() {
    const double sr = 48000.0;
    // Log-spaced octave anchors 125 Hz .. 8 kHz (well inside the audio band,
    // away from DC/Nyquist edge deviation of the 7-pole approximation).
    const double f0 = 125.0;
    const int    oct = 6;                       // 125,250,500,1k,2k,4k,8k
    double prev_db = 0.0;
    double slope_sum = 0.0;
    for (int k = 0; k <= oct; ++k) {
        const double f  = f0 * std::pow(2.0, k);
        const double db = 10.0 * std::log10(mag2_at(f, sr));
        if (k > 0) {
            const double d = db - prev_db;      // dB change per octave
            slope_sum += d;
            // each octave step is close to the ideal −3.0103 dB
            CHECK(d < -2.2 && d > -3.8);
        }
        prev_db = db;
    }
    const double mean_slope = slope_sum / oct;
    std::printf("[%s] mean slope = %.3f dB/oct over 125 Hz..8 kHz (ideal -3.01)\n",
                (mean_slope < -2.6 && mean_slope > -3.4) ? "PASS" : "FAIL", mean_slope);
    CHECK(mean_slope < -2.6 && mean_slope > -3.4);
    // Monotone low-pass tilt: low band louder than high band.
    CHECK(mag2_at(125.0, sr) > mag2_at(8000.0, sr));
}

// ---- 3: reset / determinism / finite ----
static void test_reset_determinism_finite() {
    PinkKellet p;
    // deterministic pseudo-white drive (LCG), no engine RNG needed
    auto run = [](PinkKellet& f, int n) {
        uint32_t s = 0x12345678u;
        double acc = 0.0; float last = 0.f;
        for (int i = 0; i < n; ++i) {
            s = 1664525u * s + 1013904223u;
            const float white = static_cast<float>(static_cast<int32_t>(s)) / 2147483648.f;
            last = f.processSample(white);
            acc += last;
        }
        return std::make_pair(acc, last);
    };
    auto r1 = run(p, 4096);
    p.reset();
    auto r2 = run(p, 4096);
    CHECK(r1.first == r2.first && r1.second == r2.second);  // reset → identical
    CHECK(std::isfinite(r1.first) && std::isfinite(r1.second));
    // bounded for bounded input (|white|<=1): not NaN/Inf, stays sane
    CHECK(std::fabs(r1.second) < 4.0f);
    std::printf("[PASS] reset deterministic + finite + bounded\n");
}

int main() {
    test_canonical_coefficients();
    test_minus3db_per_octave();
    test_reset_determinism_finite();
    if (failures) { std::printf("[RESULT] FAIL (%d)\n", failures); return 1; }
    std::printf("[RESULT] PASS\n");
    return 0;
}
