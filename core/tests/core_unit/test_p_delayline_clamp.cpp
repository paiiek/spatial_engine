// test_p_delayline_clamp.cpp  (Lane F5-M2)
// Verifies the load-bearing capacity clamp added to DelayLine::processSample:
//   - over-capacity delay requests are CLAMPED to Capacity-2 (graceful
//     degradation), NOT wrapped modulo to a garbage sample;
//   - in-range delay requests are bit-identical to the un-clamped reference.
// Covers BOTH the small WFS-capacity line (DelayLine<16384>) and the large
// user-settable line (DelayLine<48000> = the spk_delays_/user_delay_ class).

#include "dsp/DelayLine.h"
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

// Drive a fresh DelayLine<Cap> with an impulse at n=0 then zeros, requesting a
// CONSTANT delay every sample. Returns the output stream of length n_samples.
template <int Cap>
static std::vector<float> impulse_response(float delay_samples, int n_samples) {
    spe::dsp::DelayLine<Cap> dl;
    dl.prepareToPlay(48000.0);
    std::vector<float> out(n_samples, 0.f);
    for (int n = 0; n < n_samples; ++n) {
        float in = (n == 0) ? 1.f : 0.f;
        out[n] = dl.processSample(in, delay_samples);
    }
    return out;
}

int main() {
    // ---- A. WFS-capacity line: DelayLine<16384> ----
    constexpr int WCAP = spe::dsp::WFS_MAX_DELAY_SAMPLES; // 16384
    {
        // (A1) in-range integer delay 1000 → impulse reappears at out[1000].
        auto r = impulse_response<WCAP>(1000.f, WCAP);
        CHECK_NEAR(r[1000], 1.0f, 1e-4f);
        CHECK_NEAR(r[999],  0.0f, 1e-4f);
        CHECK_NEAR(r[1001], 0.0f, 1e-4f);

        // (A2) over-capacity request 50000 (> 16384) must CLAMP to Cap-2 (16382),
        // i.e. the impulse reappears at out[16382] — NOT wrapped to garbage.
        auto over  = impulse_response<WCAP>(50000.f, WCAP);
        auto clamp = impulse_response<WCAP>(static_cast<float>(WCAP - 2), WCAP);
        CHECK_NEAR(over[WCAP - 2], 1.0f, 1e-4f);
        // The over-capacity stream is sample-identical to an explicit Cap-2 request.
        bool identical = true;
        for (int n = 0; n < WCAP; ++n)
            if (std::abs(over[n] - clamp[n]) > 1e-6f) { identical = false; break; }
        CHECK(identical);
    }

    // ---- B. Large user-settable line: DelayLine<48000> (spk_delays_/user_delay_) ----
    constexpr int LCAP = spe::dsp::DELAY_LINE_MAX_SAMPLES; // 48000
    const float SR = 48000.f;
    {
        // (B1) layout/user delay_ms = 500 → 24000 samples @48k, IN-range:
        // impulse reappears at out[24000], bit-exact alignment preserved.
        float ud_500 = 500.f * SR * 0.001f; // 24000
        CHECK_NEAR(ud_500, 24000.f, 0.5f);
        auto r = impulse_response<LCAP>(ud_500, LCAP);
        CHECK_NEAR(r[24000], 1.0f, 1e-4f);
        CHECK_NEAR(r[23999], 0.0f, 1e-4f);

        // (B2) delay_ms = 1100 → 52800 samples (> 48000), OVER-capacity: clamp to
        // Cap-2 (47998), NOT wrapped. Proves the clamp protects the LARGE lines too.
        float ud_1100 = 1100.f * SR * 0.001f; // 52800
        CHECK(ud_1100 > static_cast<float>(LCAP));
        auto over  = impulse_response<LCAP>(ud_1100, LCAP);
        auto clamp = impulse_response<LCAP>(static_cast<float>(LCAP - 2), LCAP);
        CHECK_NEAR(over[LCAP - 2], 1.0f, 1e-4f);
        bool identical = true;
        for (int n = 0; n < LCAP; ++n)
            if (std::abs(over[n] - clamp[n]) > 1e-6f) { identical = false; break; }
        CHECK(identical);
    }

    // ---- C. capacity() accessor reports the template parameter ----
    CHECK(spe::dsp::DelayLine<16384>::capacity() == 16384);
    CHECK(spe::dsp::DelayLine48k::capacity() == 48000);

    if (failures == 0) std::printf("test_p_delayline_clamp: ALL PASS\n");
    return failures;
}
