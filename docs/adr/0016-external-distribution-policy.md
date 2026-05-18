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
- `.omc/plans/critic-r-v0.6-retro.md` §A.3 + §D MEDIUM-3 — Critic
  retroactive review that drove Appendix A below.

---

## Appendix A — GPL-3 §6 obligation mapping per band

Added 2026-05-18 in response to the v0.6 critic retroactive review §A.3
finding that the original ADR enumerated four distribution bands without
spelling out which subsection of GPL-3 §6 ("Conveying Non-Source
Forms") applies to each. The omission was real but bounded — no Band-1
or Band-2 handoff has occurred during the v0.x cycle — so the mapping
below is *expressed forward* rather than retroactive.

**GPL-3 §6 recap** (paraphrased; the canonical text is at
<https://www.gnu.org/licenses/gpl-3.0.txt>): when you *convey* a covered
work in non-source (binary) form, you must accompany it with the
corresponding source under one of the following options:

- **§6.a** — convey the source on a physical medium with the binary.
- **§6.b** — convey the binary on a physical medium *and* accompany it
  with a **written offer, valid for at least three years**, to provide
  source to any third party for no more than the cost of physically
  performing the source conveyance.
- **§6.c** — accompany the binary with the §6.b written offer (only
  available for noncommercial conveyance and only if you received the
  binary with such an offer).
- **§6.d** — convey the binary by offering access from a *designated
  place* (e.g., a network server) *with equivalent access* to the
  source from the same or a different designated place at no further
  charge. The source-server URL must remain accessible **for as long
  as you are offering the binary**.
- **§6.e** — peer-to-peer conveyance accompanied by §6.d-style
  information about where peers can get the source.

### Per-band §6 election (this project's choice)

| Band | Distribution mechanism | §6 election | Why this election | Audit-log entry required |
|------|------------------------|-------------|-------------------|--------------------------|
| **Band 0** — internal lab | No conveyance occurs. The binary stays on machines owned/operated by SNU MARG. | **N/A** — §6 only triggers on *conveyance* to a third party. | Internal use does not engage GPL-3 §6 at all. | No. |
| **Band 1** — named research collaborator (≤5, written ack) | Direct binary handoff (email attachment, USB stick, cloud storage link). Recipient identity is known + recorded. | **§6.d** — the public git repository (e.g., `git@github.com:paiiek/spatial_engine.git`) is the "designated place" + the recipient is told the exact tag commit SHA so they can fetch the corresponding source. The repo URL is the equivalent-access source location. | Direct conveyance to a known recipient with a stable source URL is the cleanest mapping. §6.b (3-year written offer) is an admissible fallback if the repo URL becomes inaccessible; we do not pre-issue the §6.b offer because the repo is the source of truth. | Yes — record tag commit SHA, repo URL, recipient identity, ack timestamp in `docs/license_procurement_plan.md §Audit log`. |
| **Band 2** — public download | Public-URL binary distribution (e.g., GitHub Releases attachment, lab website). Recipients individually unknown. | **§6.d** — the GitHub Releases page itself is the "designated place"; the corresponding source is at the same tag in the public repo. The release page must explicitly link the corresponding source tag. | Same rationale as Band 1, scaled. §6.b is undesirable here because it shifts a recurring 3-year obligation onto the project for each release. §6.d's "equivalent access" via the same repo is much easier to maintain. | Yes — append release-page URL + corresponding source tag to the audit log. |
| **Band 3** — commercial / paid / productized | Commercial product, paid pilot, exhibition install. | **N/A under GPL-3** — Band 3 is gated on JUCE commercial license procurement (see `docs/license_procurement_plan.md`) before any conveyance. Once JUCE commercial is procured, the project is no longer constrained to GPL-3 for distribution; the license terms shift to the JUCE commercial terms + whatever the project chooses for its own code. | The point of Band 3 is precisely to exit the GPL-3 obligation regime by paying for the commercial license. | N/A — JUCE procurement record replaces the GPL audit log for Band 3 binaries. |

### The §6.b "written offer for 3 years" option — when to use

The project does **not pre-issue** §6.b written offers because §6.d
satisfies the obligation more cleanly via the public repo. However, a
§6.b offer becomes the safer election in any of these scenarios:

- The repository visibility changes (private → public is irrelevant;
  public → private would force §6.b on all prior Band 1/2 recipients).
- The repository is taken down or migrated to a non-equivalent host.
- A specific recipient asks for the §6.b form explicitly (e.g., their
  organization's compliance team requires a written offer artifact).

If a §6.b election ever becomes necessary, the template is short
(roughly 6 sentences). A draft is **not currently in this ADR** — it
should be drafted at the trigger moment with the legal review pass
described below.

---

## Limitations & legal review status

This ADR was authored by the project lead (without legal counsel) in a
doc-tightening pass during the v0.6 cycle. The §Decision and Appendix A
above represent **the project's own interpretation** of GPL-3 §6 as
applied to four specific distribution patterns. They are **not a
substitute for legal review** and have not been reviewed by a lawyer or
the Software Freedom Law Center.

**Hard rule:** before the **first** Band-1 conveyance to a non-SNU-MARG
recipient (and unconditionally before any Band-2 or Band-3 conveyance),
the project lead should:

1. Have this ADR (Decision + Appendix A) reviewed by an attorney
   familiar with FOSS licensing (e.g., SNU's tech transfer office, or
   an external counsel familiar with GPL-3). Cost: a few hours of
   counsel time; not gating for SNU-internal research collaborators
   under Band 0 / 1 where audit-trail discipline is the primary risk
   control.
2. Adjust the §Decision bands or Appendix A based on the review.
3. Append a `### Legal review status` block to this ADR noting the
   reviewer, date, and any amendments. Mark this ADR as Status:
   "Accepted (legal-reviewed YYYY-MM-DD)" once complete.

Until that review happens, this ADR is the project's **operational
policy** for staying inside the lab (Band 0) and onboarding a small
number of named research collaborators (Band 1, ≤5, written ack), but
it is **not a defense** that would survive an FSF enforcement letter or
a Software Freedom Conservancy compliance inquiry without on-the-spot
counsel engagement. The audit-log discipline at
`docs/license_procurement_plan.md §Audit log` exists precisely to make
that counsel engagement (if ever needed) fast and cheap by giving
counsel a complete record of *what was conveyed to whom and when*.
