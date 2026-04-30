# JUCE License Procurement Plan (C5)

**Owner**: project lead (Seungryeol Paik / SNU MARG).
**Status**: v0 GPL-compliant; commercial license **not yet procured**; trigger event has not occurred.

## Why this plan exists

JUCE 7.x ships under a dual GPL / commercial license. While `spatial_engine` v0 is
developed for lab / research use we may use the GPL track. As soon as a binary
derived from this repo is **distributed externally** or **deployed commercially**, a JUCE
commercial license becomes mandatory before that distribution / deployment.

## Trigger events (commit to procurement when ANY of these occur)

1. **First external distribution of any binary** built from this repository
   (handing a `.deb`, `.AppImage`, signed installer, or any compiled artifact to anyone outside
   the SNU MARG lab).
2. **First commercial deployment** outside the user's research lab — performance, exhibition,
   client install, productized v1+ release.
3. **First contributor outside SNU MARG**, if the contributor's organization requires
   commercial-license-already-procured before code review.

## Options (pricing per JUCE.com as of 2026-04; verify at trigger event)

| Plan | Cost (USD) | Distribution rights | Notes |
|------|-----------|---------------------|-------|
| **JUCE Indie** | ~$40 / month per developer | Up to **$500K/yr** in revenue | Cheapest; matches early commercial deployment |
| **JUCE Pro** | ~$130 / month per developer | Unlimited revenue | Required if revenue or distribution scale exceeds Indie cap |
| **JUCE Education** | $0 | Education-only; commercial use forbidden | Useful if v0+ stays in lab indefinitely |
| **Perpetual** | Negotiate with JUCE | Locked-in v7.x perpetual rights | Insurance against future price hikes |

> Numbers are illustrative — re-quote at trigger event.

## Procurement steps (when trigger fires)

1. Owner files a `juce-license-procurement` issue with a 30-day SLA.
2. Determine seat count: each developer who **builds locally** counts as one seat.
3. Determine plan: Indie if first commercial install OR projected annual revenue < $500K;
   Pro if >$500K / year; Perpetual if a multi-year fixed cost is preferred.
4. Procure via JUCE.com checkout; capture invoice and license key in
   `~/.config/spatial_engine/juce_license.toml` (gitignored).
5. Update `LICENSE.md` to reflect the new dual-license posture and remove the GPL-only
   contributor reminder.
6. Audit all PRs since last GPL-only checkpoint to confirm contributors had not assumed
   GPL-only and require a CLA-equivalent for commercial relicensing of their lines.

## Cost model (rough; revisit at trigger T)

- v0 → v1 transition: 1–2 developers, 1 lab → 1 deployment
  - Indie at $40/mo × 2 devs × 12 mo ≈ **$960/yr**
- v1 staged production rollout: 2 devs, 5+ deployments
  - Pro at $130/mo × 2 devs × 12 mo ≈ **$3,120/yr** OR Perpetual one-shot per JUCE Sales

## v0 contributor reminder (replicated in `LICENSE.md`)

PRs to v0 must be **GPL-compatible**. No proprietary blobs; no copyleft-incompatible licenses.
At trigger T the project may need to relicense; contributors will be contacted then.

## Audit log (append-only)

- 2026-04-30 — plan filed under P0 (no trigger fired; v0 still GPL-only).
