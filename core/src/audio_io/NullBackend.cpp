// core/src/audio_io/NullBackend.cpp

#include "audio_io/NullBackend.h"

#include "core/Constants.h"
#include "util/RtAssertNoAlloc.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace spe::audio_io {

NullBackend::NullBackend(double sample_rate, int output_channels, int block_size)
    : sample_rate_(sample_rate),
      out_channels_(output_channels),
      block_size_(block_size) {
    flat_buffer_.assign(static_cast<std::size_t>(out_channels_) * block_size_, 0.0f);
    channel_pointers_.resize(static_cast<std::size_t>(out_channels_));
    for (int ch = 0; ch < out_channels_; ++ch) {
        channel_pointers_[static_cast<std::size_t>(ch)] = flat_buffer_.data() + ch * block_size_;
    }
}

NullBackend::~NullBackend() {
    stop();
}

std::string NullBackend::description() const {
    return "NullBackend(rate=" + std::to_string(static_cast<int>(sample_rate_))
         + ",ch=" + std::to_string(out_channels_)
         + ",block=" + std::to_string(block_size_) + ")";
}

BackendError NullBackend::start(AudioCallback* callback) {
    if (running_.load()) return BackendError::AlreadyStarted;
    if (!callback) return BackendError::DeviceOpenFailed;
    if (block_size_ > spe::MAX_BLOCK) return BackendError::BlockSizeExceedsMax;

    callback_ = callback;
    callback_->prepareToPlay(sample_rate_, block_size_);
    running_.store(true);
    worker_ = std::thread(&NullBackend::thread_loop, this);
    return BackendError::Ok;
}

BackendError NullBackend::stop() noexcept {
    if (!running_.load()) return BackendError::NotStarted;
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (callback_) {
        callback_->releaseResources();
        callback_ = nullptr;
    }
    return BackendError::Ok;
}

void NullBackend::thread_loop() {
    using clock = std::chrono::steady_clock;
    const auto block_period = std::chrono::nanoseconds(
        static_cast<std::int64_t>(1e9 * block_size_ / sample_rate_));

    auto next_deadline = clock::now() + block_period;

    while (running_.load(std::memory_order_acquire)) {
        // Zero output buffer; engine writes the block.
        std::fill(flat_buffer_.begin(), flat_buffer_.end(), 0.0f);

        AudioBlock blk;
        blk.output_channels      = channel_pointers_.data();
        blk.input_channels       = nullptr;
        blk.output_channel_count = out_channels_;
        blk.input_channel_count  = 0;
        blk.num_frames           = block_size_;
        blk.sample_rate          = sample_rate_;
        blk.hw_timestamp_ns      = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count());

        if (callback_) {
            SPE_RT_NO_ALLOC_SCOPE();
            callback_->audioBlock(blk);
        }

        const auto now = clock::now();
        if (now > next_deadline) {
            xruns_.record_underrun();
            next_deadline = now + block_period;
        } else {
            std::this_thread::sleep_until(next_deadline);
            next_deadline += block_period;
        }
    }
}

BackendError NullBackend::pump_synchronous(AudioCallback* callback, int blocks) {
    if (running_.load()) return BackendError::AlreadyStarted;
    if (!callback) return BackendError::DeviceOpenFailed;
    if (block_size_ > spe::MAX_BLOCK) return BackendError::BlockSizeExceedsMax;

    callback_ = callback;
    callback_->prepareToPlay(sample_rate_, block_size_);
    for (int i = 0; i < blocks; ++i) {
        std::fill(flat_buffer_.begin(), flat_buffer_.end(), 0.0f);
        AudioBlock blk;
        blk.output_channels      = channel_pointers_.data();
        blk.input_channels       = nullptr;
        blk.output_channel_count = out_channels_;
        blk.input_channel_count  = 0;
        blk.num_frames           = block_size_;
        blk.sample_rate          = sample_rate_;
        blk.hw_timestamp_ns      = static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(block_size_);
        SPE_RT_NO_ALLOC_SCOPE();
        callback_->audioBlock(blk);
    }
    callback_->releaseResources();
    callback_ = nullptr;
    return BackendError::Ok;
}

std::unique_ptr<AudioBackend> make_null_backend(double sample_rate,
                                                int output_channels,
                                                int block_size) {
    return std::make_unique<NullBackend>(sample_rate, output_channels, block_size);
}

}  // namespace spe::audio_io
