// core/src/render/ported/RoomBiquad.h
//
// Dreamscape Convergence ⑥e-3a — Room absorption-EQ biquad core ported from the
// reference Room Engine's JUCE IIR filters.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/RoomEngine.cpp:168-197 (syncRoomEqCoeffsIfNeeded) builds the room
//   absorption EQ from juce::dsp::IIR::Coefficients::makeHighPass / makeLowPass
//   with Q = 1/sqrt(2), applied as the per-object early-cluster HP+LP
//   (earlyClusterHp/Lp), the cluster-bus HP+LP (clusterBusHp/Lp) and the
//   late-bus HP+LP (lateBusHp/Lp). This core reproduces, juce-free and
//   byte-faithfully, both the coefficient formulas and JUCE's runtime topology:
//     - makeLowPass/makeHighPass (Q-form): JUCE juce_IIRFilter.cpp:75-117
//     - processSample: Transposed Direct Form II, juce_IIRFilter_Impl.h:207-218
//   All arithmetic is in float (NumericType = float in the reference, which uses
//   Coefficients<float>), so the realised coefficients are bit-identical.
//
// State only (no allocation): an EQ is just 5 coefficients + 2 state words. The
// per-object / per-bus instantiation, the coeff-cache (syncRoomEqCoeffsIfNeeded)
// and the live wiring into the early/cluster/late paths are the follow-on
// increment (mirrors the ⑥a/⑥c/⑥e-2 core-first split).

#pragma once

namespace iae {

// 1/sqrt(2) — JUCE's `inverseRootTwo` default Q (and the literal Q the reference
// passes to make{High,Low}Pass). As a float this is 0.70710677f.
constexpr float kRoomEqDefaultQ = 0.70710678118654752440f;

// Reference room absorption-EQ corner frequencies (SpatialSessionState.h:154-158).
constexpr float kRoomEarlyClusterHpfHz = 120.f;
constexpr float kRoomEarlyClusterLpfHz = 10000.f;
constexpr float kRoomLateHpfHz         = 45.f;
constexpr float kRoomLateLpfHz         = 16000.f;

// A single second-order IIR section, byte-faithful to juce::dsp::IIR::Filter<float>
// with Coefficients<float>::make{Low,High}Pass. Coefficients are stored already
// normalised by a0 (a0 == 1 for these forms): {b0, b1, b2, a1, a2}.
class RoomBiquad {
public:
    // Configure as a low-pass (JUCE makeLowPass, juce_IIRFilter.cpp:75-91).
    void setLowPass(double sampleRate, float frequency, float q = kRoomEqDefaultQ) noexcept;
    // Configure as a high-pass (JUCE makeHighPass, juce_IIRFilter.cpp:101-117).
    void setHighPass(double sampleRate, float frequency, float q = kRoomEqDefaultQ) noexcept;

    // Clear the filter state (keeps coefficients). A default-constructed biquad
    // is a unity passthrough (b0=1, rest 0) until set{Low,High}Pass configures it.
    void reset() noexcept { s0_ = 0.f; s1_ = 0.f; }

    // RT-safe, no allocation. Transposed Direct Form II (JUCE
    // juce_IIRFilter_Impl.h:212-217):
    //   y = b0*x + s0;  s0 = b1*x - a1*y + s1;  s1 = b2*x - a2*y.
    [[nodiscard]] inline float processSample(float x) noexcept {
        const float y = b0_ * x + s0_;
        s0_ = b1_ * x - a1_ * y + s1_;
        s1_ = b2_ * x - a2_ * y;
        return y;
    }

    // Raw coefficients (for verification/tests): {b0, b1, b2, a1, a2}.
    float b0() const noexcept { return b0_; }
    float b1() const noexcept { return b1_; }
    float b2() const noexcept { return b2_; }
    float a1() const noexcept { return a1_; }
    float a2() const noexcept { return a2_; }

private:
    float b0_ = 1.f, b1_ = 0.f, b2_ = 0.f, a1_ = 0.f, a2_ = 0.f;
    float s0_ = 0.f, s1_ = 0.f;
};

} // namespace iae
