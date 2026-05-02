// core/src/reverb/IRConvReverb.h
// OlaConvolver-based IR convolution reverb.  IReverbEngine implementation.
#pragma once

#include "reverb/ReverbEngine.h"
#include "hrtf/OlaConvolver.h"
#include <vector>

namespace spe::reverb {

class IRConvReverb final : public IReverbEngine {
public:
    explicit IRConvReverb(const ReverbConfig& cfg);

    void prepareToPlay(double sampleRate, int blockSize) override;

    // In-place mono processing.  Alloc-free after prepareToPlay().
    void process(float* inOut, int numSamples) noexcept override;

    // Load an external IR from the control thread (NOT RT-safe).
    // Must be called after prepareToPlay().
    void loadIR(const float* ir, int len);

    // Load IR from a WAV file (mono, 48 kHz, PCM-16 or IEEE-float-32).
    // Returns false if the file cannot be opened or the format is unsupported.
    bool loadIRFromWav(const std::string& path);

    const char* name() const noexcept override { return "IRConvReverb"; }

private:
    void buildDefaultIR();
    void applyIR(const float* ir, int len);

    spe::hrtf::OlaConvolver ola_;
    std::vector<float>      tmp_buf_;   // block-sized output scratch
    std::vector<float>      default_ir_;
    float                   wet_mix_     = 1.f;
    int                     block_size_  = 512;
    double                  sample_rate_ = 48000.0;
};

} // namespace spe::reverb
