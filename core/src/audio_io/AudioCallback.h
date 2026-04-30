// core/src/audio_io/AudioCallback.h
//
// Engine-internal callback interface that audio_io backends drive. This is
// deliberately framework-neutral: NullBackend (pure std::thread) and
// DanteBackend (juce::AudioIODevice) both translate their respective
// hardware-side callbacks into a single audioBlock() shape so the engine
// only has to implement one path.
//
// Principle 1: implementations of audioBlock() must be allocation-free,
// lock-free, syscall-free, log-free. RT_ASSERT_NO_ALLOC scoped guard is
// applied at the backend's outer entry point.

#pragma once

#include <cstdint>

namespace spe::audio_io {

struct AudioBlock {
    // Interleaved is forbidden; planar f32 only on the audio thread.
    float* const* output_channels = nullptr;   // size == output_channel_count
    const float* const* input_channels = nullptr;  // size == input_channel_count, may be null
    int output_channel_count = 0;
    int input_channel_count = 0;
    int num_frames = 0;                        // <= MAX_BLOCK
    double sample_rate = 0.0;

    // Filled by backend just before the engine callback runs. Used by P10
    // latency harness as T1 instrumentation point (C3 pin).
    std::uint64_t hw_timestamp_ns = 0;
};

class AudioCallback {
public:
    virtual ~AudioCallback() = default;
    // Called once per backend block. Implementations zero-fill on the first
    // call (no implicit silence guarantee). The backend zeroes the output
    // before calling if it has not been touched at the device layer.
    virtual void audioBlock(const AudioBlock& block) = 0;

    // Backend lifecycle hooks; called from the control thread before/after
    // start()/stop(). prepareToPlay must complete all allocations.
    virtual void prepareToPlay(double sample_rate, int max_block_size) = 0;
    virtual void releaseResources() = 0;
};

}  // namespace spe::audio_io
