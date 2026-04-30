// test_p3_gainramp_click.cpp
// C8 three-part check on GainRamp:
// (i)  first per-sample step <= 1.0/MAX_BLOCK
// (ii) cumulative gain at sample MAX_BLOCK matches target within 1e-6
// (iii) ClickDetectorFFT spike < 10 dB  (only active when SPE_RT_ASSERTS)

#include "dsp/GainRamp.h"
#include "util/ClickDetectorFFT.h"
#include "core/Constants.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a,b,tol) \
    do { float _a=(a),_b=(b); \
         if (std::abs(_a-_b)>(tol)) { \
             std::printf("FAIL %s:%d  |%.8f-%.8f|=%.2e>%.2e\n", \
                 __FILE__,__LINE__,(double)_a,(double)_b, \
                 (double)std::abs(_a-_b),(double)(tol)); \
             ++failures; \
         } } while(0)

int main() {
    const int BLOCK = spe::MAX_BLOCK; // 512
    const float from_gain = 1.0f;    // "front"
    const float to_gain   = 0.5f;    // "right" (arbitrary target)

    spe::dsp::GainRamp ramp(from_gain);

    // Set target to transition over MAX_BLOCK samples (mid-block jump)
    ramp.setTarget(to_gain, BLOCK);

    float prev = from_gain;
    std::vector<float> rendered(BLOCK);

    for (int n = 0; n < BLOCK; ++n) {
        float g = ramp.next();
        rendered[n] = g; // track gain envelope

        if (n == 0) {
            // (i) first step <= 1.0/MAX_BLOCK
            float step = std::abs(g - prev);
            float max_step = std::abs(to_gain - from_gain) / static_cast<float>(BLOCK);
            // step should equal exactly max_step for linear ramp
            CHECK(step <= max_step + 1e-6f);
        }
        prev = g;
    }

    // (ii) After MAX_BLOCK samples, should be at target
    CHECK_NEAR(ramp.currentValue(), to_gain, 1e-6f);

    // (iii) FFT click detection on the gain envelope (only real when SPE_RT_ASSERTS)
    {
        // Render a sine tone modulated by the ramp to detect spectral discontinuity
        std::vector<float> audio(BLOCK);
        spe::dsp::GainRamp ramp2(from_gain);
        ramp2.setTarget(to_gain, BLOCK);
        for (int n = 0; n < BLOCK; ++n) {
            float g = ramp2.next();
            // Use a 1 kHz sine at 48 kHz SR
            float sr = 48000.f;
            float sine = std::sin(2.f * 3.14159265f * 1000.f * n / sr);
            audio[n] = g * sine;
        }

        auto report = spe::util::detect_click(
            std::span<const float>(audio.data(), audio.size()), 10.0f);

        // With a smooth linear ramp there should be no spike > 10 dB
        CHECK(!report.spike_detected);
    }

    if (failures == 0) std::printf("test_p3_gainramp_click: ALL PASS\n");
    return failures;
}
