// core/src/ipc/HeartbeatMonitor.h
// Monitors incoming heartbeat pings. Detects consecutive misses.
// 3 consecutive misses (300 ms) => emit /sys/heartbeat_miss.
// 10 s no ping => emit catastrophic-stall warning.
// Mockable via tick(ms) — no wallclock dependency.

#pragma once
#include "Command.h"
#include <cstdint>
#include <functional>

namespace spe::ipc {

class HeartbeatMonitor {
public:
    using MissCallback = std::function<void(const Reply&)>;

    static constexpr uint64_t PERIOD_MS         = 100;   // expected ping period
    static constexpr int      MISS_THRESH        = 3;     // consecutive misses
    static constexpr uint64_t CATASTROPHIC_MS    = 10000; // 10 s

    explicit HeartbeatMonitor(MissCallback cb)
        : cb_(std::move(cb)) {}

    // Notify monitor that a ping arrived at now_ms.
    void onPing(uint64_t now_ms) noexcept;

    // Advance mock clock. Detects missed pings.
    void tick(uint64_t delta_ms) noexcept;

    uint64_t missCount()        const noexcept { return miss_count_; }
    uint64_t catastrophicCount()const noexcept { return catastrophic_count_; }
    uint64_t nowMs()            const noexcept { return now_ms_; }

    void reset() noexcept {
        now_ms_             = 0;
        last_ping_ms_       = 0;
        consecutive_misses_ = 0;
        miss_count_         = 0;
        catastrophic_count_ = 0;
        started_            = false;
    }

private:
    MissCallback cb_;
    uint64_t now_ms_             = 0;
    uint64_t last_ping_ms_       = 0;
    int      consecutive_misses_ = 0;
    uint64_t miss_count_         = 0;
    uint64_t catastrophic_count_ = 0;
    bool     started_            = false;

    void emitMiss(const char* reason) noexcept;
};

} // namespace spe::ipc
