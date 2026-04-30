# ADR 0003 — IPC transport: OSC over UDP, single transport, single Command schema

- **Status**: Accepted (P0 first-draft; finalized at P4)
- **Date**: 2026-04-28
- **Related**: ADR 0001 (process model), spec v2.1 R13 lock

## Context

Spec v2.1 R13 locks the IPC transport to **a single OSC schema** spanning UI IPC, external
OSC controllers, and the future v1+ VST3Control plugin. Principle 5 makes the schema the
v0→v1 invariant.

## Decision

v0 uses **OSC over UDP loopback** between JUCE core (Process A) and PySide6 UI (Process B).
The same transport and the same `Command` schema serve UI IPC + external OSC + v1+ VST3Control.

- **Default ports**: 9100 (cmd) / 9101 (state) — moved off TouchOSC defaults.
- **Override**: `--osc-cmd-port` / `--osc-state-port` (P4).
- **Wire format**: every command's first OSC argument is `schema_version` u16.
- **Handshake**: `/sys/protocol_version` request/reply at startup; mismatch = explicit error
  dialog (UI) and refuse-to-send.
- **Heartbeat (C6)**: **10 Hz / 100 ms period**, lossy publish; **miss threshold = 3
  consecutive missed beats (300 ms)**. Control thread emits `/sys/heartbeat_miss` on miss.

## Drivers

1. Latency: UDP loopback adds <0.2 ms to the budget table.
2. Schema-as-invariant: one schema for UI + external OSC + v1+ VST3.
3. Spec R13 lock.
4. External-controller compatibility (TouchOSC, OSCulator, vid2spatial trajectory adapter).

## Alternatives considered (steelmanned)

### A — Shared-memory ring buffer (state mirror) + Unix Domain Socket control channel

- **Steelman**: lowest-possible-latency state mirror.
- **Why rejected for v0**: more code than the latency budget needs (<0.2 ms IPC budget is
  comfortably met by UDP); POSIX shm is OS-specific and harder to inspect; Python
  `multiprocessing.shared_memory` + PySide6 + Qt event loop = GIL/GC tarpit;
  external-controller story breaks.
- **Migration target on falsifier trigger** (see below).

### B — ZeroMQ over `ipc://` or `tcp://127.0.0.1`

- **Why rejected**: framing overhead vs raw UDP; opaque to engineers; still need schema choice.

### C — Cap'n Proto over UDS

- **Why rejected**: codegen complicates `bootstrap.sh` 60-min target; less L-ISA-idiomatic
  than OSC; no external-controller story.

## Why chosen

Only OSC/UDP simultaneously satisfies all four drivers. shm+UDS antithesis is preserved as
the v1 migration target conditional on the falsifier.

## Consequences

- ✓ IPC contribution <0.2 ms to latency budget.
- ✓ Free external-controller support (TouchOSC, OSCulator).
- ✓ Plain-text schema; `tcpdump` / `osc_debug_console.py` debuggable.
- ✓ Schema versioning is in-band (per-packet).
- − UDP best-effort. Mitigations: UI 120 Hz coalescing, sequence-number reorder defense
  (Pre-mortem C), 10 Hz lossy heartbeat for resync.
- − Default ports 9100/9101 are *moved off* TouchOSC defaults; document in onboarding.

## Falsifier (operational)

`p99 drag-to-render latency in IPC-dominant stages > 3.0 ms for ≥1% of windows under
sustained 8-object 120 Hz drag in a 1 h soak.`

Operationalization at P10:
- Latency harness extended to 1 h drag soak.
- Measure end-to-end input-OSC → speaker-onset latency in 1 s windows (3,600 windows total).
- Compute p99 of the IPC + decode + audio-callback-wait portion (latency table stages 2–4).
- Trigger: ≥36 of 3,600 windows with p99 > 3.0 ms in IPC-dominant stages → file
  `migrate-to-shm-uds` issue.

## Follow-ups

- P4 ships `--osc-cmd-port` / `--osc-state-port` overrides.
- P4 ships `/sys/protocol_version` handshake + `schema_version` field on every command +
  10 Hz heartbeat with 300 ms miss gate (C6).
- P10 latency harness produces the 1 h 8-object 120 Hz drag soak measurement.
