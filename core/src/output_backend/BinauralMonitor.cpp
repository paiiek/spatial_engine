// core/src/output_backend/BinauralMonitor.cpp
// No-op stub implementation for SPATIAL_ENGINE_NO_JUCE builds.
// See BinauralMonitor.h for the full interface contract and coordinate conventions.

#include "output_backend/BinauralMonitor.h"

#include <algorithm>
#include <cstring>

namespace spe::output {

BinauralMonitor::InitResult BinauralMonitor::initialize(const Config& /*cfg*/)
{
#if defined(SPATIAL_ENGINE_NO_JUCE) || !defined(SPE_HAVE_JUCE)
    // Stub path: accept any config without loading SOFA.
    initialized_ = true;
    return InitResult::Ok;
#else
    // TODO (JUCE path):
    //   1. Open cfg.sofaPath with a lightweight HDF5/SOFA reader or JUCE file.
    //   2. Validate Data.SamplingRate == cfg.sampleRate → SofaSampleRateMismatch.
    //   3. Validate Data.IR length in {256, 512, 1024} → SofaIRLengthUnsupported.
    //   4. Load left/right HRIRs for all measurement positions.
    //   5. Initialize juce::dsp::Convolution (zero-latency partitioned):
    //      head block = 64 samples (cfg.blockSize), tail = 128/256/512.
    //   6. Store IR table for direction-switched lookup in setDirection().
    initialized_ = true;
    return InitResult::Ok;
#endif
}

void BinauralMonitor::processBlock(const float* monoIn, int numSamples,
                                   float* leftOut, float* rightOut)
{
#if defined(SPATIAL_ENGINE_NO_JUCE) || !defined(SPE_HAVE_JUCE)
    // Stub path: mono pass-through to both channels.
    if (monoIn && leftOut)
        std::memcpy(leftOut,  monoIn, static_cast<std::size_t>(numSamples) * sizeof(float));
    if (monoIn && rightOut)
        std::memcpy(rightOut, monoIn, static_cast<std::size_t>(numSamples) * sizeof(float));
#else
    // TODO (JUCE path):
    //   Look up the nearest HRIR pair for (az_rad_, el_rad_) using
    //   SOFA az_sofa = az_engine_ (AmbiX convention, no sign flip for az).
    //   Run juce::dsp::Convolution::processSamples() for left and right channels.
    if (monoIn && leftOut)
        std::memcpy(leftOut,  monoIn, static_cast<std::size_t>(numSamples) * sizeof(float));
    if (monoIn && rightOut)
        std::memcpy(rightOut, monoIn, static_cast<std::size_t>(numSamples) * sizeof(float));
#endif
}

void BinauralMonitor::reset()
{
    // TODO (JUCE path): left_conv_.reset(); right_conv_.reset();
}

bool BinauralMonitor::isInitialized() const
{
    return initialized_;
}

void BinauralMonitor::setDirection(float az_rad, float el_rad)
{
    az_rad_ = az_rad;
    el_rad_ = el_rad;
    // TODO (JUCE path): trigger HRIR crossfade to new direction.
}

} // namespace spe::output
