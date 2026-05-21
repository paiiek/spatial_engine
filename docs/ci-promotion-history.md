# CI Promotion History

Records when CI legs are promoted from signal-only to required (blocking) status.
Cross-reference: `docs/release/v0.7.0/cross-platform-gating.md` for rollback procedures.

---

## v0.7 — Linux ARM64 promotion (2026-05-19)

**Leg:** `core-linux-arm64` (runner: `ubuntu-24.04-arm`, arch: `aarch64`)
**Status before v0.7:** `continue-on-error: true` (signal-only, v0.6 cycle)
**Status after v0.7:** REQUIRED — `continue-on-error` removed. ARM64 failure BLOCKS merge.

**Plan reference:** `spatial-engine-v0.7.md` §2 Item #5 (D-S5), Architect §A.4, Critic §C.4 promotion order.

**5-green soak gate:** Per §7.1 Scenario C pre-mortem criteria, 5 consecutive green runs on
`linux-arm64` must be confirmed before this promotion is considered stable. The soak occurs
AFTER merge of the promotion PR. If the gate fails (>2 consecutive flakes post-merge), the
AS-4 rollback procedure applies — see `docs/release/v0.7.0/cross-platform-gating.md`.

**Critic §C.4 promotion order:** Linux ARM64 is P0 (promotes first in v0.7). Relacy CI
promotion is P1, deferred until after ARM64 is stable.

**TODO(repo-admin):** After merging the v0.7 promotion PR, mark
`core-linux-arm64 (linux-arm64 / aarch64)` as a required status check in
Settings → Branches → branch protection rules for `main`.

---

## v0.7 — macOS-14 (Apple Silicon) owner assignment (2026-05-19)

**Leg:** `core-macos-arm64` (runner: `macos-14`, arch: `arm64`)
**Status:** signal-only (`continue-on-error: true`). No promotion in v0.7.
**Named owner:** `paiiek` (AS-6 deferral)
**Promotion target:** v0.8, after:
  1. 1 full green cycle post-v0.7 ship.
  2. `macos-arm64-verify.md` checklist sign-off by `paiiek`.
  3. Fresh 5-green soak per §7.1 Scenario C criteria.

**Plan reference:** `spatial-engine-v0.7.md` §2 Item #5 (AS-6), Architect §A.4.
