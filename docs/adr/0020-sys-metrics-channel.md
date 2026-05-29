# ADR 0020 — `/sys/metrics` 1 Hz Telemetry Channel

| | |
|---|---|
| **Status** | Accepted (shipped v0.9 Lane A — `81271dc` / `0ad9ab3`; dashboard consumers `805b79a`–`147d8d6`) |
| **Date** | 2026-05-30 |
| **Authors** | Planner / Architect / Critic (v0.9 Lane A ralplan consensus) |
| **Related** | ADR 0003 (IPC over OSC/UDP), ADR 0017 (runtime-demote telemetry `/sys/binaural_diag`), ADR 0019 §6 (`/sys/warning`, `/sys/state` shm telemetry) |
| **Plan** | `.omc/plans/spatial-engine-v0.9-laneA-metrics-dashboard.md` (DD-A / DD-B; §5 ADR) |

This is a **thin channel doc**: it records the existence, encoding, field set, and
semantics of one new outbound OSC address. It is **not** an encoder-signature ADR
(contrast ADR 0017 §B) — `/sys/metrics` reuses the existing `,s` key=value
`sendReply` overload, so there is no new C++ encoder to justify.

---

## Context

The engine exposed per-block CPU/latency counters only as struct *intent*:
`core/src/util/ObservabilityCounters.h` reserved the `/sys/metrics` address in
comments, but no instance was constructed and no emitter existed (dead code). There
was no always-on outbound channel carrying CPU load, p99 block latency, or xrun
counts — operators had no real-time view of engine headroom regardless of the audio
backend (null / Dante / shm). The WebGUI had a telemetry bridge but no dashboard.

v0.9 Lane A adds the measurement (`core/src/util/CpuMeter.h`), gives `SpatialEngine`
a single owned `ObservabilityCounters obs_counters_` instance (NET-NEW — Principle 3),
and emits the snapshot once per second on the control-thread heartbeat as
`/sys/metrics`.

---

## Decision A — New `/sys/metrics` address, not `/sys/state` trailing-args

`/sys/metrics` is a **new** 1 Hz address emitted from the engine binary's
control-thread tick (`core/src/bin/spatial_engine_core.cpp`), unconditionally —
independent of the audio backend (null / Dante / shm all emit it).

**Rationale.** The plan's outline assumed extending an existing full `/sys/state`
1 Hz string, but no such emitter exists. The only `/sys/state` producer is the shm
sidecar (`ShmTelemetryEmitter::emitState`), which sends per-key on-change messages
(`shm_producer_alive` / `shm_producer_state` / `shm_consumer_locked`) — a
shm-specific channel, not a full-state snapshot. Hanging CPU/xrun off it would (a)
collide semantically with shm-only telemetry and (b) emit nothing on the null/Dante
paths, breaking the "always-on CPU/xrun view" acceptance criterion. A dedicated
address sidesteps both.

---

## Decision B — `,s` key=value encoding, reusing the existing `sendReply` overload

Each of the six fields is emitted as one OSC message at `/sys/metrics` with typetag
`,s` and a single `"key=value"` string argument. The emit is centralised in
`core/src/bin/MetricsEmit.h` (`spe::bin::emitSysMetrics(...)`) so both the engine
binary's control-thread tick and the `test_p_sys_metrics_extended` e2e test exercise
the identical wire shape.

**Rationale.** This reuses the existing 3-arg `sendReply(addr, ",s", kv)` overload
(`OSCBackend.h`) and the `ShmTelemetryEmitter::emitState` precedent — **zero new OSC
encoders**. Consequently there is no encoder-signature decision to make and no
ADR-0017-§B-style flag-extension trade-off. Field order and key spelling are the wire
contract and must not change; new fields append.

