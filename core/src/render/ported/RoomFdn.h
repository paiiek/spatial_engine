// core/src/render/ported/RoomFdn.h
//
// Dreamscape Convergence ⑥a — Late-reverb FDN core (8x8 Hadamard feedback
// delay network) ported from the reference Room Engine.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/RoomEngine.{h,cpp} @ commit f2cb796 (initial commit).
//   Byte-faithful port of the late-FDN signal path (RoomEngine::prepare delay
//   setup + RoomEngine::finishBlock Phase-4 loop): 2 cascaded input-diffusion
//   allpasses, 8 sample-rate-scaled delay lines, per-line one-pole HF damping,
//   Sylvester-8 Hadamard feedback with T60-derived loop gain, and per-line
//   output taps. The SPATIAL distribution of the 8 line taps (cube-corner VBAP
//   + uniform-diffuse blend) and the early-reflection / cluster stages are
//   separate convergence increments (⑥b/⑥c); this core emits the 8 raw line
//   taps so the spatialiser can pan them.
//
// Frame-agnostic: this is a pure scalar DSP network — no geometry, no JUCE.
// Adaptations vs the reference: juce::jlimit/jmax -> std::clamp/std::max,
// juce::MathConstants -> literals, juce::AudioBuffer late bus -> caller buffers.

#pragma once

#include <array>
#include <vector>

namespace iae {

// Reference mid-range defaults (RoomEngine / SpatialAudioPull).
struct RoomFdnParams {
    float t60Seconds        = 1.2f;   // [0.2, 6.0]  RT60 decay
    float hfDecayRatio01    = 0.62f;  // [0.05, 1.0] 1=no HF damping, <1=bright tail decays faster
    float hfDecayCornerHz   = 6200.f; // [800, 16000] one-pole damping corner
};

class RoomFdn {
public:
    static constexpr int kOrder = 8;

    // Allocates the 8 delay lines (control thread). May allocate.
    void prepare(double sampleRate, int maxBlock);

    // Clear all delay/allpass/damping state (keeps allocation).
    void reset() noexcept;

    void setParams(const RoomFdnParams& p) noexcept { params_ = p; }
    const RoomFdnParams& params() const noexcept { return params_; }

    bool ready() const noexcept { return ready_; }

    // RT-safe: no allocation. Reads `in[0..n)`, writes the 8 per-line output
    // taps into `outLines`, laid out line-major: line k occupies
    // outLines[k * n + i] for i in [0, n). `outLines` must hold kOrder * n floats.
    void process(const float* in, int n, float* outLines) noexcept;

private:
    static inline int wrapIndex(int x, int m) noexcept {
        x %= m; if (x < 0) x += m; return x;
    }
    // In-place Sylvester order-8 fast Hadamard (unnormalised).
    static void fastHadamard8(float* a) noexcept;

    double sampleRate_ = 48000.0;
    int    maxBlock_   = 0;
    bool   ready_      = false;
    RoomFdnParams params_{};

    std::array<std::vector<float>, kOrder> delayLines_{};
    std::array<int,   kOrder> writePos_{};
    std::array<int,   kOrder> delaySamples_{};
    std::array<float, kOrder> lpState_{};

    // Two cascaded input-diffusion allpasses (reference coeffs 0.72 / 0.62).
    std::array<float, 256> ap1_{}, ap2_{};
    int   ap1Pos_ = 0, ap2Pos_ = 0;
    float ap1Coeff_ = 0.72f, ap2Coeff_ = 0.62f;
};

} // namespace iae
