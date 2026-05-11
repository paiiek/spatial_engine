# VST3 OSC Reverse-Path Smoke Matrix

## Status: DEFERRED

Execution deferred to user offline week (alongside S8 DAW hands-on).
S2.6 A.7-prereq closure is PARTIAL: code + 1000-iter harness done;
smoke matrix requires real DAW installations and is waived for CI.

Waiver recorded in commit footer of feat(C4-S2.6).

---

## Purpose

Verify that the `performEdit` marshaling path (strategy a — SPSC ring)
functions correctly end-to-end across three target hosts:

- Reaper 7.x (Linux)
- Bitwig 5.x (Linux)
- Ardour 8.x (Linux)

---

## Prerequisites

1. Build the plugin with OSC enabled:

```bash
cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_VST3_OSC=ON
make -j$(nproc)
```

2. Locate the built `.vst3` bundle (e.g. `core/build/SpatialEngine.vst3`).

3. Install `oscsend` (part of `liblo-tools`):

```bash
sudo apt install liblo-tools
```

---

## Test Procedure (per host)

### Step 1 — Load the plugin

Open the DAW, create a new session, and insert **SpatialEngine** as an
instrument or effect on a track. Enable the plugin.

### Step 2 — Confirm OSC port

Check DAW console or plugin log for a line like:

```
[SpatialEnginePluginUdp] bound to port 9100
```

Note the bound port (default 9100; may be 9101–9115 if ports are busy).

### Step 3 — Send a single OSC packet

```bash
oscsend osc.udp://localhost:9100 /adm/obj/0/azim f 90.0
```

Replace `9100` with the actual bound port if different.

### Step 4 — Observe automation lane

In the DAW's automation view for the plugin:

- **Pan Azimuth** (param 0) should jump to approximately **0.75** normalized
  (90° maps to norm = (90°/360° + 0.5) = 0.75 for the [-180°, 180°] range).

For a non-round-trip test, simply verify the DAW's automation lane shows a
new value and no crash occurs.

### Step 5 — 100-packet burst

```bash
for i in $(seq 0 99); do
  oscsend osc.udp://localhost:9100 /adm/obj/0/azim f $(echo "$i * 1.8" | bc)
  sleep 0.01
done
```

Expected: no host crash; final automation value reflects the last packet.

---

## Expected Outcome per Strategy

| Strategy | Expected behaviour |
|----------|--------------------|
| **(a) SPSC ring** | `performEdit` called on message thread; automation lane updates smoothly; no crash |
| **(c) fallback** | `restartComponent(kParamValuesChanged)` triggers host re-read; automation cache refresh visible in DAW |

---

## Failure Modes and Fallback Trigger

| Failure mode | Action |
|---|---|
| DAW crashes on OSC packet | Switch to strategy (c); file bug with host vendor |
| `performEdit` called on wrong thread (detected via host assertion) | Switch to strategy (c) |
| Ring full under burst (push returns false > 5% of packets) | Increase ring capacity or add back-pressure in S4 |
| Automation lane not updated after strategy (c) `restartComponent` | Investigate `getParamNormalized` return value |

To switch to strategy (c), set `#define SPE_PERFORM_EDIT_STRATEGY_C 1` in
`SpatialEngineController.cpp` (reserved preprocessor hook; S4 will wire).

---

## Host-Specific Notes

### Reaper 7.x
- Uses a dedicated message thread; `performEdit` should be safe under strategy (a).
- Automation recording: enable "Write" mode on the parameter before sending OSC.

### Bitwig 5.x
- Bitwig dispatches `notify()` from a host-managed thread pool. Strategy (a)
  relies on the drain inside `notify()` being called from that thread.
  Confirm via Bitwig's VST3 thread model documentation before production use.

### Ardour 8.x
- Ardour's VST3 support (JUCE bridge) may not call `notify()` at all.
  If drain never fires, strategy (c) may be needed on Ardour.
  Workaround: call `drainParamEdits()` from a periodic IDLE timer (Phase D6).

---

## Sign-off Checklist

- [ ] Reaper 7.x: single-packet test PASS
- [ ] Reaper 7.x: 100-packet burst PASS
- [ ] Bitwig 5.x: single-packet test PASS
- [ ] Bitwig 5.x: 100-packet burst PASS
- [ ] Ardour 8.x: single-packet test PASS
- [ ] Ardour 8.x: 100-packet burst PASS
- [ ] No strategy fallback to (c) required on any host

Complete this checklist and update A.7-prereq status from PARTIAL → FULL
in `.omc/plans/spatial-engine-v0.3.md`.
