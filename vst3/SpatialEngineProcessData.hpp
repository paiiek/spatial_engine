// vst3/SpatialEngineProcessData.hpp
// Lock-free adapter: VST3 ProcessData -> spe::audio_io::AudioBlock
// Pointer copy only — zero memory allocation, zero memory copy.
// Phase C C2 Option-B.
#pragma once

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "audio_io/AudioCallback.h"

namespace spe::vst3 {

// Wraps a VST3 ProcessData reference for the duration of one process() call.
// The adapter is stack-allocated inside process(); must not outlive the call.
struct ProcessDataAdapter {
    // Build an AudioBlock from VST3 ProcessData (planar float32 only).
    // VST3 input bus 0 channel buffers -> AudioBlock::input_channels
    // VST3 output bus 0 channel buffers -> AudioBlock::output_channels
    static spe::audio_io::AudioBlock adapt(Steinberg::Vst::ProcessData& data,
                                            double sample_rate) noexcept
    {
        spe::audio_io::AudioBlock block;
        block.num_frames  = data.numSamples;
        block.sample_rate = sample_rate;

        // Output bus 0
        if (data.outputs && data.numOutputs > 0) {
            block.output_channels      = data.outputs[0].channelBuffers32;
            block.output_channel_count = data.outputs[0].numChannels;
        }

        // Input bus 0 (may be absent — source-only plugins)
        if (data.inputs && data.numInputs > 0) {
            block.input_channels      = const_cast<const float* const*>(
                                            data.inputs[0].channelBuffers32);
            block.input_channel_count = data.inputs[0].numChannels;
        }

        return block;
    }
};

} // namespace spe::vst3
