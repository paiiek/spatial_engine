# Band-1 Distribution Handoff Template

**Template version**: 1.0 (2026-05-19)

> **DISCLAIMER — 법률 자문 없이 작성 / This template is written WITHOUT legal counsel review.**
> Recipient and sender are responsible for independent legal review before signing or relying on it.
> See ADR 0016 §Limitations for the policy context.

---

## Purpose

This template formalises a single Band-1 handoff of a `spatial_engine` binary artefact to a named
external research collaborator, as required by ADR 0016 §Band 1 — Research collaboration.
Complete all five fields below. After both parties sign/date, the sender appends a one-line entry
to `docs/legal/handoff-audit-log.md`.

---

## Field 1 — Recipient identity

| Field | Value |
|-------|-------|
| Full name | _____________________________________________________ |
| Role / position | _____________________________________________________ |
| Organization | _____________________________________________________ |
| Contact email | _____________________________________________________ |

---

## Field 2 — Repository URL

Canonical git URL of the `spatial_engine` repository (the source corresponding to the binary):

```
https://github.com/<org>/spatial_engine
```

*(Replace `<org>` with the actual GitHub organisation or user at the time of handoff.)*

---

## Field 3 — Tag SHA

The specific release being handed off.

```
<TAG> = ___________________________   (e.g., v0.7.0)
<SHA> = ________________________________________________________________
        (40-character git commit SHA; verify with `git rev-parse <TAG>`)
```

The binary conveyed corresponds exactly to the source reachable at `<REPO_URL>/tree/<SHA>`.

---

## Field 4 — Recipient acknowledgement

By signing below, the recipient confirms all of the following:

1. I have read ADR 0016 (`docs/adr/0016-external-distribution-policy.md`) and understand that
   this binary is distributed under **GPL-3.0-or-later**.
2. I understand ADR 0016 §Limitations: this ADR was authored without legal counsel review and is
   not a substitute for independent legal advice. I accept responsibility for my own compliance
   obligations under GPL-3.
3. I have received the full corresponding source URL (Field 2) and tag SHA (Field 3), which
   satisfies the §6.d source-availability obligation.
4. I commit not to redistribute this binary to third parties during the v0.x cycle (prior to
   Band-3 commercial release), as requested in ADR 0016 §Band 1 — though I retain all GPL-3
   redistribution rights.
5. I understand this handoff is logged in `docs/legal/handoff-audit-log.md` per ADR 0016.

```
Recipient name  : _______________________________
Recipient signature : _______________________________
Date (ISO 8601) : _______________________________
```

*(Email reply containing the text "I have read and accept the Band-1 handoff terms in
BAND_1_HANDOFF_TEMPLATE.md v1.0 for tag <TAG>" is accepted in lieu of physical signature.
Sender must save the email thread alongside this completed template.)*

---

## Field 5 — Audit-log entry (sender completes)

After recipient acknowledges, append the following one-line entry to
`docs/legal/handoff-audit-log.md` under `## Entries`:

```
<DATE ISO8601> | <Recipient name — Role — Organization — email> | <TAG>=<SHA> | template-v1.0 | <Sender name>
```

Example:

```
2026-05-19 | Alice Researcher — PhD Student — MIT CSAIL — alice@mit.edu | v0.7.0=abc123...def456 | paiiek
```

---

## Sender checklist (before sending the binary)

- [ ] Commit tagged: `git tag v0.x.y` and pushed to origin.
- [ ] `git rev-parse <TAG>` output matches Field 3 SHA.
- [ ] Recipient has been emailed: this completed template, `LICENSE.md`, corresponding release notes.
- [ ] Recipient acknowledgement received (email reply or physical signature).
- [ ] Audit-log entry appended to `docs/legal/handoff-audit-log.md`.
- [ ] Recipient count after this handoff is ≤ 5 (ADR 0016 v0.x cap). If this would be the 6th,
  STOP and escalate to Band-3 / procurement conversation before proceeding.
