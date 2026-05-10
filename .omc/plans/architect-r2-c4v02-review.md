# Architect Round-2 Verification — Phase C4 + v0.2 Release Plan

**Plan**: `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` (1563 lines, was 1098)
**Round-1 review**: `.omc/plans/architect-r-c4v02-review.md`
**Critic Round-1 review**: `.omc/plans/critic-r-c4v02-review.md`
**Verdict**: **APPROVE — Architect Round-2 APPROVE**

---

## Round-1 blocker disposition (7/7 CLOSED)

| # | Blocker (R1) | Status | Evidence |
| - | ------------ | ------ | -------- |
| 1 | ADR carve-out: plugin↔sidecar UDS orthogonal to ADR 0003 | **CLOSED** | ADR 0010 §Context, lines 1284–1294 — explicit carve-out citing ADR 0003 §Migration target ("shm + UDS for v1+") as orthogonality basis; A1-ε vs A1-δ scope split called out. |
| 2 | ADR 0011 (was 0008) hardening: locking + GC + EPIPE + `schema_version` | **CLOSED** | ADR 0011 §Hardening rules, lines 1331–1347 — atomic `tmpfile + rename(2)`, `flock(LOCK_EX)`/`LOCK_SH`, `/proc/{pid}/comm` PID liveness, GC sweep (5s), EPIPE handling at sidecar relay (WARN→stderr), `schema_version=1` field at top of registry. All 5 R1 sub-asks present. |
| 3 | State v3 / kMute deferred to v0.3 (decoupled from A1-δ) | **CLOSED** | §1.4 deliverable matrix line 157 (state v3 → v0.3); §1.5 freeze line 175 (A2.1-α-temporary in v0.2, A2.1-β v0.3); §A2.1 lines 267–282 (Round-2 freeze rationale, C2B-Q2 pre-resolution cited); §6 risk row line 991 ("DEFERRED to v0.3"). Coupling to A1-δ removed — kMute now justified independently as "no consumer until C4 sidecar lands". |
| 4 | Plugin-process thread topology contract (N ≤ 8 ceiling) | **CLOSED** | ADR 0010 §Thread-budget invariant, lines 1299–1305 — 1 SPSC ring (audio→sidecar) + 1 reverse-channel reader = 2 threads/instance; **N ≤ 8 plugin instances per host**; single sidecar consumer drains all rings. Documented in v0.2 ADR draft so contract survives the deferral. |
| 5 | Sidecar binary as B3-β artifact (DISSOLVED post-deferral) | **CLOSED** | §1.5 line 181 ("Sidecar binary NOT included (item 5 deferral)"); §B3 option-row line 569 ("source + prebuilt VST3 .so" — no sidecar mentioned); §1.4 matrix line 155 (sidecar = v0.3 only); Appendix D item 5 line 1480 ("Sidecar binary explicitly NOT in v0.2 B3-β artifact"). Cleanly dissolved. |
| 6 | v0.1.1 rollback ↔ §B1-β contradiction | **CLOSED** | §B1 v0.1.1 reservation policy lines 545–548 ("v0.1.1 strictly for true patch-only emergencies … NOT an escape hatch for C3"); Pre-mortem 3 decision tree lines 905–917 (revised — fallback is "ship v0.2.0 with affected param marked experimental", v0.1.1 explicitly forbidden as a C3 carrier per §B1 Why-not-γ). Direct contradiction resolved with self-citation. |
| 7 | Falsifiable per-param test criteria in R3 | **CLOSED** | §R3 Acceptance table lines 686–694 — 7-param table (kPanAz, kPanEl, kSourceWidth, kMasterGain, kAmbiOrder, kRoomPreset, kBypass) each with measurable criterion (impulse polar log angle ±1°, 66 dB ramp, `cmp` of recorded WAV, etc.). `test_vst3_bypass` reused per R1 ask. Criteria are reviewer-objective. |

---

## Cross-coupling spot-checks (Critic items I concurred on)

