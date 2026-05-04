// core/src/util/SpscRing.h
//
// C1.b — Generic SPSC (single-producer / single-consumer) lock-free ring
// buffer with fixed compile-time capacity. No allocations after construction.
//
// Used by `LtcChase` (audio thread → control thread sample stream) and
// available to future RT plumbing. Distinct from `CommandFifo`/`TraceRing`:
//   - `CommandFifo<N>` is a typed FIFO of `QueuedCmd` PODs.
//   - `TraceRing<N>` carries `TraceEvent` records, drops on full.
//   - `SpscRing<T,N>` is a generic, drop-on-full SPSC with the same memory
//     ordering invariants but parameterized over T.
//
// Invariants:
//   - N must be a power of two ≥ 2.
//   - T must be trivially copyable (POD-like) so the audio-thread copy is
//     a memcpy and there is no destructor / allocator interaction.
//   - Capacity is N - 1 usable slots (one slot reserved for the empty/full
//     distinguishing index — keeps push/pop branchless).
//
// RT-safety:
//   - `push()` (producer-only) and `pop()` (consumer-only) are wait-free
//     and allocation-free.
//   - Memory ordering: producer release on head_; consumer release on tail_.
//   - Drop-on-full semantics increment `drops_`; the producer never blocks.
//
// SPE_RT_NO_ALLOC_SCOPE may safely wrap producer/consumer calls; the macro
// expands to a no-op when SPE_RT_ASSERTS=0.

#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace spe::util {

template <typename T, std::size_t CapacityPow2>
class SpscRing {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(CapacityPow2 >= 2, "Capacity must be ≥ 2");
    static_assert(std::is_trivially_copyable<T>::value,
                  "SpscRing payload must be trivially copyable for RT-safe memcpy");

public:
    static constexpr std::size_t capacity() noexcept { return CapacityPow2 - 1; }

    // Producer (audio thread). Returns false when full; increments drops_.
    bool push(const T& v) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire)) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (control thread). Returns false when empty.
    bool pop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = slots_[tail];
        tail_.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Approximate count (snapshot under concurrent load). Control-side use.
    std::size_t size_approx() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return (h - t) & MASK;
    }

    bool empty_approx() const noexcept {
        return head_.load(std::memory_order_acquire)
            == tail_.load(std::memory_order_acquire);
    }

    std::uint64_t drops() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

    void reset_drops() noexcept {
        drops_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t MASK = CapacityPow2 - 1;
    std::array<T, CapacityPow2> slots_{};
    alignas(64) std::atomic<std::size_t>   head_{0};
    alignas(64) std::atomic<std::size_t>   tail_{0};
    alignas(64) std::atomic<std::uint64_t> drops_{0};
};

}  // namespace spe::util
