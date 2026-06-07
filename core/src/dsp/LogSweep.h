// core/src/dsp/LogSweep.h
// Exponential (log) sine sweep 20 Hz → 20 kHz, 1-second period, repeating.
// Byte-faithful to the reference engine's speaker-check sweep (immersive-audio-
// engine Source/AudioEngine.cpp:985-996, Dreamscape v0.2.1): a normalised time
// position advances 1/sr per sample and wraps at 1.0 (so the sweep restarts
// every second), the instantaneous frequency is f0·(f1/f0)^pos, and the output
// is the running sine of the integrated phase.
//
// Used as a per-channel speaker-check calibration signal (each NoiseChan owns
// its own generator → channels sweep independently). RT-safe: 1 float + 1
// double of state, no alloc/lock/syscall; std::sin/std::pow are the only calls.
#pragma once

#include <cmath>

namespace spe::dsp {

class LogSweep {
public:
    static constexpr float  kF0   = 20.f;       // sweep start (Hz)
    static constexpr float  kF1   = 20000.f;    // sweep end (Hz)
    static constexpr double kTwoPi = 6.283185307179586;

    void reset() noexcept { pos_ = 0.f; phase_ = 0.0; }

    // Instantaneous sweep frequency (Hz) at normalised position pos∈[0,1].
    static float instFreq(float pos) noexcept {
        return kF0 * std::pow(kF1 / kF0, pos);
    }

    // Advance one sample at sample rate sr and return the sweep value in [-1,1].
    float processSample(double sr) noexcept {
        pos_ += static_cast<float>(1.0 / sr);
        if (pos_ >= 1.f) pos_ -= 1.f;
        const double inst = static_cast<double>(instFreq(pos_));
        phase_ += kTwoPi * inst / sr;
        while (phase_ >= kTwoPi) phase_ -= kTwoPi;
        return static_cast<float>(std::sin(phase_));
    }

private:
    float  pos_   = 0.f;   // normalised time position [0,1)
    double phase_ = 0.0;   // integrated phase [0,2π)
};

} // namespace spe::dsp
