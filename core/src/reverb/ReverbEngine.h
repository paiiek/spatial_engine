// core/src/reverb/ReverbEngine.h
// IReverbEngine interface + factory.

#pragma once

#include <memory>
#include <string>

namespace spe::reverb {

enum class ReverbType {
    FDN,
    IRConvolution,
};

struct ReverbConfig {
    ReverbType type        = ReverbType::FDN;
    double     sampleRate  = 48000.0;
    int        blockSize   = 512;
    float      wetMix      = 1.f;
    float      damping     = 0.7f;
    float      feedback    = 0.98f;
};

class IReverbEngine {
public:
    virtual ~IReverbEngine() = default;
    virtual void prepareToPlay(double sampleRate, int blockSize) = 0;
    // In-place mono processing.
    virtual void process(float* inOut, int numSamples) noexcept = 0;
    virtual const char* name() const noexcept = 0;
};

// Creates an IReverbEngine instance based on config.type.
std::unique_ptr<IReverbEngine> createReverbEngine(const ReverbConfig& cfg);

} // namespace spe::reverb
