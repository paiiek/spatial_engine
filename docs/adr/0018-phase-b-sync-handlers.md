# ADR 0018 — Phase B Sync Handlers: handshake, heartbeat, transport timetag

- **Status**: Accepted (proposed 2026-05-19; shipped — engine-side handlers wired live + control-tick player-heartbeat staleness watchdog, commit `9311902`)
- **Driver**: adm_player M3 (`adm_player/osc_sync.py`) emits `/sys/handshake`, `/hb/ping`, `/transport/play` with a `,d unix_time` timetag. The engine already has handlers for the three addresses, but the **type-tag contract diverges** in two places. This ADR resolves the contract and pins the engine-side expectations.
- **Related**: ADR 0006 (ADM-OSC v1.0 spec freeze), ADR 0006a (algorithm runtime swap), ADR 0010 (VST3 OSC binding model), ADR 0011 (multi-instance discovery), ADR 0017 (runtime demote telemetry).

---

## 1. Context

### 1.1 What the engine has today

`core/src/ipc/CommandDecoder.cpp` already has *address-level* branches for the three Phase B addresses:

| Address              | Address branch present? | Type tags actually accepted by the parser | Net effect today                            |
|----------------------|--------------------------|-------------------------------------------|---------------------------------------------|
| `/sys/handshake`     | yes                      | `,ii` ✓                                   | sends `/sys/handshake_ok` to reply target   |
| `/hb/ping`           | yes                      | `,h` *parsed but skipped*; `,t` ✓; `,d` ✗ | `HbPing` arrives with `timestamp_ms=0` (always) |
| `/transport/play`    | yes                      | *(no args path only)*; `,d` ✗             | gate flips iff no args present              |
| `/transport/stop`    | yes                      | *(no args)*                               | gate flips                                  |

The decoder's *type-tag parser* (`CommandDecoder.cpp:84-124`) is the binding constraint. The switch handles `i`, `f`, `h`, `t`, `s` only — and `h` is **`p += 8; // skip (not used currently)`** at line 98-100, so the int64 value never lands in `args.u64s`. Anything else, including `d` (OSC double, 8 bytes), hits **`default: return false;`** at line 121-122 and the *entire packet* is rejected — not just the bad argument.

The engine's outbound `HeartbeatPublisher` produces 10 Hz `/hb/ping` to subscribers; the inbound branch above is the subscriber-side hook that today **never sees the producer's timestamp** because of the parser gap. Heartbeat liveness on the engine side has been working "by accident" — the address arrival is what `HeartbeatMonitor` watches, not the payload.

### 1.2 What adm_player sends (M3)

```python
/sys/handshake   ,ii   1, reply_port           # ✓ parser accepts; handshake works
/hb/ping         ,d    unix_time_seconds       # ✗ parser REJECTS entire packet (line 121-122)
/transport/play  ,d    unix_time_seconds       # ✗ parser REJECTS entire packet
/transport/pause (no args)                     # ✗ address branch missing → Unknown
/transport/stop  (no args)                     # ✓ accepted; gate flips
```

This means **all three of M3's new wire formats land in the decoder bin today**. Phase B heartbeat does not actually tick the engine from the player; `/transport/play` from the player does not flip the engine gate. Phase B is, in practice, *not functional* on the engine side — only the handshake survives because it uses `,ii`.

The mismatch rationale on the player side is real: `adm_player/osc_sync.py:19-22` argued for `,d` over `,t` because pythonosc's `t` builder packs into the NTP era (1900-01-01), which has caused interop bugs in other ADM-OSC bridges. We accept that rationale; the fix is on the engine side.

### 1.3 What we need to decide

1. Should the engine accept `,d unix_time_seconds` for `/hb/ping`?
2. Should `/transport/play` carry a scheduled-start timetag (delayed start), or stay edge-triggered (immediate)?
3. Add `/transport/pause`?
4. Reply / handshake_ok routing semantics under Phase B (already partially answered by v0.5.1 Q1 / WM-2 — re-confirm).
5. Heartbeat-miss policy under the Player's 1 Hz cadence (engine internal cadence is 10 Hz / 100 ms — different units).

