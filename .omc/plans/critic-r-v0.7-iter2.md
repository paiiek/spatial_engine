# spatial_engine v0.7 iter-2 — Critic final-gate review (deliberate)

| | |
|---|---|
| **Date** | 2026-05-18 |
| **Reviewer** | Critic (omc-critic; iter-2 final gate; this is the autopilot entry decision) |
| **Plan under review** | `.omc/plans/spatial-engine-v0.7.md` iter-2 (747 lines) |
| **Prior reviews** | iter-1 Architect (APPROVE-WITH-RECOMMENDATIONS, 4 Must-fix), iter-1 Critic (ITERATE, 16 items), iter-2 Architect (APPROVE) |
| **Mode** | DELIBERATE — final gate before autopilot ratifies execution |
| **Verdict preview** | **ITERATE** — 1 narrow blocker (plan §header line 10 still carries the iter-1 "117/117" count in contradiction with §3 / §4 / §7.3 / §8's "118/118" — an internal-consistency MUST-FIX). 1 spec error (plan invents accessor `lastObservedBlockSize` that does not exist in current code; only `blockSize()` exists). 15 of 16 iter-1 items LANDED-AS-INTENDED; 1 LANDED-AS-INTENDED but the consequence (count change 117→118) was not propagated to the §header. After 2 surgical fixes (~5 lines of plan edits), the plan is autopilot-ready. ITERATE is **light** — iter-3 turnaround estimated < 15 min. |

This review focuses on iter-2 deltas. iter-1 findings already on record are not re-litigated except where the iter-2 amendment introduced a new defect.

---

## §A. 16 iter-1 items — land-verification

Cross-checked iter-2 plan against my iter-1 Critic verdict ITERATE list. Citations are to iter-2 plan line numbers.

### Spec-precision (4 items)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 1 | §B.1 ctest baseline 86→115 | **LANDED-WITH-INTERNAL-INCONSISTENCY** | §-1.1 (line 20) says "115 → 117/117 (default) / 118 (relacy ON) everywhere." §header line 10 says "115/115 baseline + 2 NEW (default build) = 117/117 PASS … Relacy-ON optional build adds 1 more (118)." BUT §3 (line 541, 546), §4 (line 558, 583), §7.3 (line 698), §8 (line 715) all say **118/118 default + 119/119 relacy ON** (115 + 3 NEW because §C.1 heartbeat bench was added per iter-2). The §header + §-1.1 numbers were NOT updated when §C.1 added the 3rd new test. See §B.1 below — this is a blocker. |
| 2 | §B.2 pytest path | **LANDED-AS-INTENDED** | Verified `grep -n "tests/soak_harness/test_osc_warning_channel.py"` returns 5+ matches in iter-2 plan. Old path `tests/test_osc_warning_channel.py` (without `soak_harness/`) returns 0 matches. Architect iter-2 §B row 2 also confirms. |
| 3 | §B.3 Item #7 over-claim | **LANDED-AS-INTENDED** | Item #7 (lines 442-476) re-scoped to (a) audit script + (b) conditional precision pass. Bulk P-tag-add work explicitly DROPPED (line 461). Verified `grep -c "P[0-3]-" docs/weekly_progress_report_2026-05-18.md` = **36** (matches plan claim). Architect APPROVE-WITH-NOTE accepted. |
| 4 | §B.4 Item #6 over-claim | **LANDED-AS-INTENDED** | Item #6 (lines 409-441) re-scoped to (a) BAND_1_HANDOFF_TEMPLATE + (b) Limitations trigger-events precision. "DO NOT touch Appendix A" at line 431 — well-placed defense. Verified `grep -ciE 'lawyer\|attorney\|legal counsel\|written offer' docs/adr/0016-external-distribution-policy.md` = **9** (already passes the ≥4 acceptance vacuously per Architect §B note — see §B.3 below for my final call on this). |

### Architect Must-fix amendments (3 confirm + 1 downgrade)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 5 | §A.1 AM-1 extended (6-atomic reset) | **LANDED-AS-INTENDED** | Item #1 lines 160-164 enumerate all 6 atomics explicitly with LAST-clear ordering on `runtime_demoted_`. Step 4 of the ctest (line 183) verifies the AM-1 zero-hysteresis regression invariant ("**CRITICAL**: drive a single over-budget `recordB2BlockTiming` call — assert `runtime_demoted_.load() == false`"). Architect §A.1 also approves. Strong land. |
| 6 | §A.2 AM-2 read-then-store correction | **LANDED-AS-INTENDED** | Lines 261-269 contain the exact read-then-store pattern I specified in iter-1 §A.2. Comment block at lines 252-260 explicitly attributes the correction to "Critic §A.2 correction over Architect AM-2's `store(max(...))` wording." Inversion of mitigation order also lands at §7.1 Scenario A (line 636). |
| 7 | §A.3 AM-3 + comment block + ADR §B | **LANDED-AS-INTENDED + STRONGER** | Item #3 (c) lines 285-312 lock option (b). Comment block at lines 289-302 references ADR 0017 §B. ADR 0017 §B at line 696 contains the full rejection paragraph + "Future maintainers: do NOT fuse" defense. Three-place reinforcement (code comment + plan §2 + ADR §B). Strong land. Verified existing OSCBackend has zero `sendReplyImplIIF` references (`grep -n sendReplyImplIIF core/src/ipc/OSCBackend.{h,cpp}` returns 0) — the new impl is genuinely new work, not a duplicate. |
| 8 | §A.4 AM-4 downgrade | **LANDED-AS-INTENDED** | §3 table row for pytest (line 530) explicitly notes "Existing `TestBothWarningCodesObserved` test left UNCHANGED — presence-only assertion, ordering NOT asserted per Critic §A.4 AM-4 downgrade." Verified existing test at `tests/soak_harness/test_osc_warning_channel.py:187` (`class TestBothWarningCodesObserved`) — name matches. Architect APPROVE. |

### New MAJOR finding (1)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 9 | §B.6 relacy license audit | **LANDED-AS-INTENDED + EXTENSIBLE** | Item #4 (a) lines 336-345 enumerate 5-deliverable checklist: upstream URL + commit SHA pin + verbatim LICENSE + transitive grep `boost\|tbb\|absl` + CMake integration. Architect §B row 9 flagged the transitive grep regex as possibly too narrow (~2014-era relacy may use other patterns). I AGREE with Architect's nuance but treat it as Should-have not blocker — the grep recipe should be **documented as a starting set, not exhaustive** in `relacy-promotion-gate.md`. See §B.4 below. |

### Should-have additions (4)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 10 | §B.7 5-green unified | **LANDED-AS-INTENDED** | Verified `grep -n "5 consecutive\|5-green" .omc/plans/spatial-engine-v0.7.md` returns 9+ matches. `grep -n "1-green\|1 consecutive" .omc/plans/spatial-engine-v0.7.md` returns 1 match — but it's in §-1.10 (line 35: "`1-green` wording removed everywhere") which is the *report* about removal, not the residue. Confirmed removal complete. |
| 11 | §C.1 heartbeat bench | **LANDED-AS-INTENDED-BUT-FRAGILE** | `bench_heartbeat_drain_latency` added in §3 (line 525), §6 (line 621), §7.2 (line 686). Hard +50% gate vs persisted baseline. Architect §C.1 raised baseline-environment-portability concern; I CONFIRM (see §B.2 below). |
| 12 | §C.2 concurrent reset ctest | **LANDED-AS-INTENDED** | `b2_runtime_underrun_user_reset_concurrent` (line 524, 683) covers Scenario A (audio-thread vs IO-thread race) + Scenario B (initialize() vs OSC reset). Invariant at line 190 is testable. Architect APPROVE. |
| 13 | §C.4 promotion order ARM64 P0 → Relacy P1 | **LANDED-AS-INTENDED** | 3-place lock per Architect §B row 13: §-1.13 (line 38), §2 Item #4 (c) (line 361), §2 Item #5 (a) (line 386), §7.1 Scenario C (line 650). Actually 4 places — over-pinned which is fine. |

### Pre-mortem additions (2)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 14 | §D.7 DOS via reset spam | **LANDED-AS-INTENDED** | Scenario G at lines 659-667. Rate-limit via `reset_cooldown_warning_emitted_` flag in Item #1 (lines 159, 163). Test invariant at line 185-186 (step 6) is testable. Architect APPROVE-WITH-NOTE. |
| 15 | §D.8 init+reset race | **LANDED-AS-INTENDED** | Scenario H at lines 669-677. Scenario B in concurrent ctest (line 191) tests cooldown-survives-initialize invariant. AS-5 process-lifetime semantic explicitly preserved. |

### Obsolete removal (1)

| # | Critic iter-1 finding | iter-2 status | Evidence |
|---|---|---|---|
| 16 | §B.5 AS-1 obsolete | **LANDED-AS-INTENDED** | Item #2 Fix paragraph (line 203) explicitly says "AS-1 is DROPPED per Critic §B.5 obsolete-removal" and "No new guard needed for div-by-zero protection." Verified existing guard at `BinauralMonitor.cpp:461`. Clean. |

**Land summary: 15 LANDED-AS-INTENDED + 1 LANDED-WITH-INTERNAL-INCONSISTENCY (item #1).** The single inconsistency is purely numeric in the plan §header — code-side work is unaffected.

---

## §B. Architect iter-2 §D (3 verification asks) — Critic judgment

### §B.1 Architect §C.1 heartbeat baseline portability — **CONFIRM (Should-have, NOT blocker)**

Architect proposes Option A: capture baseline on `ubuntu-24.04` GHA runner, document as "portable to same runner class."

**My take:** Confirm. Three additional precision points:

1. **Where is the baseline file regenerated when the system gets faster?** If a future CI runner upgrade halves the measured time, the +50% gate becomes inappropriately tight (catches phantom regressions). The plan should specify: "Baseline regenerated **only** when (a) intentional `recordB2BlockTiming` / `heartbeatLoop` refactor lands, OR (b) GHA runner image upgrade. Manual regeneration via a tagged workflow_dispatch."
2. **What about local developer runs?** A developer running `ctest` on a fast workstation would see the bench far under baseline → pass trivially. A slow ARM laptop developer would see false-positive failures. The plan should say: "Bench is gated to `ubuntu-24.04` GHA runner CI only. Local runs are advisory; if `CI=true` environment variable is NOT set, the bench warns but does not fail. Documented in `bench_heartbeat_drain_latency` source header comment."
3. **+50% threshold is generous but unjustified.** Why 50% vs 30% vs 100%? Plan doesn't say. Architect doesn't say either. Should-have: pick a number with a reason, e.g., "50% chosen because GHA `ubuntu-24.04` runner shows ~30% run-to-run variance per `actions/runner` issue #N (cite)." If no reason can be cited, use the variance observed in 5 consecutive baseline captures + 2σ.

**Severity: Should-have, not blocker.** The plan does not need to lock all 3 before autopilot; the 3 are appropriate for autopilot to address as part of `bench_heartbeat_drain_latency` implementation (executor judgment, anchored by `relacy-promotion-gate.md`-style doc).

### §B.2 Architect §C.2 single-producer invariant guard — **DOWNGRADE to comment-only sufficient**

Architect's Option A (comment-only on the atomic declaration in `BinauralMonitor.h`) vs Option B (`SingleWriterAtomicInt` wrapper type).

**My take:** Option A is sufficient for v0.7. Reasons:

1. **The audio-thread invariant is already documented in 3 places** in iter-2 plan: §2 Item #3 (a) lines 252-260 (the source-side comment block), §-1.6 (line 27), §7.1 Scenario A (line 636). A 4th location at the atomic declaration is doc-redundancy, not enforcement.
2. **`static_assert` cannot enforce "this atomic is only written from one thread"** — there is no compile-time fact that captures threading discipline. Any wrapper type just moves the comment, not the enforcement.
3. **The v0.8 B-3 follow-up risk is real but specific.** Architect's concern is "future contributor adding heartbeat IO thread peek to runtime_demote_max_ratio_x1000_ for slow-degradation B-3." The mitigation that ACTUALLY catches this is: when B-3 lands, the v0.8 plan must do its own RT-safety review and re-evaluate the atomic ordering. v0.7 cannot pre-prevent every future refactor.
4. **The relacy test (Item #4) is the actual enforcement.** If a future contributor adds a producer to the outbound ring, relacy will catch the failure under exhaustive interleaving. relacy doesn't currently model `runtime_demote_max_ratio_x1000_` because it's a single-producer atomic, but the v0.8 B-3 PR that introduces a second writer would presumably extend the relacy model — and at that point the test fails honestly.

**Decision: comment-only is sufficient. ESCALATE to Should-have ONLY IF Architect's concern proves prescient (v0.8 B-3 lands without re-reviewing this atomic).** Defer the wrapper type idea to a v0.8 carry-forward open question. No iter-3 change required.

**The plan does not even need to add Architect's Option A comment at the atomic declaration.** The 3 existing comment locations are adequate. If the implementer wishes to add a 4th comment at the declaration site, fine — it's a 6-line edit and adds no risk. Should-have, not blocker.

### §B.3 Architect §B Item #6 acceptance criterion — **CONFIRM concern, RE-AFFIRM Critic iter-1 §B.4 fix language**

Architect flags that `grep -ciE 'lawyer|attorney|legal counsel|written offer' docs/adr/0016-external-distribution-policy.md ≥ 4` will pass **vacuously** at HEAD (I verified: the grep returns 9 at HEAD right now).

**My take:** CONFIRM the concern. The acceptance criterion needs to be re-anchored on **new artifacts**, not on grep-of-pre-existing-text.

**Specific rewording (Should-have for iter-3, but acceptable for autopilot to apply post-hoc):**

Replace iter-2 plan line 438 with two acceptance items:

> **Acceptance (NEW work — primary signals):**
> - `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` exists, contains the "법률 자문 없이 작성" disclaimer header, has all 5 fields (recipient identity + repo URL + tag SHA + acknowledgement section + audit-log entry).
> - ADR 0016 `## Limitations & legal review status` section contains a **numbered 3-trigger list** with `paiiek` named as default owner per trigger.
> 
> **Sanity floor (pre-existing — must not regress):**
> - `grep -ciE 'lawyer|attorney|legal counsel|written offer' docs/adr/0016-external-distribution-policy.md` ≥ 9 (current value; gate against accidental rollback of ADR 0016 §Limitations section in a future PR).

This is **NOT a hard blocker** — the plan's iter-2 wording (line 438) is honest ("likely ALREADY passes at HEAD; re-confirm at v0.7 ship and document delta") — but the primary acceptance signal SHOULD be the NEW work, not a grep count that already passes. Should-have for iter-3; otherwise autopilot can apply this as a Step 6 acceptance refinement.

---

## §C. New iter-2 risks (Critic-discovered, not in Architect iter-2)

### §C.1 MAJOR — Plan §header line 10 contradicts §3 / §4 / §7.3 / §8 on ctest count (117 vs 118)

**Evidence:**
- §header line 10: `"ctest **115/115 baseline + 2 NEW (default build) = 117/117 PASS** … Relacy-ON optional build adds 1 more (118)."`
- §-1.1 line 20: `"corrected from '86 → 92/92' to '115 → 117/117 (default) / 118 (relacy ON)'"`
- §3 line 541: `"Default build target = **115 + 3 = 118/118 PASS**"`
- §3 line 542: `"(Iter-1's '117' undercount: missed `b2_runtime_underrun_user_reset_concurrent` and `bench_heartbeat_drain_latency` — added in iter-2)"`
- §3 line 546: `"default build = exactly 118/118 PASS (115 pre-existing + 3 new)"`
- §4 line 558: `"100% tests passed, 0 tests failed out of 118"`
- §4 line 583: `"exactly 118/118 PASS"`
- §7.3 line 698: `"v0.7 ctest count = 118/118 in default build (115 baseline + 3 NEW)"`
- §8 line 715: `"count = 118/118 in default build"`

**Diagnosis:** When iter-2 added the 3rd new test (`bench_heartbeat_drain_latency` per Critic §C.1, on top of `b2_runtime_underrun_user_reset` and `b2_runtime_underrun_user_reset_concurrent`), the §3 / §4 / §7.3 / §8 counts were updated to 118. The §header line 10 and §-1.1 line 20 ("117/117") were NOT updated. **The plan contradicts itself internally** about its primary verification gate.

This is exactly the class of inconsistency that bit the iter-1 plan in §B.1 (where the header claimed 86 vs reality 115). The iter-2 fix landed in §3 / §4 / §7.3 / §8 but missed the header.

**Confidence: HIGH.** **Severity: MAJOR (blocking — verification gate is the autopilot's primary success criterion; ambiguous gate = ambiguous done-state).** 

**Fix (Must-fix for iter-3):**
- Line 10: `"117/117"` → `"118/118"`; `"adds 1 more (118)"` → `"adds 1 more (119)"`; `"2 NEW"` → `"3 NEW"`.
- Line 20: `"117/117 (default) / 118 (relacy ON)"` → `"118/118 (default) / 119 (relacy ON)"`.

Two-line edit. Trivial. But MUST land before autopilot — otherwise an executor reading the §header gets one number, reading §3 gets another, and the "done = ctest 118" or "done = ctest 117" decision becomes spec-ambiguous.

### §C.2 MAJOR — Plan invents accessor `lastObservedBlockSize` that does not exist; correct name is `blockSize()`

**Evidence:**
- Plan §2 Item #3 (b) lines 279-281 specifies:
  - `block_size = binauralMonitor().lastObservedBlockSize()` (existing accessor).
  - `sample_rate_int = static_cast<int>(binauralMonitor().lastObservedSampleRate())` (existing accessor).
- Plan §2 Item #3 Files (line 315): "2 new accessors `int snapshotRuntimeDemoteMaxRatioX1000() const noexcept`, `int lastObservedBlockSize() const noexcept` (returns existing field), `float lastObservedSampleRate() const noexcept`."

**Verification** (`grep -n "blockSize\|sampleRate\|lastObservedBlockSize\|lastObservedSampleRate" core/src/output_backend/BinauralMonitor.h`):
- Line 125: `int blockSize() const noexcept { return block_size_; }` — EXISTS, returns the stored block size.
- `lastObservedBlockSize`: **NO match.** Does not exist.
- `lastObservedSampleRate`: **NO match.** Does not exist.
- `sampleRate` accessor: there is a `Config::sampleRate` field at line 74, but no `sampleRate()` accessor method on `BinauralMonitor` itself.

**Diagnosis:** The plan is internally contradictory at lines 279-281 ("existing accessor") vs line 315 ("2 new accessors … `lastObservedBlockSize`"). Whichever way it's read:
- If "existing" — wrong. There is no `lastObservedBlockSize` method. There IS `blockSize()`.
- If "new" — the plan double-classifies them as both existing AND new in the SAME item spec.

Also: the implementation needs `sample_rate_` exposed (currently a private field with no accessor). Plan must specify whether to:
- (a) Add `float sampleRate() const noexcept { return sample_rate_; }` as new accessor, OR
- (b) Use the existing `Config` snapshot mechanism (no `Config` snapshot accessor exists either — verified), OR
- (c) Capture block_size + sample_rate INTO new dedicated atomics at demote latch time (so the diag packet uses the values **at demote moment**, not the **current** value, which may have changed if `prepareToPlay` ran since).

Option (c) is actually the MOST CORRECT — telemetry should report the demote-moment context, not the post-demote context. But the plan doesn't specify any of (a)/(b)/(c).

**Confidence: HIGH.** **Severity: MAJOR (spec ambiguity that an executor would hit on day 1 of Item #3 — exactly the class of failure Critic iter-1 §B.2 / §B.3 / §B.4 caught).**

**Fix (Must-fix for iter-3):**
- Line 279: change `binauralMonitor().lastObservedBlockSize()` → `binauralMonitor().blockSize()` (existing accessor at line 125).
- Line 280: specify the new accessor to add, e.g., "Add new accessor `float BinauralMonitor::sampleRate() const noexcept { return sample_rate_; }` symmetric with existing `blockSize()`." Document why a thread-safety review is not needed (sample_rate_ is set in `initialize()` on control thread; read on IO thread for diag emit; race window is tiny and last-writer-wins semantics are acceptable for telemetry — but if the IO thread reads it during a re-`initialize()`, a corrupted float read is theoretically possible on platforms without atomic float reads. Recommend making `sample_rate_` `std::atomic<float>` OR documenting the acceptable-stale-read tradeoff.)
- Line 315: remove the `lastObservedBlockSize` / `lastObservedSampleRate` names; replace with `blockSize()` (existing) + `sampleRate()` (new) per above.
- **Alternative (preferred for correctness):** specify Option (c) — snapshot `block_size` + `sample_rate_int` into 2 NEW atomics at demote latch CAS-success time, alongside `runtime_demote_max_ratio_at_event_x1000_`. Diag packet reads the snapshots, not the live fields. This avoids the race entirely.

This is a spec-precision MUST-FIX, similar in shape to iter-1's §B.2 pytest-path correction.

### §C.3 MINOR — `bench_heartbeat_drain_latency` baseline-environment risk (Architect §C.1 confirmed)

Already covered above in §B.1. Should-have for iter-3 acceptance refinement; not a blocker.

### §C.4 MINOR — Item #6 acceptance vacuously-passes risk (Architect §B confirmed)

Already covered above in §B.3. Should-have for iter-3 acceptance refinement; not a blocker.

### §C.5 MINOR — Item #8 saturation-cap test specification is fragile

**Evidence** (line 507): "call `recordB2BlockTiming` with over-budget elapsed_ns 2000 times in a row without invoking initialize() (use a back-door that skips the demote latch via reflection — actually, just CAS-fail the demote latch in a controlled way). Assert `runtime_demote_strikes_.load()` ≤ saturation ceiling."

**Concern:** "use a back-door that skips the demote latch via reflection" is C++ — there is no reflection. The fallback "just CAS-fail the demote latch in a controlled way" requires a test hook that does not yet exist. Plan doesn't specify which test hook to add OR whether the existing `clearRuntimeDemoteForTest()` + careful timing can simulate the failure.

**Diagnosis:** This is a test-spec defect, not a code defect. The implementer will hit this on Item #8 day-1. It's also a v0.6 carry-forward — Item #8 is largely belt-and-suspenders for an unreachable scenario.

**Confidence: MEDIUM.** **Severity: MINOR (Item #8 is itself low-priority defensive code).**

**Recommendation:** Item #8 (a)'s test acceptance is satisfied if EITHER (a) the test demonstrates strike counter staying ≤ ceiling under a controlled CAS-loss scenario, OR (b) the test demonstrates that adding the guard does not regress the existing `b2_runtime_underrun_auto_demote` scenarios. Option (b) is the implementer's escape valve. Plan can be amended post-autopilot.

Should-have, not blocker.

### §C.6 MINOR — `b2_runtime_underrun_user_reset_concurrent` C++20 dependency

**Evidence** (line 189): "Use `std::atomic<int>` spin-fence + `std::thread`. **Scenario A** … (C++20) or `std::atomic<int>` spin-fence."

**Concern:** Plan mentions `std::barrier` (C++20) as one option. The project's C++ standard target should be verified — if the build targets C++17, the C++20 path is moot. Plan already provides the C++17 fallback (atomic spin-fence), so this is informational not blocking.

**Verification:** `grep -n "CXX_STANDARD\|cxx_std" core/CMakeLists.txt` would resolve this. Not verified by Critic — punt to executor.

**Confidence: LOW.** **Severity: MINOR.** Implementer self-resolves.

---

## §D. Verdict + conditions

### Verdict: **ITERATE** (light — 2 surgical fixes only)

**Rationale:**
- 15 of 16 iter-1 Critic items LANDED-AS-INTENDED. 1 (item #1, ctest baseline) LANDED but with an internal-consistency gap (§C.1 above).
- 4 of 4 iter-1 Architect Must-fixes (AM-1..AM-4) LANDED correctly with Critic corrections honored.
- All 8 pre-mortem scenarios (A-H) present; expanded test plan covers all required layers (unit / integration / e2e / observability / CI / cross-platform). **Deliberate-mode pre-mortem + expanded test plan obligation IS satisfied.**
- However: deliberate-mode obligation also requires "specific verification steps." The §header verification gate is currently SPEC-AMBIGUOUS (117 vs 118 contradiction at §C.1) — this fails the verification-step-specificity criterion strictly. Per deliberate-mode policy, weak verification step rigor forces ITERATE/REJECT. I choose ITERATE because the fix is 2-line, not REJECT.
- §C.2 (invented accessor) is a separate spec-ambiguity that an executor hits at Item #3 implementation — exactly the failure class iter-1 §B.2/§B.3/§B.4 was designed to catch. Honest application of the iter-1 standard requires flagging this iter-2 too.

### Conditions for upgrade to ACCEPT — autopilot entry is BLOCKED until:

**Must-fix (block autopilot — 2 items):**

1. **§C.1 — §header line 10 + §-1.1 line 20 count harmonization.** Replace "117/117" with "118/118" and "1 more (118)" with "1 more (119)" and "2 NEW" with "3 NEW" at line 10. Replace "117/117 (default) / 118 (relacy ON)" with "118/118 (default) / 119 (relacy ON)" at line 20. ~5 line edits.
2. **§C.2 — Item #3 (b) accessor names + §"Files" accessor list reconciliation.** Either (a) use existing `blockSize()` + add new `sampleRate()` (specify thread-safety stance), OR (b) preferred: snapshot block_size + sample_rate_int into 2 new atomics at demote latch time (Item #3 (a) extension; 4 atomics total instead of 2). Pick one explicitly. ~10 line edits.

**Should-have (lock in plan or accept post-autopilot):**

3. §B.1 — heartbeat baseline-environment portability (Architect §C.1 confirmed) — document CI-only gating + regeneration trigger conditions + +50% threshold justification. Can land in `bench_heartbeat_drain_latency` source-header comment during autopilot Item Critic §C.1 implementation.
4. §B.3 — Item #6 acceptance rewording (primary signal = NEW BAND_1_HANDOFF_TEMPLATE artifact + 3-trigger list; sanity floor = current grep ≥ 9 floor). Can land in §2 Item #6 line 438 during iter-3 OR as autopilot Step 6 amendment.
5. §C.5 — Item #8 saturation-cap test spec (escape valve language). Implementer self-resolves.
6. §C.6 — `b2_runtime_underrun_user_reset_concurrent` C++17/C++20 verification. Implementer self-resolves.

### Estimated iter-3 turnaround: **< 15 minutes of Planner pass** (2 surgical edits: ~5 lines + ~10 lines = ~15 lines total).

### Autopilot entry: **DENIED until §C.1 + §C.2 land in iter-3.**

After iter-3 lands those 2 Must-fixes, no Critic ratification pass is needed — the fixes are mechanical and the rest of the plan is approved. Architect's APPROVE stands as-is. iter-3 → autopilot is the next step.

---

## §E. Out-of-scope confirmation (plan §5)

I scanned plan §5 (lines 589-600) for any iter-2 changes vs iter-1. The deferral table is unchanged except for the D-L1 row, which gained Critic §B.8 trigger language ("any audio-thread `sendReply` call site is introduced"). All deferrals are correctly motivated:

- **MUSHRA** — out-of-scope correctly (no in-house panel; v0.8+ research-collab).
- **Live legal counsel** — out-of-scope correctly (user/lead-driven budget decision).
- **macOS hardware-handoff** — out-of-scope correctly (named owner `paiiek` per AS-6; v0.8).
- **D-L1 SPSC ring** — deferral now has explicit trigger condition (Critic §B.8 honored).
- **D-L2 EWMA** — deferral correctly ordered after D-S3 telemetry.
- **D-L3 auto-re-arm** — deferral correctly ordered after D-S1 covers UX gap.
- **D-L4 commit-tag policy** — appropriate v0.8 process item.
- **MAX_ORDER bump** — separate ADR scope, correctly out-of-scope.

**§E verdict: Out-of-scope list is sound. No changes required for iter-3.**

---

## Ralplan summary row

- **Principle/Option Consistency**: PASS — 5 principles + 3 Decisions A/B/C all consistent across iter-1 → iter-2 deltas.
- **Alternatives Depth**: PASS — all 3 Decisions retain ≥2 viable options with explicit rejection rationale; no degradation from iter-1.
- **Risk/Verification Rigor**: PARTIAL PASS — pre-mortem 8 scenarios complete; expanded test plan layers complete; BUT §header verification gate is SPEC-AMBIGUOUS (§C.1 above) — strictly fails verification-step-specificity under deliberate-mode. Two surgical fixes resolve.
- **Deliberate Additions**: PASS — pre-mortem 3 base + 3 Architect + 2 Critic = 8 scenarios all present; expanded test plan covers unit/integration/e2e/observability/CI layers; ADR 0017 placeholder ready for v0.7 retrospective.

---

**Verdict**: **ITERATE** (light — 2 mechanical fixes). **Autopilot entry: DENIED** until §C.1 (count harmonization) + §C.2 (accessor reconciliation) land in iter-3. Estimated iter-3 cost: < 15 min Planner pass. No Critic ratification needed post-iter-3; iter-3 → autopilot is the path forward.

---

## Open Questions (unscored — low-confidence or Architect-could-refute)

1. **§C.2 Option (c) recommendation (snapshot block_size + sample_rate_int at demote latch).** I prefer this for correctness; the Planner may choose Option (a) `blockSize()` + new `sampleRate()` for minimal-diff. Either is acceptable as long as ONE is explicit.
2. **§B.2 single-producer invariant.** I argue comment-only is sufficient; Architect's wrapper-type idea could be re-evaluated if v0.8 B-3 telemetry tap actually materializes. Defer to v0.8 carry-forward.
3. **§C.5 Item #8 reflection language.** Item #8 is itself low-priority. If autopilot drops Item #8 (b)/(c) and only ships (a) saturation guard with a simpler test, that's acceptable — Item #8 is a v0.6 carry-forward not a v0.7 core ask.
4. **§C.6 C++17 vs C++20 std::barrier.** Implementer self-resolves; minor.
