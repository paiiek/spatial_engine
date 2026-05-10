# Phase C3 — ADM-OSC v1.0 Compatibility Layer

**Status**: Round-2 patched (RALPLAN-DR short mode, R2 amendments applied; Round-3 skip per Critic R1 verdict; ready for autopilot pending user)
**Date**: 2026-05-09 (R1) → 2026-05-10 (R2 patch)
**Owner**: Planner agent
**ETA**: 11 working days (was 10.5d in R1; +0.5d for R2 amendments — within 2–3 week banner)
**Related**: `docs/adr/0003-ipc-osc-udp.md` (esp. lines 70-77 falsifier), `docs/adr/vid2spatial_osc_contract.md`,
`bridge/spike_vid2spatial_osc.py`, `core/src/ipc/CommandDecoder.cpp:317-373`,
`core/src/ipc/OSCBackend.{h,cpp}`, `core/src/core/SpatialEngine.cpp:239-260` (obj_cache_ direct path),
`core/src/ipc/StateModel.cpp:37,54,69,84` (seq-drop logic — NOT for ADM), `core/src/core/Constants.h:12` (`MAX_OBJECTS=64`)

---

## 0. Context — what already exists

The repo is **not** starting from zero. Inventory:

| Surface                              | File                                                              | Status                                                         |
| ------------------------------------ | ----------------------------------------------------------------- | -------------------------------------------------------------- |
| ADM-OSC receive subset (5 addresses) | `core/src/ipc/CommandDecoder.cpp:317-373`                         | `azim`, `elev`, `dist`, `aed`, `gain`, `mute` decoded → Command |
| ADM-OSC tags                         | `core/src/ipc/Command.h:31, 121-124`                              | `ObjMute = 0x05` already present                                |
| Receive-path tests                   | `core/tests/core_unit/test_p_adm_osc.cpp` (8 cases)               | green, including out-of-range and unknown-sub fall-through      |
| UDP listener (JUCE-free)             | `core/src/ipc/OSCBackend.cpp:51-93`                               | POSIX `recv()` + 100 ms timeout, port from `--osc-port` (`bin/spatial_engine_core.cpp:150`) |
| `vid2spatial → /adm/...` translator  | `bridge/spike_vid2spatial_osc.py`, `bridge/vid2spatial_osc.py:7,254` | port 9000→9100, prefix rewrite, IIR + 60 Hz rate-limit          |
| UI / WebGUI helpers                  | `ui/spatial_engine_ui/ipc/protocol.py`, `ui/webgui/server.py:165` | already emit `/adm/obj/{n}/{aed,gain}`                          |
| **Encode (send) path for `/adm/*`**  | `core/src/ipc/CommandDecoder.cpp:422-602`                         | **MISSING** — `encode()` only emits the legacy `/obj/*` schema |
| ADM-OSC config (subscribed objects)  | —                                                                 | **MISSING** — no per-object filter / domain awareness          |
| `/adm/scene/*`, `/adm/config/*`      | —                                                                 | **MISSING**                                                     |
| VST3 plugin OSC integration          | `vst3/SpatialEngineProcessor.cpp` (581 LOC), `Controller.cpp` (599) | **NOT WIRED** — no OSC at all in the plugin                    |

So Phase C3 is **"close the v1.0 gap"**, not "design from scratch". This framing is critical for scope and ETA.

---

## 1. Principles (3–5)

1. **ADM-OSC v1.0 spec verbatim.** Address, type-tag, units, ranges, defaults match
   `immersive-audio-live.github.io/ADM-OSC` exactly. We never invent dialects on the
   `/adm/...` namespace; if we extend, it lives under a different prefix
   (e.g. `/spe/...`).
2. **Backward-compat with the existing `/obj/...` and `/sys/...` schemas.** Every test
   in `core/tests/core_unit/` and `ui/webgui/tests/` that passes today must still pass
   on the same port (9100) without any sender change.
3. **RT-safe.** Audio thread alloc==0 (existing invariant from
   `docs/adr/0003-ipc-osc-udp.md`). All ADM parsing runs on the `OSCBackend` UDP
   thread (`core/src/ipc/OSCBackend.cpp:75-83`); no STL allocation reaches the audio
   callback.
4. **Linux-first, JUCE-free preserved.** Only the existing `SPE_HAVE_JUCE=0` path is
   touched. No new JUCE includes, no new third-party deps.
5. **Layered on the existing `vid2spatial` contract.** The bridge keeps owning
   coordinate sign flips (`docs/adr/vid2spatial_osc_contract.md`); `core/` only
   accepts ADM-OSC standard semantics. No "magic" coordinate transforms inside the
   decoder.

---

## 2. Decision Drivers (top 3)

1. **Korean live-venue console compatibility** — DiGiCo / Avid / Yamaha consoles ship
   ADM-OSC clients; first paying contract requires ingesting their `/adm/obj/N/aed`
   stream over UDP without an adapter.
