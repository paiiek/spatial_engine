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

    // v0.5.1 Q1 — optional 1-Hz binaural-status callback. Invoked on the
    // same control-thread tick that drives heartbeat pings, but only when
    // at least BINAURAL_STATUS_PERIOD_MS has elapsed since the last
    // invocation. Receives the wall-clock ms accumulator. Caller is
    // responsible for routing the int counter to OSCBackend::sendReply().
    using BinauralStatusCallback = std::function<void(uint64_t /*now_ms*/)>;

    static constexpr uint64_t PERIOD_MS                  = 100;  // 10 Hz ping
    static constexpr uint64_t BINAURAL_STATUS_PERIOD_MS  = 1000; // 1 Hz status

    explicit HeartbeatPublisher(PublishCallback cb)
        : cb_(std::move(cb)) {}

    // Optional sidecar — set once at construction (control thread). Pass
    // {} to disable.
    void setBinauralStatusCallback(BinauralStatusCallback cb) noexcept {
        binaural_status_cb_ = std::move(cb);
    }

    // Advance mock clock by delta_ms. Emits ping(s) as needed.
    // In production, call tick(elapsed_since_last_call_ms) from a timer.
    void tick(uint64_t delta_ms) noexcept;

    // Direct: emit one ping immediately with given timestamp.
    void emitPing(uint64_t now_ms) noexcept;

    uint64_t pingsSent() const noexcept { return pings_sent_; }
    uint64_t nowMs()     const noexcept { return now_ms_; }
    uint64_t binauralStatusEmissions() const noexcept {
        return binaural_status_emissions_;
    }

    // Reset state (for tests).
    void reset() noexcept {
        now_ms_                    = 0;
        accum_ms_                  = 0;
        pings_sent_                = 0;
        next_seq_                  = 1;
        binaural_status_accum_ms_  = 0;
        binaural_status_emissions_ = 0;
    }

private:
    PublishCallback        cb_;
    BinauralStatusCallback binaural_status_cb_;
    uint64_t now_ms_                     = 0;
    uint64_t accum_ms_                   = 0;
    uint64_t pings_sent_                 = 0;
    uint32_t next_seq_                   = 1;
    uint64_t binaural_status_accum_ms_   = 0;
    uint64_t binaural_status_emissions_  = 0;
};

} // namespace spe::ipc
