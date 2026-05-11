// vst3/SpscRing.h
// Single-Producer Single-Consumer lock-free ring buffer.
// Capacity must be a power of two. T must be trivially copyable.
// Push returns false if full (non-blocking). Pop returns false if empty.
//
// Thread-safety: exactly one producer thread + one consumer thread.
// Memory order: acquire/release pairs on head_/tail_ atomics guarantee
// visibility of T data across the producer/consumer boundary.
//
// S2.6 (C4): controller-side marshaling ring for performEdit calls.
// S3 will add a separate audio-path ring; this ring is distinct.
#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace spe::vst3 {

template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SpscRing: Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscRing: T must be trivially copyable");

public:
    SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer side: push one item. Returns false if ring is full.
    bool push(const T& item) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side: pop one item into `out`. Returns false if ring is empty.
    bool pop(T& out) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false; // empty
        out = slots_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Returns approximate number of items pending (for diagnostics only).
    std::size_t sizeApprox() const noexcept
    {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & kMask;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Pad to separate cache lines: producer writes head_, consumer writes tail_.
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    T slots_[Capacity];
};

} // namespace spe::vst3