2. **ADM-OSC ecosystem parity** — Round-trip with L-ISA Controller, Spat Revolution,
   d&b Soundscape. We must both *receive* their output and *send* compatible state
   updates back so they can mirror our scene.
3. **Self-contained implementation** — Zero new deps. No `liblo`, no
   `oscpack`, no JUCE. We extend the existing `CommandDecoder` and `OSCBackend` only.

---

## 3. Viable Options (≥2 per decision point)

### Decision A — Where does the ADM-OSC handler live?

| Option | Description | Pros | Cons |
| ------ | ----------- | ---- | ---- |
| **A-α (separate handler)** | New `core/src/ipc/AdmOscHandler.{h,cpp}` registered alongside `OSCBackend`, owns its own dispatch table | Clean separation, easy spec-version pinning, easy to unit-test in isolation | Two listeners on port 9100 = port conflict; or split ports = breaks "single OSC schema" of ADR-0003 |
| **A-β (extend `CommandDecoder`)** ★ | Add ADM v1.0 addresses to the existing `if/else` chain in `CommandDecoder::buildCommand` (already partially done at line 317) | Re-uses the proven dispatcher, single port (9100), preserves ADR-0003 "single OSC schema" invariant, ADM and legacy commands flow through the same `Command` variant | The dispatcher grows; mitigation = extract a `decodeAdm()` helper inside the same file |
| **A-γ (bridge-only)** | Keep all ADM-OSC translation in `bridge/spike_vid2spatial_osc.py` | No core changes | Defeats the purpose: external consoles cannot reach core directly; bridge becomes a single point of failure; not "core spatial engine speaks ADM-OSC" |

**Recommendation: A-β.** Already partially adopted (`CommandDecoder.cpp:317-373`). Refactor: extract the ADM block into `decodeAdmAddress(addr, args, cmd)` inside the same translation unit to keep the file scannable. Reject A-α (port conflict + ADR violation), reject A-γ (does not satisfy Driver 1).

### Decision B — Wire-format alias strategy

| Option | Description | Pros | Cons |
| ------ | ----------- | ---- | ---- |
| **B-α (verbatim, both prefixes coexist)** ★ | `/adm/obj/N/...` is decoded per ADM v1.0 (degrees, normalised dist, LEFT-handed az). `/obj/...` continues with internal radians/metres. No alias table | Spec compliance, no surprise translations, both schemas remain testable independently | Senders must pick one schema; mixing on the same object can cause "last write wins" surprises |
| **B-β (alias map)** | Maintain a `std::unordered_map<std::string,std::string>` of ADM↔internal address aliases at start-up | Lets old clients hit `/adm/...` and get translated to `/obj/...` for free | Allocation in hot path or at start (acceptable), but obscures provenance: a `/obj/move` consumer cannot tell whether the source was `/adm/...` or `/obj/...`; complicates debugging |

**Recommendation: B-α.** ADR-0003 already mandates "single Command schema"; both wire prefixes funnel into the same `Command` variant. Document the rule in a new ADR appendix: *"A given object should be driven by one prefix at a time; mixing is permitted but last-arrived wins."*

### Decision C — VST3 plugin ADM-OSC handling

| Option | Description | Pros | Cons |
| ------ | ----------- | ---- | ---- |
| **C-α (VST3 + standalone both bind UDP)** | `SpatialEngineProcessor.cpp` opens a UDP socket on a per-instance port (auto-assigned) | Plugin instances become independent OSC endpoints | Two plugin instances in the same DAW collide on a known port; auto-assign requires UI to surface the port; some hosts (Pro Tools) restrict thread/socket lifetime; out of v0 scope |
| **C-β (standalone only)** ★ | Standalone `spatial_engine_core` binds UDP on 9100 (already does via `bin/spatial_engine_core.cpp:150`); VST3 plugin defers OSC entirely to host automation | Zero risk to plugin certification; honours JUCE-free + DAW thread rules; ADM-OSC senders target the standalone today | Plugin users in DAWs cannot drive automation from a console without going through the standalone bridge; acceptable for v0 / first contract |
| **C-γ (deferral)** | Defer the entire VST3 ADM-OSC decision to Phase C4 | Keeps Phase C3 surface small, lets us ship console compat in 2–3 weeks | Leaves a documented gap; OK because Driver 1 is satisfied by C-β |

**Recommendation: C-β + write-down for C-γ.** Ship standalone ADM-OSC in Phase C3, file Phase C4 issue "VST3 plugin ADM-OSC strategy" with the C-α/C-γ trade-off. Plugin users have a workable path (run standalone alongside DAW) for now.

---

## 4. Pre-mortem (3 failure scenarios)

### Scenario 1 — "Console talks `/adm/obj/N/xyz` but our decoder only knows `/aed`"

