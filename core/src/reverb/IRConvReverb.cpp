// core/src/reverb/IRConvReverb.cpp

#include "reverb/IRConvReverb.h"
#include <cmath>
#include <cstring>

namespace spe::reverb {

static constexpr int kDefaultIRLen = 1024;

IRConvReverb::IRConvReverb(const ReverbConfig& cfg)
    : wet_mix_(cfg.wetMix)
    , block_size_(cfg.blockSize)
    , sample_rate_(cfg.sampleRate)
{}

void IRConvReverb::buildDefaultIR() {
    default_ir_.resize(kDefaultIRLen);
    default_ir_[0] = 1.0f;
    for (int i = 1; i < kDefaultIRLen; ++i) {
        default_ir_[i] = 0.3f * std::exp(-static_cast<float>(i) / 200.f);
    }
}

void IRConvReverb::applyIR(const float* ir, int len) {
    ola_.prepare(ir, len, block_size_);
    tmp_buf_.assign(static_cast<size_t>(block_size_), 0.f);
}

void IRConvReverb::prepareToPlay(double sampleRate, int blockSize) {
    sample_rate_ = sampleRate;
    block_size_  = blockSize;
    buildDefaultIR();
    applyIR(default_ir_.data(), static_cast<int>(default_ir_.size()));
}

void IRConvReverb::process(float* inOut, int numSamples) noexcept {
    if (!ola_.isReady()) return;
    // OlaConvolver requires exactly block_size_ samples per call.
    // In normal use numSamples == block_size_; guard against mismatch.
    if (numSamples > block_size_) numSamples = block_size_;
    ola_.process(inOut, numSamples, tmp_buf_.data());
    if (wet_mix_ >= 1.f) {
        std::memcpy(inOut, tmp_buf_.data(), static_cast<size_t>(numSamples) * sizeof(float));
    } else {
        const float dry = 1.f - wet_mix_;
        for (int i = 0; i < numSamples; ++i) {
            inOut[i] = inOut[i] * dry + tmp_buf_[static_cast<size_t>(i)] * wet_mix_;
        }
    }
}

void IRConvReverb::loadIR(const float* ir, int len) {
    if (!ir || len < 1) return;
    applyIR(ir, len);
}

} // namespace spe::reverb
