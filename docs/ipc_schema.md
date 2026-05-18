# IPC Schema — spatial_engine v0

All inter-process communication uses OSC over UDP. Ports: **9100** (UI → Core commands),
**9101** (Core → UI state broadcasts). Schema version for v0: **1**.

Every packet includes `schema_version` as the first integer argument. On mismatch,
core emits `/sys/warning` and UI blocks audio start with an error dialog.

---

## Command Table (UI → Core, port 9100)

### Object Commands

#### `/obj/{id}/pos`

Move a spatial object to a new position.

```
/obj/{id}/pos  ,iiiff
               schema_version  seq  az_deg  el_deg  dist_m
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i (int32) | Must equal 1 for v0 |
| `seq` | i (int32) | Monotonic u32 per object; last-write-wins on engine side |
| `az_deg` | f (float32) | Azimuth in degrees, RIGHT=+az, range [-180, +180] |
| `el_deg` | f (float32) | Elevation in degrees, UP=+el, range [-90, +90] |
| `dist_m` | f (float32) | Distance in metres from listener origin, > 0 |

Engine frame: RIGHT=+az (pipeline frame). The binaural monitor path negates az internally
before SOFA lookup (AmbiX LEFT=+az). See `docs/coordinate_convention.md`.

#### `/obj/{id}/algorithm`

Switch rendering algorithm for one object.

```
/obj/{id}/algorithm  ,iis
                     schema_version  seq  algo
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | Must equal 1 |
| `seq` | i | Monotonic sequence number |
| `algo` | s (string) | One of: `vbap`, `wfs`, `dbap` |

Triggers a 256-sample crossfade on the audio thread (ADR 0006). If `(layout, algo)` pair
is incompatible, core sends `/sys/warning type=layout_incompatible` and ignores the swap.

### Noise Generator Commands

#### `/noise/{channel}/type`

```
/noise/{channel}/type  ,is
                       schema_version  noise_type
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | Must equal 1 |
| `noise_type` | s | One of: `white`, `pink` |

#### `/noise/{channel}/gain`

```
/noise/{channel}/gain  ,if
                       schema_version  gain_dB
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | Must equal 1 |
| `gain_dB` | f | Gain in dBFS, range [-120, +6] |

### System Commands

#### `/sys/protocol_version`

Startup handshake. UI sends first; core echoes back.

```
/sys/protocol_version  ,i
                       schema_version
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | 1 for v0 |

Handshake flow:
1. UI sends `/sys/protocol_version ,i 1` on startup.
2. Core receives, validates, echoes `/sys/protocol_version ,i 1` on port 9101.
3. UI receives echo, unblocks audio start.
4. On mismatch: core sends `/sys/warning ,iis 1 0 "protocol_version_mismatch" "<detail>"`
   and UI shows error dialog; audio does not start.

---

## State Broadcast Table (Core → UI, port 9101)

### `/sys/state`

Periodic engine state summary (10 Hz default).

```
/sys/state  ,i
            schema_version
```

Value encodes engine state as bitmask (bit 0 = audio running, bit 1 = reverb active,
bit 2 = binaural active).

### `/sys/matrix`

Speaker routing matrix snapshot.

```
/sys/matrix  ,i...
             schema_version  [channel_gains_linearQ16 ...]
```

N integers follow `schema_version`, one per output channel, encoding linear gain as
fixed-point Q16 (multiply by 1/65536.0 to get linear gain).

### `/sys/metrics`

10 performance counters (sent every 100 ms).

```
/sys/metrics  ,i...
              schema_version  [10 × int32 counters]
```

Counter index mapping (v0):

| Index | Name | Unit |
|-------|------|------|
| 0 | `audio_callbacks_total` | count |
| 1 | `audio_xrun_count` | count |
| 2 | `cpu_pct_audio_thread` | percent × 100 |
| 3 | `osc_packets_received` | count |
| 4 | `osc_packets_dropped` | count |
| 5 | `active_object_count` | count |
| 6 | `algorithm_swap_count` | count |
| 7 | `fdn_denormal_guard_hits` | count |
| 8 | `binaural_overrun_count` | count |
| 9 | `reserved` | 0 |

### `/sys/heartbeat_miss`

Sent when UI misses N consecutive state heartbeats (UI-side watchdog).

```
/sys/heartbeat_miss  ,iis
                     schema_version  count  msg
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | 1 |
| `count` | i | Number of consecutive missed heartbeats |
| `msg` | s | Human-readable description |

