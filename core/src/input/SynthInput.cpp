// core/src/input/SynthInput.cpp

#include "input/SynthInput.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace spe::input {

SynthInput::SynthInput(SynthKind kind,
                       float     frequency_hz,
                       float     amplitude,
                       int       sample_rate,
                       int       chunk_frames,
                       int       fifo_capacity_pow2)
    : kind_(kind),
      frequency_hz_(frequency_hz),
      amplitude_(amplitude),
      sample_rate_(sample_rate),
      chunk_frames_(chunk_frames),
      fifo_(fifo_capacity_pow2)
{}

SynthInput::~SynthInput() {
    stop();
}

int SynthInput::pull(float* dst, int n_frames) noexcept {
    int got = fifo_.read(dst, n_frames);
    if (got < n_frames) {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return got;
}

float SynthInput::xorshiftWhite(uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    // Map to [-1, 1).
    return (static_cast<int32_t>(s) * (1.0f / static_cast<float>(0x7FFFFFFF)));
}

bool SynthInput::decodeMore() {
    const int want = chunk_frames_;
    // Peek FIFO free space without consuming the FIFO API: write a chunk and
    // see how many were accepted.
    std::vector<float> scratch(static_cast<size_t>(want), 0.f);

    const SynthKind kind = kind_.load(std::memory_order_acquire);
    const float     freq = frequency_hz_.load(std::memory_order_acquire);
    const float     amp  = amplitude_.load(std::memory_order_acquire);
    const double    sr   = static_cast<double>(sample_rate_);
    const double    omega = 2.0 * 3.14159265358979323846 * static_cast<double>(freq) / sr;

    if (kind == SynthKind::Sine) {
        for (int i = 0; i < want; ++i) {
            scratch[static_cast<size_t>(i)] =
                amp * static_cast<float>(std::sin(phase_ + omega * static_cast<double>(i)));
        }
        phase_ += omega * static_cast<double>(want);
        // Wrap to keep magnitude bounded.
        const double two_pi = 2.0 * 3.14159265358979323846;
        if (phase_ > 1e6) {
            phase_ = std::fmod(phase_, two_pi);
        }
    } else { // White
        for (int i = 0; i < want; ++i) {
            scratch[static_cast<size_t>(i)] = amp * xorshiftWhite(rng_state_);
        }
    }

    int wrote = fifo_.write(scratch.data(), want);
    if (wrote > 0) {
        frames_produced_.fetch_add(static_cast<uint64_t>(wrote),
                                   std::memory_order_relaxed);
        // If we couldn't write all of `want`, the surplus phase advance was a
        // small bookkeeping cost — acceptable for a test-tone source.
        return true;
    }
    return false;
}

void SynthInput::start() {
    if (running_.exchange(true)) return;
    decoder_thread_ = std::thread([this] { decoderLoop(); });
}

void SynthInput::stop() {
    if (!running_.exchange(false)) return;
    if (decoder_thread_.joinable()) decoder_thread_.join();
}

void SynthInput::decoderLoop() {
    using namespace std::chrono_literals;
    while (running_.load(std::memory_order_acquire)) {
        const bool progressed = decodeMore();
        if (!progressed) std::this_thread::sleep_for(1ms);
    }
}

} // namespace spe::input
