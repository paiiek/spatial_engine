// core/src/ipc/HeartbeatPublisher.h
// Control-thread heartbeat publisher: 10 Hz (100 ms period).
// Time dependency is mockable — call tick(ms) directly in tests.
// Never called from audio thread.

#pragma once
#include "Command.h"
#include <cstdint>
#include <functional>
#include <atomic>

namespace spe::ipc {

class HeartbeatPublisher {
public:
    // Callback invoked when a heartbeat ping is published.
    using PublishCallback = std::function<void(const Command&)>;

    static constexpr uint64_t PERIOD_MS = 100; // 10 Hz

    explicit HeartbeatPublisher(PublishCallback cb)
        : cb_(std::move(cb)) {}

    // Advance mock clock by delta_ms. Emits ping(s) as needed.
    // In production, call tick(elapsed_since_last_call_ms) from a timer.
    void tick(uint64_t delta_ms) noexcept;

    // Direct: emit one ping immediately with given timestamp.
    void emitPing(uint64_t now_ms) noexcept;

    uint64_t pingsSent() const noexcept { return pings_sent_; }
    uint64_t nowMs()     const noexcept { return now_ms_; }

    // Reset state (for tests).
    void reset() noexcept {
        now_ms_         = 0;
        accum_ms_       = 0;
        pings_sent_     = 0;
        next_seq_       = 1;
    }

private:
    PublishCallback cb_;
    uint64_t now_ms_     = 0;
    uint64_t accum_ms_   = 0;
    uint64_t pings_sent_ = 0;
    uint32_t next_seq_   = 1;
};

} // namespace spe::ipc
