// core/src/bin/PlayerStaleWatchdog.h
// ADR 0018 D-5 — production wiring for the external-player heartbeat staleness
// watchdog. The staleness check detects the ABSENCE of incoming `,d` pings, so
// it cannot be driven by ping arrival; it must run on a control/IO-thread
// periodic timer. This header factors out the exact per-tick decision the
// standalone run loop performs so it is exercised by a unit test (it drives the
// SAME entry point with an injected clock) rather than only the direct
// SpatialEngine::checkPlayerHeartbeatStale() call.
//
// Control / IO thread ONLY (never the audio thread): a wall-clock read is fine
// here and there is no allocation in the body.

#pragma once

#include "core/SpatialEngine.h"

#include <chrono>
#include <cstdint>

namespace spe::bin {

// Period between staleness evaluations. The player pings at 1 Hz, so a 1 Hz
// watchdog is the natural cadence: it is cheap and the underlying check
// rate-limits its own /sys/warning emission to once per 30 s anyway.
inline constexpr std::chrono::seconds kPlayerStaleCheckPeriod{1};

// One run-loop iteration of the watchdog.
//
//   now            : the loop's steady_clock "now" (cadence gate).
//   last_check     : in/out — steady_clock timestamp of the previous evaluation
//                    (default-constructed time_point on the first call forces an
//                    immediate evaluation).
//   unix_now_ms    : current wall-clock unix ms (caller-supplied so tests inject
//                    a deterministic clock; production reads system_clock).
//
// Returns true iff the period elapsed AND checkPlayerHeartbeatStale() emitted a
// warning on this tick. No-op (returns false) when the period has not elapsed
// or when no external ping has been seen yet.
inline bool servicePlayerStaleWatchdog(
    spe::core::SpatialEngine& engine,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point& last_check,
    int64_t unix_now_ms) noexcept
{
    if (now - last_check < kPlayerStaleCheckPeriod) {
        return false;
    }
    last_check = now;
    return engine.checkPlayerHeartbeatStale(unix_now_ms);
}

}  // namespace spe::bin