---

## 2. Decisions

### D-1 — Type-tag parser extension: accept `,d` (double) and wire `,h` (int64) into args

This is two fixes in one place — the type-tag parser at `CommandDecoder.cpp:84-124`:

1. **Wire `,h` into `args.u64s`** so the int64 value actually lands somewhere. Today line 98-100 reads `p += 8; // skip (not used currently)`. Change to:

   ```cpp
   case 'h': // int64
       if (p + 8 > end) return false;
       if (out.n_u64 < OscArgs::MAX_ARGS)
           out.u64s[out.n_u64++] = readU64(p);
       p += 8;
       break;
   ```

2. **Add `,d` as double**, stored in a new `args.doubles[]` slot:

   ```cpp
   case 'd': // float64
       if (p + 8 > end) return false;
       if (out.n_double < OscArgs::MAX_ARGS)
           out.doubles[out.n_double++] = readF64(p);   // bit-cast u64 → double
       p += 8;
       break;
   ```

   `OscArgs` gains a parallel `doubles[MAX_ARGS]` array and a `n_double` counter; `out.n_int = ... = out.n_double = 0;` at line 84 is extended. The `readF64` helper is the symmetric pair of `readU64` (big-endian decode then bit-cast).

With the parser fixed, `/hb/ping` branch becomes:

```cpp
} else if (addr == "/hb/ping") {
    cmd.tag = CommandTag::HbPing;
    PayloadHbPing p;
    if (args.n_u64 > 0) {
        // ,h path (legacy, engine-internal HeartbeatPublisher — already in ms)
        p.timestamp_ms = args.u64s[0];
    } else if (args.n_double > 0) {
        // ,d seconds → ms (Phase B, adm_player M3 path); clamp negatives to 0
        const double s = args.doubles[0];
        p.timestamp_ms = (s > 0.0) ? static_cast<uint64_t>(s * 1000.0) : 0;
    } else {
        p.timestamp_ms = 0;
    }
    cmd.payload = p;
}
```

Rationale:
- **The parser change is the load-bearing fix** — without it, no `,d`-tagged packet reaches the address dispatcher at all. The current "address branch exists" comfort is misleading.
- The `,h` wire-up is strictly additive; it changes a `// skip` into "store and skip". Nothing in the codebase reads `args.u64s` outside the address dispatchers in this same file, so the blast radius is the dispatchers we own.
- **Player keeps the NTP-avoidance argument** intact (`osc_sync.py:19-22`).
- Latency telemetry (how stale is this ping?) becomes valid for both producers.
- Cost: ~12 lines in the parser, ~6 lines in `OscArgs`, ~6 lines in the `/hb/ping` branch, plus 4 ctest cases.

**Out of band side effect**: any *other* incoming OSC packet that previously contained an unsupported type tag was being rejected; the parser change does not silently accept new types — `'d'` is added explicitly, everything else still falls into `default: return false;`. So no security/permissiveness regression.

### D-2 — `/transport/play` stays edge-triggered. The `,d` timetag is **advisory** (logged), not scheduled.

The engine **does not implement a scheduled-start scheduler** in this milestone. Reasons:
- Implementation would require a control-thread queue keyed on `unix_seconds`, plus a tick that fires the gate at the right block boundary. That belongs in M4 PCM IPC (ADR 0019), where sample-accurate timing has a real meaning.
- For Phase B (OSC sync over UDP), the wall-clock jitter dominates anyway: a `,d` timetag arriving 5 ms before the audio it's tagging means a 5 ms quantization error to the next audio block — which is exactly what Phase A already had (`adm_player_integration.md §2.3`).
- The player's M3 design accepts this: it sends `time.time()` at the moment play is pressed, so "scheduled" and "immediate" coincide.

