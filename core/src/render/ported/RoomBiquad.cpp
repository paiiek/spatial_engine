// core/src/render/ported/RoomBiquad.cpp
// See RoomBiquad.h for provenance (immersive-audio-engine RoomEngine @ f2cb796
// + the vendored JUCE juce::dsp::IIR coefficient formulas).

#include "render/ported/RoomBiquad.h"

#include <algorithm>
#include <cmath>

namespace iae {

namespace {
// JUCE MathConstants<float>::pi (juce_MathsFunctions.h).
constexpr float kPiF = 3.14159265358979323846f;

// Defense-in-depth: keep the corner strictly inside (0, Nyquist) so the tan()
// below can never hit π/2 → ±∞ → NaN coefficients (which would poison the whole
// room bus). At/above Nyquist tan blows up; at 0 the LP form divides by zero.
// Current callers already clamp tighter (SpatialEngine clampHz at 0.45·Fs), so
// in-range corners (≤ ~0.49·Fs) are unchanged and byte-faithfulness is preserved;
// this only neutralises an out-of-range corner from any future caller.
inline float clampCorner(float frequency, double sampleRate) noexcept {
    return std::clamp(frequency, 1.f, 0.49f * static_cast<float>(sampleRate));
}
} // namespace

void RoomBiquad::setLowPass(double sampleRate, float frequency, float q) noexcept {
    frequency = clampCorner(frequency, sampleRate);
    // juce_IIRFilter.cpp:75-91 (makeLowPass, Q form). All float arithmetic.
    const float n        = 1.f / std::tan(kPiF * frequency / static_cast<float>(sampleRate));
    const float nSquared = n * n;
    const float invQ     = 1.f / q;
    const float c1       = 1.f / (1.f + invQ * n + nSquared);

    // {b0,b1,b2,a0,a1,a2} = {c1, c1*2, c1, 1, c1*2*(1-nSq), c1*(1-invQ*n+nSq)};
    // a0 == 1 so the stored coeffs are already normalised.
    b0_ = c1;
    b1_ = c1 * 2.f;
    b2_ = c1;
    a1_ = c1 * 2.f * (1.f - nSquared);
    a2_ = c1 * (1.f - invQ * n + nSquared);
}

void RoomBiquad::setHighPass(double sampleRate, float frequency, float q) noexcept {
    frequency = clampCorner(frequency, sampleRate);
    // juce_IIRFilter.cpp:101-117 (makeHighPass, Q form). All float arithmetic.
    const float n        = std::tan(kPiF * frequency / static_cast<float>(sampleRate));
    const float nSquared = n * n;
    const float invQ     = 1.f / q;
    const float c1       = 1.f / (1.f + invQ * n + nSquared);

    // {b0,b1,b2,a0,a1,a2} = {c1, c1*-2, c1, 1, c1*2*(nSq-1), c1*(1-invQ*n+nSq)}.
    b0_ = c1;
    b1_ = c1 * -2.f;
    b2_ = c1;
    a1_ = c1 * 2.f * (nSquared - 1.f);
    a2_ = c1 * (1.f - invQ * n + nSquared);
}

void RoomBiquad::setPeak(double sampleRate, float frequency, float q, float gainDb) noexcept {
    // juce::dsp::IIR::Coefficients<float>::makePeakFilter (RBJ peaking EQ). All
    // float arithmetic, matching the reference Coefficients<float> instantiation,
    // so the realised coefficients are bit-identical to the JUCE chain.
    //
    // The reference passes gainFactor = juce::Decibels::decibelsToGain(gainDb)
    // (juce_Decibels.h): linear = 10^(dB/20) for dB > -100, else 0. We fold that
    // conversion in here so the caller works in dB (the SpatialSessionState
    // contract field is binauralEqGainDb). A 0 dB band gives gainFactor = 1 →
    // A = 1 → alphaTimesA == alphaOverA → b == a → unity passthrough.
    const float gainFactor = (gainDb > -100.f)
                                 ? std::pow(10.f, gainDb * 0.05f)
                                 : 0.f;
    // A floor on A keeps alpha/A finite for pathological deep cuts; in-range
    // gains (the engine clamps to ±24 dB) never reach it, so byte-faithfulness
    // is preserved where it matters.
    const float A = std::max(1.0e-6f, std::sqrt(gainFactor));

    const float omega = (2.f * kPiF * std::max(frequency, 2.f))
                        / static_cast<float>(sampleRate);
    const float alpha = std::sin(omega) / (q * 2.f);
    const float c2    = -2.f * std::cos(omega);
    const float alphaTimesA = alpha * A;
    const float alphaOverA  = alpha / A;

    // JUCE Coefficients ctor stores {b0,b1,b2,a1,a2} normalised by a0.
    //   (b0,b1,b2,a0,a1,a2) = (1+αA, c2, 1-αA, 1+α/A, c2, 1-α/A)
    const float a0inv = 1.f / (1.f + alphaOverA);
    b0_ = (1.f + alphaTimesA) * a0inv;
    b1_ = c2 * a0inv;
    b2_ = (1.f - alphaTimesA) * a0inv;
    a1_ = c2 * a0inv;
    a2_ = (1.f - alphaOverA) * a0inv;
}

} // namespace iae
