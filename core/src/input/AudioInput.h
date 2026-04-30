// core/src/input/AudioInput.h
// Per-object mono input source. Decoder thread fills a lock-free FIFO;
// audio thread pulls non-blocking. Underflow returns short reads — caller
// is responsible for filling the remainder with silence and incrementing
// audio_underrun_count.per_object[id].
//
// All concrete inputs (FileInput, SynthInput, future LiveMicInput) implement
// this interface so the engine treats every source uniformly.

#pragma once

#include <cstdint>

namespace spe::input {

class AudioInput {
public:
    virtual ~AudioInput() = default;

    // Audio-thread side (RT-safe, no allocation, no syscalls).
    // Pulls up to n_frames mono samples into dst. Returns frames written.
    // Underflow → returns < n_frames. Caller fills remainder with silence.
    virtual int pull(float* dst, int n_frames) noexcept = 0;

    // Decoder-thread side. Implementations may block on file I/O / DSP.
    // Returns true if more data was queued, false on end-of-stream.
    virtual bool decodeMore() = 0;

    virtual int      sampleRate()     const noexcept = 0;
    virtual uint64_t underrunCount()  const noexcept = 0;
    virtual uint64_t framesProduced() const noexcept = 0;
    virtual bool     atEnd()          const noexcept = 0;

    // Lifecycle (ok to be no-op).
    virtual void start() {}
    virtual void stop()  {}
};

} // namespace spe::input
