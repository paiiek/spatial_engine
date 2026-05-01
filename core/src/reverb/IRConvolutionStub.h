// core/src/reverb/IRConvolutionStub.h
// Stub IR convolution: validates IR metadata, returns dry signal from process().

#pragma once

#include <string>

namespace spe::reverb {

struct IRMetadata {
    int    sampleRate    = 48000;
    int    channelCount  = 1;
    int    irLengthFrames = 0;
};

// Error codes returned by validate().
enum class IRValidationError {
    Ok                  = 0,
    SampleRateMismatch  = 1,
    BadChannelCount     = 2,
    IRTooLong           = 3,
};

struct SofaMetadata {
    float       sample_rate_hz      = 48000.f;
    int         ir_length_samples   = 0;
    int         measurement_count   = 0;
    int         receiver_count      = 2;
    bool        loaded              = false;
    std::string path;
};

class IRConvolutionStub {
public:
    static constexpr int kMaxIRLengthFrames = 192000; // 4 s @ 48 kHz

    // Set engine sample rate before calling validate().
    void setEngineSampleRate(int sr) noexcept { engineSampleRate_ = sr; }

    // Returns Ok if metadata is acceptable, error code otherwise.
    // Stores error description in lastError().
    IRValidationError validate(const IRMetadata& meta) noexcept;

    // Stub process: copies src to dst unchanged (dry pass-through).
    // RT-safe: no alloc, no lock.
    void process(const float* src, float* dst, int numSamples) noexcept;

    // NO_JUCE stub: checks file existence and caches metadata path.
    // Returns true if file exists, false otherwise.
    bool loadFromSofa(const std::string& sofaPath);
    const SofaMetadata& sofaMetadata() const noexcept { return sofa_meta_; }

    const std::string& lastError() const noexcept { return lastError_; }

private:
    int          engineSampleRate_ = 48000;
    std::string  lastError_;
    SofaMetadata sofa_meta_;
};

} // namespace spe::reverb