Decoder change (depends on D-1's parser fix to populate `args.doubles`):

```cpp
} else if (addr == "/transport/play") {
    cmd.tag = CommandTag::TransportPlay;
    PayloadTransportPlay p;
    if (args.n_double > 0) {
        p.start_unix_seconds = args.doubles[0];   // advisory; logged at INFO
    }
    cmd.payload = p;
}
```

Without D-1's parser fix the `,d` packet is rejected at line 121-122 — so the address branch is unreachable in the M3 wire format. **D-1 is a hard prerequisite of D-2.**

`PayloadTransportPlay` gains one optional field:

```cpp
struct PayloadTransportPlay {
    double start_unix_seconds = 0.0;  // 0 = unset → immediate (legacy behavior)
};
```

The control-thread handler logs the value if present (one INFO line per play, rate-limited at the same place we rate-limit other transport state changes) and stores `true` to `transport_play_`. No new scheduling.

**Future-proof escape**: if M4 introduces a sample-clock-locked scheduler, the field is already in the wire format.

### D-3 — `/transport/pause` accepted as a synonym of `/transport/stop`

Player sends `/transport/pause` (no args) to mirror its UI; engine has no pause concept distinct from stop (the audio gate is binary). Map:

```cpp
} else if (addr == "/transport/pause") {
    cmd.tag = CommandTag::TransportStop;        // alias
    cmd.payload = PayloadTransportStop{};
}
```

This is intentionally an **alias** at decode time, not a new `CommandTag`. Two reasons:
- We don't want a third state in `transport_play_`.
- If a future engine version learns to "pause + retain playhead", we can promote the tag without changing the wire format.

Player UX (resume from same playhead) is unaffected — the player owns its own playhead; the engine gate only multiplies the audio.

### D-4 — `/sys/handshake_ok` reply routing — re-confirm v0.5.1 Q1 / WM-2

Already shipped, re-stated for cross-referencing:

- If `reply_port > 0`, engine sends `/sys/handshake_ok ,is` to `<sender_ip>:<reply_port>` (same host as the incoming sender; peer validation ensures `sender_ip` is on the allowlist).
- If `reply_port == 0`, engine replies to the recvfrom-captured source port. This is the "legacy" path for ADM-OSC v1.0 clients that don't yet send a reply port.
- **Security gate**: peer-validation enforced upstream in `OSCBackend.cpp` — an unauthenticated `(ip, port)` cannot redirect telemetry to a victim via a forged handshake. The existing `osc_security_peer_validation` ctest covers the gate. We add **one** new case asserting the same gate applies when `,d`-format heartbeats land before a handshake (see §3).

### D-5 — Heartbeat-miss policy for Phase B 1 Hz cadence

`HeartbeatMonitor.h` was sized for the engine's own 10 Hz publisher (100 ms period; 3 misses = 300 ms). adm_player sends at **1 Hz**, so 300 ms of latency would falsely trip every cycle.

Resolution: **the monitor's miss-window is the subscriber's responsibility**, not the engine's. Restate:

- The engine **does not** run `HeartbeatMonitor` on `/hb/ping` *inbound* from the player. The engine itself is the audio source-of-truth; it does not need to detect the player's death (the player's death just means OSC stops; the engine continues to render silence per object).
- The engine **does** route `/hb/ping ,d` into a low-priority "session liveness" timestamp atomic (last seen wall-clock), exposed via the `SpatialEngine::lastPlayerPingUnixMs()` accessor for whatever external layer surfaces it (core itself emits no `/sys/state` packet — see implementation note below). Drift > 5× expected period (5 s for the 1 Hz path) emits one `/sys/warning ,iis 0 0 "player_heartbeat_stale" "<seconds>"` at most once per 30 s.

That makes the engine's behaviour:
- Player crash → engine keeps rendering current object positions until something else changes them. Audio doesn't glitch. UI gets a single warning.
- Player resume → next `/hb/ping` clears the staleness latch.

