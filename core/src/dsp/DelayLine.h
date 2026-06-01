// core/src/dsp/DelayLine.h
// Fixed-size fractional delay line with linear interpolation.
// RT-safe by construction: capacity is a compile-time template parameter, so the
// backing storage is a fixed-size std::array (zero heap, no per-block alloc).
// Each instantiation right-sizes its buffer to its real delay need:
//   - WFS / propagation (geometry-bounded, r/c) → DelayLine<WFS_MAX_DELAY_SAMPLES>
//   - user_delay / speaker-alignment (user-settable, may want ~1 s) → DelayLine48k
//
// 2026-06-01 (Lane F5): templated on Capacity to kill the dominant footprint term
// (WFS delays_ was MAX_OBJECTS*num_speakers * 187.5 KB always-allocated). See
// .omc/plans/spatial-engine-v0.9-laneF5-wfs-memory.md and docs/RT_BUDGET_MAX_OBJECTS.md.

#pragma once
#include <array>
#include <cmath>
#include "core/Constants.h"

namespace spe::dsp {

// Default / large-buffer capacity: 1 second at 48 kHz = 48000 samples.
// Used by the user-settable delay lines (user_delay_, spk_delays_) which may
// legitimately request up to ~1 s and are NOT geometry-bounded.
inline constexpr int DELAY_LINE_MAX_SAMPLES = 48000;

// WFS / propagation capacity. Geometry-bounded: source distance is hard-capped
// at ADM_OSC_MAX_DIST = 20 m and the speaker-array radius is bounded, so the
// source-to-speaker delay r/c stays small. Derivation: worst-case 50 m @ 96 kHz
//   50 m / 343 m·s^-1 * 96000 Hz ≈ 13994 samples → rounded up to 2^14 = 16384.
// Delays beyond this are gracefully clamped (see processSample), never wrapped.
inline constexpr int WFS_MAX_DELAY_SAMPLES = 16384;

template <int Capacity = DELAY_LINE_MAX_SAMPLES>
class DelayLine {
public:
    static constexpr int capacity() noexcept { return Capacity; }

    void prepareToPlay(double /*sr*/) noexcept {
        buf_.fill(0.f);
        write_ = 0;
    }

    // Push one sample and read back fractional-delayed output.
    float processSample(float input, float delay_samples) noexcept {
        // Write
        buf_[write_] = input;

        // Read with linear interpolation
        float rd = static_cast<float>(write_) - delay_samples;
        while (rd < 0.f) rd += static_cast<float>(Capacity);

        int ri0 = static_cast<int>(rd) % Capacity;
        int ri1 = (ri0 + 1) % Capacity;
        float frac = rd - std::floor(rd);
        float out = buf_[ri0] * (1.f - frac) + buf_[ri1] * frac;

        write_ = (write_ + 1) % Capacity;
        return out;
    }

    void setDelaySamples(float /*d*/) noexcept {} // hint only, no-op

private:
    std::array<float, Capacity> buf_{};
    int write_ = 0;
};

// Convenience alias for the large (1 s @48 k) user-settable delay lines.
using DelayLine48k = DelayLine<>;

} // namespace spe::dsp
