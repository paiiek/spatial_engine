// core/src/input/FileInput.cpp

#include "input/FileInput.h"
#include <algorithm>
#include <chrono>

#if defined(__linux__) || defined(__APPLE__)
  #include <sys/mman.h>
#endif

namespace spe::input {

FileInput::FileInput(std::vector<float> mono_samples,
                     int sample_rate,
                     int chunk_frames,
                     int fifo_capacity_pow2)
    : samples_(std::move(mono_samples)),
      sample_rate_(sample_rate),
      chunk_frames_(chunk_frames),
      fifo_(fifo_capacity_pow2)
{
    tryMlockBuffer();
}

FileInput::~FileInput() {
    stop();
}

void FileInput::tryMlockBuffer() {
#if defined(__linux__) || defined(__APPLE__)
    if (samples_.empty()) return;
    // Best-effort: failure is acceptable in unprivileged tests / CI.
    (void)::mlock(samples_.data(), samples_.size() * sizeof(float));
#endif
}

int FileInput::pull(float* dst, int n_frames) noexcept {
    int got = fifo_.read(dst, n_frames);
    if (got < n_frames) {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return got;
}

bool FileInput::decodeMore() {
    if (at_end_.load(std::memory_order_acquire)) return false;
    if (paused_.load(std::memory_order_acquire)) return false;

    const size_t pos = read_pos_.load(std::memory_order_relaxed);
    const size_t total = samples_.size();
    if (pos >= total) {
        at_end_.store(true, std::memory_order_release);
        return false;
    }

    const int remaining = static_cast<int>(total - pos);
    const int want      = std::min(chunk_frames_, remaining);
    const int wrote     = fifo_.write(samples_.data() + pos, want);
    if (wrote > 0) {
        read_pos_.store(pos + static_cast<size_t>(wrote),
                        std::memory_order_release);
        frames_produced_.fetch_add(static_cast<uint64_t>(wrote),
                                   std::memory_order_relaxed);
        if (pos + static_cast<size_t>(wrote) >= total) {
            at_end_.store(true, std::memory_order_release);
        }
        return true;
    }
    // FIFO full — caller can retry later.
    return false;
}

void FileInput::start() {
    if (running_.exchange(true)) return; // already running
    decoder_thread_ = std::thread([this] { decoderLoop(); });
}

void FileInput::stop() {
    if (!running_.exchange(false)) return;
    if (decoder_thread_.joinable()) decoder_thread_.join();
}

void FileInput::decoderLoop() {
    using namespace std::chrono_literals;
    while (running_.load(std::memory_order_acquire)) {
        const bool progressed = decodeMore();
        if (!progressed) {
            // Either FIFO full, paused, or end-of-stream — sleep briefly.
            std::this_thread::sleep_for(1ms);
        }
    }
}

} // namespace spe::input
