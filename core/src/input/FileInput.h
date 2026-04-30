// core/src/input/FileInput.h
// File-backed AudioInput. v0 JUCE-free path: caller supplies in-memory mono PCM
// samples; a decoder thread copies fixed-size chunks into a lock-free FIFO.
// The audio thread reads non-blocking via pull().
//
// JUCE path (SPE_HAVE_JUCE=1, deferred to v1+) will replace the in-memory source
// with juce::AudioFormatReader so real WAV/FLAC files can be streamed.
//
// On Linux the decoded buffer is mlock()'d to keep it resident; failure is
// non-fatal (warnings only via diagnostics path).

#pragma once

#include "AudioInput.h"
#include "util/LockFreeFloatFifo.h"
#include <atomic>
#include <thread>
#include <vector>

namespace spe::input {

class FileInput final : public AudioInput {
public:
    // chunk_frames: how many samples decoderLoop pushes per iteration.
    // fifo_capacity_pow2: ring-buffer size (must be power of two).
    FileInput(std::vector<float> mono_samples,
              int sample_rate,
              int chunk_frames        = 512,
              int fifo_capacity_pow2  = 16384);
    ~FileInput() override;

    int      pull(float* dst, int n_frames) noexcept override;
    bool     decodeMore() override;
    int      sampleRate()     const noexcept override { return sample_rate_; }
    uint64_t underrunCount()  const noexcept override { return underrun_count_.load(); }
    uint64_t framesProduced() const noexcept override { return frames_produced_.load(); }
    bool     atEnd()          const noexcept override { return at_end_.load(); }

    void start() override;
    void stop()  override;

    // Test hooks: pause/resume the decoder loop without stopping the thread.
    void setPaused(bool p) noexcept { paused_.store(p); }
    bool isRunning() const noexcept { return running_.load(); }
    int  fifoAvailable() const noexcept { return fifo_.available(); }

private:
    std::vector<float>       samples_;
    int                      sample_rate_;
    int                      chunk_frames_;
    util::LockFreeFloatFifo  fifo_;
    std::atomic<size_t>      read_pos_{0};
    std::atomic<bool>        at_end_{false};
    std::atomic<bool>        paused_{false};
    std::atomic<bool>        running_{false};
    std::atomic<uint64_t>    underrun_count_{0};
    std::atomic<uint64_t>    frames_produced_{0};
    std::thread              decoder_thread_;

    void decoderLoop();
    void tryMlockBuffer();
};

} // namespace spe::input