Implementation footprint: a `std::atomic<int64_t> last_player_ping_unix_ms_` on `SpatialEngine`, ticked from the control-thread drain when `HbPing` arrives with source = "external" (we know it's external because it carries `,d`, vs. the engine's internal publisher which uses `,h`). The staleness evaluation itself, `SpatialEngine::checkPlayerHeartbeatStale(now_unix_ms)`, runs on a periodic control/IO-thread timer (it detects the *absence* of pings, so it cannot be triggered by ping arrival). The standalone wires it from its ~1 Hz run-loop tick via `spe::bin::servicePlayerStaleWatchdog()` (`core/src/bin/PlayerStaleWatchdog.h`), passing wall-clock unix ms.

> **Implementation note (accuracy):** core emits **no** `/sys/state` packet — there is no `/sys/state` writer anywhere in `core/src/`. The "last seen" value is exposed via the `SpatialEngine::lastPlayerPingUnixMs()` accessor for any external layer that wants to surface it; the staleness *signal* is delivered solely via `/sys/warning` (now wired into the periodic control-thread tick). The earlier `/sys/state` snapshot field described above is therefore not part of core.

We **do not** add a new outbound address — the staleness signal piggybacks on the existing `/sys/warning` channel (ADR 0017 telemetry rules).

---

## 3. Tests (gating the implementation)

Two test files. Both run under the existing `core_unit` ctest matrix.

### 3a. Parser-level (`test_osc_type_tag_parser.cpp`) — D-1 prerequisite

Today the parser at `CommandDecoder.cpp:84-124` does NOT have these cases. They MUST exist before the address-level branches can be exercised.

P1. **`tag_d_double_populates_args_doubles`** — `,d 1.5` → `args.n_double == 1 && args.doubles[0] == 1.5`.
P2. **`tag_h_int64_populates_args_u64s`** — `,h 42` → `args.n_u64 == 1 && args.u64s[0] == 42` (today this is silently skipped; this test pins the new behaviour).
P3. **`tag_d_mixed_with_i`** — `,id 7, 3.25` → both lanes populated.
P4. **`tag_unknown_still_rejects`** — `,z` → parse returns false (no permissiveness regression).

### 3b. Address-level (`test_phase_b_sync_handlers.cpp`)

1. **`hb_ping_h_decodes_to_ms`** — `,h 1700000000000` → `payload.timestamp_ms == 1700000000000`.
2. **`hb_ping_d_decodes_seconds_to_ms`** — `,d 1700000000.5` → `payload.timestamp_ms == 1700000000500`.
3. **`hb_ping_d_negative_clamped_to_zero`** — `,d -1.0` → `payload.timestamp_ms == 0`.
4. **`hb_ping_no_args_zero`** — `,` → `payload.timestamp_ms == 0`.
5. **`transport_play_no_args_immediate`** — `,` → `payload.start_unix_seconds == 0.0`, gate flips true.
6. **`transport_play_d_arg_stored_advisory`** — `,d 1700000123.456` → `payload.start_unix_seconds == 1700000123.456`, gate flips true (no scheduling).
7. **`transport_pause_aliases_stop`** — `/transport/pause` → `CommandTag::TransportStop`, gate flips false.
8. **`handshake_with_reply_port_routes_ok`** — pre-existing test re-run as guard.
9. **`handshake_d_format_heartbeat_rejected_until_handshake`** — peer-validation test extension: send `/hb/ping ,d` from unauth `(ip, port)` → silently dropped (no reply, no outbound).
10. **`player_heartbeat_stale_warning_once`** — feed `/hb/ping ,d` then withhold for 6 s (deterministic mocked clock); expect exactly one `/sys/warning player_heartbeat_stale` and no second one within 30 s; resume → next ping clears latch (verified via the `lastPlayerPingUnixMs()` accessor). Companion case **11** drives the production periodic entry point (`spe::bin::servicePlayerStaleWatchdog()`) with an injected clock to prove the watchdog fires from the control-thread tick, not only from a direct call.

