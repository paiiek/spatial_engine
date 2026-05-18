# ADR 0016 — External binary distribution policy (beta-tester scope + GPL-3 trigger boundary)

**Status**: Accepted (v0.6 cycle).
**Date**: 2026-05-18 (drafted + ratified in the same v0.6 doc-tightening pass).
**Related**: `LICENSE.md`, `docs/license_procurement_plan.md`,
ADR 0002 (native core C++ + JUCE), `.claude/CLAUDE.md`,
`docs/release/v0.5.1/RELEASE_NOTES_EN.md`,
`docs/weekly_progress_report_2026-05-18.md` §5 (P0-2).

---

## Context

`spatial_engine` v0.x ships under **GPL-3.0-or-later** because JUCE 7.x's
GPL track is the active dependency option (see `LICENSE.md` and ADR 0002).
The procurement plan (`docs/license_procurement_plan.md`) defines three
trigger events that mandate JUCE commercial license procurement *before*
the action:

1. First external distribution of any binary built from this repository.
2. First commercial deployment outside the user's research lab.
3. First contributor outside SNU MARG when their organization requires
   commercial-license-already-procured before review.

The v0.5.1 / v0.6 weekly review (`docs/weekly_progress_report_2026-05-18.md`)
identified a **policy gap**: the "external beta tester" workflow that the
roadmap repeatedly cites ("Reaper/Bitwig 사용자 핸즈온 피드백 수집" — see
`progress.txt:179` and earlier) sits in a grey area between trigger 1
(distribution) and trigger 2 (commercial deployment). The team needs a
clear, falsifiable definition of which beta workflows are GPL-3-only
admissible and which require JUCE commercial procurement first.

