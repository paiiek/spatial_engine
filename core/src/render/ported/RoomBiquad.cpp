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

} // namespace iae