All 14 cases (P1–P4 + 1–10) must pass before this ADR's code lands.

---

## 4. Implementation steps

1. **(load-bearing)** `core/src/ipc/CommandDecoder.{cpp,h}` parser: add `case 'd':` to the type-tag switch; wire `case 'h':` into `args.u64s`; add `doubles[MAX_ARGS]` + `n_double` to `OscArgs`; add `readF64()` helper. Land tests P1–P4 in the same PR.
2. `core/src/ipc/Command.h`: add `start_unix_seconds` field to `PayloadTransportPlay`. Keep `PayloadHbPing` as-is (already `uint64_t timestamp_ms`).
3. `core/src/ipc/CommandDecoder.cpp` address dispatcher: extend `/hb/ping`, `/transport/play` branches per D-1/D-2; add `/transport/pause` alias per D-3.
4. `core/src/core/SpatialEngine.cpp`/`.h`: add `std::atomic<int64_t> last_player_ping_unix_ms_`; tick on external-source `HbPing`; emit `/sys/warning player_heartbeat_stale` once per 30 s when stale; clear on resume.
5. `docs/ipc_schema.md`: document `/hb/ping ,d`, `/transport/play ,d` (advisory), `/transport/pause` (alias), and the `player_heartbeat_stale` warning code.
6. Tests above (P1–P4 with PR1; 1–10 with PR3/4).
7. Soak harness: add `tests/soak_harness/test_phase_b_player_handshake.py` that drives a real adm_player `SyncEmitter` against a local engine and asserts the round-trip (handshake_ok received, gate flips on play/stop, heartbeat keeps `/sys/state.last_player_ping_unix_ms` fresh).

Estimated patch size: ~140 LoC core/IPC (12 parser + 6 OscArgs + 6 hb/ping + ~30 transport/state + housekeeping), ~110 LoC tests (parser + handlers), ~30 LoC schema docs.

---

## 5. Non-goals (out of scope here)

- **Sample-accurate scheduled start**: deferred to ADR 0019 (M4 PCM IPC). No control-thread scheduler is added.
- **LTC chase**: orthogonal — `/sys/ltc_chase` already exists and is unchanged.
- **Bi-directional transport mirroring** (engine → player): not added. Engine remains the audio source-of-truth; if the engine's own state changes via VST3/scene-load, it does **not** echo back to the player. Echo support belongs in M5 Recorder integration (ADR 0020), not here.
- **Pause-with-playhead semantics**: see D-3 — the engine doesn't own a playhead.

---

## 6. Migration / backwards compatibility

- Engine-internal `HeartbeatPublisher` (10 Hz, `,h ms`) is unchanged. Any existing subscriber still receives `,h`.
- VST3 plugin and WebGUI clients that send no `/hb/ping` are unaffected.
- adm_player M3 wire format works as-is on day 1 with the decoder patch — no player-side change required.
- `last_player_ping_unix_ms` is exposed via the `SpatialEngine::lastPlayerPingUnixMs()` accessor for any external layer that wants to surface it. Core emits no `/sys/state` packet, so there is no state-schema change here.

---

## 7. Open follow-ups (not blocking this ADR)

- **F-1**: If the demo eventually wants a click-track sync test (D-2's "future-proof escape"), promote `start_unix_seconds` from advisory to scheduled. Requires sample-clock alignment — natural fit for M4.
- **F-2**: Add a `/sys/handshake_ok ,is` payload field for `engine_schema_version` (today the reply is `,is` with a message string; a numeric field would let the player auto-detect feature flags). Coordinate with `ProtocolVersion::CURRENT_SCHEMA_VERSION` bump.
- **F-3**: Decide whether `player_heartbeat_stale` should also force-mute objects whose last position update came from the same player (defensive). Default no — current behavior (keep last position) is what film/atmos workflows expect.
