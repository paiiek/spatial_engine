// core/src/reverb/FdnReverb.cpp

#include "FdnReverb.h"
#include <cmath>
#include <cstring>

// FTZ/DAZ denormal suppression (SPATIAL_ENGINE_NO_JUCE path).
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace spe::reverb {

// ---------------------------------------------------------------------------
// 16x16 Hadamard matrix (H16). Generated via H2 = [[1,1],[1,-1]], then
// H16 = H2 kron H8. Values are +1 or -1.
// ---------------------------------------------------------------------------
const std::array<std::array<int8_t, FdnReverb::kLines>, FdnReverb::kLines>
FdnReverb::kHadamard = {{
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    { 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1},
    { 1, 1,-1,-1, 1, 1,-1,-1, 1, 1,-1,-1, 1, 1,-1,-1},
    { 1,-1,-1, 1, 1,-1,-1, 1, 1,-1,-1, 1, 1,-1,-1, 1},
    { 1, 1, 1, 1,-1,-1,-1,-1, 1, 1, 1, 1,-1,-1,-1,-1},
    { 1,-1, 1,-1,-1, 1,-1, 1, 1,-1, 1,-1,-1, 1,-1, 1},
    { 1, 1,-1,-1,-1,-1, 1, 1, 1, 1,-1,-1,-1,-1, 1, 1},
    { 1,-1,-1, 1,-1, 1, 1,-1, 1,-1,-1, 1,-1, 1, 1,-1},
    { 1, 1, 1, 1, 1, 1, 1, 1,-1,-1,-1,-1,-1,-1,-1,-1},
    { 1,-1, 1,-1, 1,-1, 1,-1,-1, 1,-1, 1,-1, 1,-1, 1},
    { 1, 1,-1,-1, 1, 1,-1,-1,-1,-1, 1, 1,-1,-1, 1, 1},
    { 1,-1,-1, 1, 1,-1,-1, 1,-1, 1, 1,-1,-1, 1, 1,-1},
    { 1, 1, 1, 1,-1,-1,-1,-1,-1,-1,-1,-1, 1, 1, 1, 1},
    { 1,-1, 1,-1,-1, 1,-1, 1,-1, 1,-1, 1, 1,-1, 1,-1},
    { 1, 1,-1,-1,-1,-1, 1, 1,-1,-1, 1, 1, 1, 1,-1,-1},
    { 1,-1,-1, 1,-1, 1, 1,-1,-1, 1, 1,-1, 1,-1,-1, 1},
}};

FdnReverb::FdnReverb() = default;

void FdnReverb::prepareToPlay(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;

    // Enable FTZ and DAZ to suppress denormals in this thread.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    const double ratio = sampleRate / 48000.0;
    for (int i = 0; i < kLines; ++i) {
        int len = static_cast<int>(kBaseDelaysSamples[i] * ratio);
        if (len < 2) len = 2;
        lines_[i].buf.assign(len, 0.f);
        lines_[i].writePos = 0;
        lines_[i].length   = len;
        lines_[i].lpState  = 0.f;
    }
    prepared_ = true;
}

void FdnReverb::process(float* inOut, int numSamples) noexcept {
    static constexpr float kNorm = 1.f / 4.f; // 1/sqrt(16)

    for (int n = 0; n < numSamples; ++n) {
        const float input = inOut[n];

        // Read delay-line outputs.
        float delayOut[kLines];
        for (int i = 0; i < kLines; ++i) {
            auto& dl = lines_[i];
            int readPos = dl.writePos - 1;
            if (readPos < 0) readPos += dl.length;
            delayOut[i] = dl.buf[readPos];
        }

        // Hadamard mix (normalized by 1/4).
        float mixed[kLines] = {};
        for (int i = 0; i < kLines; ++i) {
            float acc = 0.f;
            for (int j = 0; j < kLines; ++j) {
                acc += static_cast<float>(kHadamard[i][j]) * delayOut[j];
            }
            mixed[i] = acc * kNorm;
        }

        // Write into delay lines: feedback * mixed + input + DC offset injection.
        float outputSample = 0.f;
        for (int i = 0; i < kLines; ++i) {
            auto& dl = lines_[i];

            // 1-pole LP filter on the feedback signal.
            const float fb = mixed[i] * feedback_;
            dl.lpState = fb * (1.f - damping_) + dl.lpState * damping_;

            // DC offset: alternating sign per line.
            const float dcSign = (i & 1) ? -kDCOffset : kDCOffset;
            const float writeVal = dl.lpState + input + dcSign;

            dl.buf[dl.writePos] = writeVal;
            dl.writePos++;
            if (dl.writePos >= dl.length) dl.writePos = 0;

            outputSample += delayOut[i];
        }
        outputSample /= static_cast<float>(kLines);

        inOut[n] = wetMix_ * outputSample + (1.f - wetMix_) * input;
    }
}

} // namespace spe::reverb
