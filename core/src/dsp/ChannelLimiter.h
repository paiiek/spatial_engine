// core/src/dsp/ChannelLimiter.h
// Per-channel 1-pole envelope follower soft-knee limiter.
// Header-only, no JUCE dependency.
#pragma once
#include <cmath>
#include <algorithm>

namespace spe::dsp {

class ChannelLimiter {
public:
    void prepare(double sample_rate) {
        sr_ = sample_rate;
        const float attack_ms  = 1.0f;
        const float release_ms = 100.0f;
        attack_coef_  = std::exp(-1.0f / (attack_ms  * 0.001f * static_cast<float>(sample_rate)));
        release_coef_ = std::exp(-1.0f / (release_ms * 0.001f * static_cast<float>(sample_rate)));
        envelope_     = 0.f;
    }

    // threshold_lin: 1.0 = no limit (default). e.g., 0.708 = -3 dB.
    void setThreshold(float threshold_lin) noexcept {
        threshold_ = std::max(0.001f, threshold_lin);
    }

    float getThreshold() const noexcept { return threshold_; }

    float processSample(float in) noexcept {
        const float abs_in = std::abs(in);
        if (abs_in > envelope_) {
            envelope_ = abs_in;
        } else {
            envelope_ = release_coef_ * envelope_ + (1.f - release_coef_) * abs_in;
        }
        float gain = 1.f;
        if (envelope_ > threshold_) {
            gain = threshold_ / envelope_;
        }
        return in * gain;
    }

private:
    double sr_            = 48000.0;
    float  attack_coef_   = 0.f;
    float  release_coef_  = 0.f;
    float  envelope_      = 0.f;
    float  threshold_     = 1.f; // 1.0 = no compression
};

} // namespace spe::dsp
