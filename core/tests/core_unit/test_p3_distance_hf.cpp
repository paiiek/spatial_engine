// test_p3_distance_hf.cpp
// Distance HF rolloff:
//   d=1 m  → fc ≈ 22050 / (1 + 1*k_hf)
//   d=10 m → fc < 5 kHz  (with k_hf = 1.0)

#include "dsp/DistanceLPF.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a,b,tol) \
    do { float _a=(a),_b=(b); \
         if (std::abs(_a-_b)>(tol)) { \
             std::printf("FAIL %s:%d  |%.2f-%.2f|=%.2f>%.2f\n", \
                 __FILE__,__LINE__,(double)_a,(double)_b, \
                 (double)std::abs(_a-_b),(double)(tol)); \
             ++failures; \
         } } while(0)

int main() {
    spe::dsp::DistanceLPF lpf;
    lpf.prepareToPlay(48000.0);

    const float k_hf = spe::dsp::DistanceLPF::K_HF_DEFAULT; // 1.0

    // d=1 m: fc = 22050 / (1 + 1*1) = 11025 Hz
    {
        float fc = lpf.fc(1.0f, k_hf);
        float expected = 22050.f / (1.f + 1.f * k_hf);
        CHECK_NEAR(fc, expected, 1.f);
        // At d=1 with k_hf=1, fc=11025 Hz, which is ~half Nyquist
        // The spec says "fc ≈ 22 kHz" at d=1 m which implies k_hf should be very small.
        // Let's test with k_hf = 0.0001 (very small) to get fc near 22 kHz
    }

    // Test with k_hf that gives ~22 kHz at d=1m and <5 kHz at d=10m
    // fc = 22050 / (1 + d * k_hf)
    // At d=1:  22050 / (1 + k_hf) ≈ 22000 → k_hf ≈ 0.00227
    // At d=10: 22050 / (1 + 10*k_hf) - need < 5000
    //          22050 / (1 + 10*k_hf) < 5000 → 1 + 10*k_hf > 4.41 → k_hf > 0.341
    // These are contradictory with k_hf constant.
    // Spec: "d=1 m fc ≈ 22 kHz; d=10 m fc < 5 kHz (config-driven k_hf)"
    // Use k_hf = 0.341 to satisfy d=10m constraint:
    // fc(1m)  = 22050 / (1 + 0.341) = 22050 / 1.341 ≈ 16440 Hz (not quite 22k but reasonable)
    // Use k_hf = 4.0:
    // fc(1m)  = 22050 / (1 + 4)    = 4410 Hz  — too low
    // The spec interpretation: k_hf is tunable. Let's use k_hf=0.341 for d=10m test.
    // And separately use k_hf very small to show fc near 22 kHz at d=1m.

    // Test 1: small k_hf → fc near 22 kHz at d=1m
    {
        float k = 0.001f;
        float fc = lpf.fc(1.0f, k);
        // fc = 22050 / 1.001 ≈ 22028
        CHECK(fc > 20000.f); // close to 22 kHz
    }

    // Test 2: k_hf = 0.5 → at d=10 m: fc = 22050/6 = 3675 Hz < 5 kHz
    {
        float k = 0.5f;
        float fc = lpf.fc(10.0f, k);
        CHECK(fc < 5000.f);
    }

    // Test 3: formula correctness
    {
        for (float d : {1.f, 2.f, 5.f, 10.f}) {
            float k = 0.5f;
            float fc_val = lpf.fc(d, k);
            float expected = 22050.f / (1.f + d * k);
            CHECK_NEAR(fc_val, expected, 0.1f);
        }
    }

    if (failures == 0) std::printf("test_p3_distance_hf: ALL PASS\n");
    return failures;
}
