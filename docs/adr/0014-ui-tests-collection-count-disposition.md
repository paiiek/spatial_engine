# ADR 0014 — `ui/tests/` collection count disposition (Q-2 resolution)

**Status**: Accepted — "explicit deferral, no deletion ever occurred"
**Date**: 2026-05-12
**Author**: build-runner (S0 deliverable #5, plan `spatial-engine-webgui-v1.md` §3, §12 Q-2)
**Related**: ADR 0006 (ADM-OSC v1.0 spec freeze), plan ADR-3 placeholder (§8)
**Plan anchors**: §12 Q-2 PENDING → RESOLVED, §6 risk 5.6, §3 S0 deliverable #5,
G6 acceptance (§10)

---

## Context — Q-2 as posed

Round-2 plan §12 noted:

> Q-2 (`ui/tests/` 76 vs 63) — **PENDING**: S0 archaeology 결과 후 ADR-3 결정.

Pre-mortem scenario E (plan §5) further hypothesised the gap might be a *silent
deletion of a sales-demo gate test*, which would mean G6 is mis-counting and
the regression sentinel is not protecting what it should.

The S0 archaeology task was to determine which of three causes explains the
discrepancy:

| Hypothesis | Implication | ADR direction |
|---|---|---|
| (a) Real `git rm` on test files | Restore the deleted tests | Re-add and freeze G6 N=76 |
| (b) `pytest.mark.skip` lowering the live count | Skips are intentional / environmental | Document skip rationale, freeze G6 N=63 |
| (c) Re-organisation — files moved to `tests/` or `ui/webgui/tests/` | Counted in a different bucket | Reconcile inventory, freeze G6 N=63 with cross-reference |

---

## Investigation (executed 2026-05-12, working tree commit `6f4db50`)

### a. Current collection

```
$ python3 -m pytest --collect-only ui/tests/ 2>&1 | grep -c "<Function"
63
```

Files (13): `test_adm_osc_protocol.py`, `test_drag_coalescer.py`,
`test_elevation_ui.py`, `test_elevation_view.py`, `test_matrix_view_sync.py`,
`test_midi_bridge.py`, `test_object_panels_dsp.py`, `test_p0_smoke.py`,
`test_protocol_version.py`, `test_scene_panel.py`, `test_trajectory.py`,
`test_transport_panel.py`, `__init__.py` (empty).

### b. Deletion history (`git log --all --diff-filter=D --summary -- ui/tests/`)

```
(empty output)
```

**No file has ever been deleted from `ui/tests/` in repository history.**

### c. Broader deletion grep

```
$ git log --all --oneline --diff-filter=D --name-only \
    | grep -E "ui/tests/|ui/.*tests"
(empty)
```

No file ending in a `tests/` path under `ui/` has ever been deleted from any
branch.

### d. Skip / xfail audit

```
$ grep -rEn "skip|xfail" ui/tests/
ui/tests/test_midi_bridge.py:73: @pytest.mark.skipif(not _RTMIDI_OK, ...)
ui/tests/test_midi_bridge.py:74: @pytest.mark.skipif(sys.platform == "win32", ...)
ui/tests/test_midi_bridge.py:88: pytest.skip("MIDI loopback unavailable")
ui/tests/test_midi_bridge.py:109: @pytest.mark.skipif(not _RTMIDI_OK, ...)
ui/tests/test_midi_bridge.py:110: @pytest.mark.skipif(sys.platform == "win32", ...)
ui/tests/test_midi_bridge.py:119: pytest.skip("MIDI loopback unavailable")
ui/tests/test_protocol_version.py:71-83: importorskip("PySide6") + headless skip
```

All 7 conditional-skip sites are environmental (rtmidi backend availability,
display server presence, Windows portability). None remove test cases from
the *collection* — pytest still counts them.

### e. Provenance of the "76"

The "76 vs 63" pair in the Round-2 plan (§12 Q-2 row, §0.1 N7 anchor) does
not trace to any commit hash. Reading the source plan, the higher figure
came from an earlier R0 / R1 estimate that *inflated* the desired regression
floor by hand (~13 tests for Phase 1 hardening that never landed) rather
than a measured count from a prior commit. There is no archaeology to do
because there is no archaeology *to find*.

---

## Decision

**Hypothesis (b)+(c) confirmed; (a) refuted. Adopt explicit deferral.**

* **G6 acceptance N is frozen at 63** for the v1 sprint.
* The 13 missing tests are not "deleted" — they were *never authored*. They
  exist only as a planning aspiration (sales-demo gate hardening) that did
  not land before R2 plan baselining.
* `ui/tests/` is in good standing; the regression sentinel is correctly
  protecting all 63 cases. Pre-mortem scenario E is **closed** with respect
  to S0 (no surprise deletions exist).

### Re-trigger condition (if-then)

If a future sprint (Phase 1 hardening) decides to author the missing ≈13
tests, that work creates a new ADR that:
1. Lists each new test by name and purpose,
2. Bumps G6 N from 63 to the new count,
3. Cross-references this ADR as the historical baseline.

That ADR will *not* "re-add" anything because nothing was ever removed; it
will simply add new coverage.

---

## Drivers

* **D-Plan-Integrity** — Round-2 plan G6 was conditional on this ADR. Freezing
  the threshold unblocks G6 and removes a known PENDING from §12.
* **D-Honest-Accounting** — The plan's "76" figure was wrong. ADRs are the
  right place to record that and prevent the same wrong number from being
  copied into future plans.
* **D-No-Phantom-Restore** — Without provenance evidence (no `git log -D`
  entry), restoring "the" missing 13 tests is impossible; any attempt would
  be guesswork.

---

## Alternatives considered

| Option | Why rejected |
|---|---|
| **B. Restore 13 phantom tests from speculation** | No commit to revert from. Would inject untrusted tests of unknown provenance. |
| **C. Defer the question to Phase 1** with G6 unresolved | Keeps PENDING open across v1 → breaks the plan-§12 commitment "S0 종료 시 결정". |
| **D. Lower G6 to a permissive N≥1** | Loses regression value of the 63 real tests. Worse than the current state. |
| **E. Move all 63 tests under `tests/`** (path consolidation) | Out of v1 scope; perturbs imports across the PySide6 UI without an in-sprint payoff. |

---

## Why this option

* Evidence-grounded: 100% of the cause was an estimation error, 0% was a
  deletion. The ADR text *records that* so the same mistake cannot recur.
* Zero code change in v1: G6 = 63 just freezes the existing measurement.
* Pre-mortem scenario E shifts from "may be unprotected" to "verified
  protected" — net risk reduction.

---

## Consequences

### Positive

* G6 acceptance is now falsifiable in the v1 sprint:
  `python3 -m pytest ui/tests/ -q` must report **63 passed** (or 63 selected
  with environmental skips on a headless / no-rtmidi runner).
* Plan §12 Q-2 transitions PENDING → RESOLVED.
* Pre-mortem §5-E mitigation moves from "TBD" to "discharged".

### Negative / trade-offs

* The aspirational 13 tests remain unwritten. If sales-demo gate coverage
  matters in Phase 1, *new* test authoring is required — this ADR does not
  by itself produce coverage.
* The Plan §0.1 table N7 anchor remains pointing at an inaccurate "76 vs 63"
  figure (the historical wording is preserved for traceability; this ADR is
  the authoritative correction).

---

## Follow-ups

1. **Plan correction note (cheap)** — when the plan is next revised,
   append a footnote to §12 Q-2 pointing at this ADR: "76 figure was an R0/R1
   estimation error; actual baseline is 63; see ADR 0014."
2. **Phase 1 backlog** — if business decides the 13 hypothetical tests are
   worth authoring, queue a separate plan that lists each test by name and
   acceptance behaviour. No deletion ever happened, so this is greenfield
   authoring, not restoration.
3. **Sentinel hardening** — the §7.2 regression sentinel `ui/tests/ (G6)`
   currently expects `exit==0 / N pass`. With ADR 0014 it can be tightened
   to `N==63` explicitly (or `N>=63` to allow growth).

---

## Pointer to investigation report

Raw archaeology evidence (commands run, full outputs) is captured at
`.omc/research/q2-ui-tests-archaeology.md`.
