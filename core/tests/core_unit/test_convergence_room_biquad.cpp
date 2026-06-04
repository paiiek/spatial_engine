// test_convergence_room_biquad.cpp
// Dreamscape Convergence ⑥e-3a — room absorption-EQ biquad core (iae::RoomBiquad).
//
// Two independent, non-circular checks:
//   (1) Analytic transfer function from the coefficients alone:
//       |H(e^jw)| = |b0 + b1 z^-1 + b2 z^-2| / |1 + a1 z^-1 + a2 z^-2|.
//       A 2nd-order Butterworth LP/HP must have DC (z=1) and Nyquist (z=-1)
//       gains of exactly {1,0} (LP) / {0,1} (HP), and -3 dB at the corner.
//       This validates the *coefficients* (juce_IIRFilter.cpp:75-117) without
//       re-deriving them (non-circular).
//   (2) Time-domain processSample sine-RMS gain at pass/stop frequencies, which
//       validates the *topology* (TDF-II, juce_IIRFilter_Impl.h:212-217) is
//       consistent with the coefficients.
//   (3) reset() determinism.

#include "render/ported/RoomBiquad.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

static constexpr double kSR = 48000.0;
static constexpr double kPi = 3.14159265358979323846;

// |H(e^jw)| from raw coeffs {b0,b1,b2,a1,a2} (a0=1).
static double mag_at(const iae::RoomBiquad& f, double freqHz) {
    const double w = 2.0 * kPi * freqHz / kSR;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = (double) f.b0() + (double) f.b1() * z1 + (double) f.b2() * z2;
    const std::complex<double> den = 1.0       + (double) f.a1() * z1 + (double) f.a2() * z2;
    return std::abs(num) / std::abs(den);
}

// Steady-state RMS gain of a `freqHz` sine pushed through `f` (skip transient).
// `f` is taken by value (fresh state per call) — do not change to a reference.
static double sine_gain(iae::RoomBiquad f, double freqHz) {
    const int warm = 8192, meas = 16384;
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 0; i < warm + meas; ++i) {
        const float x = (float) std::sin(2.0 * kPi * freqHz * i / kSR);
        const float y = f.processSample(x);
        if (i >= warm) { sumIn += (double) x * x; sumOut += (double) y * y; }
    }
    return std::sqrt(sumOut / sumIn);
}

static double db(double g) { return 20.0 * std::log10(g + 1.0e-30); }

int main() {
    // --- Low-pass @ 10 kHz (reference roomEarlyClusterLpfHz) -------------------
    {
        iae::RoomBiquad lp;
        lp.setLowPass(kSR, iae::kRoomEarlyClusterLpfHz);   // Q = 1/sqrt(2)

        const double gDC  = mag_at(lp, 0.0);
        const double gNyq = mag_at(lp, kSR / 2.0);
        const double gFc  = mag_at(lp, iae::kRoomEarlyClusterLpfHz);
        CHECK(std::abs(gDC  - 1.0) < 1.0e-4);              // LP passes DC
        CHECK(gNyq < 1.0e-3);                              // LP kills Nyquist
        CHECK(std::abs(db(gFc) + 3.0103) < 0.2);          // -3 dB at corner (Butterworth)
        // Behavioural (topology) agreement at a passband + stopband freq.
        CHECK(std::abs(sine_gain(lp, 1000.0) - mag_at(lp, 1000.0)) < 2.0e-3);
        CHECK(sine_gain(lp, 18000.0) < sine_gain(lp, 1000.0));
        std::printf("[lp] DC=%.5f Nyq=%.2e fc=%.3fdB  b0=%.6g a1=%.6g a2=%.6g\n",
                    gDC, gNyq, db(gFc), lp.b0(), lp.a1(), lp.a2());
    }

    // --- High-pass @ 120 Hz (reference roomEarlyClusterHpfHz) -----------------
    {
        iae::RoomBiquad hp;
        hp.setHighPass(kSR, iae::kRoomEarlyClusterHpfHz);

        const double gDC  = mag_at(hp, 0.0);
        const double gNyq = mag_at(hp, kSR / 2.0);
        const double gFc  = mag_at(hp, iae::kRoomEarlyClusterHpfHz);
        CHECK(gDC < 1.0e-4);                               // HP kills DC
        CHECK(std::abs(gNyq - 1.0) < 1.0e-3);              // HP passes Nyquist
        CHECK(std::abs(db(gFc) + 3.0103) < 0.2);           // -3 dB at corner
        CHECK(std::abs(sine_gain(hp, 1000.0) - mag_at(hp, 1000.0)) < 2.0e-3);
        CHECK(sine_gain(hp, 30.0) < sine_gain(hp, 1000.0));
        std::printf("[hp] DC=%.2e Nyq=%.5f fc=%.3fdB  b0=%.6g a1=%.6g a2=%.6g\n",
                    gDC, gNyq, db(gFc), hp.b0(), hp.a1(), hp.a2());
    }

    // --- reset() determinism --------------------------------------------------
    {
        iae::RoomBiquad f; f.setLowPass(kSR, 2000.f);
        std::vector<float> a, b;
        for (int i = 0; i < 256; ++i)
            a.push_back(f.processSample(i == 0 ? 1.f : 0.f));
        f.reset();
        for (int i = 0; i < 256; ++i)
            b.push_back(f.processSample(i == 0 ? 1.f : 0.f));
        double d = 0.0;
        for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
        CHECK(d == 0.0);
        std::printf("[reset] sum|a-b|=%.3g\n", d);
    }

    if (failures == 0) std::printf("test_convergence_room_biquad: ALL PASS\n");
    return failures;
}
