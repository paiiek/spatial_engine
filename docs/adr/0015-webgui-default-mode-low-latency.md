# ADR 0015 — WebGUI ships `low_latency` as the default bridge mode

**Status**: Accepted
**Date**: 2026-05-14
**Related**: ADR 0013 (WebGUI / vid2spatial 9100 producer arbitration)
**Supersedes**: the default value only of `_bridge_mode` in `ui/webgui/server.py` — ADR 0013's arbitration logic is unchanged.

---

## Context

ADR 0013 introduced `_bridge_mode ∈ {ai, low_latency}` to arbitrate the single
UDP 9100 wire between two producers:

* `ai` — vid2spatial_osc owns 9100; WebGUI positional sends (`obj_pos`,
  `obj_gain`) are dropped at `_dispatch_to_osc()`.
* `low_latency` — WebGUI is the sole 9100 producer; all dispatch branches fire.

The shipped default was `ai`. ADR 0013's own "Negative consequences" section
predicted the problem this ADR fixes:

> AI mode **fully removes the WebGUI's ability to nudge object position
> manually**.

In practice this made the WebGUI's most obvious interaction silently
non-functional out-of-the-box:

1. A fresh `uvicorn ui.webgui.server:app` starts in `ai` mode.
2. The user drags an object on the canvas. `canvas.js` optimistically moves
   the dot locally, then sends `{type:"obj_pos"}`.
3. `_dispatch_to_osc()` drops it with a server-side `WARNING` the user never
   sees. No OSC reaches the engine — **the audio object does not move**, but
   the canvas says it did.

This is the worst failure mode: silent, and visually contradicted. It was
reported in the field as "the object's sound doesn't move".

## Decision

Ship `_bridge_mode = "low_latency"` as the default in `ui/webgui/server.py`.

* The WebGUI is the 9100 producer out-of-the-box; canvas drags and trajectories
  reach the engine with no manual step.
* `ai` mode becomes an **explicit opt-in**, which is correct: `ai` mode is only
  meaningful when vid2spatial is actually running and owns the wire. The user
  who starts vid2spatial (via the WebGUI's own `vid2spatial [시작]` button or
  `POST /api/vid2spatial/start`) is exactly the user who should also flip to
  `ai` mode — both are deliberate "AI tracking is now driving" actions.

Accompanying changes in the same commit:

* `_dispatch_to_osc()` returns a `drag_suppressed` notice when it drops a
  positional message; `websocket_endpoint` broadcasts it so the browser shows
  feedback instead of a silently-lying dot.
* `/health` now reports `osc_ready` and `bridge_mode` for field diagnosis.

## Consequences

### Positive

* The default first-run experience works: drag a dot, the audio object moves.
* `ai` mode's wire-arbitration guarantee (ADR 0013) is untouched — it is still
  enforced in `_dispatch_to_osc()`, just not the default.
* Dropped positional sends are now visible in the browser (`drag_suppressed`).

### Negative / trade-offs

* If an operator runs vid2spatial **without** flipping to `ai` mode, the WebGUI
  and vid2spatial briefly co-produce on 9100 — the exact race ADR 0013
  describes. Mitigation: the vid2spatial start button and `ai` mode are both
  in the same header panel; operationally they are flipped together. A future
  ADR could auto-flip `ai` mode when `/api/vid2spatial/start` succeeds.
* Test/soak code that assumed the `ai` default is now stale. All such call
  sites already `POST /api/mode?mode=low_latency` or monkeypatch
  `_bridge_mode` explicitly, so behaviour is unchanged; only their comments
  were updated to reference this ADR.

## Follow-ups

1. Consider auto-setting `ai` mode inside `POST /api/vid2spatial/start` (and
   reverting to `low_latency` on stop) so producer ownership tracks the actual
   producer without a separate manual toggle.
2. `WebTrajectoryRunner` still sends `/adm/obj/N/aed` directly, bypassing the
   `_dispatch_to_osc()` ADR 0013 guard. Harmless under the new `low_latency`
   default, but in `ai` mode it is an unguarded second producer — route it
   through the same guard in a follow-up.
