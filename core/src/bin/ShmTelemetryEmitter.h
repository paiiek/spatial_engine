// core/src/bin/ShmTelemetryEmitter.h
// ADR 0019 Phase C PCM IPC — PR4 (telemetry).
//
// Header-only, extractable unit (mirrors PlayerStaleWatchdog.h): a small
// spe::bin class that maps the SharedRingBackend's four PR2 warning counters +
// the producer/consumer state onto real /sys/warning + /sys/state OSC, emitted
// via an injected OSCBackend&. Factored out so a ctest drives the SAME entry
// point production drives (the D-5 watchdog precedent: PlayerStaleWatchdog.h +
// test_phase_b_sync_handlers.cpp case 11).
//
// ── Thread model ─────────────────────────────────────────────────────────
//   Control / IO thread ONLY (never the audio thread). seed()/tick() read the
//   backend's const accessors (which load header atomics) + call OSCBackend::
//   sendReply (a control-thread enqueue). NO allocation in tick(): detail
//   strings are built into stack char buffers (precedent SpatialEngine.cpp:1071
//   snprintf). PR4 adds NO RT-path code — the four counters are already
//   RT-incremented in the backend; this unit only READS them.
//
// ── Edge / latch semantics (plan Decision 1 / 3, CR-2 table) ───────────────
//   /sys/warning (4 codes): SEED-SILENT, then emit once per counter INCREMENT
//     (edge). shm_underrun is additionally gated to ≤1 per 1000 ms wall-clock
//     window via last_underrun_emit_ms_ (MJ-1 — the only counter with no
//     source rate-limit). A code's prev-cache advances ONLY on a successful
//     sendReply (true); a no-peer (false) return HOLDS the edge so it is
//     re-attempted next tick (CR-2 / PM6).
//   /sys/state (3 keys): SEED-AND-EMIT (a late consumer learns current level),
//     then emit on value-change. Per-field retry-on-no-peer latch cleared only
//     on a true sendReply (the frozen fallback_mode semantics,
//     vst3/SpatialEngineProcessor.cpp:1362-1371).
//
// Linux + macOS (POSIX shm) only — same guard as SharedRingBackend.

#pragma once

#include "audio_io/SharedRingBackend.h"

#if defined(__linux__) || defined(__APPLE__)

#include "ipc/OSCBackend.h"

#include <cstdint>
#include <cstdio>

namespace spe::bin {

class ShmTelemetryEmitter {
public:
    // seed(): capture the initial snapshot. Warnings SEED-SILENT (capture the
    // current counts as baseline, emit nothing — a pre-existing non-zero count
    // is NOT replayed as a fresh event). State SEED-AND-EMIT (arm all three
    // latches so the initial level is delivered to a — possibly late — peer).
    void seed(const spe::audio_io::SharedRingBackend& be) noexcept {
        if (isDetached(be)) {
            // Defensive: a detached region yields 0 counters/heartbeat; do not
            // capture a misleading baseline. The first attached tick will seed.
            seeded_ = false;
            return;
        }
        prev_underrun_          = be.underrunWarningCount();
        prev_stale_            = be.staleWarningCount();
        prev_pacing_           = be.pacingWarningCount();
        prev_attached_no_data_ = be.attachedNoDataWarningCount();

        // State: capture current values as the prev-cache, then arm every
        // latch so the initial snapshot emits (seed-and-emit).
        prev_producer_alive_   = 0;   // placeholder; recomputed on the seed tick
        prev_producer_state_   = 0;
        prev_consumer_locked_  = 0;
        have_state_            = false;  // first tick computes + emits all three
        seeded_                = true;
    }

    // tick(): one diagnostics pass. now_unix_ms is the wall-clock epoch ms the
    // caller already computed (D2: poll_diagnostics' now_ms is wall-clock).
    void tick(const spe::audio_io::SharedRingBackend& be, spe::ipc::OSCBackend& osc,
              std::uint64_t now_unix_ms) noexcept {
        // (a) Detached-region guard (fold-in 7b): a detached / never-started
        //     backend returns 0 for heartbeat/state/lock; now_unix_ms - 0 would
        //     be misread as a huge "producer stale" age. The correct signal
        //     there is "no shm", not "producer died" — so early-return without
        //     emitting.
        if (isDetached(be)) {
            return;
        }
        if (!seeded_) {
            seed(be);
            // After a (re)seed the warning baseline is captured; fall through so
            // the state snapshot is emitted on this same tick (seed-and-emit).
        }

        emitWarnings(be, osc, now_unix_ms);
        emitState(be, osc, now_unix_ms);
    }

private:
    // Detached / never-started proxy (fold-in 7b). The backend does NOT expose
    // region_.is_attached() publicly and PR4 must add exactly ONE new backend
    // surface (consumerLockWord(), CR-1) — so detachment is inferred from the
    // existing public surface: a successfully-attached+started SharedRingBackend
    // has inputChannelCount() >= 1 (the header channel count, validated 1..64 in
    // start()); a never-attached / detached backend has inputChannelCount() == 0.
    // In production the tick is reached only after a successful start() (so this
    // is true throughout the run); the guard exists for the never-attached
    // default-constructed path (AC-4c) so a heartbeat of 0 is never misread as
    // "producer stale".
    static bool isDetached(const spe::audio_io::SharedRingBackend& be) noexcept {
        return be.inputChannelCount() <= 0;
    }

