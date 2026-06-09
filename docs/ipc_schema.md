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

#### `/obj/input`

A3 (v0.9 Lane 6) — per-object input→object routing. Route an input channel into one
object with a per-route linear gain (input trim), enabling channel remap and many-to-one
fan-out. Only meaningful when the engine runs `--object-source input`. A native `/obj/*`
command (alongside `/obj/gain`, `/obj/dsp`), distinct from the ADM `/adm/obj/n/*` family.

```
/obj/input  ,iif
            obj_id  src_ch  gain
```

On the wire, native `/obj/*` commands carry the `schema_version`+`seq` int prefix, so the
full datagram is `,iiiif schema_version seq obj_id src_ch gain` (same convention as
`/obj/dsp`).

| Arg | Type | Notes |
|-----|------|-------|
| `obj_id` | i (int32) | Target object index `[0, MAX_OBJECTS)`; out-of-range dropped at the drain. |
| `src_ch` | i (int32) | Input channel to feed into the object. **`-1`** = default 1:1 (object `i` ← input channel `i`) and the reset value. `src_ch >= -1` required (else the packet is rejected as Unknown). |
| `gain`   | f (float32) | Linear input trim, **unclamped** (negative = phase invert, matching `/obj/gain`). Default `1.0` = unity. |

Semantics:

- **Block-stepped gain** — `gain` is applied as a per-block scalar at the dry-source copy
  stage (it changes only at block boundaries when the command FIFO drains), composing
  multiplicatively with the per-object `gain_lin`. Consistent with `gain_lin`'s
  `ramp_samples=0` step; no new click class.
- **Out-of-range `src_ch`** (>= the live `input_channel_count`) falls through to the
  object's internal **sine** test tone and silently ignores `gain` — the legacy fallback.
- **Default route is byte-identical** to the previous 1:1 behaviour (`in[n]*1.0f == in[n]`).
  `/sys/reset` clears every object's route to the default.
- **Deferred:** live per-change echo (F-A3-echo) and scene-JSON persistence (F-A3-persist).

The full-state resync dump (`/sys/state_request`) re-emits a non-default route as the
**outbound** echo `/adm/obj/N/input ,if <src_ch:int> <gain:float>` (type-tag `,if` ⇒ int
slot = `src_ch`, float slot = `gain`), so a client reconciles EXACTLY to the engine state.

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

### Transport Commands (Phase B — ADR 0018)

The engine audio gate is **binary** (playing / silent) and **edge-triggered**:
each transport command flips the gate immediately on decode. There is no
scheduled-start scheduler in this milestone (deferred to ADR 0019 / M4 PCM IPC).

#### `/transport/play`

```
/transport/play              # no args → immediate
/transport/play  ,d  start_unix_seconds   # advisory timetag (adm_player M3)
```

| Arg | Type | Notes |
|-----|------|-------|
| `start_unix_seconds` | d | **Optional, advisory only.** When present, logged but **not** used to schedule a delayed start — the gate still flips immediately (edge-triggered). `0`/absent = immediate (legacy). The field is preserved on the wire for a future M4 sample-clock scheduler (ADR 0018 D-2). Requires the type-tag parser's `,d` support (ADR 0018 D-1). |

#### `/transport/stop`

```
/transport/stop              # no args → gate silent
```

#### `/transport/pause`

```
/transport/pause             # no args → ALIAS of /transport/stop
```

Decoded as `/transport/stop` at decode time (ADR 0018 D-3). The engine has no
pause state distinct from stop (it owns no playhead — the player does). Mirrors
the player's UI; intentionally not a new command tag so the wire format can
later promote to a true pause without a breaking change.

#### `/hb/ping`

Heartbeat liveness. Accepted from two producers, distinguished by type tag:

```
/hb/ping  ,h  timestamp_ms          # engine-internal HeartbeatPublisher (10 Hz)
/hb/ping  ,t  ntp_timetag           # OSC timetag form (also internal)
/hb/ping  ,d  unix_time_seconds     # EXTERNAL adm_player (M3, ~1 Hz)
/hb/ping                            # no args → timestamp 0
```

| Arg | Type | Notes |
|-----|------|-------|
| `timestamp_ms` | h | int64 wall-clock ms (engine-internal publisher). |
| `ntp_timetag`  | t | OSC timetag (engine-internal). |
| `unix_time_seconds` | d | float64 unix seconds (external player). Converted to ms internally; negatives clamp to 0. The `,d` tag flags the ping as **external**, which is what ticks the player-liveness timestamp (ADR 0018 D-5). |

Only the `,d` (external) form ticks `last_player_ping_unix_ms` (see `/sys/state`
below) and feeds the `player_heartbeat_stale` watchdog. The internal `,h`/`,t`
publisher loopback is excluded so it can never mask a dead player.

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

String-coded warnings (emitted with the `type` int = 0, code carried in the
`details`-string position — ADR 0018 / ADR 0017 telemetry style):