**What goes wrong**: ADM-OSC v1.0 also defines `/adm/obj/N/x`, `/adm/obj/N/y`,
`/adm/obj/N/z` (Cartesian) and `/adm/obj/N/xyz` (combined). A DiGiCo or Lawo console
sends `/xyz`; our decoder hits the `else { makeUnknown(); }` at
`CommandDecoder.cpp:368-370` and silently rejects.

**Mitigation**:

* Sub-task **S1** explicitly enumerates every `/adm/obj/N/*` address in the v1.0 spec
  before coding (no "we'll add it later" gaps).
* Acceptance criterion: a fixture file `core/tests/fixtures/adm_osc_v1_compliance.csv`
  lists every spec address with sample args and expected `Command`. The test loops
  the fixture and asserts no row produces `Unknown` unless explicitly marked
  "unsupported in v0".
* `OSCBackend::injectPacket` (`core/src/ipc/OSCBackend.cpp:32-37, 110-115`)
  increments `decoder().rejectCount()` on Unknown; standalone `spatial_engine_core`
  logs once-per-second the reject count so unsupported addresses become visible
  during integration.

### Scenario 2 — "Console-specific subset divergence"

**What goes wrong**: L-Acoustics L-ISA, Flux:: Spat Revolution, and d&b Soundscape
each implement *different* slices of ADM-OSC v1.0. Spat Revolution sends `/aed`
with normalised distance `[0..1]`; L-ISA sends absolute metres; d&b sends
spherical. We pass the L-ISA test but fail Spat Revolution because our
`/aed dist` arg is multiplied by `MAX_DIST=20.0` (`CommandDecoder.cpp:321, 348, 353`).

**Mitigation**:

* **No silent unit conversion**. The spec's official `dist` semantic is normalised
  `[0..1]` where `1.0 = far`; we treat that as the contract and document it.
* Sub-task **S5** ships **3 console capture fixtures** (`.osc.bin` files captured
  from public demo videos / vendor SDKs) and the test must round-trip all three.
* If a vendor diverges from the spec, document the deviation in a new ADR
  `docs/adr/0006-adm-osc-vendor-quirks.md` and add a vendor-toggle flag at the
  bridge (`bridge/`) layer, **not** the core decoder.

### Scenario 3 — "RT thread back-pressure from OSC parsing under flood"

**What goes wrong**: A console sprays `/adm/obj/N/aed` at 1 kHz × 64 objects = 64
kHz aggregate. The UDP recv thread (`core/src/ipc/OSCBackend.cpp:75-83`) keeps up
in user-space, but the SPSC FIFO between control thread and audio thread overflows;
audio thread starts seeing dropped commands.

**Mitigation**:

* `OSCBackend` already has rate-limiting at the bridge (`bridge/spike_vid2spatial_osc.py:81-91`,
  60 Hz cap). For direct console traffic, add a per-object coalescer in the
  control thread sink (the `CommandSink` lambda in `OSCBackend.h:20-23`): if a
  newer `ObjMove` for the same `obj_id` arrives before the previous one is drained,
  drop the older. This must live **outside** the audio thread.
* Sub-task **S5** runs a **soak test**: 1 kHz × 64 obj for 60 s, asserting:
  - audio thread alloc count == 0 (existing harness)
  - dropped-command count is reported in `/sys/state` for observability
  - p99 input-to-render latency stays below the ADR-0003 falsifier (3.0 ms IPC stages)

---

## 5. Implementation Steps

### S1 — Spec freeze and gap analysis  (1 day)

* **Deliverable**: `docs/adr/0006-adm-osc-v1-subset.md` enumerating every supported
  v1.0 address with type-tag, units, range, default, and "in v0?" yes/no/deferred.
