// core/src/reverb/ReverbEngine.cpp

#include "ReverbEngine.h"
#include "FdnReverb.h"
#include "IRConvReverb.h"
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

// --- Factory ---------------------------------------------------------------
std::unique_ptr<IReverbEngine> createReverbEngine(const ReverbConfig& cfg) {
    switch (cfg.type) {
        case ReverbType::FDN: {
            auto eng = std::make_unique<FdnReverbEngine>(cfg);
            eng->prepareToPlay(cfg.sampleRate, cfg.blockSize);
            return eng;
        }
        case ReverbType::IRConvolution: {
            auto eng = std::make_unique<IRConvReverb>(cfg);
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
