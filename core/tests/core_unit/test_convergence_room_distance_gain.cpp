// test_convergence_room_distance_gain.cpp
// Dreamscape Convergence ⑥f — room distance-gain curve core (iae::RoomDistanceGain).
//
// Verifies the byte-faithful port of AudioEngine.cpp:29-62:
//   - normalizedRoomDistance01: window mapping, linearity power curve, clamps.
//   - roomDistanceGainDbLinear: close→far dB interpolation along the curve,
//     dB→linear, [0,1.5] clamp.
// The reference formula is recomputed independently here (not via the ported
// function) and compared, so this is a real cross-check, not a tautology.

#include "render/ported/RoomDistanceGain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Independent reference re-implementation (mirrors AudioEngine.cpp exactly).
static float ref_norm(float dist, float nearM, float farM, float lin) {
    const float n = std::max(0.05f, nearM);
    const float f = std::max(n + 0.1f, farM);
    float d01 = std::clamp((dist - n) / (f - n), 0.f, 1.f);
    const float l = std::clamp(lin, 0.f, 1.f);
    return std::pow(d01, 1.f + (1.f - l) * 2.f);
}
static float ref_gain(float dist, float nearM, float farM, float cDb, float gDb, float lin) {
    const float t = ref_norm(dist, nearM, farM, lin);
    const float c = std::clamp(cDb, -48.f, 12.f);
    const float g = std::clamp(gDb, -48.f, 12.f);
    const float db = c + t * (g - c);
    return std::clamp(std::pow(10.f, db * 0.05f), 0.f, 1.5f);
}

int main() {
    // ---- normalizedRoomDistance01 ----
    // At/below near → 0; at/above far → 1.
    CHECK(feq(iae::normalizedRoomDistance01(0.5f, 0.5f, 24.f, 0.35f), 0.f), "norm: at near = 0");
    CHECK(feq(iae::normalizedRoomDistance01(0.1f, 0.5f, 24.f, 0.35f), 0.f), "norm: below near = 0");
    CHECK(feq(iae::normalizedRoomDistance01(24.f, 0.5f, 24.f, 0.35f), 1.f), "norm: at far = 1");
    CHECK(feq(iae::normalizedRoomDistance01(99.f, 0.5f, 24.f, 0.35f), 1.f), "norm: above far = 1");
    // linearity=1 → exponent 1 (linear); midpoint distance → 0.5.
    {
        const float mid = 0.5f + 0.5f * (24.f - 0.5f);
        CHECK(feq(iae::normalizedRoomDistance01(mid, 0.5f, 24.f, 1.f), 0.5f), "norm: lin=1 linear midpoint");
    }
    // linearity=0 → exponent 3; d01=0.5 → 0.125.
    {
        const float mid = 0.5f + 0.5f * (24.f - 0.5f);
        CHECK(feq(iae::normalizedRoomDistance01(mid, 0.5f, 24.f, 0.f), 0.125f), "norm: lin=0 cubic midpoint");
    }
    // Degenerate window (far <= near): f floored to n+0.1; finite, in [0,1].
    {
        const float v = iae::normalizedRoomDistance01(5.f, 10.f, 1.f, 0.5f);
        CHECK(v >= 0.f && v <= 1.f, "norm: degenerate window stays in [0,1]");
    }

    // ---- roomDistanceGainDbLinear: cross-check against the independent ref ----
    const float nearM = 0.5f, farM = 24.f;
    for (float dist : {0.2f, 0.5f, 2.f, 6.f, 12.f, 24.f, 40.f}) {
        for (float lin : {0.f, 0.35f, 1.f}) {
            // early defaults (-10 / -18), late defaults (-12 / 0).
            const float ge = iae::roomDistanceGainDbLinear(dist, nearM, farM, -10.f, -18.f, lin);
            const float re = ref_gain(dist, nearM, farM, -10.f, -18.f, lin);
            CHECK(feq(ge, re), "gain: early curve matches independent ref");
            const float gl = iae::roomDistanceGainDbLinear(dist, nearM, farM, -12.f, 0.f, lin);
            const float rl = ref_gain(dist, nearM, farM, -12.f, 0.f, lin);
            CHECK(feq(gl, rl), "gain: late curve matches independent ref");
        }
    }
    // At near, gain = decibelsToGain(closeDb); at/after far, gain = decibelsToGain(farDb).
    CHECK(feq(iae::roomDistanceGainDbLinear(0.5f, nearM, farM, -12.f, 0.f, 0.35f),
              std::pow(10.f, -12.f * 0.05f)), "gain: at near = closeDb gain");
    CHECK(feq(iae::roomDistanceGainDbLinear(24.f, nearM, farM, -12.f, 0.f, 0.35f),
              std::pow(10.f, 0.f * 0.05f)), "gain: at far = farDb gain");
    // Upper clamp: huge closeDb is clamped to +12 dB then to 1.5 linear (10^0.6≈3.98).
    CHECK(feq(iae::roomDistanceGainDbLinear(0.5f, nearM, farM, 100.f, 100.f, 0.5f), 1.5f),
          "gain: +inf dB clamps to 1.5 linear");
    // Lower bound: very negative dB → tiny but >= 0.
    CHECK(iae::roomDistanceGainDbLinear(24.f, nearM, farM, -48.f, -48.f, 0.5f) >= 0.f,
          "gain: -48 dB stays non-negative");

    if (failures == 0) { std::printf("test_convergence_room_distance_gain: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_room_distance_gain: %d FAIL\n", failures);
    return 1;
}
