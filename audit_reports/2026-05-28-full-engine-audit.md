# Full-Engine Audit — spatial_engine @ `868f750` (v0.7.0+5) — 2026-05-28

Independent multi-reviewer audit (5 parallel streams, fresh-eyes/no-prior-context). Remediation plan: `.omc/plans/spatial-engine-v0.8-audit-remediation.md`.

## Ground truth (HEAD 868f750)
- Build NO_JUCE: **0 compiler warnings**.
- `core/build` ctest: **95/95 passed** (build_rton RT-sentinel = 97/98).
- pytest: **225 passed / 4 skipped**.

## Verdicts
| Lens | Verdict |
|---|---|
| Architecture | SOUND-WITH-CONCERNS (runtime design solid; doc/process/hygiene debt) |
| DSP correctness | CORRECT-WITH-RISKS — **2 real defects + 1 RT-safety hole** |
| Security (attack surface) | **NO REACHABLE VULNS** within trust boundary (ADR 0016/0019 §10); deps hardening only |
| Test adequacy | ADEQUATE-WITH-GAPS |

## Headline findings (full inventory in the remediation plan)
**MAJOR (real shipped bugs, CI-invisible):**
- **DSP-1** Runtime ambisonic decoder-type switch silently ignored — `applyPendingDecoderTypeChange()` never called in the production control loop (only in `prepareToPlay`). = unresolved **M2HOA-Q14**. Live PINV→MAX_RE/ALLRAD/EPAD/IN_PHASE switch never takes effect until device restart.
- **DSP-2** SH encoder not SN3D-normalized for m≠0 channels (orders 2-3): ACN 4/5/7/8 peak 0.866 not 1.0 (peak-over-sphere verified). Wrong for B2 binaural + external AmbiX interchange.
- **DSP-3** VBAP cold-cache-miss allocates `std::vector` on the audio thread inside the RT-no-alloc scope (xrun risk on fast pans/scene changes; RT-sentinel covers it).

**HIGH (test blind spot):**
- **Test-1** 29 VST3 state/param tests excluded from the NO_JUCE `core/build` CI → a `setState` deserializer regression passes "95/95 green".

**MEDIUM:** SpatialEngine god-object (Arch-1); duplicate ADR 0006 + missing 0007-0009 (Arch-2); ADR 0018/0019 still "Proposed" though shipped (Arch-3); open-questions.md stale — 99 open, ADM-OSC cohort shipped v0.2.0, and it had M2HOA-Q14/DSP-1 open-but-unwired (Arch-4); missing golden vectors — FDN T60, ambisonic absolute-gain, HrtfLookup interpolation (Test-2/3/4); `vst3_bind_collision` passes vacuously under `-j` (Test-5); EPAD energy scale + VBAP fallback (DSP-4/5 minor).

**LOW:** no `[Unreleased]` CHANGELOG (Arch-5); 7.8GB build-dir sprawl + stray `NUL` + empty `core/src/matrix/` (Arch-6); OSC sleep-barrier flakes (Test-6); shm regular-file branch w/o `O_NOFOLLOW` (Sec-1, PR3-Q7 deferred); Python dep advisories on WebGUI/dev tooling only (Sec-4).

## Key cross-cutting insight
The runtime design is genuinely strong (RT hot path is alloc/lock/throw-free, no control↔audio race, no in-boundary security vulns, 0-warning build). The problems cluster in two places: real DSP defects hidden behind passing tests, and a **stale open-questions tracker that had already recorded one of the defects (M2HOA-Q14 = DSP-1) as a to-do but left it unwired**. Hygiene debt was masking a functional defect.

## Remediation
All findings MAJOR→LOW scheduled in `.omc/plans/spatial-engine-v0.8-audit-remediation.md`, phases P1-P7, autonomous phased execution with per-phase verify+commit and a resumable progress tracker.
