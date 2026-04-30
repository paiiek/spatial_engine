// core/src/audio_io/DanteBackend.cpp

#include "audio_io/DanteBackend.h"

namespace spe::audio_io {

// ---------------------------------------------------------------------------
// DantePortDiscovery — always-available stubs (no JUCE required)
// ---------------------------------------------------------------------------

std::vector<DantePortMap> DantePortDiscovery::discoverPorts() {
    // Real implementation would enumerate JACK ports filtered by "Dante"
    // prefix via jack_get_ports(). Without JACK available this returns an
    // empty vector, which tests treat as "hardware absent".
    return {};
}

bool DantePortDiscovery::validateChannelOrder(int /*channelCount*/) {
    // Real implementation sends an impulse on each channel and checks that
    // the round-trip capture lands on the same channel index.
    // Without hardware / JACK this always returns false.
    return false;
}

} // namespace spe::audio_io

#if defined(SPE_HAVE_JUCE)

#include "audio_io/DanteBackend.h"

#include "core/Constants.h"
#include "util/RtAssertNoAlloc.h"

#include <chrono>
#include <cstdio>

namespace spe::audio_io {

DanteBackend::DanteBackend(double sample_rate, int requested_block_size)
    : requested_block_size_(requested_block_size),
      requested_sample_rate_(sample_rate) {}

DanteBackend::~DanteBackend() { stop(); }

std::string DanteBackend::description() const {
    return std::string("DanteBackend(rate=") + std::to_string(static_cast<int>(effective_sample_rate_.load()))
        + ",ch_out=" + std::to_string(out_channels_.load())
        + ",ch_in="  + std::to_string(in_channels_.load())
        + ",block="  + std::to_string(effective_block_size_.load()) + ")";
}

BackendError DanteBackend::start(AudioCallback* callback) {
    if (running_.load()) return BackendError::AlreadyStarted;
    if (!callback) return BackendError::DeviceOpenFailed;
    if (requested_block_size_ > spe::MAX_BLOCK) return BackendError::BlockSizeExceedsMax;

    engine_callback_ = callback;

    // P1 spike: prefer JACK on Linux (matches ADR baseline). DanteBackend
    // selection table (Dante channel count, channel mapping) lands at P6.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.sampleRate          = requested_sample_rate_;
    setup.bufferSize          = requested_block_size_;
    setup.useDefaultInputChannels  = false;
    setup.useDefaultOutputChannels = true;

    auto err = device_manager_.initialise(/*numInputChannelsNeeded=*/0,
                                          /*numOutputChannelsNeeded=*/8,
                                          /*savedState=*/nullptr,
                                          /*selectDefaultDeviceOnFailure=*/true,
                                          /*preferredDefaultDeviceName=*/{},
                                          &setup);
    if (err.isNotEmpty()) {
        std::fprintf(stderr, "[DanteBackend] device init failed: %s\n", err.toRawUTF8());
        return BackendError::DeviceOpenFailed;
    }

    auto* device = device_manager_.getCurrentAudioDevice();
    if (!device) return BackendError::DeviceOpenFailed;
    if (device->getCurrentBufferSizeSamples() > spe::MAX_BLOCK) {
        device_manager_.closeAudioDevice();
        return BackendError::BlockSizeExceedsMax;
    }

    device_manager_.addAudioCallback(this);
    running_.store(true);
    return BackendError::Ok;
}

BackendError DanteBackend::stop() noexcept {
    if (!running_.load()) return BackendError::NotStarted;
    device_manager_.removeAudioCallback(this);
    device_manager_.closeAudioDevice();
    running_.store(false);
    if (engine_callback_) {
        engine_callback_->releaseResources();
        engine_callback_ = nullptr;
    }
    return BackendError::Ok;
}

void DanteBackend::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context) {
    SPE_RT_NO_ALLOC_SCOPE();

    // Zero out before engine writes (JUCE doesn't guarantee zeroed buffers).
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (outputChannelData[ch]) {
            for (int i = 0; i < numSamples; ++i) outputChannelData[ch][i] = 0.0f;
        }
    }

    if (numSamples > spe::MAX_BLOCK) {
        xruns_.record_overrun();
        return;
    }

    if (engine_callback_) {
        AudioBlock blk;
        blk.output_channels      = outputChannelData;
        blk.input_channels       = inputChannelData;
        blk.output_channel_count = numOutputChannels;
        blk.input_channel_count  = numInputChannels;
        blk.num_frames           = numSamples;
        blk.sample_rate          = effective_sample_rate_.load(std::memory_order_relaxed);
        // C3 instrumentation pin: T1 = sample-written-to-device timestamp.
        blk.hw_timestamp_ns      = context.hostTimeNs != nullptr
            ? static_cast<std::uint64_t>(*context.hostTimeNs)
            : 0;
        engine_callback_->audioBlock(blk);
    }
}

void DanteBackend::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    out_channels_.store(device->getActiveOutputChannels().countNumberOfSetBits());
    in_channels_.store(device->getActiveInputChannels().countNumberOfSetBits());
    effective_sample_rate_.store(device->getCurrentSampleRate());
    effective_block_size_.store(device->getCurrentBufferSizeSamples());
    if (engine_callback_) {
        engine_callback_->prepareToPlay(effective_sample_rate_.load(),
                                        effective_block_size_.load());
    }
}

void DanteBackend::audioDeviceStopped() {
    if (engine_callback_) engine_callback_->releaseResources();
}

void DanteBackend::audioDeviceError(const juce::String& errorMessage) {
    std::fprintf(stderr, "[DanteBackend] error: %s\n", errorMessage.toRawUTF8());
    xruns_.record_underrun();
}

std::unique_ptr<AudioBackend> make_dante_backend(double sample_rate,
                                                 int requested_block_size) {
    return std::make_unique<DanteBackend>(sample_rate, requested_block_size);
}

}  // namespace spe::audio_io

#endif  // SPE_HAVE_JUCE
