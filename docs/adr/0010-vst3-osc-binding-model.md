# ADR 0010 — VST3 plugin OSC binding model (Phase C4 design freeze)

**Status**: Draft (v0.2.0 ships this design contract; v0.3.0 implements)
**Date**: 2026-05-10
**Spec commit pin**: `v0.3.0-c4-draft` — frozen post-RALPLAN Round-2 APPROVE
**Related**: ADR 0003 (IPC: OSC over UDP), ADR 0006 (ADM-OSC v1.0 spec freeze), ADR 0011 (multi-instance discovery)

---

## Context

ADR 0003 establishes OSC-over-UDP-9100 as the v0 IPC channel between the
standalone `spatial_engine_core` and external controllers. ADR 0006 §C
("VST3 scope: C-β standalone-only, Phase C4 deferral") explicitly punted
the question of whether and how the VST3 plugin process itself should speak
ADM-OSC, with the trade-off `C-α` (per-instance UDP) vs `C-γ` (continued
deferral) noted but not decided.

This ADR closes that decision. It locks the binding model so the v0.3
implementation team can execute without re-litigating axis choices, while
v0.2.0 ships only this draft (per Plan §1.4 deliverable matrix).

ADR 0010 is **orthogonal to ADR 0003**. ADR 0003 §Migration target ("shm + UDS
for v1+") explicitly anticipates additional non-OSC control channels. The
plugin-side OSC socket introduced here is a *second OSC endpoint* on a
separate auto-assigned port; it does not violate ADR 0003's
"single OSC schema" invariant because the schema (the `Command` variant)
is shared across both the standalone (port 9100) and the plugin
(per-instance port). What changes is the *number of bind points*, not the
schema.

---

## Decisions (frozen post-RALPLAN Round-2)

The §1.5 Final Decision Freeze in `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md`
locks the following axis values for v0.3 implementation. v0.2.0 ships only
this draft; ratification + impl land in v0.3.

### A1 — UDP binding model: A1-ε (per-instance recv-only socket)

The plugin process binds **one UDP socket per plugin instance** at an
auto-assigned port (`bind(0)` with kernel-chosen ephemeral port). Direction
is **receive-only** (A2-α): the plugin listens for `/adm/obj/N/...` packets
addressed to it; the standalone owns the send-side. Multi-instance discovery
(ADR 0011) provides the registry that maps `(plugin_pid, obj_id_subset) →
bound_port`.

**Why A1-ε**:
- Linux has no audio plugin sandbox; `bind()` is proven by `core/src/ipc/OSCBackend.cpp:75-83`.
- Recv-only avoids the certification risk on macOS / Windows that A1-α (full bidirectional bind) would attract.
- Multi-instance port chaos (cited as A1-α rejection rationale) is bounded by ADR 0011's registry: each instance advertises its port; senders consult registry instead of guessing.
- A1-δ (sidecar binary + UDS) was the Round-1 recommendation but rejected post-Architect §6 synthesis: ~7 days of new infra to mitigate a cert risk that does not materialize on the v0.2/v0.3 Linux-only target.

### A2 — Direction: A2-α (recv-only)

Plugin reads ADM-OSC and updates internal state. Plugin does NOT publish
its state back over OSC; the standalone retains that responsibility per
ADR 0006 §C. Cert risk is minimised; one-way path matches the
console→plugin first-customer use case.

### A2.1 — Mute mapping: A2.1-α-temporary (v0.2) → A2.1-β (v0.3)

In v0.2.0, no new VST3 parameter is added. `kBypass` (id=6, added in
C2B postmortem `acb8c27`) doubles as the user-visible mute control;
the limitation is documented in `RELEASE_NOTES_KR.md` §Known limitations.
In v0.3.0 (when this ADR is implemented), state format bumps v2→v3 to add
a dedicated `kMute` (id=7) parameter; the multi-version reader at
`vst3/SpatialEngineProcessor.cpp:267-289` handles all 3 versions.

### A3 — Discovery: A3-β (file-based registry)

See ADR 0011. `~/.config/spatial_engine/instances.json` is the single
source of truth.

### A4 — Threading: A4-β (dedicated `std::thread` per plugin instance)

The plugin spawns one dedicated `std::thread` at instance construction;
this thread owns the UDP socket lifecycle, calls `recv()`, decodes via
the existing `core/src/ipc/CommandDecoder.cpp` machinery, and pushes
decoded `Command`s to a per-instance SPSC ring drained by the audio
callback. Pattern mirrors `core/src/ipc/OSCBackend.cpp:75-83`.

**Audio-thread invariant (Principle 3)**: zero allocation on the audio
thread. The dedicated UDP thread allocates freely; the SPSC ring is
fixed-size; the audio callback only does `pop()`.

**Thread-budget invariant** (Architect blocker #4): with N plugin
instances loaded in the same DAW process, the total thread cost is
**1 dedicated UDP thread per instance**. v0.3 deployment guidance:
recommend `N ≤ 8` instances per DAW process. Some hosts (Pro Tools)
cap plugin thread count; this is a v0.4+ macOS/Windows concern, not
v0.3 Linux.

### A5 — Cert risk minimization: A5-α (Linux-only `SPATIAL_ENGINE_VST3_OSC=ON` build flag)

The plugin-side OSC code is gated behind a CMake option that defaults to
`OFF`. v0.3.0 Linux builds enable it. Future cross-platform builds
(macOS, Windows) keep it `OFF` until cert evidence is collected; in that
window, A5-β (sidecar everywhere) is the contingency design.

**Why this gate matters for OFF byte-baseline**: the v0.2.0 OFF baseline
(`.ci/off_baseline.bytes.sha256` + `.symbols.sha256`) is preserved
byte-for-byte because no plugin OSC code is emitted under
`SPATIAL_ENGINE_VST3=OFF` (current OFF baseline build configuration).

---

## Drivers

1. **Korean live-venue customer dependency** (Driver 1 of Plan §1.2):
   Plugin↔console workflow is the first-contract acceptance gate.
2. **Ecosystem parity with L-ISA / Spat Revolution / d&b Soundscape**:
   Recv-only is the necessary one-way piece of bidi parity; standalone
   provides the send-back side.
3. **Reduce technical debt before next refactor**: locking A1..A5
   in this ADR while C3 cache is fresh prevents architectural drift.

---

## Alternatives considered (steelmanned)

### A1-α — Per-instance bidirectional UDP socket (rejected)

- **Steelman**: simplest model; ~50 LOC per direction; one socket, one thread.
- **Rejected because**: bidirectional cert risk on macOS / Windows
  forecast (Logic Pro / Pro Tools sandboxes restrict `bind()` for
  outbound traffic in some configurations); recv-only A1-ε is a strict
  subset that captures Driver 1 + 2 with smaller cert surface.

### A1-β — Shared port + multiplex (rejected)

- **Steelman**: one bound port across all instances; uses a routing
  layer to dispatch by `obj_id`.
- **Rejected because**: silent failure mode on N>1 instance collision;
  multiplexer becomes critical-path code; ADR 0011 file-based registry
  with per-instance sockets is more debuggable.

### A1-γ — Continued deferral (rejected)

- **Rejected because**: ADR 0006 §C already deferred from C3 to C4;
  another deferral pushes Driver 1 (Korean customer) further out.
  v0.2.0's B1-β scope already cleanly defers *implementation* to v0.3
  while shipping the design contract — that is the deferral done right.

### A1-δ — Sidecar binary + UDS bridge (rejected post-Architect synthesis)

- **Steelman**: zero plugin-process bind; cert-safest; future-proof for
  macOS / Windows.
- **Rejected because**: ~7 days of new infrastructure (sidecar binary,
  JSON registry race, EPIPE handling, GC) to mitigate a cert risk that
  does not materialize on Linux. Architect §6 path-a synthesis: defer
  to v0.4+ if cross-platform cert evidence demands it. Documented as
  contingency.

### A2-β / A2-γ / A2-δ — Send-only / bidirectional / DAW-automation-bridge (rejected)

- **Rejected because**: A2-α (recv-only) is the strict subset that
  satisfies the console→plugin first-customer use case; bidi adds cert
  surface (paired with A1-α rejection); DAW-automation-bridge required
  the sidecar binary (paired with A1-δ rejection).

### A2.1-β as v0.2 deliverable (rejected — deferred to v0.3)

- **Rejected because**: shipping a state v3 schema in v0.2 with no v0.2
  consumer = pure churn; OFF re-pin cycle for an unused param.
  C2B-Q2 (`open-questions.md:51`) supports deferral.

### A3-α (mDNS) / A3-γ (`/sys/state` poll) / A3-δ (single-instance only) — rejected

- See ADR 0011 for full rationale.

### A4-α (audio thread bind) — forbidden

- Violates Principle 3 (audio-thread alloc==0).

### A4-γ (host message thread) — unavailable

- VST3 plugin has no `createView` editor in v0.3 scope; no host message
  thread accessible. Could be reconsidered in v0.4 (Phase D6) when
  editor view ships.

### A5-β (sidecar everywhere) — held as v0.4+ contingency

- See A1-δ rejection. Reserved if Linux A5-α proves cert-blocked on
  macOS / Windows.

---

## Why chosen

A1-ε + A2-α + A2.1-β-deferred + A3-β + A4-β + A5-α:

- **Smallest cert surface** consistent with Driver 1 (Korean customer
  console→plugin path).
- **Lowest implementation cost** consistent with first-contract
  hardness: one socket per instance + one dedicated thread + a JSON
  registry. ~3 days of v0.3 work, not 7.
- **Single OSC schema** (ADR 0003 invariant) preserved: `Command` variant
  is shared across standalone + plugin endpoints; only the bind-point
  count grows.
- **OFF byte-baseline** untouched in v0.2.0 (no plugin OSC code emitted
  under `SPATIAL_ENGINE_VST3=OFF`).

---

## Consequences

### Positive
- Plugin instances become first-class OSC endpoints in v0.3.
- Korean live-venue console workflow validated end-to-end at v0.3 (C4
  plugin) on top of v0.2 (C3 standalone).
- `Command` schema continues to be the single source of truth.

### Negative
- New per-instance state: bound port, dedicated thread.
- Multi-instance registry (ADR 0011) becomes critical infrastructure.
- v0.3 OFF byte-baseline must be re-pinned once when the new code lands
  (one extra GHA cycle; see Plan §S7 v0.3 contract).
- Plugin process gains TCP/UDP code; LD_DEBUG audit (per `vst3.yml`
  workflow) must verify no new sysdep additions.

### Neutral
- Plugin↔console transport remains UDP-only at v0.3; UDS / shm migration
  is still a v1.0+ concern (ADR 0003 falsifier path).

---

## Thread-budget invariant (formal)

For N plugin instances loaded in a single DAW process:

```
threads(plugin_osc) = N × 1   // one dedicated UDP recv thread per instance
threads(plugin_audio) = N × 1 // existing host-managed audio callback
threads(plugin_total) = 2N    // upper bound under SPATIAL_ENGINE_VST3_OSC=ON
```

v0.3 deployment guidance: `N ≤ 8` per process recommended. Higher
values are functionally permitted but unverified against host thread
caps. v0.4+ cross-platform port revisits this with macOS / Windows
host-specific evidence.

---

## Follow-ups (v0.3 implementation contract)

- **S2 (v0.3)**: `vst3/sidecar_bridge/PluginInstanceRegistry.{h,cpp}` —
  reads ADR 0011 schema; written + GC'd by sidecar (or by plugin under
  A5-α direct bind, no sidecar in v0.3 Linux fast path).
- **S3 (v0.3)**: `vst3/SpatialEngineProcessor.cpp` — extend ctor to
  optionally bind UDP socket when `SPATIAL_ENGINE_VST3_OSC=ON`. Spawn
  dedicated thread; SPSC ring per instance; audio callback drains.
- **S6 (v0.3)**: end-to-end soak — 60s × 4 instances × 1 kHz packet
  rate; assert `RT_ASSERT_NO_ALLOC` holds; `p99 < 5ms` end-to-end.
- **S7 (v0.3)**: OFF baseline re-pin once for new plugin OSC code path.
- **v1.0 candidate**: mDNS discovery (A3-α upgrade); editor view
  (`createView`) for in-DAW configuration UI.
- **v0.4+ contingency**: A5-β sidecar everywhere if A5-α blocked on
  macOS / Windows. ADR 0010 §A5-β is the design seed.
- **60-day post-first-contract**: replace synthetic ADM-OSC fixtures
  with real vendor capture (inherited from ADR 0006 follow-ups).
