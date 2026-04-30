// core/src/util/TraceRing.h
//
// Lock-free, allocation-free SPSC ring buffer of POD TraceEvent records.
// The audio thread pushes; the control thread drains and forwards to
// `juce::Logger` or `/sys/metrics`. Capacity is power-of-two, fixed at
// compile time; overflow drops the new event and bumps the drop counter.

#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace spe::util {

struct TraceEvent {
    std::uint64_t timestamp_ns{0};   // monotonic clock (audio thread fills)
    std::uint32_t kind{0};           // enum-tagged event type
    std::uint32_t payload_a{0};
    std::uint32_t payload_b{0};
    std::uint32_t payload_c{0};
};
static_assert(sizeof(TraceEvent) <= 32, "TraceEvent kept tiny for cache density");

template <std::size_t CapacityPow2>
class TraceRing {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(CapacityPow2 >= 16, "Ring too small");

public:
    // Audio thread (single producer). Returns false when full.
    bool push(const TraceEvent& e) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire)) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head] = e;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Control thread (single consumer). Returns false when empty.
    bool pop(TraceEvent& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = slots_[tail];
        tail_.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    std::uint64_t drops() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t MASK = CapacityPow2 - 1;
    std::array<TraceEvent, CapacityPow2> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<std::uint64_t> drops_{0};
};

using TraceRing256 = TraceRing<256>;

}  // namespace spe::util
