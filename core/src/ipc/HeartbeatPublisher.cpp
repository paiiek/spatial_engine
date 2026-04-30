// core/src/ipc/HeartbeatPublisher.cpp
#include "ipc/HeartbeatPublisher.h"

namespace spe::ipc {

void HeartbeatPublisher::tick(uint64_t delta_ms) noexcept {
    now_ms_   += delta_ms;
    accum_ms_ += delta_ms;
    while (accum_ms_ >= PERIOD_MS) {
        accum_ms_ -= PERIOD_MS;
        emitPing(now_ms_);
    }
}

void HeartbeatPublisher::emitPing(uint64_t now_ms) noexcept {
    Command cmd;
    cmd.tag            = CommandTag::HbPing;
    cmd.schema_version = SCHEMA_VERSION;
    cmd.seq            = next_seq_++;
    cmd.id             = next_seq_;
    PayloadHbPing p;
    p.timestamp_ms = now_ms;
    cmd.payload = p;
    ++pings_sent_;
    if (cb_) cb_(cmd);
}

} // namespace spe::ipc
