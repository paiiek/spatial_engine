// core/src/dsp/PropagationDelay.h
// Fractional propagation delay: d / SOUND_C seconds.
// Uses DelayLine with GainRamp-smoothed delay target to avoid zipper artifacts.
// RT-safe.

#pragma once
#include "dsp/DelayLine.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <algorithm>

namespace spe::dsp {

class PropagationDelay {
public:
    void prepareToPlay(double sample_rate) noexcept {
        sr_    = static_cast<float>(sample_rate);
        delay_.prepareToPlay(sample_rate);
        // Initialize ramp at current target
        ramp_.reset(current_delay_samples_);
    }

    // Call from control thread (or start of block) to set new distance.
    // Smooths the delay over ramp_samples to avoid zipper noise.
    void setDistance(float dist_m, int ramp_samples = 512) noexcept {
        float target = distToSamples(dist_m);
        // Clamp to buffer capacity
        if (target > static_cast<float>(DELAY_LINE_MAX_SAMPLES - 1))
            target = static_cast<float>(DELAY_LINE_MAX_SAMPLES - 1);
        ramp_.setTarget(target, ramp_samples);
    }

    float processSample(float input) noexcept {
        current_delay_samples_ = ramp_.next();
        return delay_.processSample(input, current_delay_samples_);
    }

    // Delay in samples for given distance (no ramp).
    float delayForDistance(float dist_m) const noexcept {
        return distToSamples(dist_m);
    }

private:
    float distToSamples(float dist_m) const noexcept {
        return std::max(0.f, dist_m / spe::SOUND_C * sr_);
    }

    float     sr_                   = 48000.f;
    float     current_delay_samples_= 0.f;
    // F5-M1: kept at the large 48000 capacity for now; right-sized to
    // WFS_MAX_DELAY_SAMPLES in F5-M3 (propagation is geometry-bounded).
    DelayLine48k delay_;
    GainRamp  ramp_;
};

} // namespace spe::dsp
