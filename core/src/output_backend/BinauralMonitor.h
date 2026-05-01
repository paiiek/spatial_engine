// core/src/output_backend/BinauralMonitor.h
// BinauralMonitor — binaural rendering via HRTF convolution.
//
// Under SPATIAL_ENGINE_NO_JUCE (CI / headless builds):
//   Compiles as a no-op stub: initialize() returns Ok without loading SOFA;
//   processBlock() copies monoIn to both channels (pass-through).
//
// Under a full JUCE build (SPE_HAVE_JUCE):
//   JUCE path uses juce::dsp::Convolution zero-latency partitioned
//   (head 64 frames + tail 128/256/512). Not yet implemented — see TODO below.
//
// Coordinate convention (IMPORTANT for ITD sign):
//   Engine frame: az=+90 deg = LEFT  (same as AmbiX/B-format convention)
//   SOFA frame:   az=+90 deg = LEFT  (AES69 positive-az = left)
//   Mapping:      az_sofa = az_engine  (no sign flip needed for az lookup)
//   ITD check: source at az_engine=+30 deg (left of centre) → left ear first → ITD > 0

#pragma once

#include <string>

namespace spe::output {

class BinauralMonitor {
public:
    struct Config {
        std::string sofaPath;
        float       sampleRate = 48000.f;
        int         blockSize  = 64;
    };

    enum class InitResult {
        Ok,
        SofaNotFound,
        SofaSampleRateMismatch,
        SofaIRLengthUnsupported,
    };

    // Initialize with SOFA HRTF file.
    // Stub (NO_JUCE): always returns Ok; sofaPath is ignored.
    InitResult initialize(const Config& cfg);

    // Process one block of mono input into stereo output.
    // Stub (NO_JUCE): copies monoIn to both leftOut and rightOut (pass-through).
    // JUCE path: applies HRTF convolution for the current az/el direction.
    void processBlock(const float* monoIn, int numSamples,
                      float* leftOut, float* rightOut);

    // Reset internal convolution state.
    void reset();

    // Returns true after a successful initialize() call.
    bool isInitialized() const;

    // Set source direction for HRTF lookup (radians, engine frame).
    // Call before processBlock when direction changes.
    void setDirection(float az_rad, float el_rad = 0.f);

private:
    bool  initialized_ = false;
    float az_rad_      = 0.f;
    float el_rad_      = 0.f;

    // TODO (JUCE path): juce::dsp::Convolution left_conv_, right_conv_;
    // TODO (JUCE path): load SOFA HRIRs, partition into uniform-partitioned
    //   blocks: head=64 samples, tail partitions 128/256/512.
};

} // namespace spe::output
