# Scene and Cue Workflow

This document covers the library management, cuelist format, cue automation
semantics, OSC reference, and threading model for Spatial Engine's scene/cue
subsystem (v0.9 Lane E).

---

## 1. Scene Library Management

### 1.1 Storage layout

Each scene is stored as a JSON file in the configured scene directory:

```
<scene_dir>/
  <name>.json        ← snapshot file (SceneSnapshot)
  index.json         ← rebuildable metadata cache
```

`index.json` is a **rebuildable cache** — it is never the source of truth for
snapshot content. If it is corrupt or missing, `SceneController` rebuilds it
by scanning all `*.json` files in the directory (corruption recovery / rescan).
Deleting `index.json` and restarting the daemon triggers an automatic rebuild.

### 1.2 Library operations

| Operation | OSC address | Args | Effect |
|-----------|-------------|------|--------|
| List scenes | `/scene/list` | (none) | Enumerates `<scene_dir>/*.json` |
| Save scene | `/scene/save` | `,s name` | Writes `<name>.json` + updates index |
| Load scene | `/scene/load` | `,s name` | Reads `<name>.json` → applies objects |
| Rename | `/scene/rename` | `,ss from to` | Renames file; updates index entry |
| Duplicate | `/scene/duplicate` | `,ss from to` | Copies file; adds index entry for `to` |
| Delete | `/scene/delete` | `,s name` | Removes file; removes index entry |
| Set meta | `/scene/meta` | `,ss name meta_json` | Parses tags/note from `meta_json`; updates index only (no snapshot rewrite) |

All operations that mutate the library (`save`, `rename`, `duplicate`,
`delete`, `meta`) call `persistIndex()` before returning to keep `index.json`
consistent. If `persistIndex()` fails the in-memory index is still updated and
the operation returns `false` to signal the persistence error.

### 1.3 Snapshot field gaps

`SceneSnapshot` serialises these fields per object:
`id`, `az_rad`, `el_rad`, `dist_m`, `algorithm`, `gain_linear`, `muted`.

Two fields are **absent from the snapshot format** and therefore **default to
zero across every cue load**:

- `width_rad` — always 0 after a cue load (Follow-up F4).
- `reverb_send` — always 0 after a cue load (Follow-up F4).

### 1.4 Corruption recovery

If `index.json` cannot be parsed, `SceneController` falls back to a directory
rescan and rebuilds the index from the present `*.json` files. The rebuild
produces a best-effort index (no tags/notes — those live only in `index.json`).
To force a rebuild, delete `index.json` and send any scene operation or restart
the daemon.

---

## 2. Cuelist Format

Cuelists are stored in `<scene_dir>/cuelist.json`.

### 2.1 JSON schema

```json
{
  "cues": [
    {
      "scene":        "intro",    // string, required — scene name to load
      "crossfade_ms": 500.0,      // float ≥ 0, required — fade duration
      "dwell_ms":     3000.0      // float ≥ 0, optional — auto-advance delay
    },
    {
      "scene":        "main",
      "crossfade_ms": 200.0
      // dwell_ms absent → no auto-advance; cue holds until manual go/next/stop
    }
  ]
}
```

Rules enforced on deserialisation:
- `scene` = empty string → cue silently dropped.
- Negative `crossfade_ms` or `dwell_ms` → clamped to 0.
- `"dwell_ms": null` or key absent → `dwell_ms = nullopt` (no auto-advance).
- Malformed JSON → empty cuelist (no crash).

### 2.2 Dwell semantics

When `dwell_ms` is set, `CueEngine` arms an auto-advance timer **after the
crossfade completes**. The timer fires on the next `tick()` call whose
`now_ms` argument exceeds `crossfade_end_ms + dwell_ms`. The advance is
**generation-latched** (D2): if a manual `go()` or `next()`/`prev()` fires
before the deadline the generation counter is bumped and the stale dwell tag
is silently discarded (see §4 for the threading model and §3.3 for the OSC
commands).

### 2.3 Crossfade semantics — honest quantisation note

`CueEngine`'s crossfade interpolator runs on the **daemon control loop at
≈50 ms cadence**. Interpolated object-update `Command`s are forwarded through
the control→UDP outbound mailbox; the UDP listener drains that mailbox on its
**existing 100 ms `SO_RCVTIMEO` wake** (`OSCBackend.cpp:128`). As a result,
forwarded parameter steps land at **≈100 ms granularity**, not 50 ms.

**Concrete consequence**: a `crossfade_ms` in the 100–300 ms band produces
**only ~1–3 forwarded parameter steps** — a coarse audible staircase, **NOT
click-free**. This is acceptable for cue-automation fades (≥100 ms, where the
`/obj/move` command rate already matches the staircase granularity). Fades
below 50 ms or smooth morphs are **out of scope** (Follow-up F1 — wiring
`SceneCrossfade` into the audio path via a lock-free double-buffer).

---

## 3. OSC Reference

All addresses below are received on **UDP port 9100**. Argument type tags
follow OSC 1.0 notation: `s` = string, `i` = int32, `,` separates the tag
string from the rest.

### 3.1 Scene commands

