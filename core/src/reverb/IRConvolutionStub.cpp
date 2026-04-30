// core/src/reverb/IRConvolutionStub.cpp

#include "IRConvolutionStub.h"
#include <cstring>
#include <cstdio>

namespace spe::reverb {

IRValidationError IRConvolutionStub::validate(const IRMetadata& meta) noexcept {
    if (meta.sampleRate != engineSampleRate_) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "IR sample rate %d != engine sample rate %d",
            meta.sampleRate, engineSampleRate_);
        lastError_ = buf;
        return IRValidationError::SampleRateMismatch;
    }
    if (meta.channelCount < 1) {
        lastError_ = "IR channel count < 1";
        return IRValidationError::BadChannelCount;
    }
    if (meta.irLengthFrames > kMaxIRLengthFrames) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "IR length %d frames exceeds max %d",
            meta.irLengthFrames, kMaxIRLengthFrames);
        lastError_ = buf;
        return IRValidationError::IRTooLong;
    }
    lastError_.clear();
    return IRValidationError::Ok;
}

void IRConvolutionStub::process(const float* src, float* dst, int numSamples) noexcept {
    std::memcpy(dst, src, static_cast<size_t>(numSamples) * sizeof(float));
}

} // namespace spe::reverb
