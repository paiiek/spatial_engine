# ADR 0013 — WebGUI / vid2spatial 9100 producer arbitration via explicit mode toggle

**Status**: Accepted
**Date**: 2026-05-12
**Author**: build-runner (S0 deliverable #4, plan `spatial-engine-webgui-v1.md` §3)
**Related**: ADR 0003 (IPC OSC UDP), ADR 0006 (ADM-OSC v1.0 spec freeze), ADR `vid2spatial_osc_contract.md`
**Plan anchors**: §1.1 P3 (Single Producer, Single Wire), §6 risk 5.2, §3 S0 deliverable #4

---

## Context

After the WebGUI MVP landed (`ui/webgui/server.py`, `ui/webgui/osc_bridge.py`), the
spatial_engine cmd port **UDP 9100** has two independent senders inside the same
host:

| Producer | File / line | Triggered by |
|---|---|---|
| **WebGUI bridge** | `ui/webgui/osc_bridge.py:65` (`SimpleUDPClient(host, 9100)`) — invoked from `ui/webgui/server.py::_dispatch_to_osc()` (~L165, L181, L185, ...) | every browser drag / scene-save / transport / algo / dsp / noise event |
| **vid2spatial bridge** | `bridge/vid2spatial_osc.py:42` (`target_port=9100`) → `bridge/vid2spatial_osc.py:254-255` (`self._client.send_message("/adm/obj/N/aed", ...)`) | every inbound `/vid2spatial/*` packet on 9000 (AI mode only) |
| **Indirect WebGUI handle** | `ui/webgui/server.py:305` (`BridgeServer(target_port=9100, ...)`) | `POST /api/vid2spatial/start` |

Both producers write to **the same wire**. If both are active concurrently we get:

1. Per-object state thrash in the engine — vid2spatial pushes 60 Hz IIR-smoothed
   `/adm/obj/N/aed` packets while a browser drag pushes its own `/adm/obj/N/aed`
   in the same frame. The engine's `setMove()` accepts whichever arrives last;
   wire-byte hash determinism (G3c sentinel, plan §2) is impossible.
2. Test non-determinism — the byte-baseline regression sentinel
   (`scripts/verify_byte_baseline.py`) only verifies build artefacts, not
   run-time wire output. A second producer is invisible to the gate.
3. **Risk 5.2** in the v1 plan: the issue is explicitly flagged with no chosen
   resolution before this ADR.

`server.py` already has `_bridge_mode ∈ {ai, low_latency}` (L83) plus an HTTP
endpoint (`POST /api/mode`) and a `/tmp/.spe_bridge_mode` file used by
`vid2spatial_osc.py:302` to pick up the switch. The toggle exists; it is just
not **enforced** in `_dispatch_to_osc()`.

---

## Decision

Adopt **explicit mode arbitration** rather than a multiplexer.

* `_bridge_mode == 'low_latency'`:
  * vid2spatial_osc returns early in every OSC handler (already implemented at
    `bridge/vid2spatial_osc.py:228, :262, :269, :276, :284`).
  * WebGUI is the **sole producer** on 9100. All `_dispatch_to_osc()` branches
    fire unconditionally.
* `_bridge_mode == 'ai'`:
  * vid2spatial_osc is the active producer (60 Hz IIR-smoothed positional
    stream — see `bridge/vid2spatial_osc.py:243-255`).
  * WebGUI **must NOT** emit positional packets. Specifically, message types
    `obj_pos` and `obj_gain` are dropped at the dispatcher entry point with a
    `WARNING`-level log line. (Reasoning: these two carry per-object position /
    level that *vid2spatial* is concurrently writing to the same object number.)
  * Non-positional control-plane traffic — `scene_save`, `scene_load`,
    `scene_list`, `transport`, `obj_algo`, `obj_dsp`, `noise` — passes through.
    These message classes do not contend with vid2spatial's `/adm/obj/N/aed`
    output (vid2spatial only ever emits the `aed` address, see
    `bridge/vid2spatial_osc.py:254`).

The arbitration is implemented as a single early-return guard inside
`_dispatch_to_osc()` (`ui/webgui/server.py`), backed by a frozenset
`_POSITION_MTYPES = {'obj_pos', 'obj_gain'}` and a helper
`_ai_mode_position_conflict()`. The list is in code (not a config file) so the
guard travels with the dispatcher and the byte-baseline sentinel covers it.

---

## Drivers

* **D1 Wire Compatibility** — guarantees a single producer per UDP send so the
  G3c wire-byte hash gate (plan §2) is deterministic.
* **D2 Latency** — no multiplexer / queue between WS event and 9100; the AI mode
  drop is a *no-op early return*, cheaper than a queue.
* **Single-owner simplicity** — one explicit branch in one file; reviewable in
  ≤ 20 lines.

---

## Alternatives considered

| Option | Why rejected |
|---|---|
| **B. Multiplexer / queue inside `osc_bridge`** that arbitrates packets via priority | Adds an ordering buffer between WS and UDP — breaks the existing G3a/G3b RTT budgets (5 ms / 20 ms p99) and creates a new state to deadlock-test in soak (G7). For two well-known producers, an `if mode == 'ai': return` is strictly cheaper. |
| **C. Bind WebGUI sender to a different port** (9102 etc.) and add a coalescer in the engine | Touches `core/` CommandDecoder — violates the v1 plan's "no C++ core change" invariant and the byte baseline. |
| **D. Detect collision at run-time and back off** | Statistical wire-byte tests cannot tell racing senders apart from drop-and-retry. Reactive arbitration is unverifiable. |
| **E. Make `/api/mode` failure-loud** — refuse `mode=ai` while WS clients are connected | Forces a UX/operations decision (kick the browser? freeze the bridge?) that belongs to ADR 0014 follow-up, not v1 sprint. |

---

## Why this option

* Falsifiable: pytest can mock `_bridge_mode = 'ai'` and assert
  `_dispatch_to_osc({'type': 'obj_pos', ...})` produces *zero* `osc_send_fn`
  calls. Mock-side covers the contract without UDP traffic.
* Minimum delta — three lines of guard logic + one frozenset; no new modules,
  no new processes, no new ports.
* Zero RT-safety touch — change is entirely Python-side; `core/` is untouched.
* Plan §6 risk 5.2 closed in S0 (foundation phase) before the riskier S5 soak
  starts.

---

## Consequences

### Positive

* G3c (wire byte hash) becomes deterministic — only one producer can race on
  9100 at a time.
* Engineers reading `server.py:_dispatch_to_osc` see the contract inline —
  no hidden state in `osc_bridge` or `vid2spatial_osc`.
* WS clients in AI mode see a noisy log warning rather than a silent drop,
  which surfaces misconfiguration to the operator within seconds.

### Negative / trade-offs

* AI mode **fully removes the WebGUI's ability to nudge object position
  manually**. A user who wants to override a tracked object must either
  (a) toggle to `low_latency` first, or (b) intercept at the vid2spatial
  upstream (out of scope here). This is the documented price of single-producer.
* The `_POSITION_MTYPES` set is duplicated knowledge between the dispatcher
  and this ADR. Drift mitigation: future positional message types (e.g. a
  hypothetical `obj_xyz`) **must** be appended to the frozenset in the same
  commit that introduces them — failure mode is loud (the message would slip
  past the guard and break G3c).
* The `/tmp/.spe_bridge_mode` shared-file dependence stays — ADR 0014 (4)
  follow-up tracks the `$XDG_RUNTIME_DIR` migration.

---

## Follow-ups

1. **S2 (Scene TX E2E)** — verify scene_save / scene_load are *not* in
   `_POSITION_MTYPES`; the G5 wire-hash sentinel passes through cleanly under
   both modes.
2. **S5 (48h soak)** — fault inject 5-minute mode flips and confirm the
   warning rate matches mode duty cycle (no spurious drops in
   `low_latency`).
3. **S6 (RTT triple path)** — G3c wire-byte hash test must explicitly bind
   `_bridge_mode = 'low_latency'` to keep WebGUI as the producer.
4. **ADR-2 deferred** (`/scene/list` reply 양방향) — when implemented, the
   reply path is *engine → 9101 → osc_bridge*, NOT 9100, so it does not
   re-open this arbitration question.
5. **ADR 0015 candidate** — if vid2spatial ever needs to emit gain
   (`/adm/obj/N/gain`) the `_POSITION_MTYPES` set widens; mention here as
   an explicit decision pointer.
