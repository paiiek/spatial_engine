// core/src/util/XrunCounter.h
//
// Lock-free monotonic counter for audio-device underruns / overruns.
// The audio thread increments via fetch_add; the control thread reads via
// load. Exposed at /sys/xruns (P4 IPC).

#pragma once

#include <atomic>
#include <cstdint>

namespace spe::util {

class XrunCounter {
public:
    void record_underrun() noexcept {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    void record_overrun() noexcept {
        overruns_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t underruns() const noexcept {
        return underruns_.load(std::memory_order_relaxed);
    }
    std::uint64_t overruns() const noexcept {
        return overruns_.load(std::memory_order_relaxed);
    }
    std::uint64_t total() const noexcept {
        return underruns() + overruns();
    }

    void reset() noexcept {
        underruns_.store(0, std::memory_order_relaxed);
        overruns_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overruns_{0};
};

}  // namespace spe::util