    // ── /sys/warning edge emission (D1, CR-2 hold) ─────────────────────────
    void emitWarnings(const spe::audio_io::SharedRingBackend& be,
                      spe::ipc::OSCBackend& osc, std::uint64_t now_unix_ms) noexcept {
        // shm_underrun — count edge, additionally 1/s wall-clock gated (MJ-1).
        {
            const unsigned long long cur = be.underrunWarningCount();
            if (cur > prev_underrun_) {
                const bool window_open =
                    (now_unix_ms - last_underrun_emit_ms_) >= 1000ull;
                if (window_open) {
                    char detail[24];
                    std::snprintf(detail, sizeof(detail), "%llu", be.xrunCount());
                    if (osc.sendReply("/sys/warning", ",iis", 0, 0,
                                      "shm_underrun", detail)) {
                        prev_underrun_         = cur;         // advance on success
                        last_underrun_emit_ms_ = now_unix_ms;
                    }
                    // else: no-peer HOLD — prev_underrun_ stays behind, re-tried.
                }
                // else: 1/s window closed — HOLD; re-evaluated next window.
            }
        }

        // shm_producer_stale — heartbeat age in seconds (BYTE-FOR-BYTE D-5 shape).
        {
            const unsigned long long cur = be.staleWarningCount();
            if (cur > prev_stale_) {
                const std::uint64_t hb = be.producer_heartbeat_ms();
                const std::uint64_t age_ms = (now_unix_ms >= hb) ? (now_unix_ms - hb) : 0ull;
                char detail[24];
                std::snprintf(detail, sizeof(detail), "%llu",
                              static_cast<unsigned long long>(age_ms / 1000ull));
                if (osc.sendReply("/sys/warning", ",iis", 0, 0,
                                  "shm_producer_stale", detail)) {
                    prev_stale_ = cur;
                }
            }
        }

        // shm_producer_pacing — fixed marker.
        {
            const unsigned long long cur = be.pacingWarningCount();
            if (cur > prev_pacing_) {
                if (osc.sendReply("/sys/warning", ",iis", 0, 0,
                                  "shm_producer_pacing", "pacing_drift")) {
                    prev_pacing_ = cur;
                }
            }
        }

        // shm_attached_no_data — channels=N (once on attach).
        {
            const unsigned long long cur = be.attachedNoDataWarningCount();
            if (cur > prev_attached_no_data_) {
                char detail[32];
                std::snprintf(detail, sizeof(detail), "channels=%d",
                              be.inputChannelCount());
                if (osc.sendReply("/sys/warning", ",iis", 0, 0,
                                  "shm_attached_no_data", detail)) {
                    prev_attached_no_data_ = cur;
                }
            }
        }
    }

    // ── /sys/state on-change emission (D3, retry-on-no-peer latch) ──────────
    void emitState(const spe::audio_io::SharedRingBackend& be,
                   spe::ipc::OSCBackend& osc, std::uint64_t now_unix_ms) noexcept {
        // shm_producer_alive: attached AND heartbeat fresh (≤ 100 ms). The
        // attachment side is guaranteed by tick()'s early-return guard; the
        // freshness uses the same threshold the stale-warning uses, so
        // shm_producer_alive=0 ⇔ the shm_producer_stale edge (fold-in 7a).
        const std::uint64_t hb = be.producer_heartbeat_ms();
        const std::uint32_t alive =
            (now_unix_ms >= hb &&
             (now_unix_ms - hb) <= spe::audio_io::SharedRingBackend::kStaleThresholdMs)
                ? 1u : 0u;
        const std::uint32_t pstate = be.producer_state();
        const std::uint32_t locked = (be.consumerLockWord() != 0u) ? 1u : 0u;

        if (!have_state_) {
            // Seed-and-emit: arm all three latches at the current value.
            prev_producer_alive_  = alive;
            prev_producer_state_  = pstate;
            prev_consumer_locked_ = locked;
            alive_pending_  = true;
            state_pending_  = true;
            locked_pending_ = true;
            have_state_     = true;
        } else {
            if (alive != prev_producer_alive_)   { prev_producer_alive_  = alive;  alive_pending_  = true; }
            if (pstate != prev_producer_state_)  { prev_producer_state_  = pstate; state_pending_  = true; }
            if (locked != prev_consumer_locked_) { prev_consumer_locked_ = locked; locked_pending_ = true; }
        }

        if (alive_pending_) {
            char kv[40];
            std::snprintf(kv, sizeof(kv), "shm_producer_alive=%u", prev_producer_alive_);
            if (osc.sendReply("/sys/state", ",s", kv)) alive_pending_ = false;
        }
        if (state_pending_) {
            char kv[40];
            std::snprintf(kv, sizeof(kv), "shm_producer_state=%u", prev_producer_state_);
            if (osc.sendReply("/sys/state", ",s", kv)) state_pending_ = false;
        }
        if (locked_pending_) {
            char kv[40];
            std::snprintf(kv, sizeof(kv), "shm_consumer_locked=%u", prev_consumer_locked_);
            if (osc.sendReply("/sys/state", ",s", kv)) locked_pending_ = false;
        }
    }

    // ── State ──────────────────────────────────────────────────────────────
    bool seeded_ = false;

    // prev-4 warning counts (prev-cache; advances on a confirmed send).
    unsigned long long prev_underrun_          = 0;
    unsigned long long prev_stale_            = 0;
    unsigned long long prev_pacing_           = 0;
    unsigned long long prev_attached_no_data_ = 0;

    // MJ-1: emitter-owned 1/s wall-clock latch for shm_underrun.
    std::uint64_t last_underrun_emit_ms_ = 0;

    // /sys/state prev-values + per-field pending latch (cleared on send true).
    bool          have_state_           = false;
    std::uint32_t prev_producer_alive_  = 0;
    std::uint32_t prev_producer_state_  = 0;
    std::uint32_t prev_consumer_locked_ = 0;
    bool          alive_pending_        = false;
    bool          state_pending_        = false;
    bool          locked_pending_       = false;
};

}  // namespace spe::bin

#endif  // defined(__linux__) || defined(__APPLE__)
