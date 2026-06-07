// core/src/dsp/PinkNoise.h
// Paul Kellet "refined" 7-state pink filter — true −3 dB/octave. Replaces
// mmhoa's earlier single-pole LP placeholder ("gentle LP for pink"), which was
// ~−6 dB/oct and not log-flat per octave.
//
// NOTE on the reference engine: immersive-audio-engine AudioEngine.cpp:975-983
// (Dreamscape v0.2.1) uses the same six pole values but DIFFERENT input gains
// (0.822/0.479/0.247/0.123/0.075/-0.064/0.047, decreasing) — those measure a
// ~−5.3 dB/oct slope analytically, i.e. they are NOT true pink. Per the project
// rule that DSP is self-implemented/tuned (not byte-identical to the reference),
// we use the well-established canonical Kellet gains (increasing,
// 0.0555179 … 0.5329522) that actually deliver −3 dB/oct. b6 is a pure
// feedthrough section (pole 0), summed in-line.
//
// The caller owns the white source (RNG policy stays in the engine); this filter
// turns a white sample in [-1, 1] into a pink sample including a level scale.
// RT-safe: 7 floats of state, no alloc, branch-free. The transfer function is
// H(z) = sum_i g_i / (1 - a_i z^-1) + 0.5362, scaled by kOutScale — see
// test_convergence_pink_noise for the analytic −3 dB/oct verification.
#pragma once

#include <array>
#include <cstddef>

namespace spe::dsp {

class PinkKellet {
public:
    // Pole (a_i) and input-gain (g_i) per one-pole section. Canonical Kellet
    // "refined" gains (music-dsp). b6 (index 6) is a feedthrough (a=0): note the
    // canonical structure DELAYS b6 by one sample (assigned after the sum); here
    // it is summed undelayed in the same tick. This is intentional and only
    // affects the very top octave (white-direct path |0.5362+0.115926·z⁻¹|: 0.60
    // @8 kHz, 0.44 @Nyquist vs a flat 0.652 here) — the 125 Hz..8 kHz slope is
    // unchanged at −3.01 dB/oct. Do NOT "fix" b6 into a delay without also
    // re-trimming kOutScale.
    static constexpr std::array<float, 7> kA = {
        0.99886f, 0.99332f, 0.96900f, 0.86650f, 0.55000f, -0.7616f, 0.0f };
    static constexpr std::array<float, 7> kG = {
        0.0555179f, 0.0750759f, 0.1538520f, 0.3104856f, 0.5329522f,
        -0.0168980f, 0.1159260f };
    static constexpr float kWhiteFeed = 0.5362f;  // canonical direct white term
    static constexpr float kOutScale  = 0.11f;    // level trim (keeps headroom)

    void reset() noexcept { b_.fill(0.f); }

    // white in [-1, 1] -> pink sample (already scaled by kOutScale).
    float processSample(float white) noexcept {
        float s = 0.f;
        for (std::size_t i = 0; i < 7; ++i) {
            b_[i] = kA[i] * b_[i] + white * kG[i];
            s += b_[i];
        }
        s += white * kWhiteFeed;
        return s * kOutScale;
    }

private:
    std::array<float, 7> b_{};
};

} // namespace spe::dsp
