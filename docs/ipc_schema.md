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

## Sequence Number Semantics

- Each object has an independent monotonic sequence counter, starting at 0.
- `seq` is a **u32** stored as OSC int32 (wraps at 2³²).
- Engine applies **last-write-wins**: if two packets arrive out-of-order, the one with the
  higher `seq` wins. Packets with `seq` lower than the last applied value are silently dropped.
- The UI increments `seq` per object per send; it does not rely on round-trip acks.

---

## Schema Versioning

- `schema_version = 1` for all v0 packets.
- Future breaking changes increment this field.
- On core startup, `schema_version` is compiled in as a constant in `proto/schema_version.h`.
- Full schema artifacts (JSON Schema, flatbuffers stub): `proto/`.
