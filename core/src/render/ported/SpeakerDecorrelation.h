// core/src/render/ported/SpeakerDecorrelation.h
//
// Dreamscape Convergence ⑦ — per-speaker decorrelation bank, ported from the
// reference engine.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/SpeakerDecorrelation.{h,cpp}. Byte-faithful port of the per-channel
//   Schroeder first-order allpass cascade (1–8 stages), the deterministic
//   per-speaker micro-delay (Murmur-style speaker hash → frac → delay samples),
//   and the energy-preserving dry/wet mix (dry = √(1−mix²), wet = mix). Reduces
//   inter-channel correlation (IACC) on the speaker bus for wider envelopment.
//
// juce-free already in the reference. The ONE mmhoa adaptation: the per-channel
// delay store is a std::vector (heap, sized at prepare) instead of an inline
// std::array<float, kDelayCapacity> — 128 channels × 4096 floats inline would be
// ~2 MB on the object, overflowing the stack where tests construct the engine.
// The ring indices / capacity / wrap are identical, so the DSP is bit-faithful.

#pragma once

#include "render/ported/SpatialMath.h"  // iae::kPrototypeChannels

#include <array>
#include <cstdint>
#include <vector>

namespace iae {

class SpeakerDecorrelationBank {
public:
    static constexpr int kMaxStages = 8;
    static constexpr int kDelayCapacity = 4096;

    void prepare(double sampleRateHz, int maxSamplesPerBlock) noexcept;
    void reset() noexcept;

    // Decorrelate one speaker channel in place. enabled==false or mix01<=1e-5 →
    // passthrough (no-op). Reconfigures the channel's delay/stages/g lazily when
    // the (stages, spread, ap, seed) config changes (cfgHash), clearing that
    // channel's state on change. RT-safe: no allocation (buffers prepared).
    void processChannel(int speakerIndex,
                        float* ioBuffer,
                        int numSamples,
                        bool enabled,
                        float mix01,
                        int stages,
                        float delaySpreadMs,
                        float apCoeff01,
                        std::uint32_t seed) noexcept;

private:
    struct ChannelState {
        std::vector<float> delay;   // sized kDelayCapacity at prepare (heap)
        int   writePos = 0;
        std::array<float, kMaxStages> apZm1 {};
        int   delaySamples = 1;
        int   stagesUsed = 1;
        float apG = 0.62f;
        std::uint32_t cfgHash = 0;
    };

    std::array<ChannelState, kPrototypeChannels> channels {};
    double sr = 48000.0;
    int    maxBlock = 512;

    // First-order Schroeder allpass (reference SpeakerDecorrelation.h:50-56).
    static inline float schroederAllpass(float x, float g, float& zm1) noexcept {
        const float v = x + g * zm1;
        const float y = zm1 - g * v;
        zm1 = v;
        return y;
    }

    // Murmur-style speaker hash (reference SpeakerDecorrelation.h:58-65).
    static inline std::uint32_t hashSpeaker(unsigned speakerIndex, std::uint32_t seed) noexcept {
        std::uint32_t x = seed ^ static_cast<std::uint32_t>(speakerIndex + 1u) * 2654435761u;
        x ^= x >> 16;
        x *= 2246822519u;
        x ^= x >> 13;
        return x;
    }

    void ensureConfigured(ChannelState& ch,
                          int speakerIndex,
                          float delaySpreadMs,
                          int stages,
                          float apCoeff01,
                          std::uint32_t seed) noexcept;
};

} // namespace iae
