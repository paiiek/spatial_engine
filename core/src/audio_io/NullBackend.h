// core/src/audio_io/NullBackend.h
//
// CI-runnable audio backend that drives a callback at sample_rate / block_size
// from a real-time-priority std::thread, without any system audio device.
// Used by NullBackend integration tests and tests/e2e (no JACK / no Dante).
//
// This impl deliberately has no JUCE dependency so the CI Linux runner can
// build it without ALSA/JACK/X11 system libraries.

#pragma once

#include "audio_io/AudioBackend.h"
#include "util/XrunCounter.h"

#include <atomic>
#include <thread>
#include <vector>

namespace spe::audio_io {

class NullBackend final : public AudioBackend {
public:
    NullBackend(double sample_rate, int output_channels, int block_size);
    ~NullBackend() override;

    int          outputChannelCount() const noexcept override { return out_channels_; }
    int          inputChannelCount()  const noexcept override { return 0; }
    double       sampleRate()         const noexcept override { return sample_rate_; }
    int          blockSize()          const noexcept override { return block_size_; }
    std::string  description()        const override;

    BackendError start(AudioCallback* callback) override;
    BackendError stop() noexcept override;
    bool         isRunning() const noexcept override { return running_.load(); }
    unsigned long long xrunCount() const noexcept override { return xruns_.total(); }

    // Test hooks: drain N blocks synchronously instead of waiting for the
    // worker thread. Keeps unit tests deterministic.
    BackendError pump_synchronous(AudioCallback* callback, int blocks);

private:
    void thread_loop();

    double sample_rate_;
    int    out_channels_;
    int    block_size_;

    std::vector<float>              flat_buffer_;        // out_channels_ * block_size_
    std::vector<float*>             channel_pointers_;   // size == out_channels_
    AudioCallback*                  callback_{nullptr};
    std::atomic<bool>               running_{false};
    std::thread                     worker_;
    util::XrunCounter               xruns_;
};

}  // namespace spe::audio_io
