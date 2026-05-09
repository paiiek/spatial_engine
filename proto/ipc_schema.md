# IPC Schema — version 1

## Overview

All messages use OSC 1.1 wire format (big-endian, 4-byte aligned strings).
Every command carries `seq` (uint32, per-object monotone counter) and `id`
(uint32, globally unique per session) as the first two integer arguments.

Engine address: UDP port configurable (default 9001).
Schema version handshake must complete before any object commands are accepted.

---

## Commands (client → engine)

| OSC Address        | Type Tags     | Arguments                                    | Description                                         |
|--------------------|---------------|----------------------------------------------|-----------------------------------------------------|
| `/sys/handshake`   | `,iii`        | seq, id, schema_version(uint16 as int)       | Client declares schema version. Engine replies OK or mismatch. |
| `/sys/algo_swap`   | `,iii`        | seq, id, algo(0=VBAP,1=WFS,2=DBAP)          | Set engine-wide default rendering algorithm.        |
| `/sys/reset`       | `,ii`         | seq, id                                      | Reset all object states to defaults.                |
| `/sys/ambi_order`  | `,iii`        | seq, id, order(1..3)                         | Set Ambisonics decode order (clamped 1..3).         |
| `/sys/ambi_decoder_type` | `,iii`  | seq, id, type(0=PINV,1=MAX_RE,2=ALLRAD,3=EPAD,4=IN_PHASE) | Select Ambisonic decoder algorithm. |
| `/obj/move`        | `,iifff`      | seq, id, obj_id, az_rad, el_rad, dist_m      | Set spatial position of one object.                 |
| `/obj/gain`        | `,iiif`       | seq, id, obj_id, gain                        | Set per-object gain scalar (linear, 0.0–2.0).       |
| `/obj/active`      | `,iiii`       | seq, id, obj_id, active(0=off,1=on)          | Enable or disable an object.                        |
| `/obj/algo`        | `,iiii`       | seq, id, obj_id, algo(0=VBAP,1=WFS,2=DBAP)  | Select rendering algorithm for one object.          |
| `/hb/ping`         | `,iit`        | seq, id, timestamp_ms(timetag)               | Heartbeat ping from publisher (10 Hz).              |
| `/hb/pong`         | `,iit`        | seq, id, timestamp_ms(timetag)               | Heartbeat pong reply from subscriber.               |

---

## Replies (engine → client)

| OSC Address             | Type Tags | Arguments          | Description                                              |
|-------------------------|-----------|--------------------|----------------------------------------------------------|
| `/sys/handshake_ok`     | `,is`     | seq, message       | Handshake accepted. Schema versions match.               |
| `/sys/error`            | `,is`     | seq, message       | Handshake rejected (version mismatch) or generic error.  |
| `/sys/state`            | `,iiffff` | seq, obj_id, az_rad, el_rad, dist_m, gain | Current authoritative object state. Emitted at 10 Hz. |
| `/sys/warning`          | `,is`     | seq, warning_id    | Non-fatal warning. `osc_reorder_burst` = 5+ reorders/s. |
| `/sys/heartbeat_miss`   | `,is`     | seq, message       | 3 consecutive heartbeat misses (300 ms gap).             |

---

## Algorithm Enum

| Value | Name | Description                          |
|-------|------|--------------------------------------|
| 0     | VBAP | Vector Base Amplitude Panning        |
| 1     | WFS  | Wave Field Synthesis                 |
| 2     | DBAP | Distance Based Amplitude Panning     |

---

## Reorder / Sequence Rules

- Each object has an independent `last_applied_seq` counter.
- A command with `seq <= last_applied_seq` is silently dropped; `osc_reordered_drops` counter incremented.
- 5+ drops within a 1-second window triggers `/sys/warning osc_reorder_burst`.
- 3 consecutive missed heartbeat periods (300 ms) triggers `/sys/heartbeat_miss`.
- 10 s without any heartbeat triggers catastrophic-stall warning.

---

## Handshake Flow

```
Client                          Engine
  |  /sys/handshake (ver=1)  →   |
  |  ←  /sys/handshake_ok        |   (version match)
  |  /obj/move ...            →   |   (accepted)

  |  /sys/handshake (ver=99) →   |
  |  ←  /sys/error            |   (version mismatch; connection rejected)
```
