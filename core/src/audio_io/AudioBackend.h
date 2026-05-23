// core/src/audio_io/AudioBackend.h
//
// Backend-side abstraction over an audio device. Two concrete impls land at
// P1: NullBackend (CI null-audio) and DanteBackend (juce::AudioIODevice via
// PipeWire-JACK → Digigram ALP-Dante PCIe).

#pragma once

#include "audio_io/AudioCallback.h"
#include "core/Constants.h"

#include <memory>
#include <string>

namespace spe::audio_io {

enum class BackendError {
    Ok = 0,
    DeviceOpenFailed,
    BlockSizeExceedsMax,    // device requested > MAX_BLOCK; refuse-to-start (P1 gate)
    SampleRateUnsupported,
    AlreadyStarted,
    NotStarted,
    BlockConfigMismatch,    // ADR 0019: header block_size does not divide engine block,
                            // or block_size > capacity_frames; distinct from BlockSizeExceedsMax.
    ConsumerAlreadyAttached, // ADR 0019 PR3: a second consumer tried to attach to a ring
                            // that already has a live consumer (SPSC single-consumer invariant).
};

const char* describe(BackendError e) noexcept;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual int          outputChannelCount() const noexcept = 0;
    virtual int          inputChannelCount()  const noexcept = 0;
    virtual double       sampleRate()         const noexcept = 0;
    virtual int          blockSize()          const noexcept = 0;
    virtual std::string  description()        const = 0;

    // Wires the engine callback. Allocations must happen here, never in
    // the audio block. May fail with BlockSizeExceedsMax / SampleRateUnsupported.
    virtual BackendError start(AudioCallback* callback) = 0;
    virtual BackendError stop() noexcept = 0;
    virtual bool         isRunning() const noexcept = 0;

    // Cumulative xrun count exposed to /sys/xruns at P4.
    virtual unsigned long long xrunCount() const noexcept = 0;
};

// Construct a NullBackend (alway available, no system deps).
// input_channels=0 (default) preserves output-only behavior used pre-C1.
std::unique_ptr<AudioBackend> make_null_backend(double sample_rate = 48000.0,
                                                int output_channels = 8,
                                                int block_size = 64,
                                                int input_channels = 0);

#if defined(SPE_HAVE_JUCE)
// Construct a DanteBackend (JACK device pick; pinned in configs/default.yaml).
std::unique_ptr<AudioBackend> make_dante_backend(double sample_rate = 48000.0,
                                                 int requested_block_size = 64);
#endif

}  // namespace spe::audio_io
