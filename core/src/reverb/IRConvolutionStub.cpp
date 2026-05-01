// core/src/reverb/IRConvolutionStub.cpp

#include "IRConvolutionStub.h"
#include <cstring>
#include <cstdio>
#include <filesystem>

namespace spe::reverb {

namespace {
    // Formats a short error message into IRConvolutionStub::lastError_.
    template <typename... Args>
    void setFormattedError(std::string& dst, const char* fmt, Args... args) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        dst = buf;
    }
}

IRValidationError IRConvolutionStub::validate(const IRMetadata& meta) noexcept {
    if (meta.sampleRate != engineSampleRate_) {
        setFormattedError(lastError_,
            "IR sample rate %d != engine sample rate %d",
            meta.sampleRate, engineSampleRate_);
        return IRValidationError::SampleRateMismatch;
    }
    if (meta.channelCount < 1) {
        lastError_ = "IR channel count < 1";
        return IRValidationError::BadChannelCount;
    }
    if (meta.irLengthFrames > kMaxIRLengthFrames) {
        setFormattedError(lastError_,
            "IR length %d frames exceeds max %d",
            meta.irLengthFrames, kMaxIRLengthFrames);
        return IRValidationError::IRTooLong;
    }
    lastError_.clear();
    return IRValidationError::Ok;
}

void IRConvolutionStub::process(const float* src, float* dst, int numSamples) noexcept {
    std::memcpy(dst, src, static_cast<size_t>(numSamples) * sizeof(float));
}

bool IRConvolutionStub::loadFromSofa(const std::string& sofaPath) {
    // NO_JUCE stub: only check file existence and cache the path.
    // Actual convolution is handled via the JUCE path.
    if (!std::filesystem::exists(sofaPath)) {
        lastError_ = "SOFA file not found: " + sofaPath;
        sofa_meta_.loaded = false;
        return false;
    }
    sofa_meta_.path   = sofaPath;
    sofa_meta_.loaded = true;
    lastError_.clear();
    return true;
}

} // namespace spe::reverb
