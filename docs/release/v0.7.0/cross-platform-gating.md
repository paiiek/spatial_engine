# Cross-Platform CI Gating — v0.7

## Current leg status

| Leg | Runner | Status | Owner |
|-----|--------|--------|-------|
| `core-linux-arm64` | `ubuntu-24.04-arm` | **REQUIRED** (v0.7 D-S5) | — |
| `core-macos-arm64` | `macos-14` | signal-only (AS-6) | `paiiek` |

## 5-green soak gate (§7.1 Scenario C)

Before this promotion PR is considered stable, 5 consecutive green runs of `core-linux-arm64`
must be observed on `main`. The soak happens AFTER merge. Track runs in the Actions tab:
`https://github.com/<org>/spatial_engine/actions/workflows/cross-platform.yml`

If >2 consecutive flakes occur post-merge, trigger the AS-4 rollback procedure below.

## Branch protection click-path (TODO for repo-admin)

1. Go to Settings → Branches → Edit rule for `main`.
2. Under "Require status checks to pass before merging", search for and add:
   `core-linux-arm64 (linux-arm64 / aarch64)`
3. Save. From this point, any PR where `core-linux-arm64` fails cannot merge.

## Unblock when ARM64 required gate is red (AS-4 rollback procedure)

If `core-linux-arm64` becomes required and a failure blocks all PRs including the revert:

### Option-A — Admin bypass merge (preferred)
1. Repo admin uses GitHub "Merge without waiting for requirements" on the revert PR.
   (Settings → Branches → bypass list, or via the merge button dropdown on the PR.)
2. Admin MUST leave a comment on the PR with the issue/ticket link explaining the bypass.
3. Audit log entry is mandatory (GitHub records this automatically).

### Option-B — Temporary requirement removal (fallback)
1. Repo admin removes `core-linux-arm64` from required status checks.
2. Merges the revert PR normally.
3. Re-adds `core-linux-arm64` to required status checks.
4. Gap window MUST be < 15 minutes.
5. Document the window in `docs/weekly_progress_report_2026-05-25.md` §5.3.

### Re-promotion after fix
After the fix branch is ready, a fresh 5-green soak is required before re-flipping
`continue-on-error`. Same rule as initial promotion. Document in `docs/ci-promotion-history.md`.

## Critic §C.4 promotion order

- **P0 (v0.7):** Linux ARM64 — promotes first (this document).
- **P1 (post-v0.7):** Relacy CI — promotes after Linux ARM64 is confirmed stable.

## macOS-14 (Apple Silicon) — AS-6 deferral

The `core-macos-arm64` leg remains signal-only in v0.7. Named owner: `paiiek`.
Promotion to required is scoped to v0.8 and requires:
1. 1 full green cycle post-v0.7 ship.
2. `macos-arm64-verify.md` checklist sign-off by `paiiek` (physical Apple Silicon Mac triage).
3. Fresh 5-green soak per §7.1 Scenario C criteria.
