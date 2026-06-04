// core/src/render/ported/RoomBiquad.cpp
// See RoomBiquad.h for provenance (immersive-audio-engine RoomEngine @ f2cb796
// + the vendored JUCE juce::dsp::IIR coefficient formulas).

#include "render/ported/RoomBiquad.h"

#include <cmath>

namespace iae {

namespace {
// JUCE MathConstants<float>::pi (juce_MathsFunctions.h).
constexpr float kPiF = 3.14159265358979323846f;
} // namespace

void RoomBiquad::setLowPass(double sampleRate, float frequency, float q) noexcept {
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