| Field | Type | Semantics |
|---|---|---|
| `cpu_pct` | `,s` `cpu_pct=<u32>` | EWMA (α=0.1) of per-block wall time as a percentage of the block deadline (`block_wall_us / block_budget_us × 100`). Wall-based — includes OS preemption, so slightly pessimistic vs. pure thread-CPU. |
| `cpu_peak_pct` | `,s` `cpu_peak_pct=<u32>` | Worst observed per-block CPU% since the last `CpuMeter::reset()`. **Clamped to `[0,100]`** (A9-Q8 deferral — peak rendered on its own dashboard line, A-M3/M4 follow-up may revisit the clamp). |
| `p99_us` | `,s` `p99_us=<u32>` | p99 of per-block wall time in microseconds, via a **scalar P² running estimator** (Jain & Chlamtac 1985 — five markers, O(1)/sample, no stored samples). An operational estimate, not an exact quantile. |
| `xrun_count` | `,s` `xrun_count=<u64>` | Monotonic backend underrun/overrun count = `driver->xrunCount()` (audio backend layer). |
| `engine_overrun_count` | `,s` `engine_overrun_count=<u64>` | **Separate** counter: blocks the engine rejected for exceeding `MAX_BLOCK` (`record_overrun()` early-return path). Distinct from backend `xrun_count`. |
| `binaural_demote_count` | `,s` `binaural_demote_count=<u32>` | **Sticky 0/1 runtime-demote flag** (via `binauralIsRuntimeDemoted()`), NOT a cumulative strike count — the cumulative getter is not cheaply available; revisit if the dashboard needs the count. Reset via `/sys/binaural_reset_demote ,i 1`. |

---

## Decision C — `steady_clock` wall + EWMA + scalar p99, not a reservoir

`CpuMeter` measures per-block wall time via `steady_clock::now()` at block
entry/exit (vDSO on Linux — no syscall, no alloc), updates an EWMA mean, a peak
tracker, and a scalar P² p99 estimator, then relaxed-stores three scalar atomics the
control thread loads.

**Rationale.** The audio-thread cost is a clock read + O(1) estimator update +
relaxed scalar store — no array, reservoir, sort, or double-buffer, hence no
torn-read window (RT Principle 1). Wall time directly answers "did the block finish
inside its deadline?" — the operationally actionable question — and is portable
(same API on Linux/macOS/Windows). `steady_clock` is vDSO-backed so it stays
alloc/syscall-free in the hot path. `reset()` clears estimator state on
`prepareToPlay` / sample-rate / block-size change so stale-budget samples cannot
poison the mean/peak/p99. Verified: RT-asserts (`build_rton`) alloc=0, and
`bench_cpumeter_record_latency` median 0.115 µs vs. a 5.0 µs budget.

---

## Alternatives considered (dropped)

- **(a) 1024-slot ring reservoir for an exact p99.** Dropped — cross-thread the
  array needs double-buffering and has a torn-read window. A scalar P²/GK estimator
  delivers equivalent operational value at O(1) and race-free (single relaxed atomic).
  *User-approved amendment 2026-05-30.* If exact quantiles are later needed, do the
  aggregation off-hot-path on the control thread.
- **(b) `/sys/state` trailing-args.** Dropped — no full-state `/sys/state` emitter
  exists to extend; the only producer is shm-specific on-change telemetry, and the
  null/Dante paths emit no `/sys/state` at all (CPU/xrun would silently never ship).
- **(c) A new multi-int OSC encoder** (`,iiiii` / `encodeOscReplyMetrics`). Dropped —
  reusing `,s` key=value means zero new encoder and no ADR-0017-§B-style
  signature/flag-extension decision.
- **(d) `CLOCK_THREAD_CPUTIME_ID` per-block** (pure thread CPU, preemption excluded).
  Deferred to a v1.x auxiliary metric — needs platform branching (macOS/Windows
  differences) and is not directly tied to the deadline margin operators care about.
- **(e) Chart.js / uPlot for the dashboard.** Dropped — offline / zero-dependency
  principle; a self-hosted ~210-LOC canvas minichart covers the four 60 s series.

---

## Consequences

- One new outbound OSC address (`/sys/metrics`) on the wire surface; clients that
  filter on address ignore it silently. **Zero new OSC encoders** (`,s` reused).
- `SpatialEngine` owns a single `ObservabilityCounters obs_counters_` instance plus an
  `observabilityCounters()` accessor (both NET-NEW; previously a dead struct
  definition only).
- Consumers must know the new address: `osc_bridge.py` classifies `/sys/metrics`
  (and `/sys/warning`) into typed dicts and routes them to the server's `MetricsHub`;
  `/sys/state` (shm) stays on the existing raw `/ws` path (a latest-wins cache would
  clobber distinct shm keys — A-M2/A-M3 m1 decision).
- CPU% is wall-based (includes preemption, slightly pessimistic); `p99_us` is a
  scalar estimate (sufficient for operational display, not an exact quantile);
  `binaural_demote_count` is a 0/1 flag, not a cumulative count.
- The dashboard's object-activity grid ships as a static scaffold; live wiring is
  deferred.
