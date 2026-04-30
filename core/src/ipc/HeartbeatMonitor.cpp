// core/src/ipc/HeartbeatMonitor.cpp
#include "ipc/HeartbeatMonitor.h"
#include <string>

namespace spe::ipc {

void HeartbeatMonitor::onPing(uint64_t now_ms) noexcept {
    last_ping_ms_       = now_ms;
    consecutive_misses_ = 0;
    started_            = true;
    now_ms_             = now_ms;
}

void HeartbeatMonitor::tick(uint64_t delta_ms) noexcept {
    now_ms_ += delta_ms;
    if (!started_) return;

    uint64_t elapsed = now_ms_ - last_ping_ms_;

    // Check for catastrophic stall first.
    if (elapsed >= CATASTROPHIC_MS) {
        ++catastrophic_count_;
        emitMiss("heartbeat_catastrophic_stall");
        // Reset last_ping so we don't spam.
        last_ping_ms_ = now_ms_;
        consecutive_misses_ = 0;
        return;
    }

    // Count missed periods.
    int expected_misses = static_cast<int>(elapsed / PERIOD_MS);
    if (expected_misses > consecutive_misses_) {
        int new_misses = expected_misses - consecutive_misses_;
        consecutive_misses_ = expected_misses;
        miss_count_ += static_cast<uint64_t>(new_misses);
        if (consecutive_misses_ >= MISS_THRESH) {
            emitMiss("heartbeat_miss");
        }
    }
}

void HeartbeatMonitor::emitMiss(const char* reason) noexcept {
    if (!cb_) return;
    Reply r;
    r.tag     = ReplyTag::HeartbeatMiss;
    r.seq     = 0;
    r.message = reason;
    cb_(r);
}

} // namespace spe::ipc
