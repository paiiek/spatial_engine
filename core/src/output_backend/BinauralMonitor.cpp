// core/src/output_backend/BinauralMonitor.cpp

#include "output_backend/BinauralMonitor.h"
#include "hrtf/HrtfLookup.h"

#include <algorithm>
#include <cstring>

namespace spe::output {

BinauralMonitor::InitResult BinauralMonitor::initialize(const Config& cfg)
{
#if defined(SPE_HAVE_JUCE)
    // TODO (JUCE path): use juce::dsp::Convolution zero-latency partitioned.
    // For now fall through to pure-C++ path even in JUCE builds.
#endif

    if (cfg.sofaPath.empty()) {
        // Explicit pass-through mode: no HRTF file requested.
        initialized_ = true;
        hrtf_loaded_ = false;
        return InitResult::Ok;
    }

    hrtf::SpehResult res = hrtf::loadSpeh(cfg.sofaPath, cfg.sampleRate, table_);

    switch (res) {
    case hrtf::SpehResult::Ok:
        break;
    case hrtf::SpehResult::FileNotFound:
        return InitResult::SofaNotFound;
    case hrtf::SpehResult::SampleRateMismatch:
        return InitResult::SofaSampleRateMismatch;
    case hrtf::SpehResult::IRLengthUnsupported:
        return InitResult::SofaIRLengthUnsupported;
    default:
        return InitResult::SofaInvalidFormat;
    }

    // Load the HRTF for the current direction into both convolvers.
    hrtf::HrtfPair p = hrtf::lookupHrtf(table_, az_rad_, el_rad_);
    left_conv_.prepare(p.left,  p.ir_length, cfg.blockSize);
    right_conv_.prepare(p.right, p.ir_length, cfg.blockSize);

    initialized_ = true;
    hrtf_loaded_ = true;
    return InitResult::Ok;
}

void BinauralMonitor::processBlock(const float* monoIn, int numSamples,
                                   float* leftOut, float* rightOut)
{
    if (!initialized_ || !hrtf_loaded_) {
        // Pass-through: copy mono to both channels.
        if (monoIn && leftOut)
            std::memcpy(leftOut,  monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        if (monoIn && rightOut)
            std::memcpy(rightOut, monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

    // Real HRTF convolution.
    if (leftOut)
        left_conv_.process(monoIn, numSamples, leftOut);
    if (rightOut)
        right_conv_.process(monoIn, numSamples, rightOut);
}

void BinauralMonitor::reset()
{
    left_conv_.reset();
    right_conv_.reset();
}

bool BinauralMonitor::isInitialized() const
{
    return initialized_;
}

void BinauralMonitor::setDirection(float az_rad, float el_rad)
{
    az_rad_ = az_rad;
    el_rad_ = el_rad;

    if (hrtf_loaded_) {
        hrtf::HrtfPair p = hrtf::lookupHrtf(table_, az_rad_, el_rad_);
        left_conv_.prepare(p.left,  p.ir_length, 64);
        right_conv_.prepare(p.right, p.ir_length, 64);
    }
}

} // namespace spe::output
