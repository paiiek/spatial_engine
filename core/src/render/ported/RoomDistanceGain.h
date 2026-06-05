// core/src/render/ported/RoomDistanceGain.h
//
// Dreamscape Convergence ⑥f — room distance-gain curve core, ported from the
// reference engine's per-object room send computation.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/AudioEngine.cpp:29-62 — roomDistanceGainDbLinear() and
//   normalizedRoomDistance01(). These map a source distance (m) to a linear
//   gain via a near/far window, a linearity-shaped power curve, and a close→far
//   dB interpolation. The reference computes earlyDistMul / lateDistMul per
//   object each block and feeds them to RoomEngine::computeObject (early taps and
//   late send respectively). This core reproduces both functions byte-faithfully,
//   juce-free (juce::jmax/jlimit → std::max/clamp; juce::Decibels::decibelsToGain
//   → std::pow(10, db*0.05), exact for the clamped dB range which never reaches
//   JUCE's -100 dB minus-infinity floor).
//
// Pure scalar math (no state, no allocation): the live wiring into renderRoomEarly
// (earlyDistMul) and the room late bus (lateDistMul) is the follow-on increment
// ⑥f-2 (mirrors the ⑥a/⑥e core-first split).

#pragma once

#include <algorithm>
#include <cmath>

namespace iae {

// Normalized room distance in [0,1] with a linearity-shaped curve.
// AudioEngine.cpp:50-62. nearM/farM define the window; linearity01=1 → linear,
// →0 → cubic (exponent 1 + (1-lin)*2 ∈ [1,3]).
[[nodiscard]] inline float normalizedRoomDistance01(float distM, float nearM,
                                                    float farM, float linearity01) noexcept {
    const float n = std::max(0.05f, nearM);
    const float f = std::max(n + 0.1f, farM);
    float d01 = (distM - n) / (f - n);
    d01 = std::clamp(d01, 0.f, 1.f);
    const float lin = std::clamp(linearity01, 0.f, 1.f);
    const float exponent = 1.f + (1.f - lin) * 2.f;
    return std::pow(d01, exponent);
}

// Distance → linear gain via a close→far dB interpolation along the shaped curve.
// AudioEngine.cpp:29-47. closeDb/farDb are clamped to [-48,12] dB; the result is
// clamped to [0, 1.5] linear (matching the reference, which allows up to +3.5 dB
// of make-up before the limiter).
[[nodiscard]] inline float roomDistanceGainDbLinear(float distM, float nearM, float farM,
                                                    float closeDb, float farDb,
                                                    float linearity01) noexcept {
    const float n = std::max(0.05f, nearM);
    const float f = std::max(n + 0.1f, farM);
    float d01 = (distM - n) / (f - n);
    d01 = std::clamp(d01, 0.f, 1.f);
    const float lin = std::clamp(linearity01, 0.f, 1.f);
    const float exponent = 1.f + (1.f - lin) * 2.f;
    const float t = std::pow(d01, exponent);
    const float cDb = std::clamp(closeDb, -48.f, 12.f);
    const float gDb = std::clamp(farDb, -48.f, 12.f);
    const float db = cDb + t * (gDb - cDb);
    // juce::Decibels::decibelsToGain(db) = db > -100 ? 10^(db*0.05) : 0. Our db is
    // in [-48,12] (always > -100), so the pow path is exact.
    const float gain = std::pow(10.f, db * 0.05f);
    return std::clamp(gain, 0.f, 1.5f);
}

} // namespace iae
