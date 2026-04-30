// core/src/input/SynthInput.h
// Offline-rendered test-tone source. Decoder thread synthesizes mono samples
// (sine, white noise) into a lock-free FIFO. Audio thread pulls non-blocking.
//
// Used for deterministic latency / spatial-perception tests when no file is
// loaded. Frequency / kind are runtime-configurable; output is uninterrupted
// (loops forever — never reaches at_end()).

#pragma once

#include "AudioInput.h"
#include "util/LockFreeFloatFifo.h"
#include <atomic>
#include <thread>

namespace spe::input {

enum class SynthKind : uint8_t {
    Sine  = 0,
    White = 1,
};

class SynthInput final : public AudioInput {
public:
    SynthInput(SynthKind kind,
               float     frequency_hz,
               float     amplitude,
               int       sample_rate,
               int       chunk_frames        = 512,
               int       fifo_capacity_pow2  = 16384);
    ~SynthInput() override;

    int      pull(float* dst, int n_frames) noexcept override;
    bool     decodeMore() override;
    int      sampleRate()     const noexcept override { return sample_rate_; }
    uint64_t underrunCount()  const noexcept override { return underrun_count_.load(); }
    uint64_t framesProduced() const noexcept override { return frames_produced_.load(); }
    bool     atEnd()          const noexcept override { return false; } // synth is endless

    void start() override;
    void stop()  override;

    void setFrequencyHz(float hz)  noexcept { frequency_hz_.store(hz); }
    void setAmplitude  (float amp) noexcept { amplitude_.store(amp); }
    void setKind       (SynthKind k) noexcept { kind_.store(k); }

    int fifoAvailable() const noexcept { return fifo_.available(); }

private:
    std::atomic<SynthKind>  kind_;
    std::atomic<float>      frequency_hz_;
    std::atomic<float>      amplitude_;
    int                     sample_rate_;
    int                     chunk_frames_;
    util::LockFreeFloatFifo fifo_;
    double                  phase_ = 0.0;       // decoder-thread state
    uint32_t                rng_state_ = 0xC0FFEEu;
    std::atomic<bool>       running_{false};
    std::atomic<uint64_t>   underrun_count_{0};
    std::atomic<uint64_t>   frames_produced_{0};
    std::thread             decoder_thread_;

    void decoderLoop();
    static float xorshiftWhite(uint32_t& s) noexcept;
};

} // namespace spe::input
