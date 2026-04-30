// core/src/reverb/FdnReverb.h
// 16-line Feedback Delay Network reverb with Hadamard feedback matrix.
// JUCE-free build: uses FTZ/DAZ SSE intrinsics instead of juce::ScopedNoDenormals.

#pragma once

#include <array>
#include <vector>
#include <cstdint>

namespace spe::reverb {

class FdnReverb {
public:
    static constexpr int kLines = 16;

    FdnReverb();

    // Must be called before process(). Allocates delay buffers, clears state,
    // sets FTZ/DAZ denormal suppression for this thread.
    void prepareToPlay(double sampleRate, int blockSize);

    // RT-safe: no allocation, no lock.
    // inOut is read as input and written as output (mono, in-place).
    void process(float* inOut, int numSamples) noexcept;

    // Wet/dry mix [0,1]. Default 1.0 (full wet).
    void setWetMix(float w) noexcept { wetMix_ = w; }

    // LP filter coefficient per line [0,1]. Default 0.7.
    void setDamping(float d) noexcept { damping_ = d; }

    // Feedback gain scalar (applied after Hadamard). Default 0.98.
    void setFeedback(float f) noexcept { feedback_ = f; }

    bool isPrepared() const noexcept { return prepared_; }

private:
    // Mutually-prime delay lengths in samples (at 48 kHz reference).
    // Will be scaled by actual sampleRate / 48000.
    static constexpr std::array<int, kLines> kBaseDelaysSamples = {
        1499, 1601, 1699, 1801, 1901, 2003, 2099, 2203,
        2311, 2417, 2503, 2609, 2707, 2801, 2903, 3001
    };

    // 16x16 unnormalized Hadamard matrix (H16 = H8 kron [[1,1],[1,-1]])
    // Values are +1 or -1; we normalize by dividing by 4 (sqrt(16)) at runtime.
    static const std::array<std::array<int8_t, kLines>, kLines> kHadamard;

    struct DelayLine {
        std::vector<float> buf;
        int                writePos = 0;
        int                length   = 0;
        float              lpState  = 0.f;   // 1-pole LP state
    };

    std::array<DelayLine, kLines> lines_;
    float wetMix_   = 1.f;
    float damping_  = 0.7f;   // LP coefficient (feedback * (1-damping) + prev * damping)
    float feedback_ = 0.98f;
    double sampleRate_ = 48000.0;
    bool   prepared_   = false;

    // DC offset signs per line (alternating +/- 1e-20) to keep denormals from
    // settling to exact zero even without FTZ.
    static constexpr float kDCOffset = 1e-20f;
};

} // namespace spe::reverb
