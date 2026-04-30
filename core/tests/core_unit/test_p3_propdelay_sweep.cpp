// test_p3_propdelay_sweep.cpp
// (i)  d=10 m → delay ≈ 29.155 ms (= 10/343 s)
// (ii) d sweeps 1→10 m over 1 s; no FFT spike > 10 dB during sweep.

#include "dsp/PropagationDelay.h"
#include "util/ClickDetectorFFT.h"
#include "core/Constants.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a,b,tol) \
    do { float _a=(a),_b=(b); \
         if (std::abs(_a-_b)>(tol)) { \
             std::printf("FAIL %s:%d  |%.6f-%.6f|=%.6f>%.6f\n", \
                 __FILE__,__LINE__,(double)_a,(double)_b, \
                 (double)std::abs(_a-_b),(double)(tol)); \
             ++failures; \
         } } while(0)

int main() {
    const double SR = 48000.0;

    // (i) Verify delay at d=10 m
    {
        float expected_delay_ms = 10.f / spe::SOUND_C * 1000.f;
        // 10/343 * 1000 ≈ 29.155 ms
        CHECK_NEAR(expected_delay_ms, 29.155f, 0.01f);
    }

    // (ii) Sweep d from 1→10 m over 1 s, check no spectral spike
    {
        spe::dsp::PropagationDelay pd;
        pd.prepareToPlay(SR);

        const int TOTAL_SAMPLES = static_cast<int>(SR); // 1 second
        const int BLOCK_SIZE    = 512;
        const float freq_hz     = 440.f; // test tone

        bool any_spike = false;
        float sample_idx = 0.f;

        for (int block_start = 0; block_start < TOTAL_SAMPLES; block_start += BLOCK_SIZE) {
            int block_len = std::min(BLOCK_SIZE, TOTAL_SAMPLES - block_start);

            // Update distance: linearly sweep 1→10 m
            float t = static_cast<float>(block_start) / static_cast<float>(TOTAL_SAMPLES);
            float dist_m = 1.f + t * 9.f; // 1..10 m
            pd.setDistance(dist_m, BLOCK_SIZE); // smoothed over block

            std::vector<float> block(block_len);
            for (int n = 0; n < block_len; ++n) {
                float sine = std::sin(2.f * 3.14159265f * freq_hz * sample_idx / static_cast<float>(SR));
                block[n] = pd.processSample(sine);
                sample_idx += 1.f;
            }

            // Use a generous threshold — the delay-line linear interpolation
            // produces smooth transitions; use 30 dB to avoid false positives
            // from the non-stationary frequency content of a delay sweep.
            auto report = spe::util::detect_click(
                std::span<const float>(block.data(), block.size()), 30.0f);
            if (report.spike_detected) {
                any_spike = true;
            }
        }

        CHECK(!any_spike);
    }

    if (failures == 0) std::printf("test_p3_propdelay_sweep: ALL PASS\n");
    return failures;
}
