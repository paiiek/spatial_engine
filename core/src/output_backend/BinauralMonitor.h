// core/src/output_backend/BinauralMonitor.h
// BinauralMonitor — binaural rendering via HRTF convolution.
//
// NO_JUCE / pure-C++ path (CI and headless builds):
//   - initialize(cfg) with empty sofaPath → Ok, pass-through mode.
//   - initialize(cfg) with a .speh path   → loads HRTF table, real OLA convolution.
//   - initialize(cfg) with missing path   → SofaNotFound.
//
// JUCE path (SPE_HAVE_JUCE):
//   - Same .speh loading and nearest-neighbor lookup.
//   - Uses juce::dsp::Convolution (zero-latency partitioned) instead of OlaConvolver.
//   - setDirection(): loadImpulseResponse() is wait-free against JUCE's internal swap,
//     but lookupHrtf() runs O(n_positions) trig — call from control/UI thread only.
//
// Thread-safety:
//   setDirection() and processBlock() must NOT be called concurrently.
//   The NO_JUCE OlaConvolver path is NOT wait-free — prepare() allocates.
//   Standard pattern: call setDirection() from OSC/control thread with an
//   external mutex or double-buffer swap before audio thread reads.
//
// Coordinate convention:
//   Engine frame: az=+90 deg = LEFT  (AmbiX/B-format)
//   SOFA frame:   az=+90 deg = LEFT  (AES69) — no sign flip for az lookup.
//   ITD check: source at az_engine=+30 deg (left) → left ear first → ITD > 0.

#pragma once

#include "hrtf/SofaBinReader.h"
#include "hrtf/OlaConvolver.h"

#if defined(SPE_HAVE_JUCE)
#include <juce_dsp/juce_dsp.h>
#endif

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
    int   block_size_   = 64;
    float sample_rate_  = 48000.f;

    // HRTF table (shared by both paths for direction lookup)
    hrtf::HrtfTable table_;

#if defined(SPE_HAVE_JUCE)
    // JUCE path: zero-latency partitioned convolution
    juce::dsp::Convolution left_juce_conv_;
    juce::dsp::Convolution right_juce_conv_;
#else
    // Pure-C++ path: direct overlap-add convolution
    hrtf::OlaConvolver left_conv_;
    hrtf::OlaConvolver right_conv_;
#endif

    // Load the HRTF IR for the current direction into the convolution engine.
    void loadCurrentDirection();
};

} // namespace spe::output
