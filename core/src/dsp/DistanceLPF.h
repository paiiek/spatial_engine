// core/src/dsp/DistanceLPF.h
// 1-pole HF rolloff: fc = 22050 / (1 + dist_m * k_hf)
// Models air absorption / distance-induced HF loss.
// RT-safe.

#pragma once
#include <cmath>

namespace spe::dsp {

class DistanceLPF {
public:
    static constexpr float K_HF_DEFAULT = 1.0f; // tune per room

    void prepareToPlay(double sample_rate) noexcept {
        sr_  = static_cast<float>(sample_rate);
        z1_  = 0.f;
        updateCoeff(1.0f);
    }

    void setDistance(float dist_m, float k_hf = K_HF_DEFAULT) noexcept {
        float fc = 22050.f / (1.f + dist_m * k_hf);
        updateCoeff(fc);
    }

    float fc(float dist_m, float k_hf = K_HF_DEFAULT) const noexcept {
        return 22050.f / (1.f + dist_m * k_hf);
    }

    float processSample(float x) noexcept {
        // 1-pole IIR: y[n] = (1-a)*x[n] + a*y[n-1]
        z1_ = (1.f - a_) * x + a_ * z1_;
        return z1_;
    }

private:
    void updateCoeff(float fc) noexcept {
        if (sr_ <= 0.f) return;
        // a = exp(-2*pi*fc/sr) for 1-pole lowpass
        float w = 2.f * 3.14159265f * fc / sr_;
        a_ = std::exp(-w);
    }

    float sr_ = 48000.f;
    float a_  = 0.f;
    float z1_ = 0.f;
};

} // namespace spe::dsp
