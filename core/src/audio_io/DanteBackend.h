// core/src/audio_io/DanteBackend.h
//
// JUCE-backed audio device wrapper that routes through PipeWire-JACK (default)
// or ALSA (fallback) to the Digigram ALP-Dante PCIe interface. Final wiring
// against vendor-certified driver versions is pinned in docs/lab_setup.md (A3).
//
// This header is only compiled when SPE_HAVE_JUCE is defined; the engine
// links it on lab Linux but skips it in CI null-audio mode.

#pragma once

#include <vector>

namespace spe::audio_io {

/// JACK port → Dante channel mapping entry.
struct DantePortMap {
    int jackPortIndex;  ///< 0-based JACK port index
    int danteChannel;   ///< 0-based Dante channel
};

/// Static utility: port discovery and channel-order validation.
/// Always available (no JUCE required).
struct DantePortDiscovery {
    /// Returns discovered Dante output port mappings.
    /// Returns empty vector when JACK is unavailable (CI / NO_JUCE builds).
    static std::vector<DantePortMap> discoverPorts();

    /// Validates channel order by checking that identity mapping holds for
    /// channelCount channels. Returns false when hardware is unavailable.
    static bool validateChannelOrder(int channelCount);
};

} // namespace spe::audio_io

#if defined(SPE_HAVE_JUCE)

#include "audio_io/AudioBackend.h"
#include "util/XrunCounter.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <string>

namespace spe::audio_io {

class DanteBackend final : public AudioBackend,
                           public juce::AudioIODeviceCallback {
public:
    DanteBackend(double sample_rate, int requested_block_size);
    ~DanteBackend() override;

    int    outputChannelCount() const noexcept override { return out_channels_.load(); }
    int    inputChannelCount()  const noexcept override { return in_channels_.load(); }
    double sampleRate()         const noexcept override { return effective_sample_rate_.load(); }
    int    blockSize()          const noexcept override { return effective_block_size_.load(); }
    std::string description()   const override;

    BackendError start(AudioCallback* callback) override;
    BackendError stop() noexcept override;
    bool         isRunning() const noexcept override { return running_.load(); }
    unsigned long long xrunCount() const noexcept override { return xruns_.total(); }

    // juce::AudioIODeviceCallback overrides
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

private:
    juce::AudioDeviceManager device_manager_;

    std::atomic<int>    out_channels_{0};
    std::atomic<int>    in_channels_{0};
    std::atomic<double> effective_sample_rate_{0.0};
    std::atomic<int>    effective_block_size_{0};
    std::atomic<bool>   running_{false};

    AudioCallback*      engine_callback_{nullptr};
    int                 requested_block_size_{64};
    double              requested_sample_rate_{48000.0};

    util::XrunCounter   xruns_;
};

}  // namespace spe::audio_io

#endif  // SPE_HAVE_JUCE
