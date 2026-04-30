// test_p3_algoswap_crossfade.cpp
// ADR 0006 algorithm runtime swap: VBAP → DBAP crossfade.
// (i)  RT_ASSERT_NO_ALLOC violations = 0 during crossfade
// (ii) per-sample max |Δgain| ≤ 1/256 per speaker
// (iii) FFT spike < 10 dB  (SPE_RT_ASSERTS)
//
// NOTE: This test implements the crossfade logic directly using GainRamp,
// as the full SpatialEngine integration is planned for P5/P7.

#include "dsp/GainRamp.h"
#include "render/AlgorithmAnalyticReference.h"
#include "util/RtAssertNoAlloc.h"
#include "util/ClickDetectorFFT.h"
#include "geometry/SpeakerLayout.h"
#include "core/Constants.h"
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

using namespace spe;
using namespace spe::dsp;
using namespace spe::render;
using namespace spe::geometry;

static SpeakerLayout make_4ch_layout() {
    SpeakerLayout l;
    l.name = "swap_test";
    l.regularity = Regularity::CIRCULAR;
    float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * 3.14159265f / 180.f;
        Speaker s; s.channel = i+1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

int main() {
    auto layout = make_4ch_layout();
    const int N_SPK = static_cast<int>(layout.speakers.size());
    const int K = ALGO_SWAP_K; // 256

    // Source at 45 degrees
    float az  = 45.f * 3.14159265f / 180.f;
    float src_x = std::sin(az), src_z = std::cos(az);

    // Gains from VBAP (old algo) and DBAP (new algo)
    auto vbap_gains = AlgorithmAnalyticReference::vbap_gain(layout, az, 0.f);
    auto dbap_gains = AlgorithmAnalyticReference::dbap_gain(layout, src_x, 0.f, src_z, 2.0f);

    // Per-speaker GainRamps: start at VBAP, ramp to DBAP over K samples
    std::array<GainRamp, 4> ramps;
    for (int s = 0; s < N_SPK; ++s) {
        ramps[s].reset(vbap_gains[s]);
        ramps[s].setTarget(dbap_gains[s], K);
    }

    // Pre-allocate output buffer outside no-alloc scope.
    std::array<float, ALGO_SWAP_K> audio_out{};
    audio_out.fill(0.f);

    // (i) Reset violation counter, simulate audio block with no-alloc scope.
    // Only the pure gain-ramp computation runs inside; FFT (uses vector) is outside.
    spe::util::rt_alloc_violations_reset();
    {
        SPE_RT_NO_ALLOC_SCOPE();

        std::array<float, 4> prev_gains{};
        for (int s = 0; s < N_SPK; ++s) prev_gains[s] = vbap_gains[s];

        for (int n = 0; n < K; ++n) {
            float mixed = 0.f;
            for (int s = 0; s < N_SPK; ++s) {
                float g = ramps[s].next();
                mixed += g;

                // (ii) |Δgain| ≤ 1/K per speaker per sample
                float delta = std::abs(g - prev_gains[s]);
                if (delta > 1.f / K + 1e-5f) {
                    std::printf("FAIL speaker %d sample %d: |delta|=%.6f > 1/%d=%.6f\n",
                        s, n, (double)delta, K, 1.0/K);
                    ++failures;
                }
                prev_gains[s] = g;
            }
            audio_out[n] = mixed;
        }
    }

    // (i) Check no allocation violations
    auto violations = spe::util::rt_alloc_violations();
    if (violations != 0) {
        std::printf("FAIL: %llu RT allocation violations during crossfade\n",
                    (unsigned long long)violations);
        ++failures;
    }

    // (iii) FFT spike check — done outside no-alloc scope (detect_click uses vector internally)
    {
        auto report = spe::util::detect_click(
            std::span<const float>(audio_out.data(), audio_out.size()), 10.0f);
        CHECK(!report.spike_detected);
    }

    if (failures == 0) std::printf("test_p3_algoswap_crossfade: ALL PASS\n");
    return failures;
}