| Address | Type tag | Arguments | Notes |
|---------|----------|-----------|-------|
| `/scene/save` | `,s` | `name` | Capture current object state as a named scene |
| `/scene/load` | `,s` | `name` | Load snapshot; no cuelist interaction |
| `/scene/list` | (none) | — | Triggers scene-list update in `SceneController` |
| `/scene/rename` | `,ss` | `from`, `to` | Rename scene file + index entry |
| `/scene/duplicate` | `,ss` | `from`, `to` | Copy scene; new index entry for `to` |
| `/scene/delete` | `,s` | `name` | Remove scene file + index entry |
| `/scene/meta` | `,ss` | `name`, `meta_json` | Update index tags/note; no snapshot rewrite |

### 3.2 Cue transport commands

| Address | Type tag | Arguments | Notes |
|---------|----------|-----------|-------|
| `/cue/go` | `,i` | `index` | Jump to cue `index`; arms crossfade + dwell |
| `/cue/next` | (none) | — | Advance one cue (`currentIndex + 1`); clamps at end |
| `/cue/prev` | (none) | — | Step back one cue (`currentIndex - 1`); clamps at 0 |
| `/cue/stop` | (none) | — | Cancel pending dwell; leave crossfade intact |

`/cue/go`, `/cue/next`, `/cue/prev` all bump the generation counter (D2 latch)
so any in-flight dwell deadline from a prior cue is discarded.

---

## 4. Threading Model

### 4.1 Overview

```
UDP listener thread
  recvfrom() [100 ms SO_RCVTIMEO]
  │
  ├─ /obj/* + audio tags → sink_(cmd) → cmd_fifo_ → audio callback [SPSC, RT]
  │
  ├─ /cue/* + /scene/* tags → inbound_mailbox_.push(cmd)  [fix 1a: queue]
  │                                          │
  │                              control loop (≈50 ms cadence)
  │                              drainInbound() → CueEngine.tick() / go() / …
  │                                              SceneController.handleCommand()
  │                                              │
  │                                   CueEngine emits ObjectFrame updates
  │                                   → outbound_mailbox_.push(cmd)
  │
  └─ drainOutboundToSink() [on RCVTIMEO wake or after recvfrom()]
       → sink_(cmd) → cmd_fifo_ → audio callback  [SPSC, RT]
```

### 4.2 Why this layout (fix 1a)

`cmd_fifo_` is a **SPSC** (single-producer / single-consumer) ring
(`CommandFifo.h:2`). The audio callback is the sole consumer. The **UDP
listener thread is and must remain the sole producer**. If `CueEngine` (which
runs on the control loop) were to push to `cmd_fifo_` directly, the control
loop would become a **second concurrent producer** — a data race / ring
corruption.

Fix 1a avoids this by routing `CueEngine`'s emitted object updates through a
**control→UDP outbound mailbox** (also SPSC: control loop = sole producer, UDP
thread = sole consumer). The UDP listener thread drains this mailbox and
forwards each command via the existing `sink_(cmd)` — so `cmd_fifo_` retains
its single-producer invariant. The audio callback and audio path are
**byte-identical** before and after this change.

**Single-producer invariant verification**: `grep -rn 'cmd_fifo_.push' core/src`
returns **exactly one call site** (the UDP listener thread's `sink_` wrapper).

### 4.3 Mailbox-full policy

Both mailboxes (`inbound` and `outbound`) use the same SPSC `CommandFifo`
ring. When the ring is full, `push()` returns `false` and the command is
**silently dropped**; a drop counter is incremented
(`inbound_drops_` / `outbound_drops_`). The counters are accessible for
monitoring and soak assertions. This is the same drop-and-count policy used
by the main audio `cmd_fifo_` (fix #9).

### 4.4 Gain unit conversion

Scene snapshots store `gain_linear` (linear amplitude). `CueEngine` converts
to `gain_db` for its internal interpolation frame (`ObjectFrame.gain_db`).
When emitting `PayloadObjGain`, it converts **back to linear** before pushing
to the sink (fix #7 reverse conversion). The audio callback therefore receives
linear gain, consistent with the rest of the `ObjGain` command path.

---

## 5. Known Limitations and Follow-ups

### F1 — Sample-accurate crossfade (out of scope)

Sub-50 ms click-free fades require wiring `SceneCrossfade` into the audio
callback via a lock-free double-buffer (Option B in the ADR). This adds
per-block work to the RT path and is tracked as a separate lane.

### F4 — Snapshot field gaps

`width_rad` and `reverb_send` are not serialised in `SceneSnapshot`. Both
default to 0 after any cue load. A future snapshot format revision will add
these fields.

### F5 — Pre-existing `udp_fd_` shutdown race (NOT a Lane E regression)

`OSCBackend::stop()` (`OSCBackend.cpp:269`) calls `close(udp_fd_); udp_fd_ = -1`
while the UDP listener loop may still be executing `recvfrom(udp_fd_, …)`
(`:218`). TSan flags this as a data race under both the new
`soak_cue_cmdfifo_race` and the pre-existing `soak_adm_osc_flood`. This race
predates Lane E — E-M3 did not touch `stop()` or `udp_fd_`. It is a benign
shutdown race (the fd is closed only after `running_ = false`; the next
`recvfrom()` returns an error and the loop exits).

**TSan / xrun artifact note**: the soak's `xrunCount() == 0` assertion fails
under TSan for both soaks due to instrumentation slowdown, not a real xrun.
The functional gate is the **NON-TSan Release build** soak, which reports
`xruns: 0, inbound_drops: 0`. TSan is used solely for race inspection.

A proper fix (guard `udp_fd_` with an atomic, or rely solely on
`shutdown(SHUT_RDWR)` without nulling from `stop()`) is tracked outside Lane E.
