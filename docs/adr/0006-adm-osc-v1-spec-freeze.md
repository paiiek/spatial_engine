# ADR 0006 — ADM-OSC v1.0 Compatibility Layer (Phase C3)

**Status**: Accepted (Round-2 patched, 2026-05-10)
**Spec reference**: https://immersive-audio-live.github.io/ADM-OSC/
**Spec commit pin**: `immersive-audio-live/ADM-OSC` commit `7f3e2a1` (2024-02-14, v1.0 Living Standard)

---

## Context

Phase C3 closes the ADM-OSC v1.0 receive/send gap in `spatial_engine_core`.
The existing decoder (`core/src/ipc/CommandDecoder.cpp:317-373`) already
handles `azim`, `elev`, `dist`, `aed`, `gain`, `mute` for `/adm/obj/N/`.
This ADR pins the exact address subset that is **in-scope for v0**, the
`MAX_DIST` constant, and the per-`obj_id` bounds contract.

---

## Decisions

### A — Handler location: A-β (extend `CommandDecoder`)

New ADM-OSC addresses are added to the existing `decodeAdmAddress()` helper
inside `CommandDecoder.cpp`.  A separate handler (A-α) would require a second
socket or a port-sharing mechanism that violates ADR-0003 "single OSC schema".
Bridge-only (A-γ) does not satisfy Korean console compatibility (Driver 1).

### B — Wire-format: B-α (verbatim coexistence)

`/adm/obj/N/…` and `/obj/…` coexist on port 9100.  No alias map.
Last-arrived wins via `obj_cache_` direct-write at
`core/src/core/SpatialEngine.cpp:247-250`.
Rule: a given object should be driven by ONE prefix at a time; mixing is
permitted but produces last-write-wins semantics.

### C — VST3 scope: C-β (standalone only, Phase C4 deferral)

`vst3/SpatialEngineProcessor.cpp` is constructed with `0 /*no UDP*/` and
MUST NOT be modified during Phase C3.  Phase C4 issue: "VST3 plugin
ADM-OSC strategy (C-α vs deferral)".

---

## MAX_DIST constant — v0 contract

```
inline constexpr float ADM_OSC_MAX_DIST = 20.0f;  // metres
```

Location: `core/src/ipc/AdmOscConstants.h` (single source of truth).

Semantic: ADM-OSC v1.0 normalises distance as `[0..1]` where `1.0 = far`.
We map `dist_normalised * ADM_OSC_MAX_DIST = dist_metres` in both the
decoder and the encoder.  A `static_assert` in both translation units
enforces identity.

**Vendor note**: Spat Revolution (Flux::) may send normalised `[0..1]`;
L-ISA Controller may send metres directly.  The v0 decoder treats the ADM
spec semantics (normalised) as the contract.  Vendor-specific deviations are
handled at the bridge layer (`bridge/`), NOT in `core/`.

---

## `obj_id` bounds contract

ADM-OSC packets carry `obj_id` as a decimal integer in the OSC address
(`/adm/obj/{N}/…`).  The decoder accepts any non-negative integer parsed by
`sscanf`.  The downstream bound check at `SpatialEngine.cpp:243`:

```cpp
if (qc.obj_id >= static_cast<uint32_t>(MAX_OBJECTS)) continue;
```

silently drops out-of-range commands and increments no per-object counter
(the OSC-level `reject_count_` is incremented by `makeUnknown()` only for
structurally unrecognised addresses).  Out-of-range `obj_id` values for
recognised addresses (`aed`, `xyz`, etc.) decode cleanly but are drained
without effect.  This is a **silent drop** — observable only via
`/sys/state` reject metrics.

---

## ADM-OSC v1.0 Address Support Matrix (v0 subset)

See `core/tests/core_unit/adm_osc_v1_compliance.csv` for the full
machine-readable fixture.  Summary:

| Address pattern              | Type  | v0 status | Notes |
|------------------------------|-------|-----------|-------|
| `/adm/obj/N/azim`            | `f`   | IN_V0     | degrees → radians |
| `/adm/obj/N/elev`            | `f`   | IN_V0     | degrees → radians |
| `/adm/obj/N/dist`            | `f`   | IN_V0     | normalised × MAX_DIST |
| `/adm/obj/N/aed`             | `fff` | IN_V0     | combined az+el+dist |
| `/adm/obj/N/gain`            | `f`   | IN_V0     | linear scalar |
| `/adm/obj/N/mute`            | `i`   | IN_V0     | 0/1 |
| `/adm/obj/N/xyz`             | `fff` | IN_V0     | Cartesian → ObjXYZ tag |
| `/adm/obj/N/active`          | `i`   | IN_V0     | 0/1 → ObjActiveAdm |
| `/adm/obj/N/width`           | `f`   | IN_V0     | radians → ObjWidth |
| `/adm/obj/N/name`            | `s`   | IN_V0     | char[32] → ObjName |
| `/adm/obj/N/x`               | `f`   | DEFERRED  | PHASE_C4 |
| `/adm/obj/N/y`               | `f`   | DEFERRED  | PHASE_C4 |
| `/adm/obj/N/z`               | `f`   | DEFERRED  | PHASE_C4 |
| `/adm/obj/N/divergence`      | `f`   | DEFERRED  | OUT_OF_SCOPE_V0 |
| `/adm/obj/N/gain/mute_solo`  | `i`   | DEFERRED  | OUT_OF_SCOPE_V0 |
| `/adm/obj/N/w`               | `f`   | DEFERRED  | VENDOR_QUIRK (alias for width) |
| `/adm/obj/N/h`               | `f`   | DEFERRED  | OUT_OF_SCOPE_V0 |
| `/adm/obj/N/d`               | `f`   | DEFERRED  | OUT_OF_SCOPE_V0 |
| `/adm/scene/...`             | —     | DEFERRED  | PHASE_C5 |
| `/adm/config/...`            | —     | DEFERRED  | PHASE_C5 |

---

## New CommandTag values (0x06–0x09)

```cpp
ObjXYZ       = 0x06,  // /adm/obj/N/xyz ,fff  x y z  (Cartesian)
ObjActiveAdm = 0x07,  // /adm/obj/N/active ,i 0|1
ObjWidth     = 0x08,  // /adm/obj/N/width ,f radians
ObjName      = 0x09,  // /adm/obj/N/name ,s char[32]
```

Existing legacy tags (0x01–0x60) are untouched (Decision B-α).

---

## Encode dialect

`CommandDecoder::encode()` gains a `WireDialect` parameter:

```
enum class WireDialect { Legacy, AdmV1 };
```

Default is `Legacy` to preserve all existing tests.
`--osc-dialect adm` CLI flag in `bin/spatial_engine_core.cpp` selects AdmV1.

---

## StateModel routing invariant (A2 M2)

ALL ADM-OSC commands route through `obj_cache_` direct-write
(`SpatialEngine.cpp:239-260`).  **NEVER** through `StateModel::apply()`.

Reason: `StateModel` drops commands where `cmd.seq <= last_applied_seq`.
ADM-OSC sets `cmd.seq=0` (decoder line `CommandDecoder.cpp:329-330`), so
every ADM packet after the first would be silently dropped.

---

## Follow-ups

- Phase C4: VST3 plugin ADM-OSC strategy.
- Phase C4: Replace ONE synthetic fixture with real vendor capture within
  60 days of first contract signing.
- Phase C4: Single-axis `/adm/obj/N/{x,y,z}` decode.
- Phase C5: `/adm/scene/*` snapshot save/load.
