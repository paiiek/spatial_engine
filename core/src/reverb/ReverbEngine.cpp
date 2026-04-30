// core/src/reverb/ReverbEngine.cpp

#include "ReverbEngine.h"
#include "FdnReverb.h"
#include "IRConvolutionStub.h"
#include <cstring>

namespace spe::reverb {

// --- FDN wrapper -----------------------------------------------------------
class FdnReverbEngine final : public IReverbEngine {
public:
    explicit FdnReverbEngine(const ReverbConfig& cfg) {
        fdn_.setWetMix(cfg.wetMix);
        fdn_.setDamping(cfg.damping);
        fdn_.setFeedback(cfg.feedback);
    }
    void prepareToPlay(double sr, int bs) override { fdn_.prepareToPlay(sr, bs); }
    void process(float* inOut, int n) noexcept override { fdn_.process(inOut, n); }
    const char* name() const noexcept override { return "FdnReverb"; }
private:
    FdnReverb fdn_;
};

// --- IR stub wrapper -------------------------------------------------------
class IRConvolutionEngine final : public IReverbEngine {
public:
    IRConvolutionEngine() = default;
    void prepareToPlay(double sr, int /*bs*/) override {
        stub_.setEngineSampleRate(static_cast<int>(sr));
    }
    void process(float* inOut, int n) noexcept override {
        // Stub: dry pass-through (in-place: src == dst is fine with memcpy).
        stub_.process(inOut, inOut, n);
    }
    const char* name() const noexcept override { return "IRConvolutionStub"; }
private:
    IRConvolutionStub stub_;
};

// --- Factory ---------------------------------------------------------------
std::unique_ptr<IReverbEngine> createReverbEngine(const ReverbConfig& cfg) {
    switch (cfg.type) {
        case ReverbType::FDN: {
            auto eng = std::make_unique<FdnReverbEngine>(cfg);
            eng->prepareToPlay(cfg.sampleRate, cfg.blockSize);
            return eng;
        }
        case ReverbType::IRConvolution: {
            auto eng = std::make_unique<IRConvolutionEngine>();
            eng->prepareToPlay(cfg.sampleRate, cfg.blockSize);
            return eng;
        }
    }
    // Fallback (unreachable in well-formed builds).
    auto eng = std::make_unique<FdnReverbEngine>(cfg);
    eng->prepareToPlay(cfg.sampleRate, cfg.blockSize);
    return eng;
}

} // namespace spe::reverb
