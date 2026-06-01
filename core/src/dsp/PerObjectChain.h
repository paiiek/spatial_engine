// core/src/dsp/PerObjectChain.h
// Full per-object DSP chain:
//   Source → 4-Band EQ → User Delay → [Pan gain applied by renderer]
//   → distance gain → distance HF rolloff → propagation delay → reverb send
// RT-safe: all state pre-allocated.

#pragma once
#include "dsp/EQ4Band.h"
#include "dsp/DelayLine.h"
#include "dsp/DistanceGain.h"
#include "dsp/DistanceLPF.h"
#include "dsp/PropagationDelay.h"
#include "dsp/GainRamp.h"

namespace spe::dsp {

struct PerObjectChainParams {
    EQ4BandParams eq;
    float user_delay_ms  = 0.f;
    float dist_m         = 1.f;
    float k_hf           = DistanceLPF::K_HF_DEFAULT;
    float reverb_send    = 0.f;
    float gain_lin       = 1.f;
};

class PerObjectChain {
public:
    void prepareToPlay(double sample_rate) noexcept {
        sr_ = static_cast<float>(sample_rate);
        eq_.prepareToPlay(sample_rate);
        user_delay_.prepareToPlay(sample_rate);
        hf_.prepareToPlay(sample_rate);
        prop_.prepareToPlay(sample_rate);
        gain_ramp_.reset(1.0f);
        applyParams(params_);
    }

    void setParams(const PerObjectChainParams& p) noexcept {
        params_ = p;
        applyParams(p);
    }

    // Process one sample through the full chain.
    // reverb_out receives the reverb send signal.
    float processSample(float input, float& reverb_out) noexcept {
        float x = input * gain_ramp_.next();
        x = eq_.processSample(x);

        // User delay (convert ms → samples)
        float ud_samples = params_.user_delay_ms * sr_ * 0.001f;
        x = user_delay_.processSample(x, ud_samples);

        // Distance gain (1/r)
        x *= dist_gain_.gainForDistance(params_.dist_m);

        // Distance HF rolloff
        x = hf_.processSample(x);

        // Propagation delay
        x = prop_.processSample(x);

        // Reverb send (tap before output)
        reverb_out = x * params_.reverb_send;

        return x;
    }

    void setGainTarget(float gain, int ramp_samples) noexcept {
        gain_ramp_.setTarget(gain, ramp_samples);
    }

private:
    void applyParams(const PerObjectChainParams& p) noexcept {
        eq_.setParams(p.eq);
        hf_.setDistance(p.dist_m, p.k_hf);
        prop_.setDistance(p.dist_m);
        gain_ramp_.setTarget(p.gain_lin, 0);
    }

    float sr_ = 48000.f;
    PerObjectChainParams params_;

    EQ4Band       eq_;
    // user_delay_ is a user-settable delay (user_delay_ms over OSC) that may
    // legitimately want up to ~1 s → KEEP the large 48000 capacity (DelayLine48k).
    DelayLine48k  user_delay_;
    DistanceGain  dist_gain_;
    DistanceLPF   hf_;
    PropagationDelay prop_;
    GainRamp      gain_ramp_;
};

} // namespace spe::dsp
