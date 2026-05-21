# spatial_engine v0.7 iter-2 — Architect re-review (deliberate)

| | |
|---|---|
| **Date** | 2026-05-18 |
| **Reviewer** | Architect (omc-architect; iter-2 verification pass against Planner's amended plan) |
| **Plan under review** | `.omc/plans/spatial-engine-v0.7.md` iter-2 (Planner; 747 lines, +257 vs iter-1) |
| **Prior reviews** | `.omc/plans/architect-r-v0.7.md` (iter-1, APPROVE-WITH-RECOMMENDATIONS, 4 Must-fix), `.omc/plans/critic-r-v0.7.md` (iter-1, ITERATE, 16 items) |
| **Mode** | DELIBERATE — Planner integrated 16 amendments; verify no regressions introduced |
| **Verdict preview** | **APPROVE** — all 4 iter-1 Must-fix have landed as intended (3 LANDED-AS-INTENDED, 1 LANDED-WITH-NUANCE); Critic 16 items integrated correctly with one minor over-correction worth flagging; 2 new iter-2 risks identified, both are Should-have not blockers. Plan is autopilot-ready conditional on §C clarifications being noted (not blocking). |

This review deliberately does NOT re-litigate iter-1 findings already on record. It focuses exclusively on iter-2 deltas.

---

## §A. AM-1..AM-4 land-verification

Cross-checked iter-2 plan against my iter-1 §D Must-fixes. Citations are to iter-2 plan line numbers.

### §A.1 AM-1 (D-S1 strike-counter reset must be explicit) — **LANDED-AS-INTENDED + EXTENDED**

**iter-2 evidence (lines 156-166):** Item #1 Fix step 3 now enumerates a **6-atomic reset** in explicit order, not just 3:
- D-S3 telemetry atomics first (`runtime_demote_max_ratio_x1000_=0`, `runtime_demote_max_ratio_at_event_x1000_=0`).
- v0.6 #5 sticky state next (`runtime_demote_strikes_=0`, `runtime_demote_warning_pending_=false`, `runtime_demoted_=false` — **explicit "clear demote flag LAST"** ordering to prevent the race-observe gap).
- Cooldown snapshot last (`runtime_demote_last_reset_ns_=now_ns`, `reset_cooldown_warning_emitted_=false`).

**Critic §A.1 extension Planner integrated correctly:** my iter-1 AM-1 listed only the 3 v0.6 atomics; Critic correctly caught that D-S3 introduces 2 more that need clearing. iter-2 plan picks this up explicitly (lines 160-161).

**Zero-hysteresis regression gate landed (line 183, ctest step 4 "CRITICAL"):** "drive a single over-budget `recordB2BlockTiming` call — assert `runtime_demoted_.load() == false` (zero-hysteresis bug would re-demote here)." This is exactly the AM-1 regression invariant.

**Architect-side check on the LAST-clear ordering rationale (line 162).** The plan justifies clearing `runtime_demoted_` last with the rationale: "audio thread's early-return at `BinauralMonitor.cpp:464` cannot race-observe `runtime_demoted_=false` AND `runtime_demote_strikes_>=threshold` simultaneously." This is correct — if `runtime_demoted_=true` still holds while strikes is being cleared to 0, the audio thread early-returns at line 464 and never observes the intermediate state. **Sound reasoning.** One nuance the plan doesn't surface: under acquire/release semantics, the writer-side ordering of release-stores is honored only if the reader uses acquire-load on the *flag it gates on*. The audio thread reads `runtime_demoted_` with `memory_order_acquire` (BinauralMonitor.cpp:464) — confirmed via iter-1 verification. So the synchronization is well-formed. **Pass.**

**Verdict: LANDED-AS-INTENDED + EXTENDED (Critic-driven D-S3 atomic addition is strictly stronger).**

### §A.2 AM-2 (D-S3 invert default — relaxed-store first) — **LANDED-WITH-NUANCE (Critic correction is binding)**

**iter-2 evidence (lines 251-270):** The plan now ships **relaxed-load-then-store as DEFAULT** (lines 253-260 comment block: "AM-2 relaxed-load-then-store pattern (NOT CAS)"). The code block at lines 261-269 is exactly the Critic §A.2 corrected pattern: `int cur = atomic.load(relaxed); if (new > cur) atomic.store(new, relaxed);`.

**Critic correction on my iter-1 wording.** My iter-1 AM-2 said `store(max(current, new), relaxed)` — Critic correctly flagged this as "two non-atomic operations evaluated together" because `max(current, new)` requires reading `current` separately. The Critic's read-then-store pattern is the strictly correct relaxed equivalent. **Concede: my iter-1 wording was sloppy; Critic's correction is binding.** iter-2 plan uses the Critic-corrected pattern. **Good.**

**Mitigation ordering inversion in §7.1 Scenario A (line 636) landed correctly.** The pre-mortem now says: "iter-2 ships relaxed-load-then-store as the DEFAULT (NOT CAS). … CAS promotion is the ESCALATION, deferred to v0.8 only if telemetry-informed precision need surfaces. Iter-1's CAS-first / relaxed-as-fallback ordering was the WRONG mitigation order."

**One residual question Planner does NOT address:** the iter-2 pattern is correct *only if the audio thread is the sole producer*. Plan justifies this at line 254-255 ("audio thread is sole producer per `SpatialEngine::audioBlock` single call site"). I verified this in iter-1. But there is no compile-time or static guard preventing a future contributor from adding a second producer (e.g., the heartbeat IO thread reading and clearing the max-ratio as part of telemetry). The relaxed-pattern correctness depends on this invariant. **See §C.2 below for new risk.**

**Verdict: LANDED-WITH-NUANCE — Critic's read-then-store correction is binding over my iter-1 wording. The semantic shift (default = relaxed) lands correctly.**

### §A.3 AM-3 (encode-API option (b) — dedicated `sendReplyImplIIF`) — **LANDED-AS-INTENDED + STRONGER**

**iter-2 evidence (lines 285-312):** Item #3 (c) explicitly locks option (b). The inline comment block at the new impl (lines 289-302) goes further than my iter-1 ask — it spells out *why* (a) was rejected, *what* future contributors should do for the next mixed-type shape ("add another dedicated `sendReplyImpl<shape>`; do NOT fuse with `sendReplyImpl`"), and references ADR 0017 §B for the canonical rejection rationale.

**ADR 0017 §B addition (lines 695-696):** The retrospective ADR explicitly pins the rejection: "**Future maintainers: do NOT fuse `sendReplyImplIIF` with `sendReplyImpl`.** If a second mixed-type outbound emerges, add another dedicated `sendReplyImpl<shape>`; consider a small helper struct refactor only once 3+ mixed-type impls exist." This is exactly the lock-strength I asked for in iter-1 §F.2 ("future-maintainer cannot re-fuse").

**Critic §A.3 stronger reasoning Planner correctly integrated:** "v0.6 #8's invariant was 'the 3 simple typetags `,s/,sf/,i` share an impl' — not 'every future typetag must share one impl.'" This is in the iter-2 ADR §B language verbatim (line 696: "mixed-type packets… are a different SHAPE of packet"). **Lock is strong.**

**Verdict: LANDED-AS-INTENDED + STRONGER (3-place reinforcement: code comment + plan §2 (c) + ADR 0017 §B).** Future re-fusion is now well-defended.

### §A.4 AM-4 (pytest packet-ordering invariant) — **LANDED-AS-INTENDED (DOWNGRADE accepted)**

**iter-2 evidence (§-1 changelog line 29):** "Plan §3 'ordering requirement' REMOVED from existing test; new `test_binaural_diag_emitted_on_demote` documents expected ordering as a comment for human readers, asserts presence + per-emission latency budget only."

**Independent verification of Critic's pytest-source claim.** I confirmed via `grep` on `tests/soak_harness/test_osc_warning_channel.py:187-242`: the test uses `ambivs_at` and `no_sofa_at` independently, drains until both are set, then asserts each is within `PER_EMISSION_LATENCY_BUDGET_MS`. **No ordering assertion exists.** The test does presence + latency, not ordering. The Critic was right; my iter-1 Scenario F concern was overstated for this specific test. **Downgrade accepted.**

The §C.6 underlying concern (D-S2 + D-S3 interaction breaking soak harness expectations) is still valid in a *narrower* form: the new test path is the VST3-fixture path, not the standalone-binary path, so the two tests are isolated. iter-2 plan's row in §3 (line 530) correctly notes "existing `TestBothWarningCodesObserved` test left UNCHANGED — presence-only assertion, ordering NOT asserted."

**Verdict: LANDED-AS-INTENDED. AM-4 correctly downgraded to Should-have.**

---

## §B. Critic 16 items — Architect cross-check

I evaluate each Critic finding for: (i) is the iter-2 amendment a correct read of the underlying problem, and (ii) any over-correction or new risk introduced.

| # | Critic finding | Architect verdict | Notes |
|---|---|---|---|
| 1 | §B.1 ctest baseline 86 → 115 | **APPROVE** | Re-pinning to verifiable `ctest -N` output (line 10, 515-518, 558) is exactly right. iter-2 explicit "Gate (NOT trivially-passable): exactly 118/118 PASS" (line 546, 583) closes the "delete tests to pass" loophole. **No over-correction.** |
| 2 | §B.2 pytest path correction | **APPROVE** | Path corrected in all 4+ places (§-1.2 enumerates them; verified at lines 83, 321, 326, 530, 532). **No over-correction.** |
| 3 | §B.3 Item #7 over-claim | **APPROVE-WITH-NOTE** | Item #7 correctly re-scoped to (a) audit script + (b) targeted RELEASE_NOTES precision (lines 442-475). The 36-P-tag-match verification (§-1.3) is independently confirmed by my `grep -c "P[0-3]-"` → 36. **Note:** the (b) sub-item is now "conditional precision pass" (line 457-459) — if `RELEASE_NOTES_EN.md:111` is already precise, sub-item skipped. This is honest scoping but the implementer should grep-verify *before* committing the audit script, otherwise they may produce an empty change. Minor — not blocking. |
| 4 | §B.4 Item #6 over-claim | **APPROVE-WITH-NOTE** | Item #6 correctly re-scoped to BAND_1_HANDOFF_TEMPLATE.md (NEW) + Limitations trigger-events precision (lines 409-441). The "DO NOT touch Appendix A" guidance at line 431 is well-placed. I verified independently: `grep -nE "^## Appendix A\|^## Limitations\|legal counsel\|written offer" docs/adr/0016-external-distribution-policy.md` confirms Appendix A at line 233 + "written offer" at line 249 + Limitations at line 291. **Critic was correct.** Acceptance criterion (line 438: `grep -ciE … ≥ 4`) is genuinely already-passing — implementer should verify and document the delta as part of the acceptance, not redo work. **No over-correction.** |
| 5 | §A.1 AM-1 extended | **APPROVE** | See §A.1 above. |
| 6 | §A.2 AM-2 read-then-store | **APPROVE** | See §A.2 above. The Critic correction is strictly better than my iter-1 wording. |
| 7 | §A.3 AM-3 comment block | **APPROVE** | See §A.3 above. ADR §B note + code-side comment block are both present. |
| 8 | §A.4 AM-4 downgrade | **APPROVE** | See §A.4 above. Downgrade is correct per source-verified pytest semantics. |
| 9 | §B.6 relacy license audit | **APPROVE-WITH-NUANCE** | iter-2 Item #4 (a) at lines 336-345 enumerates the 5-deliverable license-audit checklist (URL pin + SHA + LICENSE verbatim + transitive grep + CMake integration). **Excellent rigor.** Nuance: the transitive grep recipe at line 343 only excludes `boost\|tbb\|absl` — relacy is from ~2014 and may use other patterns (e.g., `#include <atomic_ops/`, `#include <intel/`). Generalize the regex or document the chosen exclusion set in `relacy-promotion-gate.md`. **Should-have, not blocker.** |
| 10 | §B.7 5-green unified | **APPROVE** | "1-green" wording removed everywhere (§-1.10 promise verified at lines 360, 642). 5-green standard now matches `cross-platform.yml` ARM64 soak (line 650, §7.1 Scenario C). **Internally consistent.** |
| 11 | §C.1 heartbeat bench | **APPROVE-WITH-RISK-FLAG** | `bench_heartbeat_drain_latency` new ctest (line 525, §7.2 line 686) with HARD +50% gate is a sound regression check. **Risk flag** (see §C.1 below): the baseline `heartbeat_drain_baseline.txt` value is captured at v0.7 implementation time on the implementer's machine; CI may run on different hardware. A baseline-on-laptop vs CI-on-VM mismatch can produce flake. iter-2 plan does not specify *where* the baseline file is captured. Should-have. |
| 12 | §C.2 concurrent reset ctest | **APPROVE** | `b2_runtime_underrun_user_reset_concurrent` (Scenario A + B at lines 190-191) covers both the audio-thread-vs-IO-thread race and the `initialize()`-vs-OSC-reset race. The invariant at line 190 ("acceptable end states… NOT… runtime_demoted_=true AND runtime_demote_strikes_=0 AND runtime_demote_warning_pending_=false") is exactly the race-safety property. **Strong.** |
| 13 | §C.4 promotion order ARM64 P0 → Relacy P1 | **APPROVE** | Sound reasoning ("ARM64 hardware-race surface outranks synthetic-verifier confidence"). Locked in 3 places per §-1.13 (line 38, 361, 650). My iter-1 §A.5 implicitly assumed both could promote in either order — Critic is correct that ARM64 cannot wait for relacy. |
| 14 | §D.7 DOS via reset spam | **APPROVE-WITH-NOTE** | Rate-limit via `reset_cooldown_warning_emitted_` atomic (lines 159, 169) is sound. Test invariant at line 185-186 (step 6: "warning NOT re-armed on second cooldown rejection") is testable. **Note:** the flag is `std::atomic<bool>` — the cleared-on-Accept transition (line 163) and set-on-CooldownActive transition (line 159) happen on the IO thread, so single-writer relaxed-load-then-store pattern would work, but the plan uses `memory_order_release` (line 163). Acquire/release is fine and harmless here; relaxed would also be correct. Non-issue. |
| 15 | §D.8 initialize+reset race | **APPROVE** | Scenario B in the concurrent ctest (line 191) covers this. Invariant ("`runtime_demote_last_reset_ns_` ALWAYS retains the most recent `now_ns` value") is testable. Plan §-1.15 line 42 explicit about VST3 SDK contract assumption (audio thread stopped during `prepareToPlay`). **Sound.** |
| 16 | §B.5 AS-1 obsolete | **APPROVE** | My iter-1 AS-1 was based on a source-read miss. Critic was right: line 461 guard exists in production. iter-2 plan correctly drops AS-1 (line 203, 545, §-1.16). **Concede; no over-correction.** |

**Architect over-correction watch:** I scanned for places where the Planner may have over-integrated Critic findings. **One worth flagging (minor):**

- **Item #6 acceptance criterion `grep -ciE … ≥ 4` (line 438).** Critic verified this *likely* already passes at HEAD. iter-2 plan says "Re-confirm at v0.7 ship and document delta." This is correct framing but the implementer should not silently pass the acceptance by relying on pre-existing matches — the Band-1 template (the genuinely new work) should be the load-bearing acceptance signal, not the grep count. **Should-have rewording:** acceptance should pin to "BAND_1_HANDOFF_TEMPLATE.md exists + has 5 fields + 법률 자문 없이 disclaimer; Limitations section has 3-numbered list + named owner." The grep count is a sanity floor, not the primary acceptance.

---

## §C. New iter-2 risks (introduced by amendments)

### §C.1 Heartbeat-drain baseline portability (Critic §C.1 sub-risk)

**Risk:** `bench_heartbeat_drain_latency` (line 525, §7.2 line 686) captures a baseline median to `core/tests/core_unit/heartbeat_drain_baseline.txt` and fails on +50% regression. Plan does NOT specify *which environment* establishes the baseline. If captured on the developer's local x86_64 laptop and CI runs on a slower GHA-hosted runner, the +50% gate may trip on legitimate runs.

**Severity:** Medium. Real risk of CI flake post-merge if the baseline-environment mismatch is significant.

**Mitigation options:**
- **Option A (lightweight):** Document in §6 row that the baseline file is captured *on the GHA `ubuntu-24.04` runner via a one-shot workflow run*, not on developer machines. The captured value is then portable to all subsequent GHA `ubuntu-24.04` runs.
- **Option B (more rigorous):** Use relative gates (current run vs 5-run rolling median in CI artifacts), not a checked-in absolute baseline. More complex; defer to v0.8.

**Recommendation:** Option A for v0.7. Add one sentence to §2 Item that the file says "captured on `ubuntu-24.04` GHA runner at v0.7 ship; portable to same runner class." Bench tests on non-`ubuntu-24.04` builds should soft-warn, not hard-fail.

**Should-have**, not blocker.

### §C.2 No static guard preventing future second producer to `runtime_demote_max_ratio_x1000_`

**Risk:** AM-2's relaxed-load-then-store pattern (line 261-269) is correct *only if the audio thread is the sole producer* of `runtime_demote_max_ratio_x1000_`. iter-2 plan inline comment (line 254) cites this invariant but there is no static_assert, no `[[no_unique_address]]` marker, no `// CALLED ONLY FROM AUDIO THREAD` annotation on the atomic declaration itself. A future contributor adding a "telemetry tap" (e.g., heartbeat IO thread peeks the running max for slow-degradation B-3 follow-up) would silently violate the relaxed-pattern correctness.

**Severity:** Medium-Low (slow-degradation follow-up B-3 is on the v0.8 roadmap per plan, so this trap is reachable).

**Mitigation options:**
- **Option A (cheap):** Add comment block above the atomic declaration in `BinauralMonitor.h` near line 488 (where the existing atomics live): `// PRODUCER INVARIANT: only audio-thread BinauralMonitor::recordB2BlockTiming writes this atomic. The relaxed-load-then-store pattern at BinauralMonitor.cpp:<NN> depends on single-producer. If a second writer is added, the pattern must be upgraded to CAS (AM-2 escalation rationale in ADR 0017 §<X>).` Plus mirror the warning at the snapshot accessor.
- **Option B (stronger):** Wrap the atomic in a small `SingleWriterAtomicInt` helper type with private store + audio-thread-only access via a key-attestation pattern. Heavier; defer.

**Recommendation:** Option A for v0.7. Costs ~6 lines of comments; protects the AM-2 invariant from silent erosion.

**Should-have**, not blocker.

### §C.3 5-green soak gate ↔ ARM64 P0/Relacy P1 ordering creates v0.7 ship-blocker on relacy if cppmem fallback triggers

**Risk (interaction risk between Critic §C.4 + Pre-mortem Scenario B):** Plan locks ARM64 P0 → Relacy P1. If relacy's 5-green soak fails (Scenario B false-positive triggers cppmem fallback), the **Relacy P1 promotion is deferred** while the v0.7 ship still claims "synthetic verification of v0.6 #9 weak-memory-order fix is in place." This is honestly disclosed in the plan (§7.1 Scenario B mitigation: "Document the rejection rationale in #4 acceptance log + ADR 0017 §B follow-up") but the *v0.7 ship verdict* may need to soften the "v0.7 D-S4 ships synthetic verification" claim if relacy is signal-only AND no cppmem replacement has landed by ship time.

**Severity:** Medium (process honesty risk, not technical risk).

**Recommendation:** Add to RELEASE_NOTES_EN.md acceptance criterion: "if relacy fails its 5-green soak before v0.7 ship, release notes explicitly state 'D-S4 ships as dev-only verifier (signal-only); upstream verification of v0.6 #9 release-store deferred to v0.7.x via cppmem.'" This keeps Principle 1 (honesty-first surface) intact.

**Should-have**, not blocker (the plan is already 95% in this posture; one sentence in release-notes formalizes it).

---

## §D. Recommendations for Critic iter-2

Three items where I'd like Critic's independent verification before final ACCEPT:

1. **§C.1 heartbeat baseline portability.** Plan does not name the canonical capture environment. Critic should verify whether the implementer's CI-vs-local mismatch is real-world likely on this project's GHA runner mix, and whether the +50% gate threshold is tight enough to flake.
2. **§C.2 single-producer invariant on `runtime_demote_max_ratio_x1000_`.** I propose a comment-only mitigation. Critic should consider whether a `static_assert` or wrapper type is warranted given the explicit v0.8 B-3 follow-up roadmap, OR whether the inline comment is sufficient.
3. **§B Item #6 acceptance criterion.** I flag that `grep -ciE … ≥ 4` may pass vacuously at HEAD because the work that triggers it is already done. Critic should re-confirm whether the iter-2 acceptance (line 438) is load-bearing on the NEW work (BAND_1_HANDOFF_TEMPLATE + trigger-events list) or only on the grep count. If only the grep, recommend swapping the primary acceptance signal.

---

## §E. Verdict

**APPROVE.**

Rationale:
- All 4 iter-1 Must-fix have landed correctly (3 AS-INTENDED, 1 WITH-NUANCE where Critic correction was binding and strictly better).
- All 16 Critic items integrated; 14 are clean APPROVEs, 2 carry minor APPROVE-WITH-NOTE that the Critic iter-2 pass can re-examine.
- 2 new iter-2 risks identified (§C.1 baseline portability, §C.2 single-producer invariant guard) + 1 process-honesty risk (§C.3); all 3 are Should-have not blockers.
- iter-2 verification matrix §9 (lines 724-747) gives the Critic pass a clean checklist to verify the 16-item integration.
- Plan size growth (490 → 747 lines) is justified — the §-1 changelog + §9 verification matrix together account for ~80 lines of pure traceability that materially helps Critic + autopilot.

**Conditional on:** §C.1 / §C.2 / §C.3 being noted in the Critic iter-2 pass. None block autopilot start; all three are testable post-implementation and addressable as v0.7 follow-up tickets if the Critic agrees to defer.

The plan is autopilot-ready from the Architect's perspective. Critic ratification pass is the only remaining gate.