Without this ADR, the safe-default ("any binary handed outside the lab
triggers procurement") would block all hands-on validation until a JUCE
commercial license is paid for, which is premature for a v0.x project
that has not validated product-market fit. Conversely, an absent policy
risks the team distributing a `.so` to a beta tester without realising
they have just triggered trigger 1 retroactively.

---

## Decision

The team adopts the following four-band classification for binary
artefacts leaving the SNU MARG lab during the v0.x cycle.

### Band 0 — Internal lab use (no external distribution)

- **Definition**: any build that stays on machines owned/operated by the
  SNU MARG lab, including remote development hosts that lab members SSH
  into.
- **JUCE licence required**: no.
- **Action**: none.

This is the default for all current activity (ctest, pytest, soak runs,
local DAW experiments on lab machines).

### Band 1 — Research collaboration (named, non-commercial, source visible)

- **Definition**: handing a built artefact (`spatial_engine_vst3.so` /
  `.vst3` / `.dylib` / `.AppImage`) to a **specifically named external
  collaborator** for **non-commercial research evaluation only**, where
  the recipient (a) has been emailed a copy of this ADR + `LICENSE.md`
  beforehand, (b) acknowledges in writing (email reply OK) that they
  receive the binary under GPL-3 and are entitled to the source
  (i.e., a public-repo link to the tagged commit), and (c) commits in
  writing not to redistribute the binary further during the v0.x
  cycle.
- **JUCE licence required**: no. GPL-3 covers this; the recipient
  retains all GPL-3 rights and obligations.
- **Action required before each Band-1 handoff**:
  1. Tag the commit (`git tag v0.x.y` if not already tagged) so the
     "source corresponding to the binary" is unambiguous.
  2. Email the recipient (a) the tag commit SHA, (b) the public repo
     URL, (c) `docs/release/v${ver}/RELEASE_NOTES_EN.md`, and
     (d) `LICENSE.md`. Save the sent email + their acknowledgement in
     `docs/license_procurement_plan.md §Audit log` (append-only).
  3. Confirm the recipient is **not** delivering for / on behalf of a
     commercial entity that requires commercial-license-already-procured
     for review (trigger event 3); if they are, escalate to Band 3 and
     halt distribution until JUCE Indie/Pro is procured.

**Beta-tester count cap during v0.x: 5.** If a sixth Band-1 handoff is
contemplated, the team must re-evaluate whether the workflow is
de-facto a commercial pilot (Band 3) and procure first.

### Band 2 — Public download / academic sample distribution

- **Definition**: any binary published to a public URL (GitHub Releases
  attachment, lab website, Hugging Face Spaces, conference USB stick)
  where the audience is not individually known.
- **JUCE licence required**: no, *provided* the binary is published
  alongside the corresponding source URL + a clear GPL-3 notice. The
  v0.x cycle does **not** authorise Band-2 distribution by default;
  any Band-2 release must be approved by the project lead and announced
  in a new release-notes addendum.
- **Action required**: same as Band 1 plus a one-page README on the
  download page restating GPL-3 + source URL + no-warranty notice.
- **Note**: Band 2 expands the audit-trail burden significantly because
  recipients are not individually known. Prefer Band 1 for all v0.x
  evaluation that does not require public-broadcast scale.

### Band 3 — Commercial / paid / productized

- **Definition**: any binary delivered as part of a commercial
  product, paid pilot, exhibition install, performance contract, or
  productized v1+ release. Also any Band-1 / Band-2 handoff where the
  recipient or their employer requires commercial-license-already-
  procured before reviewing the code.
- **JUCE licence required**: **yes**. JUCE Indie (≤$500 K / yr) or Pro
  per `docs/license_procurement_plan.md §Options`. Procurement must
  complete *before* any Band-3 binary leaves the lab.
- **Action**: file `juce-license-procurement` issue with 30-day SLA
  per `docs/license_procurement_plan.md §Procurement steps`.

---

## Rationale

### Why a four-band classification rather than a binary trigger

The procurement plan's "first external distribution" trigger is correct
for risk avoidance but too coarse for the realistic v0.x workflow. A
named-collaborator research evaluation (Band 1) is meaningfully
different from a Hugging Face Spaces public download (Band 2), and
both are meaningfully different from a paid pilot install (Band 3).
GPL-3 explicitly contemplates and authorises the Band-1 scenario
without commercial licensing of upstream dependencies.

### Why a beta-tester count cap of 5

To prevent Band 1 from being used as a de-facto Band 3 in disguise.
5 testers is enough for an early hands-on validation pass against the
v0.5.1 / v0.6 binaural surface plus the v0.4 two-bus VST3 workflow,
without crossing into "commercial pilot" territory. The cap is a hard
falsifier — if the team finds it constraining, the conversation should
shift to "do we have product-market signal and is it time to procure
JUCE Indie?" rather than to "stretch the band-1 definition."

### Why named, written-acknowledged Band-1 handoffs

The GPL-3 source-availability obligation requires the binary distributor
to make the corresponding source available to each recipient. Writing
down the recipient identity, tag commit SHA, and acknowledgement closes
that obligation explicitly per recipient rather than relying on the
public-repo URL alone. The audit-log append discipline also makes
future trigger-2 procurement easier because the lab can answer "who has
which binary" without forensic reconstruction.

### Why Band 2 is gated on lead approval rather than automatically allowed

Public download removes the per-recipient identity audit trail. While
GPL-3 still permits it, the recipient-count amplification means a
single bad-binary release (e.g., a forgotten debug symbol, a
contributor's unlicensed snippet, a non-GPL-compatible test fixture
accidentally bundled) is much harder to recall. The lead's approval is
the human review step that compensates.

---

## Consequences

### Positive

- The roadmap's "external beta tester" workflow now has a clear,
  falsifiable path forward (Band 1, ≤5 testers, written acknowledgement).
- Procurement is **not** required to start beta validation — the v0.6 +
  v0.5.1 binaural surface can be exercised by 1-5 external research
  collaborators without paying for JUCE commercial first.
- The GPL-3 obligations are made explicit per recipient, so a future
  trigger-2 procurement can audit "who got what" cleanly.

### Negative

- Band-1 handoff adds a per-recipient email + acknowledgement +
  audit-log step. Roughly 15-30 minutes of human time per tester.
- Recipients legally retain GPL-3 redistribution rights. A Band-1
  tester *could* republish the binary; the written acknowledgement
  asks them not to during v0.x but is not legally enforceable.
- The 5-tester cap may feel artificial if hands-on goes well. Treat
  it as a forcing function for the procurement conversation.

### Falsifiers (when to revisit this ADR)

- More than 5 prospective Band-1 testers materialise → procurement
  decision needed.
- A Band-1 tester redistributes the binary publicly → escalate to
  trigger 1 retroactively; consider procurement timeline accelerator.
- v1 commercial roadmap firms up → procurement under Band 3 must
  precede any v1 binary delivery, not follow it.
- JUCE pricing changes materially (current quote in
  `docs/license_procurement_plan.md` is illustrative; re-quote at
  procurement time).

---

## Implementation notes for the v0.5.1 / v0.6 cycle

For the immediate **next** DAW hands-on validation (Reaper / Bitwig
checklist in `docs/release/v0.3.0/daw-handson-log.md`):

1. The project lead's own machine and any SNU MARG lab machines are
   Band 0 — no action.
2. Any external collaborator the lead wants to enrol for the v0.6 DAW
   validation must go through the Band-1 ritual (email + ack +
   audit-log append). Suggest enrolling at most 2-3 in the first wave
   to validate the workflow before saturating the 5-cap.
3. If the lead wants to publish the v0.6 binary to GitHub Releases as
   a Band-2 artefact, that decision is **out of scope for the v0.6
   cycle** — it must be its own ADR amendment (or a new ADR) with
   lead approval.

---

## References

- `LICENSE.md` — v0 GPL-3.0-or-later sign-off.
- `docs/license_procurement_plan.md` — JUCE procurement triggers,
  options, audit log.
- ADR 0002 — Native core (C++ + JUCE) — original framework decision.
- `.claude/CLAUDE.md` — project workflow policy (ralplan + autopilot;
  this ADR's drafting was triggered by the P0-2 line in the v0.6
  retrospective even though the implementation work is doc-only).
- `docs/weekly_progress_report_2026-05-18.md` §5 — short-term priorities
  list naming this ADR as P0-2.
- GPL-3 source: `core/JUCE/LICENSE.md` (when JUCE submodule is
  initialised); canonical text <https://www.gnu.org/licenses/gpl-3.0.txt>.
