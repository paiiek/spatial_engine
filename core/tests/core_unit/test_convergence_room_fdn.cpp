// test_convergence_room_fdn.cpp
// Dreamscape Convergence ⑥a — late-reverb FDN core (iae::RoomFdn).
//
// Exercises the ported 8x8 Hadamard FDN's observable DSP invariants:
//   - stability & finiteness: impulse response stays finite and decays
//   - T60 ordering: a longer T60 leaves more energy in a late window
//   - HF damping: a lower hfDecayRatio01 yields a smoother (less HF) tail
//   - decorrelation: all 8 output lines carry energy
//   - reset() determinism: re-running an impulse after reset is identical
//
// Absolute T60 is intentionally NOT asserted (the reference applies a 0.918 loop
// trim + clamps, so the realised decay is shorter than the nominal seconds);
// we assert relative ordering, which is robust to those constants.

#include "render/ported/RoomFdn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

static constexpr int   kSR    = 48000;
static constexpr int   kBlock = 256;
static constexpr int   kOrder = iae::RoomFdn::kOrder;

// Drive an impulse through `fdn` for `totalSamples`, summing the 8 line taps
// into mono `mono` and accumulating per-line energy into `lineEnergy[8]`.
// Returns total energy. Sets `anyNonFinite` if any sample is non-finite.
static double run_impulse(iae::RoomFdn& fdn, int totalSamples,
                          std::vector<float>& mono,
                          std::array<double, kOrder>& lineEnergy,
                          bool& anyNonFinite) {
    mono.assign((size_t) totalSamples, 0.f);
    lineEnergy.fill(0.0);
    anyNonFinite = false;
    std::vector<float> in((size_t) kBlock, 0.f);
    std::vector<float> out((size_t) kOrder * (size_t) kBlock, 0.f);

    double total = 0.0;
    int done = 0;
    bool fired = false;
    while (done < totalSamples) {
        const int n = std::min(kBlock, totalSamples - done);
        std::fill(in.begin(), in.end(), 0.f);
        if (!fired) { in[0] = 1.f; fired = true; }      // unit impulse at sample 0
        std::fill(out.begin(), out.end(), 0.f);
        fdn.process(in.data(), n, out.data());
        for (int i = 0; i < n; ++i) {
            float s = 0.f;
            for (int k = 0; k < kOrder; ++k) {
                const float v = out[(size_t) k * (size_t) n + (size_t) i];
                if (!std::isfinite(v)) anyNonFinite = true;
                lineEnergy[(size_t) k] += (double) v * v;
                s += v;
            }
            mono[(size_t) (done + i)] = s;
            total += (double) s * s;
        }
        done += n;
    }
    return total;
}

// Energy of mono[a..b).
static double window_energy(const std::vector<float>& mono, int a, int b) {
    double e = 0.0;
    for (int i = a; i < b && i < (int) mono.size(); ++i) e += (double) mono[i] * mono[i];
    return e;
}

// Crude high-frequency proxy over [a,b): first-difference energy / signal energy.
static double hf_fraction(const std::vector<float>& mono, int a, int b) {
    double hf = 0.0, en = 0.0;
    for (int i = a + 1; i < b && i < (int) mono.size(); ++i) {
        const double dx = (double) mono[i] - (double) mono[i - 1];
        hf += dx * dx; en += (double) mono[i] * mono[i];
    }
    return en > 0.0 ? hf / en : 0.0;
}

int main() {
    const int kTotal = kSR;          // 1.0 s

    // --- Stability / finiteness / decay / decorrelation -----------------------
    {
        iae::RoomFdn fdn;
        fdn.prepare((double) kSR, kBlock);
        CHECK(fdn.ready());
        iae::RoomFdnParams p; p.t60Seconds = 1.2f; fdn.setParams(p);

        std::vector<float> mono;
        std::array<double, kOrder> le{};
        bool nf = false;
        const double tot = run_impulse(fdn, kTotal, mono, le, nf);

        CHECK(!nf);                                  // all finite
        CHECK(tot > 1.0e-6);                         // non-silent
        // All 8 lines carry energy (network is fully coupled / decorrelated).
        for (int k = 0; k < kOrder; ++k) CHECK(le[(size_t) k] > 0.0);
        // Decays: a late window holds less energy than an early window.
        const double eEarly = window_energy(mono, 0, kSR / 10);          // 0..100 ms
        const double eLate  = window_energy(mono, (9 * kSR) / 10, kSR);  // 900..1000 ms
        CHECK(eLate < eEarly);
        std::printf("[decay] total=%.4g  eEarly=%.4g  eLate=%.4g\n", tot, eEarly, eLate);
    }

    // --- T60 ordering: longer T60 -> more energy in a late window -------------
    {
        auto late_energy = [&](float t60) {
            iae::RoomFdn fdn; fdn.prepare((double) kSR, kBlock);
            iae::RoomFdnParams p; p.t60Seconds = t60; fdn.setParams(p);
            std::vector<float> mono; std::array<double, kOrder> le{}; bool nf = false;
            run_impulse(fdn, kTotal, mono, le, nf);
            return window_energy(mono, (6 * kSR) / 10, (9 * kSR) / 10); // 600..900 ms
        };
        const double eShort = late_energy(0.4f);
        const double eLong  = late_energy(2.5f);
        CHECK(eLong > eShort * 4.0);                 // clearly longer tail
        std::printf("[t60] eShort(0.4)=%.4g  eLong(2.5)=%.4g  ratio=%.2f\n",
                    eShort, eLong, eShort > 0 ? eLong / eShort : 0.0);
    }

    // --- HF damping: lower ratio -> smoother (less HF) tail -------------------
    {
        auto tail_hf = [&](float ratio) {
            iae::RoomFdn fdn; fdn.prepare((double) kSR, kBlock);
            iae::RoomFdnParams p; p.t60Seconds = 2.0f;
            p.hfDecayRatio01 = ratio; p.hfDecayCornerHz = 4000.f;
            fdn.setParams(p);
            std::vector<float> mono; std::array<double, kOrder> le{}; bool nf = false;
            run_impulse(fdn, kTotal, mono, le, nf);
            return hf_fraction(mono, (3 * kSR) / 10, (8 * kSR) / 10);   // 300..800 ms
        };
        const double hfBright = tail_hf(1.0f);   // no damping
        const double hfDamped = tail_hf(0.05f);  // strong HF damping
        CHECK(hfDamped < hfBright);
        std::printf("[hf] bright(1.0)=%.4g  damped(0.05)=%.4g\n", hfBright, hfDamped);
    }

    // --- reset() determinism --------------------------------------------------
    {
        iae::RoomFdn fdn; fdn.prepare((double) kSR, kBlock);
        iae::RoomFdnParams p; p.t60Seconds = 1.0f; fdn.setParams(p);
        std::vector<float> m1, m2; std::array<double, kOrder> le{}; bool nf = false;
        run_impulse(fdn, 4 * kBlock, m1, le, nf);
        fdn.reset();
        run_impulse(fdn, 4 * kBlock, m2, le, nf);
        double diff = 0.0;
        for (size_t i = 0; i < m1.size(); ++i) diff += std::abs(m1[i] - m2[i]);
        CHECK(diff < 1.0e-4);
        std::printf("[reset] sum|m1-m2|=%.3g\n", diff);
    }

    if (failures == 0) std::printf("test_convergence_room_fdn: ALL PASS\n");
    return failures;
}