### `/sys/warning`

General-purpose warning channel.

```
/sys/warning  ,iis
              schema_version  type  details
```

| Arg | Type | Notes |
|-----|------|-------|
| `schema_version` | i | 1 |
| `type` | i | Warning type code (see table below) |
| `details` | s | Human-readable detail string |

Warning type codes (v0):

| Code | Name | Meaning |
|------|------|---------|
| 1 | `protocol_version_mismatch` | UI/core schema_version differ |
| 2 | `layout_incompatible` | (layout, algorithm) pair invalid |
| 3 | `sofa_load_failure` | KEMAR SOFA file missing or corrupt |
| 4 | `ir_metadata_mismatch` | IR sample rate / length mismatch |
| 5 | `object_pool_full` | All 64 object slots occupied |

---

## Binaural Telemetry (v0.5.1+ outbound)

Added by `chore(release): v0.5.1 — binaural hotfix` (Q1 OSC outbound) and
extended by `feat(rt-safety): v0.6` (#5 runtime auto-demote). These three
addresses are emitted by the **heartbeat IO thread** (audio thread does
not call `sendReply` for any of these — see v0.6 #4 RT-safety hard-wall).
None of them carry a `schema_version` int prefix; they were added with a
**version-implicit** wire format because they ship in advance of any
breaking-change cycle. When schema_version bumps to 2 the contract may
re-evaluate this decision.

### `/sys/binaural_status`

1 Hz heartbeat carrying the cumulative `OlaConvolver::loadInto` failure
count. Expected steady-state value is 0; any monotonic increase signals a
control-thread reload that violated the no-allocation contract.

```
/sys/binaural_status  ,i  <failures>
```

| Arg | Type | Notes |
|-----|------|-------|
| `failures` | i | Cumulative count of `loadInto` capacity-violation events since the BinauralMonitor was initialised. |

Drained at 1 Hz from `vst3/SpatialEngineProcessor.cpp::heartbeatLoop()`.
A DAW automation lane watching this address should treat any rising-edge
transition from 0 → ≥1 as a soft alarm.

### `/sys/binaural_warning`

Edge-triggered event channel. Each code fires **at most once** between
arm-conditions; the audio thread sets an atomic latch and the heartbeat
IO thread drains.

```
/sys/binaural_warning  ,s  <code>
```

Code table:

| Code (string) | Meaning | Armed by | First shipped |
|---------------|---------|----------|---------------|
| `no_sofa_loaded` | Binaural enabled but no SOFA available → output is forced-muted to avoid passing through a placeholder signal that would mislead the user into believing SOFA is loaded. | Control thread on `prepareToPlay` when `binaural.enable==1 && hrtf_loaded==false`. | v0.5.1 |
| `xfade_truncated_cpu` | The 2-block mode-transition / slot-swap crossfade was probe-clamped to 1 block due to insufficient CPU headroom. The transition still completed without click; this is a quality-of-service signal, not an error. | Audio thread via `observeAndArmXfade()` when arming a 1-block ramp because `probe_warning_set_` is true. | v0.5.1 |
| `ambivs_demoted_runtime` | B2 wall-clock cost exceeded 90 % of the block deadline for 8 consecutive blocks. Effective mode auto-demoted to B1 (Direct) for the remainder of the current `prepareToPlay` lifetime. | Audio thread via `recordB2BlockTiming()` when strikes reach `kRuntimeDemoteStrikes`. Sticky until next `prepareToPlay` (see v0.6 D-M1 fix). | v0.6.0 |
| `rt_timing_unavailable` | The `initialize()`-time probe found that `std::chrono::steady_clock::now()` falls back to a syscall on this host (average per-call ≥ 200 ns), so the v0.6 #5 runtime auto-demote detector has been silently disabled to avoid being a self-fulfilling demote prophecy. B2 still renders, but the `ambivs_demoted_runtime` warning will not fire on this platform regardless of CPU load. | Control thread `BinauralMonitor::initialize()` after the timing probe. Sticky for the BinauralMonitor lifetime; re-armed by the next `initialize()` only if the new probe is also slow. | v0.6.1 |

Future codes will be appended here; consumers must treat unknown
codes as "log + ignore" rather than error.

### `/sys/state` — fallback_mode snapshot (v0.5.1 additive)

**Dual-tag with the v0 `/sys/state ,i` bitmask** described above. The
binaural fallback snapshot uses a **string typetag** to disambiguate
from the bitmask form. Both messages may coexist on the same UDP socket;
a consumer dispatches on the typetag.

```
/sys/state  ,s  "fallback_mode=normal" | "fallback_mode=muted"
```

| Arg | Type | Notes |
|-----|------|-------|
| `<payload>` | s | `"fallback_mode=normal"` when binaural output is live, `"fallback_mode=muted"` when binaural is enabled but no SOFA → forced silence. |

Emitted **once per `prepareToPlay` lifetime** (not periodic). Drained at
1 Hz from the heartbeat IO thread with retry-on-no-peer (the latch
stays armed until a successful sendto). The v0 `/sys/state ,i` bitmask
form remains the canonical engine-state heartbeat at 10 Hz; the new
`,s` form is a per-lifecycle snapshot for the binaural fallback path
only.

**Schema-version note.** This dual-tag is *additive* (a v0-only consumer
that filters by typetag `,i` will not see the new `,s` packets, and a
v0.5.1+ consumer that filters by typetag `,s` will not see the bitmask
heartbeats). It is therefore backward-compatible by the typetag-dispatch
contract that OSC inherently provides. The `schema_version` int prefix
that the v0 `,i` form carries is **omitted** from the `,s` form because
the recipient already disambiguates on typetag — and adding the prefix
would have broken the wire-format simplicity goal of the v0.5.1 Q1
hotfix.

---

## Sequence Number Semantics

- Each object has an independent monotonic sequence counter, starting at 0.
- `seq` is a **u32** stored as OSC int32 (wraps at 2³²).
- Engine applies **last-write-wins**: if two packets arrive out-of-order, the one with the
  higher `seq` wins. Packets with `seq` lower than the last applied value are silently dropped.
- The UI increments `seq` per object per send; it does not rely on round-trip acks.

---

## Ambisonics Control Commands (M2-HOA extended)

Added in M2-HOA sprint (A-γ revision). Applies to the Ambisonics rendering path.

### `/sys/ambi_order`

Set the active Ambisonics decode order.

```
/sys/ambi_order  ,iii  <seq> <id> <order>
```

- `order`: integer 1, 2, or 3. Values outside this range are clamped.

### `/sys/ambi_decoder_type`

Select the Ambisonics decoder algorithm. Takes effect on next audio prepare cycle.

```
/sys/ambi_decoder_type  ,iii  <seq> <id> <type>
```

- `type`:
  - `0` = PINV (Tikhonov pseudo-inverse, default, backward-compatible)
  - `1` = MAX_RE (max-rE weighted; Zotter & Frank 2019 eq. 4.49)
  - `2` = ALLRAD (All-Round Ambisonic Decoding via virtual loudspeakers; Zotter 2012)
  - `3` = EPAD (Energy-Preserving Ambi Decoding, Jacobi SVD; Zotter & Frank 2012)
  - `4` = IN_PHASE (in-phase decoder; Daniel 2000 §3.30)
- Unknown values clamp to `0` (PINV).

---

## Schema Versioning

- `schema_version = 1` for all v0 packets.
- Future breaking changes increment this field.
- On core startup, `schema_version` is compiled in as a constant in `proto/schema_version.h`.
- Full schema artifacts (JSON Schema, flatbuffers stub): `proto/`.
