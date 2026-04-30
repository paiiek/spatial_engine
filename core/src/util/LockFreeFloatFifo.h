// core/src/util/LockFreeFloatFifo.h
// Single-producer / single-consumer lock-free ring buffer of float samples.
// Producer = decoder thread (block freely). Consumer = audio thread (RT-safe,
// no allocations, no syscalls). Capacity must be a power of two.

#pragma once

#include <atomic>
#include <cassert>
#include <cstring>
#include <vector>

namespace spe::util {

class LockFreeFloatFifo {
public:
    explicit LockFreeFloatFifo(int capacity_pow2)
        : capacity_(capacity_pow2),
          mask_(capacity_pow2 - 1),
          buffer_(static_cast<size_t>(capacity_pow2), 0.f)
    {
        assert(capacity_pow2 > 0 && (capacity_pow2 & (capacity_pow2 - 1)) == 0
               && "capacity must be a power of two > 0");
    }

    // Producer: write up to n samples. Returns actual count written.
    int write(const float* src, int n) noexcept {
        const int head = head_.load(std::memory_order_relaxed);
        const int tail = tail_.load(std::memory_order_acquire);
        const int avail_space = capacity_ - (head - tail);
        const int to_write = (n < avail_space) ? n : avail_space;
        for (int i = 0; i < to_write; ++i) {
            buffer_[static_cast<size_t>((head + i) & mask_)] = src[i];
        }
        head_.store(head + to_write, std::memory_order_release);
        return to_write;
    }

    // Consumer (audio thread). Read up to n samples. Returns count read.
    int read(float* dst, int n) noexcept {
        const int tail = tail_.load(std::memory_order_relaxed);
        const int head = head_.load(std::memory_order_acquire);
        const int avail = head - tail;
        const int to_read = (n < avail) ? n : avail;
        for (int i = 0; i < to_read; ++i) {
            dst[i] = buffer_[static_cast<size_t>((tail + i) & mask_)];
        }
        tail_.store(tail + to_read, std::memory_order_release);
        return to_read;
    }

    int available() const noexcept {
        return head_.load(std::memory_order_acquire)
             - tail_.load(std::memory_order_acquire);
    }

    int capacity() const noexcept { return capacity_; }

    void clear() noexcept {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

    // Pointer to underlying storage (for mlock).
    const float* data() const noexcept { return buffer_.data(); }
    size_t      sizeBytes() const noexcept {
        return buffer_.size() * sizeof(float);
    }

private:
    int                capacity_;
    int                mask_;
    std::vector<float> buffer_;
    std::atomic<int>   head_{0};
    std::atomic<int>   tail_{0};
};

} // namespace spe::util
