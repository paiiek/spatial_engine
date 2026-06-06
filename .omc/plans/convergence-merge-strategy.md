# Convergence → main Merge Strategy (Phase 0.3 decision)

**Decided:** 2026-06-07 · **Branch:** `feat/dreamscape-convergence` · **Base:** `main@213b19e`

## State at decision time

| Fact | Value |
|---|---|
| `main` vs branch | **0 behind / 56 ahead** (`git rev-list --left-right --count main...HEAD`) |
| merge-base | `213b19e` == current `main` HEAD (main has **not** moved since branch was cut) |
| Files changed vs main | 93 |
| Mergeability | **Clean fast-forward** possible today (no divergent main commits) |
| Remote | branch pushed to `origin` (git@github.com:paiiek/spatial_engine.git), upstream tracking set |

## Decision

1. **Merge timing = Phase 5** (release prep), per v1.0 plan §5.1. Rationale: keep
   the long-lived feature branch as the integration surface through Phases 1–4 so
   each increment commits + pushes there; merge to `main` only when the v1.0 DoD
   (perf measured, flake 0, rights cleared) is met. Avoids destabilizing `main`
   mid-feature.

2. **Interim sync policy:** `main` is **frozen** for the duration of convergence
   (no parallel work lands on it — verified 0 divergent commits). Therefore **no
   periodic rebase is needed**. Before the Phase-5 merge, re-run
   `git rev-list --left-right --count main...HEAD`; if `main` is still 0-ahead,
   a fast-forward (or `--no-ff` merge commit for a clean release boundary) lands
   the branch with **zero conflict risk**.

3. **If `main` moves before Phase 5** (someone lands work): switch from
   fast-forward to `git rebase main` on the branch (preferred — keeps linear
   history) or a merge commit; resolve conflicts then, re-run the full both-builds
   ctest (64 + 128) before proceeding. Re-validate the coordinate golden tests
   and no-alloc gate specifically (highest-risk overlap surface).

4. **Merge mechanics at Phase 5:**
   - `--no-ff` merge into `main` to preserve a visible release boundary, OR
     fast-forward if a linear history is preferred — decide at 5.1.
   - Tag `v1.0.0-rc` immediately after merge (CMake `VERSION` already staged at
     0.9.0; bump to 1.0.0 at the tag — plan §5).
   - Do **not** squash: the 56 increment commits carry the per-increment
     provenance (each `feat(...)` + `docs(plan)` pair) that PROVENANCE.md and the
     review trail reference.

## Cross-references
- v1.0 plan §5 (release): `.omc/plans/spatial-engine-v1-full-coverage-plan.md`
- Provenance / rights: `docs/legal/PROVENANCE.md`