| Code string | Emitted as | Meaning |
|-------------|------------|---------|
| `player_heartbeat_stale` | `/sys/warning ,iis 0 0 "player_heartbeat_stale" "<seconds>"` | The external adm_player's `/hb/ping ,d` has not been seen for more than 5 s (5× the player's 1 Hz cadence). The `<seconds>` string is the integer age of the last external ping. Emitted **at most once per 30 s** while stale; the latch clears on the next external ping (resume re-arms). Advisory only — the engine keeps rendering the last object positions; audio does not glitch (ADR 0018 D-5). |
| `shm_underrun` | `/sys/warning ,iis 0 0 "shm_underrun" "<xruns>"` | The `--input-backend shm:` ring had no fresh block when the consumer polled, so a silent block was substituted. `<xruns>` is the cumulative underrun (silent-block) total. Emitted **at most once per second** (the emitter owns this 1/s gate; the RT counter has no source rate-limit). ADR 0019 §6. |
| `shm_producer_stale` | `/sys/warning ,iis 0 0 "shm_producer_stale" "<seconds>"` | The shm ring's `producer_heartbeat_ms` (wall-clock ms) has not advanced for more than 100 ms — the Python adm_player producer (PR5) has stalled or died. `<seconds>` is the integer heartbeat age (`age_ms / 1000`, the same shape as `player_heartbeat_stale`). Emitted **at most once per 30 s** while stale (the `poll_diagnostics` source rate-limit). ADR 0019 §6. |
| `shm_producer_pacing` | `/sys/warning ,iis 0 0 "shm_producer_pacing" "pacing_drift"` | Successive producer block presentation timestamps (`producer_meta_block_pts_ns`) drifted by more than one block period — the producer is not keeping real-time cadence. The detail string is the fixed marker `pacing_drift`. Emitted **at most once per 5 s** (the `poll_diagnostics` source rate-limit). ADR 0019 §6. |
| `shm_attached_no_data` | `/sys/warning ,iis 0 0 "shm_attached_no_data" "channels=<N>"` | The shm ring was attached but no producer has written any frames yet (`write_idx == read_idx == 0`). `<N>` is the ring channel count. Emitted **once on attach**. ADR 0019 §6. |

**Retry-on-no-peer (ADR 0019 PR4 / CR-2).** Unlike `player_heartbeat_stale`
(fire-and-forget), the four `shm_*` warning edges above are **held** until a
successful `sendReply` — if a warning edge fires before any OSC client has
connected, its edge is re-attempted on each subsequent diagnostics tick rather
than dropped (the same retry-on-no-peer latch the `/sys/state ,s` form uses).
This guarantees a producer that dies/stalls before a UI or soak client connects
still surfaces the warning once a peer attaches. The four codes are driven by a
control-thread, shm-gated 1 Hz diagnostics tick in the standalone engine loop;
they are never emitted on the audio thread.

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
| `reset_demote_accepted` | The `/sys/binaural_reset_demote ,i 1` user hatch was accepted: all 8 demote-state atomics were reset, B2 is re-armed. The cooldown counter was also reset (the next reset will be accepted after 60 s). | IO thread via `BinauralMonitor::resetRuntimeDemoteFromUser()` on Accept. | v0.7 |
| `reset_demote_cooldown_active` | The `/sys/binaural_reset_demote ,i 1` call was rejected because the last accepted reset happened less than 60 s ago. Rate-limited to **at most once per cooldown window** to prevent outbound-ring saturation under rapid reset spam (Critic §D.7). | IO thread via `BinauralMonitor::resetRuntimeDemoteFromUser()` on CooldownActive, first rejection only per window. | v0.7 |

Future codes will be appended here; consumers must treat unknown
codes as "log + ignore" rather than error.

### `/sys/binaural_reset_demote` — user-controlled demote reset hatch (v0.7 inbound)

Allows a connected client to re-arm the B2 runtime auto-demote state after it has fired, without requiring a host restart or `prepareToPlay` cycle.

```
/sys/binaural_reset_demote  ,i  <enable>
```

| Arg | Type | Notes |
|-----|------|-------|
| `enable` | i | `1` = attempt reset; `0` = no-op (reserved for future toggle use) |

**Behavior:**
- If `enable == 0`: silently ignored.
- If the monitor is **not currently demoted** (`runtime_demoted_ == false`): returns `NotDemoted` — no atomics written, no outbound warning.
- If within the **60-second cooldown window** (last accepted reset < 60 s ago): returns `CooldownActive`. At most one `reset_demote_cooldown_active` warning is emitted per cooldown window (rate-limited to prevent DOS via SPSC ring saturation).
- If outside the cooldown window (or first ever call): performs the **8-atomic AM-1 reset** and emits `reset_demote_accepted`.

**AS-5 process-lifetime cooldown semantic:** The cooldown counter (`runtime_demote_last_reset_ns_`) is **not** reset by `prepareToPlay` / `initialize()`. It persists for the lifetime of the process. Closing and re-opening a project starts a new process, which resets the counter naturally. This is intentional — it prevents a rapid close/reopen cycle from bypassing the cooldown. See `CH7_BINAURAL.md §7.5.4`.

**Peer-validation:** This verb uses the same OSC peer-validation infra as all other `/sys/` verbs. An unauthenticated sender (no prior `/sys/handshake`) can submit the packet, but no reply is routed back and no outbound drops are incurred (the existing `osc_security_peer_validation` ctest covers this).

