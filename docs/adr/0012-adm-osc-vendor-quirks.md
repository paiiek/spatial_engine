# ADR 0012 — ADM-OSC vendor-quirks overlay (reserved slot)

**Status**: Reserved (placeholder, fill on first vendor incompat)
**Date**: 2026-05-10
**Spec commit pin**: `v0.3.0-c4-draft` (slot reserved post-RALPLAN Round-2 APPROVE)
**Related**: ADR 0006 (ADM-OSC v1.0 spec freeze), ADR 0010 (binding model), ADR 0011 (discovery)

---

## Purpose

This ADR is a **reserved slot** for documenting per-vendor ADM-OSC
behavioural deviations from the v1.0 specification. It is intentionally
empty in v0.2.0; the trigger for filling it is the first observed
incompatibility between a real ADM-OSC sender (DiGiCo / Avid / Yamaha
console; L-ISA Controller; Spat Revolution; d&b Soundscape) and the
spec contract pinned in ADR 0006.

---

## Why a reserved slot

Plan §Pre-mortem Scenario 2 (`spatial-engine-phaseC4-and-v0.2-release.md`
line 866-887) identifies "v0.2 ships, console vendor reports an
ADM-OSC v1.0 incompatibility" as a real risk. The mitigation strategy
is:

1. **Core stays spec-clean**: `core/src/ipc/CommandDecoder.cpp` continues
   to implement ADM-OSC v1.0 verbatim per ADR 0006.
2. **Vendor quirks live at the bridge layer**: `bridge/_adm_osc_common.py`
   (or a new file under `bridge/vendor_overlays/`) hosts the quirk
   workaround.
3. **This ADR documents the divergence**: each new vendor quirk gets a
   numbered subsection here with a fixture path, a workaround
   description, and a sunset condition.

Reserving slot 0012 in v0.2.0 makes the future hotfix path explicit and
locked-in. When Pre-mortem Scenario 2 fires, the on-call engineer has a
predetermined home for the fix without having to renegotiate the
architecture.

---

## Trigger to fill

Fill this ADR when ANY of the following occurs:

- A real ADM-OSC sender (production console or commercial controller)
  produces packets that the v0.2 / v0.3 receive path either rejects or
  decodes incorrectly.
- A field engineering report from the Korean live-venue first contract
  (or future contracts) identifies a vendor quirk.
- The 60-day post-first-contract real-capture replacement (per ADR 0006
  follow-ups) reveals a divergence between the synthetic vendor fixture
  and the real wire output.

---

## Fill-in template

When triggered, append a new `## Quirk N — <vendor> <pattern>` section
in numerical order. Recommended structure:

```markdown
## Quirk N — <vendor> <pattern>

**Date observed**: YYYY-MM-DD
**Reporter**: <field engineer / customer / fixture audit>
**Fixture**: `core/tests/fixtures/adm_osc_vendor_quirk_<N>.osc.bin`
**Sender**: <vendor name + product name + firmware version>

### Symptom

<What the vendor sends; how it differs from ADR 0006 spec.>

### Workaround

<Where the fix lives. Should NOT touch core unless absolutely necessary.
Bridge-layer overlay is preferred. If core must change, justify why
(usually: receiver-side flexibility within spec letter, e.g. "accept
both `i 0/1` and `f 0.0/1.0` for boolean fields").>

### Sunset condition

<When does this quirk go away? E.g. "vendor firmware vN+1 fixes it" or
"never; vendor refuses to update; permanent overlay".>

### Tests

- `core/tests/core_unit/test_p_adm_osc_quirk_<N>.cpp` (if core change)
- `bridge/tests/test_quirk_<N>.py` (if bridge overlay)
```

---

## Why not put this in ADR 0006

ADR 0006 is the spec freeze. It MUST stay aligned with the
`immersive-audio-live/ADM-OSC` v1.0 spec verbatim. Mixing vendor
deviations into the spec freeze would dilute the contract; ADR 0006's
authority comes from being a faithful rendering of upstream.

ADR 0012 separates the **clean spec** (0006) from the **observed
ecosystem** (0012). Both are valid. Combined, they describe the
real-world system without compromising either layer.

---

## Quirk index

(empty — fill on first occurrence)

---

## Follow-ups

- This ADR is updated, not superseded. Quirks accumulate; they may be
  retired if the vendor fixes their firmware. Retired quirks remain
  documented (with `Sunset: <date> — vendor firmware vN+1`) for
  archaeology.
- If quirks accumulate beyond ~10 entries, consider promoting them to a
  separate per-vendor ADR (0013, 0014, ...) with this slot as the
  index page.