* Reference: `https://immersive-audio-live.github.io/ADM-OSC/` and the AES France
  paper (cited in this plan's preamble).
* Build a CSV fixture (`core/tests/fixtures/adm_osc_v1_compliance.csv`, ~80 rows
  covering `/adm/obj/N/{azim,elev,dist,aed,xyz,x,y,z,gain,mute,active,name,w,h,d,
  divergence,gain/mute_solo}` plus `/adm/scene/...` if in scope).
* **Acceptance**: ADR is reviewed by Architect/Critic in the next ralplan round
  before S2 begins.
* **[R2 / A7]** `adm_osc_v1_compliance.csv` column schema MUST be pinned to:
  `address,type_tags,arg_values,expected_command_tag,expected_payload_json,in_v0,deferred_reason`.
  ~80 rows enumerate v1.0 spec addresses + each row's v0 implementation status +
  deferral rationale (one of: `OUT_OF_SCOPE_V0`, `VENDOR_QUIRK`, `PHASE_C4`,
  `PHASE_C5`, `NOT_APPLICABLE`). Empty `deferred_reason` for `in_v0=true` rows.

### S2 — Extend `CommandDecoder` to full v1.0 receive coverage  (3 days)

* **Files**: `core/src/ipc/CommandDecoder.cpp:317-373` (extend), `core/src/ipc/Command.h`
  (add new `CommandTag` values + payload structs).
* Refactor: extract `static Command decodeAdmAddress(const std::string& addr,
  const OscArgs& args, uint32_t& reject_count)` inside `CommandDecoder.cpp` to keep
  `buildCommand()` readable.
* **New CommandTags** (additive, no renaming of existing 0x01–0x60 values):
  - `ObjXYZ        = 0x06` — Cartesian position (`PayloadObjXYZ { obj_id; x,y,z }`)
  - `ObjActiveAdm  = 0x07` — alias for `/adm/obj/N/active` (semantics differ from
    legacy `/obj/active`: ADM uses i 0/1; legacy uses i 0/1 — same wire, different
    address)
  - `ObjWidth      = 0x08` — `PayloadObjWidth { obj_id; width_rad }` (already
    enumerated in `PayloadObjDsp::Width = 7` but ADM has its own dedicated address)
  - `ObjName       = 0x09` — `PayloadObjName { obj_id; char name[32] }`
  - **Decision B-α** preserves all legacy tags untouched.
* **Acceptance**: `core/tests/core_unit/test_p_adm_osc.cpp` extended from 8 cases
  to ≥ 30 cases driven by the CSV fixture; existing 8 cases must still pass byte-
  for-byte.
* **[R2 / A3]** `obj_cache_` indexing verification:
  - Every new ADM tag's `obj_id` field MUST be range-checked against
    `MAX_OBJECTS` (`core/src/core/Constants.h:12`, value=64).
  - Out-of-range `obj_id` → `++reject_count_` (no UB, no silent acceptance).
    Mirror the existing guard at `core/src/core/SpatialEngine.cpp:243`
    (`if (qc.obj_id >= static_cast<uint32_t>(MAX_OBJECTS)) continue;`).
  - Unit test: `obj_id = MAX_OBJECTS` and `obj_id = MAX_OBJECTS + 1` both
    decode but produce a `Command` that the SpatialEngine drain at line 243
    rejects without dereferencing `obj_cache_[oid]`. Asserts:
    `decoder.rejectCount()` increments OR drain-side counter increments.
  - Document the contract in ADR 0006: "ADM-OSC `obj_id` is `uint32_t` on
    the wire; engine clamps via `MAX_OBJECTS` constant; out-of-range is a
    silent drop with reject metric."
* **[R2 / A4]** Cross-prefix collision test (mitigates Risk row 4):
  - New test in `core/tests/core_unit/test_p_adm_osc_v1_compat.cpp`:
    `test_cross_prefix_collision`.
  - Sequence: send `/obj/move oid=3 az=1.0 el=0 dist=1` then
    `/adm/obj/3/aed (1.5, 0.2, 5.0)` within the same FIFO drain window.
  - Assert: final state in `obj_cache_[3]` matches the **second** packet
    (last-write-wins via `obj_cache_` direct-write at
    `core/src/core/SpatialEngine.cpp:247-250`).
  - This implements the "unit test asserts last-write-wins" promise from
    Risk row 4.

### S3 — Add ADM-OSC encode (send-back) path  (2 days)

* **File**: `core/src/ipc/CommandDecoder.cpp:422-602` — extend `encode()`.
* Add `encodeAdm(const Command& cmd, std::vector<uint8_t>& out)` for the new tags
  plus the existing `ObjMove`, `ObjGain`, `ObjMute`. Address pattern: `/adm/obj/N/aed`
  for combined, `/adm/obj/N/azim|elev|dist` for split.
* Add a runtime selector on `OSCBackend`: `enum WireDialect { Legacy, AdmV1 }`
  default `Legacy`. `--osc-dialect adm` CLI flag in `bin/spatial_engine_core.cpp`
  switches the encoder.
* **Acceptance**: round-trip test in `test_p_adm_osc.cpp` — encode `Command{ObjMove,
  oid=3, az=π/4, el=0, dist=10}` with `dialect=AdmV1`, decode back, expect identical
  `Command`. Byte-comparison against a known-good ADM-OSC v1.0 packet captured from
  Spat Revolution demo.
* **[R2 / A5]** Round-trip MUST use the same `MAX_DIST` constant as decode
  (`core/src/ipc/CommandDecoder.cpp:321`, value=`20.0f`). Document
  `MAX_DIST=20.0f` in ADR 0006 as the v0 contract for the ADM-OSC normalised
  `dist` ↔ metres mapping. Encoder MUST NOT pull `MAX_DIST` from a runtime
  config or vendor-specific override — silent vendor mismatch is the failure
  mode. Test asserts the constant is identical at encode and decode call
  sites (single source of truth: a `static_assert` or `inline constexpr float`
  in a shared header).

### S4 — Bridge consolidation + VST3 deferral note  (1.5 days)

* **Bridge**: keep `bridge/spike_vid2spatial_osc.py` as the **vid2spatial-specific**
  translator; move common ADM-OSC helper functions (object-id allocation, IIR,
  rate limiter at lines 81-91) to `bridge/_adm_osc_common.py`; make
  `bridge/vid2spatial_osc.py:254` use it. **Do not** add an "ADM-OSC server" inside
  the bridge layer — direct console traffic now hits `core/` on port 9100.
* **VST3** (Decision C-β): file Phase C4 issue "VST3 plugin ADM-OSC strategy"
  referencing `vst3/SpatialEngineProcessor.cpp` (currently no OSC). No code change
  in the plugin during Phase C3. Update `vst3/README.md` (or create) explaining the
  standalone-bridge story.
* **Acceptance**: existing `bridge/test_*.py` unchanged and green; new doc note
  reviewed.

### S5 — Test harness + soak + 3-vendor fixtures  (3 days)

* **Files**:
  - `core/tests/fixtures/adm_osc_v1_compliance.csv` (S1)
  - `core/tests/fixtures/adm_osc_synthetic_lisa_v1.osc.bin` (synthetic, mimicking L-ISA)
  - `core/tests/fixtures/adm_osc_synthetic_spat_v1.osc.bin` (synthetic, Spat Revolution)
  - `core/tests/fixtures/adm_osc_synthetic_dnb_v1.osc.bin` (synthetic, d&b Soundscape)
  - `core/tests/core_unit/test_p_adm_osc_v1_compat.cpp` — parametric, drives the
    CSV; loops each capture file through `OSCBackend::injectPacket` and asserts
    expected `Command` sequence.
  - `core/tests/perf/soak_adm_osc_flood.cpp` — 64 obj × 1 kHz × 60 s flood,
    asserts audio-thread alloc==0, dropped-cmd count surfaced in `/sys/state`.
* **[R2 / A1, replaces R1 fixture-naming]** Fixture filenames carry **explicit
  synthetic provenance** (`adm_osc_synthetic_<vendor>_v1.osc.bin` instead of
  `adm_osc_capture_<vendor>.osc.bin`). Provenance comment at the top of each
  fixture file: "Synthetic ADM-OSC v1.0 packet stream constructed from public
  vendor docs / demo videos. NOT a real wire capture. Replacement target:
  Phase C4 follow-up issue (real capture within 60d of first contract signing)."
* **CI**: extend `core/CMakeLists.txt` to register the new test target;
  ensure `ctest --output-on-failure` picks it up. Soak test runs on `--config Release`
  only (skip on default Debug to keep CI fast).
* **[R2 / A6]** Soak harness MUST exercise the **real UDP path**, not the
  in-process `OSCBackend::injectPacket` shortcut:
  - Sender thread issues `sendto()` to `127.0.0.1:9100` (production port).
  - Receiver is the production `OSCBackend` UDP listener at
    `core/src/ipc/OSCBackend.cpp:75-83`.
  - This catches kernel UDP buffer overflow, thread-crossing latency, and
    socket-level back-pressure that `injectPacket` would silently bypass.
  - In-process `injectPacket` remains valid for **functional** decode tests
    (S2/S3) but NOT for the latency-critical soak.
* **Acceptance**:
  - All compliance CSV rows pass except those marked "deferred".
  - **[R2 / A1 — verbatim Architect A2]** During 60s soak (64 obj × 1 kHz),
    measure end-to-end input-OSC → audio-callback-completion latency across
    stages 2–4 per ADR-0003:70-77 ("Compute p99 of the IPC + decode +
    audio-callback-wait portion"). p99 < 3.0 ms in ≥99% of 60 1-second
    windows (ADR-0003 falsifier 정확 정렬). Audio-thread alloc count == 0
    (existing harness invariant).
  - 3 synthetic vendor fixtures decode to expected Command sequences with no
    `Unknown` (or only on rows annotated as out-of-v0-scope).

---

## 6. Risks and Mitigations

| Risk | Impact | Mitigation |
| ---- | ------ | ---------- |
| ADM-OSC v1.0 spec changes mid-quarter | Re-work S1 fixture | Pin to a specific git commit hash of `immersive-audio-live/ADM-OSC` in ADR 0006 |
| Real console behaviour diverges from public spec | Compat fails on first paying customer | Synthetic fixtures (S5) cover the spec; ADR 0006 reserves a "vendor-quirk overlay" slot at the bridge layer (not core) |
| RT-safety regression from new code path | Audio glitches under flood | S5 soak; per-object coalescer (Pre-mortem 3) lives in control thread, never audio thread |
| Two-prefix coexistence confusion (`/obj/...` vs `/adm/obj/...`) | Users mix prefixes, get last-write-wins surprise | ADR 0006 documents the rule; UI shows the active wire dialect; unit test asserts last-write-wins |
| VST3 deferral leaves a competitive gap | DAW users on Pro Tools cannot drive ADM-OSC | C-γ trade-off documented in Phase C4 issue; standalone-bridge workaround in `vst3/README.md` |
| Schema-version handshake collision (`/sys/handshake`) — **[R2 / A2 — REWRITE]** ADM commands route bypassing seq-drop logic | A v1.0 ADM client never sends our handshake nor a seq number; if routed through `StateModel::apply()`, every ADM packet after the first would be dropped because `cmd.seq=0 <= obj.last_applied_seq=0` is true on the second packet (logic at `core/src/ipc/StateModel.cpp:37,54,69,84`) | All ADM-OSC commands route through `obj_cache_` direct-write (`core/src/core/SpatialEngine.cpp:239-260`); StateModel seq-drop logic does not apply (already implemented for current 5 ADM addresses). New tags from S2 (`ObjXYZ`, `ObjActiveAdm`, `ObjWidth`, `ObjName`) MUST follow the same `obj_cache_` path; **DO NOT** introduce StateModel routing for ADM commands. The seq-drop guard at `StateModel.cpp:37,54,69,84` (`if (obj.valid && cmd.seq <= obj.last_applied_seq) return false;`) would silently kill ADM traffic after the first packet because ADM sets `cmd.seq=0` (decoder line `CommandDecoder.cpp:329-330`). Acceptance test: send 100 consecutive `/adm/obj/3/aed` packets, assert all 100 are reflected in `obj_cache_[3]` (last-write-wins), reject_count for legitimate addresses == 0 |

---

## 7. ADR (decision record, to be ratified after Architect/Critic)

* **Decision**: Adopt **A-β + B-α + C-β** for Phase C3.
  Extend the existing `CommandDecoder` with full ADM-OSC v1.0 receive coverage and
  add a send-side dialect selector. Keep two prefixes verbatim with no alias map.
  Ship in standalone only; defer VST3 plugin OSC to Phase C4.
* **Drivers**: Korean console compat (top), ADM-OSC ecosystem entry, zero new deps.
* **Alternatives considered**:
  - A-α (separate handler) — rejected (port conflict + ADR-0003 violation).
  - A-γ (bridge only) — rejected (does not satisfy Driver 1).
  - B-β (alias map) — rejected (obscures provenance, complicates debug).
  - C-α (VST3 OSC now) — rejected for Phase C3 (DAW thread + plugin certification risk).
* **Why chosen**: maximum spec compliance with minimum code surface; reuses the
  proven dispatcher; preserves all existing tests; ships in 2–3 weeks; opens path
  to first Korean live-venue contract without blocking on DAW plugin work.
* **Consequences**:
  - `core/src/ipc/CommandDecoder.cpp` grows by ~150 LOC; mitigated by
    `decodeAdmAddress()` extraction.
  - 4 new `CommandTag` values (0x06–0x09); existing wire IDs untouched.
  - `bin/spatial_engine_core.cpp` gains `--osc-dialect {legacy|adm}` flag; default
    stays `legacy` to preserve every test in `core/tests/`.
  - Phase C4 inherits a documented VST3-OSC issue.
  - Korean live-venue customers can connect their consoles directly to standalone
    `spatial_engine_core` on port 9100.
  - **[R2]** ADR-0003 alignment: soak harness measures the exact 3-stage
    (IPC + decode + audio-callback-wait) latency window defined in
    `docs/adr/0003-ipc-osc-udp.md:70-77`, not a freshly invented metric.
    R2 acceptance text quotes the falsifier verbatim.
  - **[R2]** `obj_cache_` bounds: new ADM tags inherit the
    `MAX_OBJECTS=64` clamp from `core/src/core/Constants.h:12`, mirrored at
    `core/src/core/SpatialEngine.cpp:243`. ADM commands NEVER touch
    `StateModel::apply()` (which would drop them via the seq guard at
    `core/src/ipc/StateModel.cpp:37,54,69,84`). This is a hard invariant
    of the design, locked by Risk row 6 (post-R2 rewrite).
* **Follow-ups**:
  - Phase C4: VST3 ADM-OSC plugin strategy (decision C-α vs deferral).
  - Phase C4: vendor-quirk overlay in `bridge/` if real console captures diverge
    from synthetic fixtures.
  - **[R2]** Phase C4: replace **one of the three** synthetic fixtures
    (`adm_osc_synthetic_*_v1.osc.bin`) with a real vendor-captured packet
    stream within **60 days** of first contract signing. Tracks the
    synthetic-vs-real fidelity gap explicitly.
  - Phase C5 candidate: ADM-OSC `/adm/scene/*` (snapshot save/load over the same
    wire) — currently out of scope; revisit if customers ask.

---

## Appendix A — Acceptance criteria (testable, ≥ 90% target)

1. `ctest --output-on-failure -R adm_osc` passes 30+ cases (CSV-driven).
2. Round-trip: `decode(encode(cmd, AdmV1)) == cmd` for every supported tag.
3. 3 synthetic vendor capture fixtures decode without `Unknown` (modulo deferred rows).
4. Soak (S5): 64 obj × 1 kHz × 60 s → audio-thread alloc==0, p99 IPC latency < 3.0 ms.
5. All existing tests pass byte-for-byte:
   - `core/tests/core_unit/test_p_adm_osc.cpp` (8 cases)
   - `ui/tests/test_adm_osc_protocol.py`
   - `ui/webgui/tests/test_dispatch.py`, `test_throughput.py`, `test_latency.py`
6. `OSCBackend` reject count surfaced in `/sys/state`; non-zero count fails CI's
   integration smoke test.
7. ADR 0006 merged with locked ADM-OSC commit hash and per-address support matrix.
8. `bin/spatial_engine_core --osc-dialect adm` emits valid ADM-OSC v1.0 packets
   captured by `tools/osc_debug_console.py` and re-decoded successfully.
9. Per-object coalescer in `OSCBackend` `CommandSink` proven by unit test:
   N rapid `ObjMove(oid=5)` followed by drain → exactly one (the latest) survives.
10. `bridge/_adm_osc_common.py` ≥ 80% line coverage in `bridge/test_*.py`.

## Appendix B — File:line citations summary

* `core/src/ipc/CommandDecoder.cpp:317-373` — current ADM-OSC subset
* `core/src/ipc/CommandDecoder.cpp:422-602` — current encode (legacy only)
* `core/src/ipc/Command.h:31, 121-124` — `ObjMute` tag + payload
* `core/src/ipc/OSCBackend.cpp:51-93` — JUCE-free UDP listener
* `core/src/ipc/OSCBackend.cpp:32-37, 110-115` — `injectPacket` reject path
* `core/src/ipc/OSCBackend.h:20-23` — `CommandSink` extension point for coalescer
* `core/src/ipc/ExternalControl.h:9-21` — base interface
* `core/src/bin/spatial_engine_core.cpp:150` — current `--osc-port` log
* `core/src/core/SpatialEngine.cpp:15, 106` — `OSCBackend` instantiation
* `core/tests/core_unit/test_p_adm_osc.cpp:1-157` — current 8-case test
* `bridge/spike_vid2spatial_osc.py:1-293` — bridge with rate-limit & IIR
* `bridge/vid2spatial_osc.py:7, 254` — production bridge sender
* `docs/adr/0003-ipc-osc-udp.md` — single-schema invariant + falsifier
* `docs/adr/vid2spatial_osc_contract.md` — coordinate-conversion contract
* `vst3/SpatialEngineProcessor.cpp` (581 LOC) — currently no OSC; deferral target
* `ui/spatial_engine_ui/ipc/protocol.py` — UI-side ADM helpers
* `ui/webgui/server.py:165, 173` — WebGUI sends `/adm/obj/{n}/{aed,gain}`

---

## Recommended next step

**[R2 update]** Round-1 Architect (ITERATE, 3 amendments) and Critic
(APPROVE-WITH-ITERATE, M1+M2 HIGH, m1–m5 MINOR) have been processed via the 7
amendments in Appendix D. Critic R1 verdict explicitly authorised
**"single Round 2 patch then autopilot"** — Round 3 review is **skip-eligible**.

Recommended path:

1. **User decision gate** — confirm Round-2 patch is satisfactory (this plan).
2. On confirmation → enter `/oh-my-claudecode:autopilot` with this plan as
   the active spec. No Round-3 ralplan needed.
3. If user wants extra confidence → optionally invoke one final Architect
   sanity-check pass on the R2 amendments only (not a full re-plan).

Original R1 review topics (now resolved by R2):

1. ~~Decision A/B/C ratification~~ — A1 Architect downgrade; A3 (VST3) Architect
   reject sustained (C-β stays); A2 Architect verbatim adopted as A1 here.
2. ~~CSV fixture scope~~ — A7 pins schema; ADR 0006 enumerates per-row.
3. ~~`--osc-dialect` default~~ — kept as `legacy` (R1 proposal); preserves all
   existing tests byte-for-byte.
4. ~~Pre-mortem Scenario 1 fixture coverage~~ — A1 (synthetic-fixture sufficiency)
   adopted with explicit synthetic provenance + Phase C4 60-day real-capture
   follow-up.

---

## Appendix D — Round-2 Changelog

| ID | Source            | Severity | Section impact                   | Round-1                                                                        | Round-2                                                                                                                         |
| -- | ----------------- | -------- | -------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| A1 | Architect M1 (=A2) | HIGH     | §5 S5 acceptance #4 (latency)    | "p99 input-to-render latency stays below the ADR-0003 falsifier (3.0 ms IPC stages)" — vague, no ADR alignment | Verbatim ADR-0003:70-77 quote: stages 2–4, p99 < 3.0 ms in ≥99% of 60 1-second windows; explicit "audio-callback-completion" end-point |
| A2 | Critic M2          | HIGH     | §6 Risks row 6 (handshake)       | "Make handshake optional for `/adm/...` traffic; StateModel must not require seq>0 for ADM-tagged commands" — stated but not enforced | Full rewrite: route ALL ADM cmds through `obj_cache_` direct path (`SpatialEngine.cpp:239-260`), NEVER via `StateModel::apply()` (would drop via `seq <= last_applied_seq` guard at `StateModel.cpp:37,54,69,84`). New S2 tags MUST follow same rule. Acceptance: 100-packet flood test |
| A3 | Critic M2 reinforce | HIGH     | §5 S2 acceptance                 | (absent)                                                                       | `obj_cache_` indexing verification: `obj_id ∈ [0, MAX_OBJECTS)`; mirror guard at `SpatialEngine.cpp:243`; out-of-range → `++reject_count_` (no UB); contract documented in ADR 0006 |
| A4 | Critic m4         | MEDIUM   | §5 S2 acceptance                 | Risk row 4 promised "unit test asserts last-write-wins" but no test was specified | New test `test_p_adm_osc_v1_compat.cpp::test_cross_prefix_collision`: `/obj/move` then `/adm/obj/3/aed` in same drain window; assert second packet wins via `obj_cache_[3]` (`SpatialEngine.cpp:247-250`) |
| A5 | Critic m3         | MEDIUM   | §5 S3 acceptance #3              | Round-trip test specified but no constant-source pinning                       | `MAX_DIST=20.0f` (`CommandDecoder.cpp:321`) becomes single-source-of-truth via `inline constexpr` shared header; `static_assert` enforces encoder/decoder identity; documented in ADR 0006 |
| A6 | Critic m5         | MEDIUM   | §5 S5 soak harness               | Soak test specified but transport unclear                                      | `core/tests/perf/soak_adm_osc_flood.cpp` MUST use real UDP path (`sendto()` → `127.0.0.1:9100` → production `OSCBackend` listener at `OSCBackend.cpp:75-83`); NOT in-process `injectPacket`. In-process path remains valid for S2/S3 functional tests only |
| A7 | Gap 4 (CSV schema) | LOW      | §5 S1 ADR 0006 acceptance        | "~80 rows" but no column schema                                                | Schema pinned: `address,type_tags,arg_values,expected_command_tag,expected_payload_json,in_v0,deferred_reason`. `deferred_reason` enum: `OUT_OF_SCOPE_V0|VENDOR_QUIRK|PHASE_C4|PHASE_C5|NOT_APPLICABLE` |

### Amendments NOT applied (rejected with rationale)

| ID         | Source            | Why rejected                                                                                                                                                                                                                                                                                                                                                                                              |
| ---------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1-arch    | Architect A1 (synthetic fixture +0.5d for real captures) | Critic m1 analysis adopted. Synthetic fixtures sufficient because Pre-mortem 2 mitigation (vendor-toggle bridge flag) provides escape valve. Compromise: **explicit synthetic provenance** in filenames (`adm_osc_synthetic_<vendor>_v1.osc.bin`) + Phase C4 follow-up "replace one of three within 60d of first contract signing". Cost: 0.1d only. |
| A3-arch    | Architect A3 (opt-in `osc_enable` plugin param) | Critic m2 analysis adopted. `vst3/SpatialEngineProcessor.cpp:32` (`std::make_unique<spe::core::SpatialEngine>(0 /*no UDP*/)`) is **constructive absence**, not active suppression. Adding `osc_enable` does not solve Pro Tools / Logic Pro / Ableton sandbox restrictions or multi-instance port collision. C-β preserved. Phase C4 issue already filed at plan §5 S4. |

### ETA delta

| Phase            | R1     | R2                | Delta  |
| ---------------- | ------ | ----------------- | ------ |
| S1 — Spec freeze | 1.0d   | 1.0d (A7 = text)  | 0      |
| S2 — Decoder     | 3.0d   | 3.2d (A3+A4 = +0.2d test code) | +0.2d  |
| S3 — Encoder     | 2.0d   | 2.0d (A5 = text + 1 header) | 0      |
| S4 — Bridge      | 1.5d   | 1.5d              | 0      |
| S5 — Test/soak   | 3.0d   | 3.3d (A1+A6 = +0.3d UDP harness wiring) | +0.3d  |
| **Total**        | **10.5d** | **11.0d**       | **+0.5d** |

Within original 2–3 week (10–15d) banner; margin preserved.
