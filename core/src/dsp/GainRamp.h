// core/src/dsp/GainRamp.h
// Per-sample linear gain interpolation across an audio block.
// JUCE-free replacement for juce::dsp::SmoothedValue.
// RT-safe: no heap allocation after construction.

#pragma once
#include <cmath>

namespace spe::dsp {

class GainRamp {
public:
    GainRamp() = default;
    explicit GainRamp(float initial) : current_(initial), target_(initial) {}

    // Set target; ramp will interpolate over next `ramp_samples` samples.
    void setTarget(float target, int ramp_samples) noexcept {
        target_ = target;
        if (ramp_samples <= 0) {
            current_ = target;
            step_ = 0.0f;
            remaining_ = 0;
        } else {
            step_ = (target - current_) / static_cast<float>(ramp_samples);
            remaining_ = ramp_samples;
        }
    }

    // Instantly jump to value (no ramp).
    void reset(float value) noexcept {
        current_ = value;
        target_  = value;
        step_    = 0.0f;
        remaining_ = 0;
    }

    // Returns current gain and advances by one sample.
    float next() noexcept {
        if (remaining_ > 0) {
            float val = current_;
            current_ += step_;
            --remaining_;
            if (remaining_ == 0) current_ = target_;
            return val;
        }
        return target_;
    }

    float currentValue() const noexcept { return current_; }
    float targetValue()  const noexcept { return target_; }
    bool  isRamping()    const noexcept { return remaining_ > 0; }

private:
    float current_   = 0.0f;
    float target_    = 0.0f;
    float step_      = 0.0f;
    int   remaining_ = 0;
};

} // namespace spe::dsp
