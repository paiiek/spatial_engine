// core/src/output_backend/BinauralMonitor.cpp

#include "output_backend/BinauralMonitor.h"
#include "hrtf/HrtfLookup.h"

#include <algorithm>
#include <cstring>

namespace spe::output {

// ---------------------------------------------------------------------------
// Shared: HRTF loading and direction lookup
// ---------------------------------------------------------------------------

BinauralMonitor::InitResult BinauralMonitor::initialize(const Config& cfg)
{
    block_size_  = cfg.blockSize;
    sample_rate_ = cfg.sampleRate;

    if (cfg.sofaPath.empty()) {
        initialized_ = true;
        hrtf_loaded_ = false;
        return InitResult::Ok;
    }

    hrtf::SpehResult res = hrtf::loadSpeh(cfg.sofaPath, cfg.sampleRate, table_);
    switch (res) {
    case hrtf::SpehResult::Ok:               break;
    case hrtf::SpehResult::FileNotFound:     return InitResult::SofaNotFound;
    case hrtf::SpehResult::SampleRateMismatch: return InitResult::SofaSampleRateMismatch;
    case hrtf::SpehResult::IRLengthUnsupported: return InitResult::SofaIRLengthUnsupported;
    default:                                 return InitResult::SofaInvalidFormat;
    }

    hrtf_loaded_ = true;

    // Load IR before prepare() so the first process() block uses the correct HRTF.
    loadCurrentDirection();

#if defined(SPE_HAVE_JUCE)
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = static_cast<double>(cfg.sampleRate);
    spec.maximumBlockSize = static_cast<juce::uint32>(cfg.blockSize);
    spec.numChannels      = 1;
    left_juce_conv_.prepare(spec);
    right_juce_conv_.prepare(spec);
#endif

    initialized_ = true;
    return InitResult::Ok;
}

void BinauralMonitor::loadCurrentDirection()
{
    if (!hrtf_loaded_) return;

    hrtf::HrtfPair p = hrtf::lookupHrtf(table_, az_rad_, el_rad_);

#if defined(SPE_HAVE_JUCE)
    {
        juce::AudioBuffer<float> left_buf(1, p.ir_length);
        left_buf.copyFrom(0, 0, p.left, p.ir_length);
        left_juce_conv_.loadImpulseResponse(
            std::move(left_buf),
            static_cast<double>(sample_rate_),
            juce::dsp::Convolution::Stereo::no,
            juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
    }
    {
        juce::AudioBuffer<float> right_buf(1, p.ir_length);
        right_buf.copyFrom(0, 0, p.right, p.ir_length);
        right_juce_conv_.loadImpulseResponse(
            std::move(right_buf),
            static_cast<double>(sample_rate_),
            juce::dsp::Convolution::Stereo::no,
            juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
    }
#else
    left_conv_.prepare(p.left,  p.ir_length, block_size_);
    right_conv_.prepare(p.right, p.ir_length, block_size_);
#endif
}

// ---------------------------------------------------------------------------
// processBlock
// ---------------------------------------------------------------------------

void BinauralMonitor::processBlock(const float* monoIn, int numSamples,
                                   float* leftOut, float* rightOut)
{
    if (!initialized_ || !hrtf_loaded_) {
        if (monoIn && leftOut)
            std::memcpy(leftOut,  monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        if (monoIn && rightOut)
            std::memcpy(rightOut, monoIn,
                        static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

#if defined(SPE_HAVE_JUCE)
    // Left channel
    if (leftOut) {
        juce::AudioBuffer<float> buf(1, numSamples);
        buf.copyFrom(0, 0, monoIn, numSamples);
        juce::dsp::AudioBlock<float> block(buf);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        left_juce_conv_.process(ctx);
        std::memcpy(leftOut, buf.getReadPointer(0),
                    static_cast<std::size_t>(numSamples) * sizeof(float));
    }
    // Right channel
    if (rightOut) {
        juce::AudioBuffer<float> buf(1, numSamples);
        buf.copyFrom(0, 0, monoIn, numSamples);
        juce::dsp::AudioBlock<float> block(buf);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        right_juce_conv_.process(ctx);
        std::memcpy(rightOut, buf.getReadPointer(0),
                    static_cast<std::size_t>(numSamples) * sizeof(float));
    }
#else
    if (leftOut)
        left_conv_.process(monoIn, numSamples, leftOut);
    if (rightOut)
        right_conv_.process(monoIn, numSamples, rightOut);
#endif
}

// ---------------------------------------------------------------------------
// reset / isInitialized / setDirection
// ---------------------------------------------------------------------------

void BinauralMonitor::reset()
{
#if defined(SPE_HAVE_JUCE)
    left_juce_conv_.reset();
    right_juce_conv_.reset();
#else
    left_conv_.reset();
    right_conv_.reset();
#endif
}

bool BinauralMonitor::isInitialized() const
{
    return initialized_;
}

void BinauralMonitor::setDirection(float az_rad, float el_rad)
{
    az_rad_ = az_rad;
    el_rad_ = el_rad;
    loadCurrentDirection();
}

} // namespace spe::output