---

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

#### `last_player_ping_unix_ms` field (Phase B — ADR 0018 D-5, additive)

The `/sys/state` snapshot gains a `last_player_ping_unix_ms` integer field
carrying the wall-clock (unix ms) of the most recent **external** player
`/hb/ping ,d`. `0` means no external ping has been seen this session.

- Backed by `SpatialEngine::lastPlayerPingUnixMs()` (a `std::atomic<int64_t>`
  ticked from the control thread only — never the audio thread). A diagnostic
  consumer reads it to confirm the player→engine heartbeat is fresh.
- Only the `,d` (external) heartbeat updates it; the engine's own 10 Hz
  `,h` publisher loopback does not.
- **State schema bump: not required** — this is an additive integer with a
  default of `0`; consumers ignore unknown keys per ADR 0014.

#### shm producer/consumer state keys (ADR 0019 PR4 — additive)

When the engine runs with `--input-backend shm:`, the control-thread 1 Hz
diagnostics tick emits three additional `,s "key=value"` `/sys/state` packets,
each in the same dual-tag additive form as `fallback_mode` above (one
`key=value` per packet, emitted **on change** with retry-on-no-peer, NOT
periodic). A consumer that filters by typetag is unaffected.

```
/sys/state  ,s  "shm_producer_alive=0" | "shm_producer_alive=1"
/sys/state  ,s  "shm_producer_state=0..3"
/sys/state  ,s  "shm_consumer_locked=0" | "shm_consumer_locked=1"
```

| Key | Values | Meaning |
|-----|--------|---------|
| `shm_producer_alive` | `0` / `1` | `1` while the producer's `producer_heartbeat_ms` (wall-clock ms) is fresh (age ≤ 100 ms); `0` when stale. Derived from the SAME 100 ms threshold as the `shm_producer_stale` warning, so `shm_producer_alive=0` co-emits with that warning edge (ADR 0019 §6). |
| `shm_producer_state` | `0`=Idle, `1`=Streaming, `2`=Draining, `3`=Closed | The producer lifecycle state from the ring header's `producer_state` (ADR 0019 §2.3). Sampled once per tick; sub-tick transitions (e.g. `Draining→Closed` within 1 s) are coalesced (ADR 0019 PR4 §MJ-2). |
| `shm_consumer_locked` | `0` / `1` | `1` while this engine holds the SPSC consumer-attach lock (`kConsumerLockOffset` non-zero); `0` otherwise. Boolean by the field name; the holder PID is not exposed. |

- Emitted on-change with a per-key retry-on-no-peer latch (the latch stays
  armed until a successful `sendReply`), exactly as the `fallback_mode`
  snapshot. The initial snapshot of all three keys is emitted on the first
  tick (seed-and-emit) so a late-connecting consumer learns current state.
- **State schema bump: not required** — additive `,s` keys; consumers ignore
  unknown keys per ADR 0014. No `RingHeader`/wire change.

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

---

## Outbound — Diagnostic Telemetry (v0.7+)

### `/sys/binaural_diag`

Emitted by the IO-thread heartbeat drain **once per runtime auto-demote event**
(NOT periodic). Immediately follows `/sys/binaural_warning ,s "ambivs_demoted_runtime"`
on the same drain pass; wire order is source-deterministic (SPSC ring FIFO).

```
/sys/binaural_diag  ,iif  <block_size>  <sample_rate_int>  <observed_max_ratio>
```

| Arg | Type | Notes |
|-----|------|-------|
| `block_size` | i (int32) | Audio block size in samples at the moment of demote |
| `sample_rate_int` | i (int32) | Sample rate in Hz (integer cast) at the moment of demote |
| `observed_max_ratio` | f (float32) | Highest (elapsed_ns / deadline_ns) ratio observed across all over-budget blocks in the strike run; 1.0 = exactly at deadline |

**Emission semantics:**
- One packet per demote event. After a `/sys/binaural_reset_demote ,i 1` accept + re-demote
  cycle, one more packet will be emitted on the next demote.
- The three fields are snapshotted at the audio-thread CAS-success demote latch — they reflect
  the session context at the exact moment of demote, not the post-demote context.
- A D-S1 reset (`/sys/binaural_reset_demote ,i 1` → `reset_demote_accepted`) clears the
  snapshot; the next demote event produces a fresh packet.

**AS-2 slow-degradation limitation:**
The slow-degradation pattern (ratio creeping up over hours, never crossing the demote
threshold) is NOT detected by the event-driven `/sys/binaural_diag` channel.
v0.8 may add a pre-demote-window summary channel (B-3) once real telemetry shape is
observed in production. Until then, consumers should treat absence of `/sys/binaural_diag`
as "no demote fired", not "ratio is healthy".

---

## Schema Versioning

- `schema_version = 1` for all v0 packets.
- Future breaking changes increment this field.
- On core startup, `schema_version` is compiled in as a constant in `proto/schema_version.h`.
- Full schema artifacts (JSON Schema, flatbuffers stub): `proto/`.
