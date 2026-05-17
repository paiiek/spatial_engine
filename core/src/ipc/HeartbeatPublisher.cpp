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

    // v0.5.1 Q1 — 1-Hz binaural status sidecar tick. Keeps a separate
    // accumulator so the ping cadence (10 Hz) is decoupled from the status
    // cadence (1 Hz). The callback is invoked at most once per tick, even
    // if delta_ms ≥ 2 * BINAURAL_STATUS_PERIOD_MS (status is a snapshot,
    // not an event stream — no point in emitting multiple back-to-back).
    binaural_status_accum_ms_ += delta_ms;
    if (binaural_status_cb_ && binaural_status_accum_ms_ >= BINAURAL_STATUS_PERIOD_MS) {
        binaural_status_accum_ms_ = 0;
        ++binaural_status_emissions_;
        binaural_status_cb_(now_ms_);
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
