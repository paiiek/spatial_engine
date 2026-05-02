// core/src/output_backend/BinauralMonitor.h
// BinauralMonitor — binaural rendering via HRTF convolution.
//
// NO_JUCE / pure-C++ path (CI and headless builds):
//   - initialize(cfg) with empty sofaPath → Ok, pass-through mode.
//   - initialize(cfg) with a .speh path   → loads HRTF table, real convolution.
//   - initialize(cfg) with a path that does not exist → SofaNotFound.
//
// JUCE path (SPE_HAVE_JUCE): TODO — juce::dsp::Convolution zero-latency partitioned.
//
// Coordinate convention:
//   Engine frame: az=+90 deg = LEFT  (AmbiX/B-format)
//   SOFA frame:   az=+90 deg = LEFT  (AES69) — no sign flip for az lookup.
//   ITD check: source at az_engine=+30 deg (left) → left ear first → ITD > 0.

#pragma once

#include "hrtf/SofaBinReader.h"
#include "hrtf/OlaConvolver.h"

#include <string>

namespace spe::output {

class BinauralMonitor {
public:
    struct Config {
        std::string sofaPath;           // .speh binary path, or "" for pass-through
        float       sampleRate = 48000.f;
        int         blockSize  = 64;
    };

    enum class InitResult {
        Ok,
        SofaNotFound,
        SofaSampleRateMismatch,
        SofaIRLengthUnsupported,
        SofaInvalidFormat,
    };

    InitResult initialize(const Config& cfg);

    void processBlock(const float* monoIn, int numSamples,
                      float* leftOut, float* rightOut);

    void reset();

    bool isInitialized() const;

    // Set source direction (radians, engine frame). Call before processBlock.
    void setDirection(float az_rad, float el_rad = 0.f);

    // True when a real HRTF table is loaded (false in pass-through mode).
    bool hasHrtf() const { return hrtf_loaded_; }

private:
    bool  initialized_  = false;
    bool  hrtf_loaded_  = false;
    float az_rad_       = 0.f;
    float el_rad_       = 0.f;

    hrtf::HrtfTable   table_;
    hrtf::OlaConvolver left_conv_;
    hrtf::OlaConvolver right_conv_;
};

} // namespace spe::output
