// core/src/input/LiveMicInput.h
// Live microphone capture AudioInput implementation.
// JUCE path (SPE_HAVE_JUCE=1): opens the default JACK/ALSA input device and
// streams captured samples into a lock-free FIFO for the audio thread.
// NO_JUCE path: silence fallback — pull() always returns n_frames of zeros.
// This lets CI unit tests exercise the interface without hardware.

#pragma once

#include "AudioInput.h"
#include "util/LockFreeFloatFifo.h"
#include <atomic>

namespace spe::input {

class LiveMicInput final : public AudioInput {
public:
    // sample_rate: nominal rate (informational in NO_JUCE stub).
    // fifo_capacity_pow2: ring-buffer size, must be power of two.
    explicit LiveMicInput(int sample_rate, int fifo_capacity_pow2 = 16384);
    ~LiveMicInput() override;

    // AudioInput interface —— RT-safe pull (audio thread).
    // NO_JUCE: writes silence and returns n_frames (never underflows).
    int      pull(float* dst, int n_frames) noexcept override;

    // Decoder-thread side: pushes captured samples into FIFO.
    // NO_JUCE: no-op, always returns true (silence source is infinite).
    bool     decodeMore() override;

    int      sampleRate()     const noexcept override { return sample_rate_; }
    uint64_t underrunCount()  const noexcept override { return underrun_count_.load(); }
    uint64_t framesProduced() const noexcept override { return frames_produced_.load(); }
    bool     atEnd()          const noexcept override { return false; } // mic never ends

    void start() override;
    void stop()  override;

    bool isRunning() const noexcept { return running_.load(); }

private:
    int                      sample_rate_;
    util::LockFreeFloatFifo  fifo_;
    std::atomic<bool>        running_{false};
    std::atomic<uint64_t>    underrun_count_{0};
    std::atomic<uint64_t>    frames_produced_{0};
};

} // namespace spe::input
