// core/src/dsp/DelayLine.h
// Fixed-size fractional delay line with linear interpolation.
// RT-safe: pre-allocated buffer at prepareToPlay.

#pragma once
#include <array>
#include <cmath>
#include "core/Constants.h"

namespace spe::dsp {

// Max delay: 1 second at 48 kHz = 48000 samples.
inline constexpr int DELAY_LINE_MAX_SAMPLES = 48000;

class DelayLine {
public:
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
        while (rd < 0.f) rd += static_cast<float>(DELAY_LINE_MAX_SAMPLES);

        int ri0 = static_cast<int>(rd) % DELAY_LINE_MAX_SAMPLES;
        int ri1 = (ri0 + 1) % DELAY_LINE_MAX_SAMPLES;
        float frac = rd - std::floor(rd);
        float out = buf_[ri0] * (1.f - frac) + buf_[ri1] * frac;

        write_ = (write_ + 1) % DELAY_LINE_MAX_SAMPLES;
        return out;
    }

    void setDelaySamples(float /*d*/) noexcept {} // hint only, no-op

private:
    std::array<float, DELAY_LINE_MAX_SAMPLES> buf_{};
    int write_ = 0;
};

} // namespace spe::dsp
