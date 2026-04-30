# ADR 0001 — Process model: two processes, OSC/UDP IPC

- **Status**: Accepted (P0 first-draft; finalized at P5)
- **Date**: 2026-04-28
- **Supersedes**: archived `v1-archive/` ADR 0001 (Rust + cpal)

## Context

Spec v2.1 (R12 + R13) locks the native audio core to **C++ + JUCE 7.x** and the IPC transport to a
**single OSC schema**. Principle 1 forbids any allocation, lock, syscall, or log on the audio
thread; Principle 5 makes the IPC schema the v0→v1 invariant while the v0 UI is throw-away.

## Decision

v0 ships **two processes**:

- **Process A** — JUCE C++ audio core. Production-grade; preserved into v1.
- **Process B** — PySide6 Python UI. Throw-away; rewritten at v1.

They communicate over **OSC over UDP loopback** (default ports 9100 cmd / 9101 state; overridable
via `--osc-cmd-port` / `--osc-state-port`). Every command carries `schema_version` u16 as the
first OSC argument; startup `/sys/protocol_version` handshake gates connection.

## Drivers

1. **Latency budget (<5 ms p99)**: Python's GIL/GC tail latency cannot leak across a process
   boundary into the audio thread. The IPC contribution is <0.2 ms (UDP loopback ≈ memcpy).
2. **v0→v1 transition cost**: only the UI is throw-away; the audio core and the IPC schema
   must survive. A process boundary makes that easier to maintain.
3. **Spec v2.1 R13 lock**: single OSC transport, single Command schema. This only works
   cleanly across a process boundary.

## Alternatives considered

### A — Single process: JUCE host with embedded CPython (`Py_Initialize` + `pybind11`)

- **Steelman**: zero IPC, direct memory share for state mirror, one binary.
- **Why rejected**: GIL acquisition on every C++→Python edge serializes; Python GC pauses
  co-occur with audio-thread CPU and cause cache thrash; embedded CPython + Qt + JUCE is a
  build-system tarpit (Conda vs system Python vs uv); spec R13's "single transport OSC"
  only works clean across a process boundary; UI throw-away rewrite at v1 is harder if
  Python is welded into the C++ host.

### B — Two processes with shared-memory state mirror + UDS control socket (ZeroMQ-on-UDS or hand-rolled)

- **Steelman**: lowest possible state-mirror latency; clean L-ISA-style mirror.
- **Why rejected for v0**: more code than the latency budget needs; POSIX shm is OS-specific
  and harder to inspect; Python `multiprocessing.shared_memory` + PySide6 + Qt event loop =
  GIL/GC tarpit; external-controller (TouchOSC, OSCulator) story breaks.
- **Migration target**: if the ADR 0003 falsifier fires (p99 IPC stages > 3.0 ms for ≥1%
  windows under sustained 8-object 120 Hz drag in 1 h soak) → start v1 with shm+UDS workstream.

## Consequences

- ✓ Production-grade C++/JUCE audio thread from day one (Principle 1).
- ✓ Python iteration speed for throw-away UI (Principle 5 from spec).
- ✓ Free external-controller compatibility (TouchOSC, OSCulator, future text2traj OSC adapter).
- − Two binaries to ship; schema versioning required to detect drift between core HEAD and
  stale UI checkout.
- − Python→OSC serialization tax (~0.05 ms; well inside latency budget).

## Falsifier

Not applicable — this is a structural choice spec R13 effectively pre-decided. Re-litigation
would require revisiting spec R13.

## Follow-ups

- Confirm Linux audio stack (PipeWire-JACK pinned in P0 `docs/lab_setup.md`).
- JUCE commercial license procurement plan (`docs/license_procurement_plan.md`, C5 trigger event).
