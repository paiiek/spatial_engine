// core/src/input/LiveMicInput.cpp

#include "input/LiveMicInput.h"

#include <cstring>

namespace spe::input {

LiveMicInput::LiveMicInput(int sample_rate, int fifo_capacity_pow2)
    : sample_rate_(sample_rate),
      fifo_(fifo_capacity_pow2) {}

LiveMicInput::~LiveMicInput() { stop(); }

void LiveMicInput::start() {
    running_.store(true);
}

void LiveMicInput::stop() {
    running_.store(false);
}

int LiveMicInput::pull(float* dst, int n_frames) noexcept {
#if defined(SPE_HAVE_JUCE)
    // JUCE path: drain from FIFO populated by the capture callback.
    int got = fifo_.read(dst, n_frames);
    if (got < n_frames) {
        std::memset(dst + got, 0, static_cast<size_t>(n_frames - got) * sizeof(float));
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return n_frames;
#else
    // NO_JUCE silence fallback.
    std::memset(dst, 0, static_cast<size_t>(n_frames) * sizeof(float));
    frames_produced_.fetch_add(static_cast<uint64_t>(n_frames), std::memory_order_relaxed);
    return n_frames;
#endif
}

bool LiveMicInput::decodeMore() {
    // NO_JUCE: silence source is infinite — nothing to decode.
    // JUCE path: capture is push-driven via audioDeviceIOCallback; no-op here.
    return true;
}

} // namespace spe::input
