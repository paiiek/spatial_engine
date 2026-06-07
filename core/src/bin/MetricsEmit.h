// core/src/bin/MetricsEmit.h
//
// v0.9 Lane A (A-M1 follow-up, review CONCERN-2) — single shared builder for
// the 6-field /sys/metrics 1 Hz emit. Both the engine binary's control-thread
// tick (spatial_engine_core.cpp) and the e2e unit test
// (test_p_sys_metrics_extended.cpp) call THIS function so the exact wire shape
// — one ",s" key=value message per field — is exercised by the same code.
//
// Control-thread ONLY: this is the 1 Hz emit path, never the audio hot path.
// It allocates a stack char buffer and calls the existing 3-arg
// sendReply(addr, ",s", kv) overload (OSCBackend.h:120). NO new OSC encoder.

#pragma once

#include "ipc/OSCBackend.h"

#include <cstdint>
#include <cstdio>

namespace spe::bin {

// Emit the 6 /sys/metrics fields as individual ",s" key=value OSC messages.
// All values are passed in already-resolved (the caller loads the scalar
// atomics / device counters) so the formatting is identical regardless of who
// calls it. Field order and key spelling are wire-contract and must not change.
inline void emitSysMetrics(ipc::OSCBackend& osc,
                           std::uint32_t cpu_pct,
                           std::uint32_t cpu_peak_pct,
                           std::uint32_t p99_us,
                           std::uint64_t xrun_count,
                           std::uint64_t engine_overrun_count,
                           std::uint32_t binaural_demote_count,
                           // v1.0 Phase 1.4b — per-stage audio-thread timing (µs,
                           // last block). APPENDED after the original 6 fields so
                           // the wire contract stays backward-compatible (clients
                           // read by key=value; older parsers ignore new keys).
                           std::uint32_t stage_render_us = 0,
                           std::uint32_t stage_room_us = 0,
                           std::uint32_t stage_decorr_us = 0,
                           std::uint32_t stage_binaural_us = 0) noexcept {
    char kv[64];
    std::snprintf(kv, sizeof(kv), "cpu_pct=%u", cpu_pct);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "cpu_peak_pct=%u", cpu_peak_pct);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "p99_us=%u", p99_us);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "xrun_count=%llu",
                  static_cast<unsigned long long>(xrun_count));
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "engine_overrun_count=%llu",
                  static_cast<unsigned long long>(engine_overrun_count));
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "binaural_demote_count=%u", binaural_demote_count);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "stage_render_us=%u", stage_render_us);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "stage_room_us=%u", stage_room_us);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "stage_decorr_us=%u", stage_decorr_us);
    osc.sendReply("/sys/metrics", ",s", kv);
    std::snprintf(kv, sizeof(kv), "stage_binaural_us=%u", stage_binaural_us);
    osc.sendReply("/sys/metrics", ",s", kv);
}

}  // namespace spe::bin