- **§1.5 Final Decision Freeze**: Present at lines 164–187. One value per axis (A1, A2, A2.1, A3, A4, A5, B1..B5); zero conditional branches; rationale cites which reviewer prevailed. **PASS**.
- **§1.4 Deliverable matrix**: Present at lines 150–162; v0.2.0 / v0.3.0 / v1.0.0 columns; cleanly maps Architect §6 path-a synthesis. **PASS**.
- **ADR numbering**: Consistent — `0010 / 0011 / 0012` everywhere; zero residual `0007/0008/0009` references (`grep -cE 'ADR 000[789]\b' = 0`). Old `0006-*.md` collision avoidance documented at lines 145–148. **PASS**.
- **Pre-mortem 5 (GLIBC)**: Added at lines 953–978; mitigation locked into B3-β (line 181), R5 asset filename (line 753 `spatial_engine_v0.2.0_linux_glibc239.tar.gz`), R4 §호환성 (lines 723–726), §6 risk row line 997. **PASS**.
- **Appendix D Round-2 Changelog**: Present at lines 1435–1561; 13 items with edit location + before/after + reviewer attribution. **PASS**.

---

## New defects introduced by Round-2 patch

**None at architectural level.** Two minor observations (NOT blocking):

1. **Open question §6 line 1429** ("ADR 0010 vs 0011 split: should they be one combined ADR?") is a meta-question that should be resolved before Round-3 ADR drafting in S1 — but this is a planning artifact, not a defect; the freeze table already commits to the two-ADR split, so the open-question is residual editorial noise.
2. ADR 0012 §Status line 1357 says "(Pre-mortem 2 escape)" but Pre-mortem 2 is in §6 prose only and doesn't have a numbered section header in the plan. Cross-reference is ambiguous but not load-bearing for autopilot.

Neither item warrants ITERATE.

---

## Steelman antithesis (Round-2 verification — required by Architect protocol)

**Strongest counterargument against approving**: The plan now has a §1.4 matrix that defers 7/8 of Track A to v0.3, which means v0.2.0 ships with **only ADR drafts** as net-new C4 content. A skeptical reviewer would argue this is a "v0.2 in name only" — the substantive C4 work is unchanged in v0.3, and the v0.2 release is effectively just C3-standalone (which was already done in commit `e5924da`). Why bother cutting v0.2 at all rather than rolling C3 into a larger v0.3?

**Refutation**: §B1 Why-not-γ (line 540) and Driver 2 (Korean live-venue first-customer ETA pressure, §1.2) settle this — C3 + 4 HOA decoders + Korean manual is significant feature delta from v0.1.0 by semver standards, and the customer has a pending ETA that v0.3 (4–5 weeks later with sidecar) cannot meet. v0.2 carries the user-facing value; v0.3 carries the infrastructure. This is a sound product-shaping decision and the plan's §1.5 freeze + §1.4 matrix make it auditable.

**Tradeoff tension acknowledged**: We pay one extra release-cycle of OFF byte-baseline re-pin churn (once for v0.2, again for v0.3 when state v3 lands) in exchange for not coupling first-customer ETA to sidecar implementation risk. Acceptable on the evidence.

---

## Verdict

**Architect Round-2 APPROVE**.

All 7 R1 blockers have explicit, line-level closure. The §1.5 freeze removes every conditional branch that would force autopilot into architectural judgment calls. The §1.4 matrix cleanly partitions v0.2 (S1 + R1..R7) from v0.3 (S2..S8) per the Architect §6 path-a synthesis. ADR numbering is consistent. Pre-mortem 5 is integrated end-to-end. Appendix D provides a complete audit trail.

The plan is **autopilot-ready** subject to Critic Round-2 verification of items C-1..C-5. From the Architect lens, no further iteration is required.

**Confidence**: HIGH — every Round-1 ask has a verifiable file:line counterpart in Round-2, and no new contradictions were introduced.
