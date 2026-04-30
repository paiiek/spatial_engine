// core/src/dsp/EQ4Band.h
// 4-band biquad parametric EQ (peaking shelves).
// RT-safe: no allocation after prepareToPlay.

#pragma once
#include <array>
#include <cmath>

namespace spe::dsp {

struct BiquadCoeffs {
    float b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
};

struct BiquadState {
    float z1{0}, z2{0};
    float process(float x, const BiquadCoeffs& c) noexcept {
        float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

struct EQ4BandParams {
    float freq[4] = {100.f, 500.f, 2000.f, 8000.f};
    float q[4]    = {0.707f, 0.707f, 0.707f, 0.707f};
    float gain_db[4] = {0.f, 0.f, 0.f, 0.f};
};

class EQ4Band {
public:
    void prepareToPlay(double sample_rate) noexcept {
        sr_ = static_cast<float>(sample_rate);
        for (auto& s : states_) s = {};
        updateCoeffs();
    }

    void setParams(const EQ4BandParams& p) noexcept {
        params_ = p;
        updateCoeffs();
    }

    float processSample(float x) noexcept {
        for (int i = 0; i < 4; ++i)
            x = states_[i].process(x, coeffs_[i]);
        return x;
    }

private:
    void updateCoeffs() noexcept {
        if (sr_ <= 0.f) return;
        for (int i = 0; i < 4; ++i) {
            const float f  = params_.freq[i];
            const float q  = params_.q[i];
            const float gdb= params_.gain_db[i];
            const float A  = std::pow(10.f, gdb / 40.f);
            const float w0 = 2.f * 3.14159265f * f / sr_;
            const float cosw = std::cos(w0);
            const float sinw = std::sin(w0);
            const float alpha = sinw / (2.f * q);

            // Peaking EQ
            float b0 = 1.f + alpha * A;
            float b1 = -2.f * cosw;
            float b2 = 1.f - alpha * A;
            float a0 = 1.f + alpha / A;
            float a1 = -2.f * cosw;
            float a2 = 1.f - alpha / A;

            coeffs_[i].b0 = b0 / a0;
            coeffs_[i].b1 = b1 / a0;
            coeffs_[i].b2 = b2 / a0;
            coeffs_[i].a1 = a1 / a0;
            coeffs_[i].a2 = a2 / a0;
        }
    }

    float sr_ = 48000.f;
    EQ4BandParams params_;
    std::array<BiquadCoeffs, 4> coeffs_{};
    std::array<BiquadState, 4>  states_{};
};

} // namespace spe::dsp
