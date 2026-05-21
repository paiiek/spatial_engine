# spatial_engine v0.7 — RT-safety follow-through + telemetry + cross-platform CI promotion

| | |
|---|---|
| **Status** | DRAFT iter-3 (Planner; iter-3 applies 2 surgical Critic iter-2 Must-fix items §C.1 + §C.2 — see §-1 iter-3 addendum. No Critic ratification pass required per Critic iter-2 §D verdict.) |
| **Authored** | 2026-05-18 (iter-1); revised 2026-05-18 (iter-2); iter-3 surgical pass 2026-05-18 |
| **Predecessor** | v0.6.x (`c304ec2`, post-hoc plan `.omc/plans/spatial-engine-v0.6-stability.md` + retro-ralplan `.omc/plans/architect-r-v0.6-retro.md` / `.omc/plans/critic-r-v0.6-retro.md`) |
| **Iter-2 inputs** | `.omc/plans/architect-r-v0.7.md` (APPROVE-WITH-RECOMMENDATIONS, 4 Must-fix + 6 Should-have + 3 Nice-to-have); `.omc/plans/critic-r-v0.7.md` (ITERATE, 16 items: 4 spec-precision + 3 Must-fix amendments + 1 downgrade + 1 new MAJOR + 4 Should-have additions + 2 pre-mortem additions + 1 obsolete removal). |
| **Cycle mode** | **DELIBERATE** — see §0.5 escalation rationale (RT-safety code on audio thread + new outbound OSC channel + cross-platform CI gating + GPL legal-surface doc work). |
| **Verification target** | ctest **115/115 baseline + 3 NEW (default build) = 118/118 PASS** (verified baseline 2026-05-18: `cd build_off && ctest -N \| tail -1` → `Total Tests: 115`; `cd build_vst3_on && ctest -N \| tail -1` → `Total Tests: 115`, same set). Relacy-ON optional build adds 1 more (119). **No "≥87" or similar trivially-passable wording** — gate is exact "115 pre-existing PASS + N new PASS" with N enumerated in §3. pytest **47/47 + 2 NEW = 49/49 PASS**. `cross-platform.yml` Linux ARM64 leg **promoted to required** after 5-green soak; `macos-14` leg signal-only with named owner. _(iter-3 §C.1 fix — count harmonized 117→118 / 118→119 / 2 NEW→3 NEW; was inconsistent with §3/§4/§7.3/§8 which already showed 118/119.)_ |
| **Out-of-scope** | MUSHRA subjective evaluation (panel recruitment), live legal counsel engagement (only doc surface), physical-machine DAW hands-on (Reaper/Bitwig/Logic/Cubase) — see §5. |

---

## §-1. Iter-2 changelog (responses to Architect + Critic) — with iter-3 surgical addendum

**Iter-3 surgical addendum (2026-05-18)** — applies the 2 Must-fix items from Critic iter-2 §D verdict (`.omc/plans/critic-r-v0.7-iter2.md` lines 229-232). Per Critic iter-2 §D: "After iter-3 lands those 2 Must-fixes, no Critic ratification pass is needed — the fixes are mechanical and the rest of the plan is approved. Architect's APPROVE stands as-is. iter-3 → autopilot is the next step."

- **iter-3 §C.1 — ctest count harmonization.** §header line 10 + §-1.1 line 20 updated from 117/117 → 118/118 (default) and 118 → 119 (relacy ON) and "2 NEW" → "3 NEW", reconciling with §3 / §4 / §7.3 / §8 which already had 118/119 after the iter-2 addition of `bench_heartbeat_drain_latency` (the 3rd new test). Verification gate now consistent across the whole plan.
- **iter-3 §C.2 — Item #3 accessor reconciliation (Option (c) — Critic preferred-for-correctness).** Removed iter-2's invented accessors `lastObservedBlockSize()` / `lastObservedSampleRate()` (do NOT exist on `BinauralMonitor` at HEAD — only `int blockSize()` at `BinauralMonitor.h:125` does, with no `sampleRate()` accessor). Replaced with **demote-moment snapshot approach**: 2 new atomics `runtime_demote_block_size_at_event_` + `runtime_demote_sample_rate_at_event_` are snapshotted on the audio-thread demote latch CAS-success path (joining the existing `runtime_demote_max_ratio_at_event_x1000_`). Heartbeat drain reads these snapshots, not live BinauralMonitor fields — avoiding the race where `prepareToPlay`-driven re-init could change block_size between latch and drain. Item #1 D-S1 reset enumeration extended from 6→8 atomics (and Item #3 atomics from 2→4); all downstream references (§-1.5, §2 Item #1 step 3 / Test (1) step 4 / step 8 / §2 Item #1 Test (2) Scenario B / Item #3 Files / §3 ctest table / §7.1 Scenario H / §-1.15 / §8 acceptance row 5) updated for internal consistency.

Both fixes are tagged `_(iter-3 §C.1)_` / `_(iter-3 §C.2)_` at every edit site for downstream traceability. **No new test files, no new code-design changes** — only spec-precision corrections that an executor must read before implementing Item #1 + Item #3.

---

This iter-2 revision applies all 16 items from Critic ITERATE verdict. Each item carries its Critic §section and Architect §section cross-references; downstream §sections of this plan are tagged "iter-2 ref" where amended.

**Spec-precision (4, Critic §B):**
1. Critic §B.1 — ctest baseline corrected from "86 → 92/92" to "115 → 118/118 (default) / 119 (relacy ON)" everywhere _(iter-3 §C.1 harmonization: iter-2 left §header/§-1.1 at 117/118 while §3/§4/§7.3/§8 had been updated to 118/119 after the 3rd new test `bench_heartbeat_drain_latency` was added. Now consistent: 115 baseline + 3 NEW default = 118; +1 relacy = 119)_. Verified via `ctest -N \| tail -1` on both `build_off/` and `build_vst3_on/` (= 115 tests, same set). All "≥87" trivially-passable wording removed. (§3, §4 amended.)
2. Critic §B.2 — pytest path corrected from `tests/test_osc_warning_channel.py` → `tests/soak_harness/test_osc_warning_channel.py` in all 4 occurrences. Verified via `find . -name test_osc_warning_channel.py` (single hit at `./tests/soak_harness/test_osc_warning_channel.py`; `tests/` top-level contains no `.py` files). (§2 Item #3, §3, §6, §7.2 amended.)
3. Critic §B.3 — Item #7 over-claim removed. Verified `grep -c "P[0-3]-" docs/weekly_progress_report_2026-05-18.md` = **36** (lines 308-321 contain P0-1..P0-4, P1-1..P1-X with explicit status table; P1-7 explicitly marked `✅ closed` at line 323 in iter-2-readable form). Item #7 re-scoped to (a) audit script NEW + first-run audit-only report + (b) targeted `RELEASE_NOTES_EN.md:111` precision pass only. Bulk P-tag-add work dropped. (§2 Item #7 rewritten.)
4. Critic §B.4 — Item #6 over-claim removed. Verified ADR 0016 already contains: Appendix A at line 233-269 (full §6 recap + per-band election table), Limitations & legal review status at line 291-297. Item #6 re-scoped to (a) `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` NEW + (b) Limitations trigger-events precision pass + per-trigger owner naming per Architect AS-6. (§2 Item #6 rewritten.)

**Architect Must-fix amendments (3 confirm + 1 downgrade):**
5. **AM-1** (Architect §D-M1 + Critic §A.1) — CONFIRMED, EXTENDED. Item #1 D-S1 reset spec now explicitly enumerates 6 atomics: `runtime_demote_strikes_=0`, `runtime_demoted_=false`, `runtime_demote_warning_pending_=false`, snapshots `runtime_demote_last_reset_ns_=now_ns`, AND clears D-S3-introduced `runtime_demote_max_ratio_x1000_=0`, `runtime_demote_max_ratio_at_event_x1000_=0` (Architect missed D-S3 atomics; Critic caught). Without strike-reset → zero hysteresis on next over-budget block. (§2 Item #1 + §2 Item #3 + §3 amended.)
6. **AM-2** (Architect §D-M2 + Critic §A.2) — CONFIRMED with read-then-store correction. Item #3 D-S3 default INVERTED from CAS to relaxed-load-then-store. Critic correction to Architect's wording: the strictly-correct pattern is `int cur = atomic.load(std::memory_order_relaxed); if (new_ratio_x1000 > cur) atomic.store(new_ratio_x1000, std::memory_order_relaxed);` — NOT `store(max(...), relaxed)` (which is two non-atomic operations evaluated together). Audio thread is sole producer (verified: only call site is `SpatialEngine::audioBlock` per v0.6 retro §A.2), so no real producer contention. CAS promotion deferred to v0.8 conditional on telemetry-informed precision need. (§2 Item #3 amended with exact pattern.)
7. **AM-3** (Architect §D-M3 + Critic §A.3) — CONFIRMED option (b). Item #3 D-S3 encode-API locked to **(b) dedicated `sendReplyImplIIF(addr, types, i1, i2, f1)`** — NOT extending the v0.6 #8 `sendReplyImpl` flag set. Per Critic §A.3: inline comment block `// v0.7 D-S3 — intentional duplication of sendReplyImpl. See ADR 0017 §B for rejection of flag-extension option (a).` added to `OSCBackend.cpp` at the new impl. ADR 0017 §B paragraph also references this. (§2 Item #3 + §6 ADR 0017 amended.)
8. **AM-4 DOWNGRADE** (Architect §D-M4 → Critic §A.4 downgrade to Should-have). Verified `tests/soak_harness/test_osc_warning_channel.py:187-251`: assertion structure is **presence-only** (`if not (ambivs_at and no_sofa_at): pytest.fail(...)`) — NO ordering assertion between `ambivs_at` and `no_sofa_at`. Heartbeat drain at `vst3/SpatialEngineProcessor.cpp:1293-1350` enqueues sequentially into SPSC ring → wire order deterministic at source. Plan §3 "ordering requirement" REMOVED from existing test; new `test_binaural_diag_emitted_on_demote` documents expected ordering as a comment for human readers, asserts presence + per-emission latency budget only. (§3 amended.)

**New MAJOR finding (Critic §B.6):**
9. **Relacy license audit deliverable** added to Item #4: explicit upstream URL `https://github.com/dvyukov/relacy` + commit SHA pin (`git ls-remote` value at vendoring time, recorded in `third_party/relacy/UPSTREAM_PIN.txt`) + verbatim `third_party/relacy/LICENSE` (BSD-2-Clause); transitive dependency audit (grep relacy headers for `#include <boost/`/`#include "../"` etc.); CMake `FetchContent_Declare(GIT_TAG <SHA>)` or git submodule with commit pin (Architect AN-3 recommends vendor over submodule for "zero network at clone" — accepted); default OFF retained. ADR 0016 amendment ONLY if dep surface expands beyond BSD/MIT/Apache (BSD-2-Clause is pre-approved by ADR 0016 §License compatibility). (§2 Item #4 amended; §6 lists ADR 0016 conditional amendment.)

**Should-have additions (4, Critic §B.7/§B.8/§C.1/§C.2 + §C.4):**
10. **Critic §B.7** — Relacy CI promotion criterion unified at **5 consecutive green runs** (matching `cross-platform.yml` Linux ARM64 5-green soak per pre-mortem §7.1 Scenario C). "1-green" wording removed everywhere. (§2 Item #4 amended.)
11. **Critic §C.1** — New ctest `bench_heartbeat_drain_latency` added: captures baseline wall-clock cost of `SpatialEngineProcessor::heartbeatLoop` single drain iteration (50 samples, median); persists baseline to `core/tests/core_unit/heartbeat_drain_baseline.txt`; fails if measured median exceeds baseline by +50%. Guards against D-S3 adding hidden heartbeat drain overhead. (§3 + §7.2 amended.)
12. **Critic §C.2** — New ctest `b2_runtime_underrun_user_reset_concurrent` added: uses `std::thread` + `std::barrier` (C++20) or `std::atomic<int>` spin-fence to synchronize audio-thread `recordB2BlockTiming` strike-bump 7→8→CAS-demote against IO-thread `resetRuntimeDemoteFromUser`. Asserts neither order leaves atomics in invalid state (i.e., `runtime_demoted_=true` + `runtime_demote_strikes_=0` is allowed; `runtime_demoted_=true` + warning_pending_=false after reset is NOT — warning must have been drained by the IO thread). (§2 Item #1 + §3 amended.)
13. **Critic §C.4** — Promotion order locked: **(i) Linux ARM64 to required P0 in v0.7** (5-green soak gate); **(ii) Relacy ON CI separate leg P1** (own 5-green soak after ARM64 promotion, with cppmem-fallback if false positives surface per §7.1 Scenario B). Rationale: ARM64 hardware-race surface outranks synthetic-verifier confidence — we cannot withhold ARM64 promotion waiting for relacy. Relacy is an upstream early-warning, not a gate. (§2 Item #5 + new note in §7.1 Scenario C.)

**Pre-mortem scenario additions (2, Critic §D.7/§D.8):**
14. **Critic §D.7 — Scenario G NEW (DOS via OSC reset spam).** `/sys/binaural_reset_demote ,i 1` reachable from any peer post-handshake. Without rate limit, N>16 rapid rejected resets could back up the 16-slot outbound SPSC ring (`OSCBackend.h:254 kOutboundRingCap=16`), causing legitimate warnings (`ambivs_demoted_runtime`) to drop via `outbound_drops_` counter. Mitigation: rate-limit `reset_demote_cooldown_active` warning to **at most once per cooldown window** via new `bool reset_cooldown_warning_emitted_` flag in `BinauralMonitor` (cleared on Accept, set on first CooldownActive emission). Also: OSC peer-validation already required by v0.5.1 P1 (`osc_security_peer_validation` ctest #115 covers this); ADR 0016 Band-0 internal-lab assumption applies — no untrusted peers in current cycle. Severity remains Medium-Low. (§2 Item #1 + §7.1 Scenario G NEW.)
15. **Critic §D.8 — Scenario H NEW (`initialize()` + OSC reset race).** `prepareToPlay`-driven `initialize()` (control thread, audio thread stopped per VST3 SDK contract) concurrent with OSC IO-thread `resetRuntimeDemoteFromUser`. Both paths converge on the same **8 atomics** _(iter-3 §C.2 — was 6 in iter-2; +2 D-S3 demote-moment snapshots)_ from Item #1 AM-1 BUT cooldown atomic `runtime_demote_last_reset_ns_` explicitly survives `initialize()` (process-lifetime semantics, per §A.1 AM-1). Design intent: "cooldown survives prepareToPlay; resets only on process restart." Mitigation: new test scenario in `b2_runtime_underrun_user_reset_concurrent` (Item #12 above) runs `initialize()` concurrent with OSC reset; asserts cooldown atomic is unchanged after `initialize()` and **other 7 atomics** converge to fresh state. (§2 Item #1 + §7.1 Scenario H NEW.)

**Obsolete removal (1):**
16. **Critic §B.5 / Architect AS-1** — DROPPED. Verified `core/src/output_backend/BinauralMonitor.cpp:461`: `if (block_size <= 0 || sample_rate <= 0.f) return;` already in production, *before* the `runtime_demoted_` early-return at line 464. Architect AS-1 missed this. D-S2 derivation simply lands *after* line 464 (post-early-return); the existing line 461 guard protects div-by-zero in the new derivation as well. (Item #2 §"Files" section amended to note "no new guard needed — existing line 461 covers".)

---

---

## §0. RALPLAN-DR summary (for Architect/Critic alignment)

### §0.1 Principles (5)

1. **Honesty-first surface.** Every binding recommendation from the v0.6 retro (Architect §D, Critic §D) either lands as code in v0.7 *or* is explicitly re-deferred in §5 with a named successor cycle. No silent carry-over.
2. **RT-safety regressions are non-negotiable.** Any v0.7 change that touches the audio callback (`SpatialEngine::processBlock`, `BinauralMonitor::recordB2BlockTiming`, OSC outbound ring) must be covered by either `rt_alloc_probe` *or* a relacy-style synthetic verification — preferably both.
3. **Adaptive heuristics beat magic numbers.** Block-size-aware strike scaling (D-S2) and EWMA-aware demote precision (D-L2 deferred) replace the v0.6 hard-coded `kRuntimeDemoteStrikes = 8 × 90%`. The v0.7 ship handles D-S2; D-L2 is queued for telemetry-informed decision.
4. **Telemetry before tuning.** D-S3 (`/sys/binaural_diag`) lands before any future demote-threshold adjustment. Without telemetry we cannot empirically defend a new `kRuntimeDemoteBudgetFraction` value.
5. **Cross-platform gating earns its name.** `cross-platform.yml` exits the `continue-on-error: true` regime for Linux ARM64 (we have an `ubuntu-24.04-arm` GHA runner) and stays signal-only for macOS-14 only until a named maintainer claims the hardware-handoff slot.

### §0.2 Decision drivers (top 3)

1. **D-M1 silent bug taught us that documented contracts must have production-path tests.** Every new state machine in v0.7 (reset_demote, telemetry counters, vDSO re-probe) needs an `initialize()`-driven test, not just a test-hook-driven test.
2. **The v0.6.x heartbeat is already at 100 ms (D-M4).** Any v0.7 new outbound (telemetry, reset_demote ack) inherits a 100 ms upper-bound latency for free, *if* it routes through the existing drain-first-then-wait pattern. New cadences must be justified.
3. **ADR 0016 GPL §6 legal-surface gap is a Band-1 handoff blocker.** Without the §6 written-offer mapping and the explicit "no legal counsel reviewed this" disclosure, a first external Band-1 binary handoff has a real GPL-compliance audit risk. v0.7 closes the *documentation* surface; live legal engagement remains user-driven.

### §0.3 Viable options considered

#### Decision A: "What does v0.7 actually ship?"

| Option | Pros | Cons |
|---|---|---|
| **A-1: Carry-only — land all Architect §D Must-fixes + Should-haves + Critic §D MAJORs.** Ship D-S1 + D-S2 + D-S3 + D-S4 (relacy) + D-S5 (CI promotion) + Critic MAJORs (ADR 0016 GPL §6 mapping, P-tag chain, weekly-report cleanup) + Long-tail (B.1 saturate, sticky-saturate guard). | Concrete + bounded. Each item self-contained, deterministic to test. Total scope ≈ 8 items × small-medium size. | Optional D-L items (auto-re-arm, EWMA, audio-thread→IO ring) stay deferred; if v0.8 also defers them, the backlog compounds. |
| **A-2: Carry + 1 long-tail. Pick exactly ONE of D-L1 (RT-safe SPSC audio→IO ring) / D-L2 (EWMA smoothed demote) / D-L3 (auto-re-arm).** Adds architectural depth. | Demonstrates architectural progression beyond pure honesty-debt repayment. | Triples the diff size for one item; risks v0.7 becoming a 2-sprint cycle. Picking the wrong D-L item without telemetry (D-L2/L3 both want D-S3 data) wastes work. |
| **A-3: Defer EVERYTHING except v0.6.x already-shipped.** Make v0.7 a docs-only cycle (ADR 0016 §6 mapping + ipc_schema annexes + weekly-report tightening). | Lowest risk. Buys time for telemetry collection. | Wastes the post-retro momentum. The 5 Should-haves are explicitly time-bounded to v0.7 in Architect §D — slipping them past v0.7 invalidates the retro's binding-recommendation framing. |

**Selected: A-1.** Rationale: Architect §D explicitly tags D-S1..D-S5 as "Should-have in v0.7," and shipping all five with the Critic MAJOR/MEDIUM honesty-debt repayments is the smallest plan that honors the retro's binding-recommendation framing. A-2 is rejected for v0.7 because (i) telemetry to inform D-L2/L3 doesn't exist yet — D-S3 must ship first; (ii) D-L1 cost-benefit (~300 LOC for a benefit gated on "future high-frequency outbound channel") is not justified by current outbound load. A-3 is rejected because it violates Principle 1 (honesty-first surface — Should-haves are not deferrable without a successor cycle, and we have not committed to v0.8 dates).

#### Decision B: "How does the v0.7 telemetry channel (D-S3) avoid scope creep?"

| Option | Pros | Cons |
|---|---|---|
| **B-1: One new OSC verb `/sys/binaural_diag ,iif <block_size> <sample_rate_int> <observed_max_ratio>`** emitted as part of the existing `ambivs_demoted_runtime` event drain. New `sendReply(addr, types, int32_t i1, int32_t i2, float f1)` overload _(iter-2: locked to dedicated `sendReplyImplIIF` per AM-3)_. Soak-harness `tests/soak_harness/test_osc_warning_channel.py` _(iter-2: path corrected per Critic §B.2)_ extension to log to `soak_reports/binaural_diag_*.jsonl`. | Smallest possible API surface. Local-only (no upload). Reuses existing drain infra. Aligned with ADR 0016 Band-0 internal-lab posture. | Single shot per demote event — no time-series. Cannot detect "ratio creeping up over hours" patterns. |
| **B-2: Continuous 1 Hz telemetry on heartbeat status tick.** `/sys/binaural_diag` emitted every second alongside `/sys/binaural_status`. | Captures slow degradation. | 1 Hz × hours of soak = log explosion. Privacy/storage burden. Requires new opt-in flag (defaulting OFF — but then we get no data from default users). |
| **B-3: Always emit pre-demote-window summary (last 32 blocks' min/max/p95 ratio).** Emitted on demote, plus on every `/sys/binaural_status` if `runtime_demote_strikes_ > 0` (i.e., we are "close to demoting"). | Best information-density / wire-cost ratio. | Requires a ring buffer of 32 doubles on the audio thread (small, fixed-size, ok), plus 3 sort operations per-emit on the IO thread (also ok). Adds 30-50 LOC vs B-1. |

**Selected: B-1 for v0.7, with explicit follow-up note for B-3 in v0.8 once telemetry-volume reality is observed.** Rationale: Principle 4 (telemetry before tuning) — we don't yet know what we don't know; let the simplest channel give us first data and decide cadence in v0.8 from observed signal. B-2 fails because of log explosion. B-3 prematurely optimizes for a not-yet-measured failure pattern.

#### Decision C: "How does v0.7 fix the ADR 0016 GPL §6 gap without engaging actual legal counsel?"

| Option | Pros | Cons |
|---|---|---|
| **C-1: Add `### Appendix A — GPL-3 §6 per-band obligation mapping` table** to ADR 0016. Surface "no legal counsel has reviewed this ADR" in a new `### Limitations & legal review status` section. | Closes Critic MAJOR (§A.3, MEDIUM-3) honestly. Cheap. | Does not actually obtain legal review — that remains user-driven and out-of-scope. |
| **C-2: C-1 plus draft a `docs/legal/BAND_1_HANDOFF_TEMPLATE.md`** — a written-acknowledgement email template that the project lead can send/receive per Band-1 recipient, capturing GPL-3 §3a source-availability commitment + §6 written-offer-for-3-years language. | Operationally usable. Surfaces what the §6 obligation looks like in practice without lawyering. | Still not legally reviewed. A real lawyer might rewrite the entire template. Risk: lulls users into thinking the template is sufficient. |
| **C-3: Block Band-1 handoffs entirely in v0.7 plan until legal review happens.** Add a top-level "GPL legal review GATING for Band-1" banner to ADR 0016. | Maximal safety. | Pure pessimism — Band 0 internal lab is fine; we just don't have any Band-1 handoff candidates yet. Premature mitigation. |

**Selected: C-1 + C-2 (paired).** Rationale: C-1 alone is honest but operationally inert. C-2 gives the user a working artifact for the day they actually attempt a Band-1 handoff. The template explicitly says at the top "this template has NOT been reviewed by legal counsel; treat as a starting point for your own review" — propagating the limitation honestly. C-3 is over-engineered for a band count currently = 0.

### §0.4 Single-option invalidation rationale (none required)

All three decisions retain ≥2 viable options. No invalidation justification required.

### §0.5 Deliberate-mode escalation

**Escalated to DELIBERATE per `/oh-my-claudecode:ralplan` policy** because v0.7 touches:

1. **RT-safety code** — `BinauralMonitor` strike-scaling (D-S2), `OSCBackend` new overload (D-S3), `runtime_demote_strikes_` saturation guard.
2. **Cross-platform behavioral expectations** — `cross-platform.yml` Linux ARM64 leg promotes from signal-only to required. Any regression breaks main.
3. **New outbound OSC verb** — `/sys/binaural_diag` joins the schema surface; back-compat semantics matter.
4. **Legal-surface document** — ADR 0016 §6 mapping. Documents that ship are read by humans who may make legal decisions based on them.

Deliberate-mode additions (pre-mortem + expanded test plan) are in §7.

---

## §1. Why this version exists

The v0.6 cycle shipped 4 RT-safety items (post-hoc plan + retro-ralplan), and v0.6.x landed:

- D-M1 silent bug fix (`BinauralMonitor::initialize()` resets sticky-demote state).
- D-M2 vDSO availability probe + `rt_timing_unavailable` warning.
- D-M4 100 Hz heartbeat with sub-tick counter (1 Hz `/sys/binaural_status` preserved).
- MEDIUM-1/2 release-validation honesty pass + ipc_schema.md outbound-channels section.
- MEDIUM-3 ADR 0016 §6 partial mapping (commit `c304ec2`).

What remains explicitly bound to v0.7 by retro:

**From Architect §D (binding for v0.7):**
- **D-S1** — `/sys/binaural_reset_demote ,i 1` user-hatch + 60 s cooldown.
- **D-S2** — Block-size-aware `kRuntimeDemoteStrikes` scaling.
- **D-S3** — `/sys/binaural_diag` telemetry channel + new `sendReply` overload + soak-harness extension.
- **D-S4** — Relacy race-detector dev-dep + `test_osc_outbound_relacy` synthetic verification of #9.
- **D-S5** — `cross-platform.yml` Linux ARM64 promotion to required; macOS-14 stays signal-only with explicit hardware-handoff owner.

**From Critic §D (remaining):**
- **MAJOR — ADR 0016 GPL §6 legal-surface honesty** (Appendix A obligation mapping + Limitations section). v0.6.x only partially addressed.
- **MAJOR — P-tag cross-reference integrity** across `weekly_progress_report_*.md`, `RELEASE_NOTES_EN.md`, ADR 0016. Audit + fix.
- **MINOR — `runtime_demote_strikes_` saturation guard** (LOW-2 in Critic §D).
- **MINOR — Band-1 handoff template** (Critic MEDIUM-3 successor; pairs with ADR §6 mapping).

**From `.omc/plans/open-questions.md` (closeable in v0.7):**
- v0.5.1 V051-OQ1 (telemetry cadence 1 Hz default) — confirmable now that `binaural_diag` lands.
- v0.5.1 V051-OQ2 (Q5 commitment level) — fully promoted (was stretch; landed in v0.6 #5).
- Open-questions appendix needs a v0.7 entry block.

These are not "feature requests" — they are the closing balance on the v0.6 retro's binding recommendations.

---

## §2. Scope (8 items)

### Item #1 — D-S1: OSC `/sys/binaural_reset_demote ,i 1` user hatch + cooldown _(iter-2 ref: AM-1, Critic §C.2/§D.7/§D.8, Architect AS-5)_

**Problem.** v0.6 #5 sticky-demote has no in-host recovery path. A user who hit one transient CPU spike (another app started compiling) lives with B1 forever in that `prepareToPlay` lifetime even after the spike clears. Architect §A.2 Option (α) recommends this hatch with a 60 s cooldown to prevent ping-pong glitch loops.

**Fix.** Add inbound OSC handler `/sys/binaural_reset_demote ,i <enable>` (where `enable != 0` triggers reset). Handler runs on the OSC IO thread (NOT audio thread), so it can safely call `BinauralMonitor::resetRuntimeDemoteFromUser()`. The new method (per AM-1 + Critic §C.3):

1. Loads `runtime_demote_last_reset_ns_` (new atomic, `int64_t` ns since epoch via `steady_clock`).
2. Computes elapsed; if elapsed < 60 s, increments `reset_rejected_count_` (new atomic). **If `reset_cooldown_warning_emitted_` (new bool atomic) is false, set it true AND emit `/sys/binaural_warning ,s "reset_demote_cooldown_active"`. Otherwise suppress emission** (per Critic §D.7 — rate-limit to once per cooldown window to prevent DOS via reset spam saturating the 16-slot outbound SPSC ring).
3. If elapsed ≥ 60 s, performs the **AM-1-extended 8-atomic reset** _(iter-3 §C.2 Option (c) extension: was 6 atomics in iter-2; +2 from D-S3 demote-moment snapshot atomics introduced by Item #3 (a))_ (explicit enumeration; the v0.6 #5 sticky state is in 3 atomics, D-S3 adds 4 atomics total — 2 max-ratio + 2 demote-moment snapshots — plus cooldown snapshot):
   - **First clear D-S3 telemetry atomics** (Critic §C.3 ordering; iter-3 §C.2 adds the 2 demote-moment snapshots to this clear-block): `runtime_demote_max_ratio_x1000_.store(0, std::memory_order_release)`, `runtime_demote_max_ratio_at_event_x1000_.store(0, std::memory_order_release)`, `runtime_demote_block_size_at_event_.store(0, std::memory_order_release)`, `runtime_demote_sample_rate_at_event_.store(0, std::memory_order_release)`.
   - **Then clear the v0.6 #5 sticky state**: `runtime_demote_strikes_.store(0, std::memory_order_release)`, `runtime_demote_warning_pending_.store(false, std::memory_order_release)`, `runtime_demoted_.store(false, std::memory_order_release)` (in THIS order — clear demote flag LAST so audio thread's early-return at `BinauralMonitor.cpp:464` cannot race-observe `runtime_demoted_=false` AND `runtime_demote_strikes_>=threshold` simultaneously).
   - **Snapshot cooldown** AND clear the warning-rate-limit flag: `runtime_demote_last_reset_ns_.store(now_ns, std::memory_order_release)`, `reset_cooldown_warning_emitted_.store(false, std::memory_order_release)`.
   - **Emit** `/sys/binaural_warning ,s "reset_demote_accepted"`.

   **Why AM-1 matters (zero-hysteresis bug rationale, per Architect §A.1 + Critic §A.1):** at the moment the demote latch fires (`BinauralMonitor.cpp:488` CAS-success), `runtime_demote_strikes_` is frozen at the trigger value (≥ effective_strikes). After demote, `recordB2BlockTiming` early-returns at line 464 — strikes never reset to 0 on their own. Without explicit strike-reset in the user-hatch path, the **next single over-budget block** bumps strikes from (say) 8→9 (still past threshold), and the demote latch fires IMMEDIATELY on the next CAS. User experiences zero hysteresis = instant re-demote.

**Files.**
- `core/src/output_backend/BinauralMonitor.h`: new public method `resetRuntimeDemoteFromUser(int64_t now_ns) -> ResetResult { Accepted, CooldownActive, NotDemoted }`. New atomics `runtime_demote_last_reset_ns_` (`std::atomic<int64_t>`, init `INT64_MIN`), `reset_rejected_count_` (`std::atomic<int>`, init 0), `reset_cooldown_warning_emitted_` (`std::atomic<bool>`, init false). New constant `kResetDemoteCooldownNs = 60LL * 1'000'000'000LL`.
- `core/src/output_backend/BinauralMonitor.cpp`: implementation per the **8-atomic reset order above** _(iter-3 §C.2 — was 6 in iter-2; +2 D-S3 snapshot atomics)_. Constructor initializes the 3 new atomics from Item #1 + the 4 D-S3 atomics from Item #3 (a). **`initialize()` does NOT reset `runtime_demote_last_reset_ns_` or `reset_cooldown_warning_emitted_`** — cooldown is process-lifetime per AS-5 design intent (close-reopen-project resets to fresh process; documented in CH7).
- `core/src/ipc/CommandDecoder.cpp`: new tag for `/sys/binaural_reset_demote ,i`. Routes to `SpatialEngine` forwarder. Peer-validation reused from existing `osc_security_peer_validation` infra (ctest #115 covers).
- `core/src/core/SpatialEngine.h/.cpp`: forwarder `resetBinauralRuntimeDemoteFromUser(int64_t now_ns) -> ResetResult` that delegates to `BinauralMonitor`.
- `vst3/SpatialEngineProcessor.cpp`: heartbeat drain emits the two new warning string codes (`reset_demote_cooldown_active`, `reset_demote_accepted`) via the existing latch mechanism. NOTE: these warnings are 1-arg `,s` so reuse existing `sendReply(addr, types, const char*)` overload — no new encode API needed for #1 (the new `sendReplyImplIIF` is Item #3 only).
- `docs/ipc_schema.md`: add `/sys/binaural_reset_demote ,i` to inbound section; add `reset_demote_cooldown_active`, `reset_demote_accepted` to warning-string table. **Document the process-lifetime cooldown semantic** per AS-5 (close-reopen project resets cooldown to fresh; this is intended behavior).
- `docs/manual_kr/CH7_BINAURAL.md` §7.5.4: document the new OSC hatch + AS-5 process-lifetime semantic ("프로젝트를 닫았다 다시 열면 cooldown 카운터가 새로 시작합니다 — 이는 의도된 동작입니다").

**Test.** Three ctest files:

(1) **`b2_runtime_underrun_user_reset`** (`core/tests/core_unit/test_b2_runtime_underrun_user_reset.cpp`, NEW):
1. Drive monitor to demoted via `injectRuntimeUnderrunStrikesForTest` + over-budget timing.
2. Assert `isRuntimeDemoted() == true`, `runtime_demote_strikes_.load() >= kRuntimeDemoteStrikes`.
3. Call `resetRuntimeDemoteFromUser(now_ns=0)` — expect `Accepted` (first call always accepted; `INT64_MIN` baseline).
4. Assert ALL 8 atomics reset _(iter-3 §C.2 — was 6; +2 demote-moment snapshots)_: `isRuntimeDemoted() == false`, `runtime_demote_strikes_.load() == 0`, `runtime_demote_warning_pending_.load() == true` (warning armed), `runtime_demote_max_ratio_x1000_.load() == 0`, `runtime_demote_max_ratio_at_event_x1000_.load() == 0`, `runtime_demote_block_size_at_event_.load() == 0`, `runtime_demote_sample_rate_at_event_.load() == 0`, `runtime_demote_last_reset_ns_.load() == 0`. **CRITICAL** (AM-1 regression gate): drive a single over-budget `recordB2BlockTiming` call — assert `runtime_demoted_.load() == false` (zero-hysteresis bug would re-demote here).
5. Immediately re-demote (use inject hook), call `resetRuntimeDemoteFromUser(now_ns=30 * 1'000'000'000LL)` — expect `CooldownActive`, demote unchanged, `reset_cooldown_warning_emitted_=true`.
6. Call again at `now_ns=31s` (still cooldown) — expect `CooldownActive`, **but warning NOT re-armed** (Critic §D.7 rate-limit assertion: `runtime_demote_warning_pending_` should NOT toggle on the second cooldown rejection).
7. Re-call at `now_ns=70s` — expect `Accepted`, `reset_cooldown_warning_emitted_=false` (cleared).
8. Call when not demoted — expect `NotDemoted`, no atomic writes (asserted by reading all 8 atomics before/after) _(iter-3 §C.2 — was 6; +2 demote-moment snapshots)_.

(2) **`b2_runtime_underrun_user_reset_concurrent`** (`core/tests/core_unit/test_b2_runtime_underrun_user_reset_concurrent.cpp`, NEW per Critic §C.2 + §D.8):
- **Scenario A (audio-thread strike-bump vs IO-thread reset).** Use `std::atomic<int>` spin-fence + `std::thread`. Thread A: spin-wait on fence, then call `recordB2BlockTiming(128, 48000, deadline_ns * 0.95)` 8 times (drive strikes 0→8→CAS-demote at line 488). Thread B: spin-wait on fence with offset, then call `resetRuntimeDemoteFromUser(now_ns=0)`. Run 1000 iterations with varying offsets. **Invariant:** at no point should the monitor end in state `(runtime_demoted_=true AND runtime_demote_strikes_=0 AND runtime_demote_warning_pending_=false)` — that combo means the warning was lost. Acceptable end states: (Accepted, demote cleared) OR (RaceLost, demote stayed, strikes near 0). Verify `outbound_drops_` (from `OSCBackend`) does NOT increment.
- **Scenario B (concurrent `initialize()` vs OSC reset, Critic §D.8).** Drive monitor to demote first. Thread A: call `initialize(cfg)` (control thread). Thread B: call `resetRuntimeDemoteFromUser(now_ns=now)`. Repeat 100 iterations. **Invariant:** `runtime_demote_last_reset_ns_` ALWAYS retains the most recent `now_ns` value (cooldown survives initialize); other 7 atomics converge to fresh state _(iter-3 §C.2 — was 5; +2 D-S3 demote-moment snapshots)_ in both orderings. AS-5 process-lifetime contract verified.

(3) **`osc_security_peer_validation`** REVISED (existing): add 1 scenario sending `/sys/binaural_reset_demote ,i 1` from unauthenticated peer — assert it is REJECTED at the OSC-layer (no audio-side effect, `outbound_drops_` unchanged).

**Acceptance.** All 3 ctest scenarios PASS; `docs/ipc_schema.md` lints; CH7 §7.5.4 has the new OSC verb section + process-lifetime cooldown paragraph (AS-5).

### Item #2 — D-S2: Block-size-aware `kRuntimeDemoteStrikes` _(iter-2 ref: Critic §B.5 obsolete-removal, Architect AN-2)_

**Problem.** Architect §A.2 — `kRuntimeDemoteStrikes = 8` is hardcoded. At 32-sample blocks (Logic Pro typical low-latency) it = 5.3 ms (false-positive on cache-cold page fault); at 1024-sample blocks (offline render) it = 170 ms (user hears 3-4 dropouts before demote).

**Fix.** Derive `effective_strikes` per-call from block-size: `effective_strikes = max(8, ceil(0.02s / block_seconds))`. This pins the time window at ~20 ms regardless of block size (matches the original 48 kHz/128 design intent).

Implementation in `recordB2BlockTiming()` — lands **after the existing line 464 early-return** (`if (runtime_demoted_.load(...)) return;`). The existing **line 461 guard** `if (block_size <= 0 || sample_rate <= 0.f) return;` is ALREADY in production code — Architect AS-1 was based on outdated source read; **AS-1 is DROPPED per Critic §B.5 obsolete-removal**. No new guard needed for div-by-zero protection on the D-S2 derivation.

```cpp
// New code, inserts AFTER line 464 (post runtime_demoted_ early-return):
const double block_seconds = static_cast<double>(block_size) / static_cast<double>(sample_rate);
const int effective_strikes = std::max(
    kRuntimeDemoteStrikes,  // floor: 8 (preserves 48 kHz/128 invariant)
    static_cast<int>(std::ceil(0.020 / block_seconds))  // 20 ms window
);
// ... use effective_strikes (not kRuntimeDemoteStrikes) in the threshold check at line 483
```

**Constant semantics drift (Architect AN-2).** `kRuntimeDemoteStrikes = 8` changes from "the demote threshold" → "the demote-strikes FLOOR." The comment block at `BinauralMonitor.h:243-245` MUST be rewritten or future maintainers will misread:

```cpp
// ──── Demote threshold derivation (v0.7 D-S2) ────
// At runtime: effective_strikes = max(kRuntimeDemoteStrikes,
//     ceil(0.020s / block_seconds))
// Time-window invariant: ~20 ms regardless of block size.
// kRuntimeDemoteStrikes is the FLOOR (preserves v0.6 behavior at
// 48 kHz / 128-sample = 2.67ms/block → effective_strikes = 8).
// At 32 samples / 48 kHz: effective_strikes = 30 (longer hysteresis).
// At 1024 samples / 48 kHz: effective_strikes = 8 (floor, since each
// block IS most of the deadline).
static constexpr int kRuntimeDemoteStrikes = 8;
```

**Files.**
- `core/src/output_backend/BinauralMonitor.cpp`: 4-line derivation inserted after line 464. Update inline rationale comment block.
- `core/src/output_backend/BinauralMonitor.h:243-245`: REWRITE the comment block above `kRuntimeDemoteStrikes` per Architect AN-2 — current comment describes "the demote threshold," v0.7 makes it the floor. Future-maintainer trap if not rewritten.

**Test.** Extend `test_b2_runtime_underrun_auto_demote.cpp` with 3 new scenarios:
1. `block_size=32 sample_rate=48000` (= 0.67 ms/block) → effective_strikes = ceil(0.020/0.000667) = 30. Drive 29 over-budget calls → not demoted. 30th call → demoted.
2. `block_size=128 sample_rate=48000` (= 2.67 ms/block) → effective_strikes = max(8, ceil(7.5)) = 8 (current behavior preserved).
3. `block_size=1024 sample_rate=48000` (= 21.3 ms/block) → effective_strikes = max(8, ceil(0.94)) = 8 (floor enforced — short hysteresis at large block sizes is fine because each block IS the deadline).

**Acceptance.** Extended `b2_runtime_underrun_auto_demote` ctest still PASS; new scenarios PASS; existing `b2_runtime_underrun_engine_integration` still PASS (it uses the default 128 block size, behavior unchanged).

### Item #3 — D-S3: `/sys/binaural_diag` telemetry channel + new `sendReplyImplIIF` _(iter-2 ref: AM-2 relaxed default, AM-3 option b, Critic §B.2 pytest path, Critic §C.3 reset ordering, Architect AS-2)_

**Problem.** Architect §A.2 — probe accuracy calibration needs telemetry from real demote events. Without `block_size`, `sample_rate`, and `observed_max_ratio` (max `elapsed_ns / deadline_ns` over the strike window), the team cannot empirically tune `kRuntimeDemoteBudgetFraction` (currently 0.9, defended only by intuition).

**Fix.** Three coordinated changes:

#### (a) Audio thread captures the max-ratio sliding-window — **AM-2: relaxed-load-then-store, NOT CAS**

New atomic `runtime_demote_max_ratio_x1000_` (`std::atomic<int>`, int = ratio × 1000 — float atomics are not lock-free on all platforms). In `recordB2BlockTiming()`, on each over-budget block, the **iter-2 default pattern** (AM-2, with Critic §A.2 read-then-store correction over Architect's `store(max(...))` wording):

```cpp
// v0.7 D-S3 — AM-2 relaxed-load-then-store pattern (NOT CAS).
// Rationale: audio thread is sole producer of this atomic per
// SpatialEngine::audioBlock single call site (verified v0.6 retro §A.2).
// No real producer contention → CAS precision benefit is unmeasured
// and unjustified. CAS promotion deferred to v0.8 conditional on
// telemetry-informed precision need surfacing.
// Critic §A.2 correction over Architect AM-2: the strictly correct
// pattern is read-then-store, NOT a single store(max(...)) which is
// two non-atomic operations.
const int ratio_x1000 = static_cast<int>(
    (static_cast<double>(elapsed_ns) /
     static_cast<double>(deadline_ns)) * 1000.0);
const int cur_max = runtime_demote_max_ratio_x1000_.load(
    std::memory_order_relaxed);
if (ratio_x1000 > cur_max) {
    runtime_demote_max_ratio_x1000_.store(ratio_x1000,
        std::memory_order_relaxed);
}
```

On `runtime_demote_strikes_` reset (good block branch at line 479), also reset max-ratio to 0 (release-store). On demote latch fire (line 488 CAS success), snapshot the max into `runtime_demote_max_ratio_at_event_x1000_` AND snapshot the audio-thread-local `block_size_` + integer `sample_rate_` into 2 additional atomics `runtime_demote_block_size_at_event_` (`std::atomic<int>`) and `runtime_demote_sample_rate_at_event_` (`std::atomic<int>`) — all 3 snapshots persist together until next `initialize()` or D-S1 user-reset per AM-1. _(iter-3 §C.2 Option (c) — snapshot block_size + sample_rate at demote-moment to avoid IO-thread reading post-reinit values; the diag packet reports the demote-moment context, not the post-demote context.)_

**Critic §C.3 interaction with D-S1 reset:** the in-progress `runtime_demote_max_ratio_x1000_` is cleared by AM-1 reset *before* the strike counter is cleared (see Item #1 atomic ordering). After reset, the first over-budget block re-establishes the max from a fresh baseline. The next demote's diag packet reports the *new* max correctly.

#### (b) IO thread emits diag — heartbeat drain extension

Heartbeat drain at `vst3/SpatialEngineProcessor.cpp:1293-1350`, when `drainRuntimeDemotePending() == true`, **immediately after** the existing `sendReply` call for `/sys/binaural_warning ,s "ambivs_demoted_runtime"`, also emits `/sys/binaural_diag ,iif <block_size> <sample_rate_int> <observed_max_ratio>` _(iter-3 §C.2 Option (c) — reads demote-moment snapshot atomics, NOT live BinauralMonitor fields; previous iter-2 wording invented `lastObservedBlockSize()` / `lastObservedSampleRate()` accessors that do not exist on BinauralMonitor — only `int blockSize()` at `BinauralMonitor.h:125` does, with no `sampleRate()` accessor at all)_:
- `block_size` = `binauralMonitor().snapshotRuntimeDemoteBlockSizeAtEvent()` (new snapshot accessor; reads `runtime_demote_block_size_at_event_.load(std::memory_order_acquire)`).
- `sample_rate_int` = `binauralMonitor().snapshotRuntimeDemoteSampleRateAtEvent()` (new snapshot accessor; reads `runtime_demote_sample_rate_at_event_.load(std::memory_order_acquire)`; `,i` wire — sample rate is integer-valued).
- `observed_max_ratio` = `binauralMonitor().snapshotRuntimeDemoteMaxRatioX1000() / 1000.0f` (float).

**Thread-safety note** _(iter-3 §C.2)_: all 3 snapshot accessors read atomics whose values are written exclusively on the audio-thread demote latch CAS-success path (single producer). IO-thread drain reads with acquire ordering; control-thread `initialize()` writes 0 with release ordering as part of the AM-1-extended D-S1 reset (now 8 atomics total — 6 from AM-1 + 2 new snapshot atomics). The diag packet always reflects the *demote-moment* context, never the *post-demote* context — even if `prepareToPlay` re-initialized the engine between latch and drain, the snapshot is still authoritative for that demote event.

Sequential enqueue → SPSC ring → IO drain → wire order is deterministic (warning packet first, diag packet second; same drain pass) per Critic §A.4 verification of `vst3/SpatialEngineProcessor.cpp:1293-1350` heartbeat drain pattern.

#### (c) Encode-API: AM-3 option (b) — dedicated `sendReplyImplIIF`

**Locked: option (b)** — a dedicated `sendReplyImplIIF(addr, types, i1, i2, f1)` parallel to v0.6 #8's `sendReplyImpl`. Inline comment at the new impl in `OSCBackend.cpp`:

```cpp
// v0.7 D-S3 (AM-3) — intentional duplication of sendReplyImpl logic.
// See ADR 0017 §B for rejection of flag-extension option (a):
// extending sendReplyImpl(addr, types, s, have_f, f, have_i, i) to
// 9 parameters with 3 boolean flags forces the 3 existing v0.6 #8
// overloads to spell extra `false, 0` defaults at every call site,
// violating the "thin forwarder" invariant. Mixed-type packets
// (,iif here, possibly ,sif/,iiif in future) are a different shape
// of packet from the simple typetags v0.6 #8 unified — forcing
// them through the same impl creates more accidental coupling than
// it removes (Critic §A.3 confirmation).
//
// New mixed-type overloads in future: add another dedicated
// sendReplyImpl<shape>; do NOT fuse with sendReplyImpl.
bool OSCBackend::sendReplyImplIIF(const char* addr, const char* types,
                                  int32_t i1, int32_t i2, float f1) noexcept
{
    // ~30 LOC of socket-plumbing duplication (peer_len_ guard,
    // outbound ring publish, notify_one).
    // ...
}
```

Public overload signature: `bool sendReply(const char* addr, const char* types, int32_t i1, int32_t i2, float f1) noexcept` — 3-line forwarder to `sendReplyImplIIF`. Matches existing v0.6 #8 forwarder pattern.

**Files.**
- `core/src/output_backend/BinauralMonitor.h`: **4 new atomics** _(iter-3 §C.2 Option (c) — was 2 in iter-2; 2 added for demote-moment snapshots)_ `runtime_demote_max_ratio_x1000_`, `runtime_demote_max_ratio_at_event_x1000_`, `runtime_demote_block_size_at_event_`, `runtime_demote_sample_rate_at_event_` (all `std::atomic<int>`, init 0). **3 new accessors** `int snapshotRuntimeDemoteMaxRatioX1000() const noexcept`, `int snapshotRuntimeDemoteBlockSizeAtEvent() const noexcept`, `int snapshotRuntimeDemoteSampleRateAtEvent() const noexcept` (all read with acquire ordering). _(iter-3 §C.2: removed iter-2's invented accessors `lastObservedBlockSize` / `lastObservedSampleRate` which do not exist on BinauralMonitor at HEAD; the existing `int blockSize() const noexcept` at `BinauralMonitor.h:125` is NOT used for the diag packet because it returns the **current** value, not the **demote-moment** value — snapshot approach avoids the race entirely.)_
- `core/src/output_backend/BinauralMonitor.cpp`: relaxed-load-then-store update in `recordB2BlockTiming` (AM-2 pattern); **3 snapshots on demote latch CAS success** (max-ratio, block_size, sample_rate snapshotted together with release ordering); reset in `initialize()` (D-M1-extended to 8 atomics total — original 6 from AM-1 + 2 new snapshot atomics; all released to 0 under the same `runtime_demoted_` LAST-clear ordering).
- `core/src/ipc/OSCBackend.h`: new `sendReply(const char*, const char*, int32_t, int32_t, float)` public overload + private `sendReplyImplIIF` declaration.
- `core/src/ipc/OSCBackend.cpp`: `sendReplyImplIIF` implementation + comment block above per AM-3. New public overload as 3-line forwarder.
- `vst3/SpatialEngineProcessor.cpp:1293-1350`: heartbeat drain extended — `/sys/binaural_diag ,iif` emitted immediately after `ambivs_demoted_runtime` warning on same drain pass.
- `docs/ipc_schema.md`: new `### Outbound — Diagnostic Telemetry (v0.7+)` section. Document the `,iif` wire format + that diag emits exactly once per demote event (NOT periodic; B-1 decision per §0.3). **AS-2 (Architect Should-have):** explicitly surface "slow-degradation pattern (ratio creeping up over hours) is NOT detected by event-driven `/sys/binaural_diag`. v0.8 may add a pre-demote-window summary channel (B-3) once real telemetry shape is observed."
- `tests/soak_harness/test_osc_warning_channel.py` _(iter-2 ref: Critic §B.2 — corrected from `tests/test_osc_warning_channel.py`)_: extend filter to capture `/sys/binaural_diag`, log to `soak_reports/binaural_diag_YYYYMMDD.jsonl` (mkdir if absent).

**Test.**
- ctest extension: `test_b2_runtime_underrun_auto_demote.cpp` adds scenarios (1) max-ratio snapshot atomic equals the over-budget ratio of the most-recent strike at demote latch; (2) snapshot persists across `drainRuntimeDemotePending()` (cleared only on D-S1 reset or `initialize()`).
- ctest extension: `osc_outbound_multi_producer` adds 1 case exercising the new `sendReply(addr, types, int32_t, int32_t, float)` overload (concurrent producer publishing `,iif` packet alongside existing `,s/,sf/,i` packets; assert FIFO ordering at IO drain).
- New pytest scenario in `tests/soak_harness/test_osc_warning_channel.py::test_binaural_diag_emitted_on_demote` _(iter-2 ref: Critic §B.2 path corrected)_: drive the engine to demote via VST3 test fixture (heartbeat drain path, NOT the standalone-binary `ambivs_disabled_cpu` path used by `TestBothWarningCodesObserved`), assert `/sys/binaural_diag` arrives with correct `,iif` schema + that the warning packet (`ambivs_demoted_runtime`) and diag packet both arrive within `PER_EMISSION_LATENCY_BUDGET_MS`. **Per Critic §A.4 AM-4 downgrade**: do NOT add ordering assertion to existing `TestBothWarningCodesObserved` (that test uses presence-only via line 218 capture pattern). New test documents expected source-deterministic ordering (warning enqueued first) as a Python comment for human readers but asserts presence + per-emission latency only.

**Acceptance.** ctest existing PASS + 2 new scenarios PASS; pytest +1 test; `soak_reports/.gitkeep` created (or `soak_reports/binaural_diag_*.jsonl` added to `.gitignore`); ipc_schema.md has the new section + AS-2 slow-degradation limitation paragraph; ADR 0017 §B has the option-(a) rejection rationale.

### Item #4 — D-S4: Relacy race-detector dev-dep + `test_osc_outbound_relacy` _(iter-2 ref: Critic §B.6 license audit MAJOR, AS-3 producer count, B.7 5-green criterion, Architect AN-3 vendor-vs-submodule)_

**Problem.** v0.6 #9 promoted `slot.ready.store(false, memory_order_relaxed)` → `memory_order_release` to close a weak-memory-order corner case on ARM/Apple Silicon. But there is **no test** that exercises the failure mode pre-fix or proves the fix correct under the C++11 memory model. ThreadSanitizer (used on x86_64) is sequentially-consistent-by-default and cannot detect this class. Architect §C.2 binding rule: "weak-memory-order fixes require synthetic verification OR hardware verification before merge."

**Fix.** Add **relacy** (header-only race detector) as a dev-dep behind a CMake flag `SPATIAL_ENGINE_BUILD_RELACY_TESTS=OFF` (default). When ON, a new ctest `test_osc_outbound_relacy` builds and runs.

#### (a) License audit deliverable — Critic §B.6 MAJOR

Per Critic §B.6 MAJOR (acceptance-blocking), the vendoring step MUST include:

1. **Upstream URL pinned**: `https://github.com/dvyukov/relacy`.
2. **Commit SHA pinned**: record at vendoring time via `git ls-remote https://github.com/dvyukov/relacy refs/heads/master` (or specific tag/release if available). Stored in `third_party/relacy/UPSTREAM_PIN.txt` with format `URL=<url>\nCOMMIT=<sha>\nDATE=<iso>\nVENDORED_BY=<commit>`.
3. **License file verbatim**: `third_party/relacy/LICENSE` contains the full upstream BSD-2-Clause text (verbatim copy, not summary). Copyright lines preserved.
4. **Transitive dependency audit**: `grep -rE '#include <(boost|tbb|absl)/' third_party/relacy/` must return zero matches. If relacy headers depend on Boost or any non-stdlib library, that license also enters the dev-dep surface — Critic §B.6 wording: "Update ADR 0016 if relacy expands the dev-dep license surface beyond MIT/BSD/Apache." BSD-2-Clause alone is pre-approved by existing ADR 0016 §License compatibility (no amendment needed unless transitive surface expands).
5. **CMake integration**: vendored (Architect AN-3 recommends "vendor over submodule for zero network at clone" — accepted). Use `add_subdirectory(third_party/relacy)` with `target_include_directories` only — no compile units (relacy is header-only).

#### (b) Test model — AS-3 explicit producer count

The test models the OSCBackend outbound ring's exact producer/consumer state machine using relacy's `rl::var<T>` and `rl::atomic<T>`. Per Critic AS-3:

- **At LEAST 2 simulated producers** in the relacy model (matching the existing `test_osc_outbound_multi_producer` ctest's real concurrency model — control thread + heartbeat IO drain producer + future audio-thread producer hard-walled out by v0.6 #4 but the multi-producer ring contract still applies).
- **1 consumer** (`outboundDrainLoop` at `OSCBackend.cpp:511`).
- Models the exact v0.6 #9 slot.ready.store sequence (release-store on enqueue; release-store on consumer drain — the v0.6 #9 fix).
- Relacy systematically explores ALL interleavings under C++11 MM, including weak-ordering relaxations that real ARM/PPC hardware exhibits.

A single-producer model would pass vacuously and miss the failure class entirely.

#### (c) Build matrix

- **Default OFF** — keeps OFF baseline byte/symbol gates unchanged (Architect §A.4 verified adjacent-impact analysis).
- **ON in a dedicated GHA job** (`relacy.yml`) — `continue-on-error: true` for v0.7 (signal-only); **promoted after 5 consecutive green runs** _(iter-2 ref: Critic §B.7 — unified at 5-green, NOT 1-green; matches `cross-platform.yml` Linux ARM64 soak gate from §7.1 Scenario C)_.
- **Promotion order** _(iter-2 ref: Critic §C.4)_: Linux ARM64 promotes FIRST (P0 in v0.7), Relacy ON CI promotes SECOND (P1 — after ARM64 stable AND own 5-green soak). Rationale: ARM64 hardware-race surface outranks synthetic-verifier confidence. Relacy is an upstream early-warning, not a gate. ARM64 cannot wait for relacy promotion.

**Files.**
- `third_party/relacy/` — vendored header-only relacy at pinned commit SHA. Includes `LICENSE` (BSD-2-Clause verbatim), `UPSTREAM_PIN.txt`, source headers under `third_party/relacy/relacy/*.hpp`.
- `core/CMakeLists.txt`: option `SPATIAL_ENGINE_BUILD_RELACY_TESTS` (default OFF). Guard the new test target behind it. `add_subdirectory(third_party/relacy)` only when flag ON.
- `core/tests/relacy/test_osc_outbound_relacy.cpp` (NEW): per AS-3, models ≥2 producers + 1 consumer with the v0.6 #9 release-store sequence. Relacy `test_suite<...>` runs N iterations (default N=1024); failure = relacy detected unsafe reordering.
- `.github/workflows/relacy.yml` (NEW): builds with `-DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON`, runs the test, uploads relacy log on failure. `continue-on-error: true` for v0.7 cycle. Header comment documents the 5-green promotion criterion + Critic §C.4 P0/P1 ordering (relacy promotes AFTER ARM64).
- `docs/release/v0.7.0/relacy-promotion-gate.md` (NEW): documents the 5-green soak procedure + cppmem-fallback path per §7.1 Scenario B + Critic §B.6 license-audit checklist (1-5 above) + transitive-dep audit grep recipe.

**Test.** The test IS the test. Acceptance: the test PASSES under relacy (proving the v0.6 #9 fix is correct under C++11 MM). **Pre-fix simulation (test-the-test, AS-3 mandate)**: temporarily revert the consumer `slot.ready.store` from release to relaxed in a local branch (DO NOT commit), confirm relacy FAILS — this validates the test catches the failure mode it claims to catch. Document the simulated-revert evidence in the test header comment as a one-shot capture.

**Acceptance.**
- Default-OFF build: OFF baseline bytes/symbols MATCH pre-v0.7 (gate unchanged).
- Relacy-ON CI job: green (signal-only in v0.7 ship; required after 5-green + Linux ARM64 promotion lands).
- Pre-fix simulated revert FAILS under relacy (documented in test header comment as one-shot evidence — captured before v0.7 ship; not part of CI).
- `third_party/relacy/LICENSE` exists, contains verbatim BSD-2-Clause text with upstream copyright lines.
- `third_party/relacy/UPSTREAM_PIN.txt` exists with URL + commit SHA + date.
- Transitive-dep audit grep returns zero non-stdlib dependencies (or ADR 0016 amendment is filed per Critic §B.6).

### Item #5 — D-S5: `cross-platform.yml` Linux ARM64 promotion to required _(iter-2 ref: AS-4 rollback procedure, Critic §C.4 promotion order)_

**Problem.** v0.6 shipped `cross-platform.yml` with both `linux-arm64` (`ubuntu-24.04-arm` runner) and `macos-arm64` (`macos-14` runner) legs as `continue-on-error: true` — failures don't block merge. The Critic §A.5 + Architect §D-S5 want this promoted to required once stable.

**Fix.** Two-step promotion strategy:

(a) **Linux ARM64 to required (P0 in v0.7 — promotes FIRST, before Relacy per Critic §C.4).** GitHub now offers Linux ARM64 runners (`ubuntu-24.04-arm`) at standard pricing. The `linux-arm64` leg has been green for the v0.6.x ship cycle. Promote by:
1. **5-green soak gate** (per §7.1 Scenario C): require 5 consecutive green triggers on `linux-arm64` before flipping `continue-on-error`. Document the gate in `cross-platform-gating.md` (see Files below).
2. Remove `continue-on-error: true` from `linux-arm64` matrix entry only (split matrix into 2 jobs OR use per-matrix-entry override via GHA include syntax).
3. Add `linux-arm64` to branch protection's required status checks (GitHub UI step — Planner documents the click-path; user executes if main is protected; otherwise Planner adds `TODO(repo-admin): mark linux-arm64 as required` comment in the workflow).
4. Document in `cross-platform.yml` header comment: "Linux ARM64 is gating as of v0.7."

(b) **macOS-14 stays signal-only but with explicit owner (Architect AS-6 + Critic §A.5 honesty pass).** GitHub `macos-14` runners are expensive and the v0.5 SSE-guard + v0.6 #9 release-store both want manual hands-on verification (`macos-arm64-verify.md` checklist). The leg stays `continue-on-error: true` but the workflow header gains a **named owner** (project lead `paiiek`) for promotion decision, with target date `v0.8` after 1 cycle of green builds AND first macOS hands-on verify checklist sign-off.

(c) **Rollback procedure when the promoted gate goes red (AS-4 binding).** If `linux-arm64` becomes required and a real race surfaces (per §7.1 Scenario C pre-mortem), the team enters chicken-and-egg deadlock: the revert PR itself must pass the failing required check. Mitigation procedure documented in `cross-platform-gating.md` §"Unblock when ARM64 required gate is red":
1. **Option-A (preferred): admin-bypass merge of revert PR.** Repo admin uses GitHub's "Merge without waiting for requirements" (Settings → Branches → bypass list). Audit log entry mandatory; admin must comment the issue link.
2. **Option-B (fallback): temporary requirement removal.** Repo admin removes `linux-arm64` from required status checks in branch protection, merges the revert PR, re-adds `linux-arm64`. Gap window must be < 15 minutes; document on `weekly_progress_report_2026-05-25.md` §5.3.
3. **Re-promotion after fix**: requires fresh 5-green soak on the fix branch before re-flipping `continue-on-error`. Same rule as initial promotion.

**Files.**
- `.github/workflows/cross-platform.yml`: split matrix into 2 jobs (one per platform) so `linux-arm64` job drops `continue-on-error: true` independently. Header comment notes v0.7 ARM64 gating + named owner for macOS-14.
- `.github/workflows/relacy.yml` (NEW, paired with Item #4): header comment notes v0.7 ship as signal-only, promotion AFTER `linux-arm64` per Critic §C.4 P0/P1 ordering.
- `docs/release/v0.7.0/cross-platform-gating.md` (NEW): documents (i) 5-green promotion criteria, (ii) current state of each leg, (iii) click-path for branch protection UI, (iv) **AS-4 rollback procedure** (full text per the 3 options above), (v) Critic §C.4 promotion order (ARM64 P0 → Relacy P1).
- `docs/weekly_progress_report_2026-05-25.md` (or current week): §5.3 entry promoting v0.7 to a v0.7-gated cycle. **Named owner `paiiek` for macOS-14 leg promotion decision** (AS-6).

**Test.** GHA workflow itself is the test. Acceptance: next 5 pushes to main trigger `cross-platform.yml` with `linux-arm64` green (`continue-on-error: true` while soak runs); on 5-green confirmation, PR flips to `continue-on-error: false`. Pre-push local validation: `yamllint .github/workflows/cross-platform.yml` + `act` dry-run if available.

**Acceptance.** Workflow file passes lint. `cross-platform-gating.md` exists with all 3 rollback options documented. PR description includes confirmation of 5-green soak (link to 5 GHA run URLs).

### Item #6 — ADR 0016 Band-1 handoff template + Limitations trigger-events precision _(iter-2 ref: Critic §B.4 over-claim removed, Architect AS-6 owner naming)_

**Problem (RE-SCOPED per Critic §B.4).** Iter-1 spec over-claimed: it asked to "add Appendix A obligation mapping" and "add Limitations section" as if they did not exist. **Verified at HEAD**: `docs/adr/0016-external-distribution-policy.md` already contains:
- **Lines 233-269**: `## Appendix A — GPL-3 §6 obligation mapping per band` (full §6 recap §6.a/b/c/d/e at lines 247-263; per-band election table at lines 265-269; better-than-iter-1-proposed because already cites §6.b 3-year written offer at line 248-251).
- **Lines 291-297**: `## Limitations & legal review status` section ("This ADR was authored by the project lead (without legal counsel)", "not a substitute for legal review", "not reviewed by a lawyer or the Software Freedom Law Center").

The actual v0.7 gap is **two narrow items**:

**Fix (narrow).**

(a) **`docs/legal/BAND_1_HANDOFF_TEMPLATE.md` (NEW, primary deliverable).** Standalone file the project lead operationally uses per Band-1 recipient. Captures GPL-3 §3a (source-availability commitment) + §6.d election (designated-place via repo URL + tag SHA) per ADR 0016 line 268. Includes:
- Header disclaimer: "이 템플릿은 법률 자문 없이 작성됐습니다. 첫 Band-1 handoff 전 GPL-aware 변호사 review 권장. ADR 0016 §Limitations 참조."
- Recipient name + email + organization + intended use case fields.
- Tag commit SHA + repo URL fields.
- Recipient acknowledgement section (GPL-3 §3a / §6 understanding).
- Audit-log entry template per ADR 0016 line 268 row "Audit-log entry required: Yes — record tag commit SHA, repo URL, recipient identity, ack timestamp in `docs/license_procurement_plan.md §Audit log`."

(b) **Limitations trigger-events precision pass (extends existing §"Limitations & legal review status" at line 291-297).** Current ADR text says "before the **first** Band-1 conveyance to a non-SNU-MARG..." but does NOT enumerate the trigger events as a numbered list nor name an owner per trigger. Iter-2 amendment adds:
- **3 explicit trigger events** (numbered list): (1) first Band-1 candidate identified, (2) any party requesting Band-2 or Band-3 redistribution, (3) any third-party GPL-3 compliance audit inquiry.
- **Named owner per trigger (Architect AS-6 + V07-Q5).** Project lead `paiiek` is the default owner for all three triggers in v0.7. Surface "should each band have a designated reviewer?" as a **v0.8 carry-forward open question** in `.omc/plans/open-questions.md`.

**Files.**
- `docs/adr/0016-external-distribution-policy.md`: extend the existing `## Limitations & legal review status` section (line 291-297) with 3-numbered-list trigger events + owner naming. **DO NOT touch Appendix A** (already complete per Critic §B.4 verification).
- `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` (NEW): standalone file per (a) above.
- `.omc/plans/open-questions.md`: V07-Q5 already exists in iter-1 plan as "trigger-event ownership" — keep open with iter-2 note "v0.7 names `paiiek` as default; v0.8 may introduce per-band reviewers."

**Test.** Documentation-only — no code test. Acceptance: `markdownlint docs/legal/BAND_1_HANDOFF_TEMPLATE.md docs/adr/0016-external-distribution-policy.md`; spot-check that the 3 trigger events match the wording in `docs/release/v0.7.0/RELEASE_NOTES_EN.md`.

**Acceptance.**
- `grep -ciE 'lawyer|attorney|legal counsel|written offer' docs/adr/0016-external-distribution-policy.md` ≥ 4 — **likely ALREADY passes at HEAD** per Critic §B.4 verification. Re-confirm at v0.7 ship and document delta.
- `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` exists, contains the "법률 자문 없이 작성" disclaimer header, has all 5 fields (recipient, repo URL, tag SHA, acknowledgement, audit-log entry).
- ADR 0016 `## Limitations & legal review status` section contains a numbered 3-trigger list + `paiiek` named as default owner.

### Item #7 — Critic MAJOR re-scoped: P-tag audit script + targeted RELEASE_NOTES precision _(iter-2 ref: Critic §B.3 over-claim removed)_

**Problem (RE-SCOPED per Critic §B.3).** Iter-1 spec over-claimed: it asked to "add P-tag tags to weekly_progress_report §5 (which has 0 P-tag matches)." **Verified at HEAD**: `grep -c "P[0-3]-" docs/weekly_progress_report_2026-05-18.md` = **36** matches. Lines 308-321 contain an explicit P-tag status table with `P0-1..P0-4, P1-1..P1-7+, P2-1..P2-6`. **Line 323** marks `P1-7 (HIGH-2)` explicitly as `✅ closed | 본 리비전` — the Critic-v0.6-retro HIGH-2 cross-reference issue is **already fixed**.

The actual v0.7 gap is **two narrow items** (audit-only + targeted precision):

**Fix (narrow).**

(a) **`scripts/audit_ptags.py` (NEW, audit-only — does NOT perform bulk repair).** Per Critic §B.3 re-scoping:
- Greps `docs/`, `.omc/plans/`, `CHANGELOG.md`, `README.md`, `core/CMakeLists.txt` etc. for `P[0-3]-\d+` patterns.
- Builds a graph: each P-tag is a node; each citation is an edge.
- Reports orphaned tags (cited but never defined) and orphaned definitions (defined but never cited).
- Output: `docs/process/ptag_audit.json` (JSON snapshot, committed) + `docs/process/ptag_audit.md` (human-readable Markdown summary).
- **Run as audit-only ONCE during v0.7 cycle**; commit the snapshot. Future regressions are detected via `diff` of the snapshot file in PRs (Architect AN-1 makes this explicit in `CONTRIBUTING.md`).

(b) **`RELEASE_NOTES_EN.md:111` targeted precision pass.** Iter-1 claimed "Update `RELEASE_NOTES_EN.md:111` 'P1 process gap' to cite the specific §5.1 entry." Verify at v0.7 time whether the line 111 wording still benefits from precision:
- If `RELEASE_NOTES_EN.md:111` still says "P1 process gap" without specifying which P-tag in `weekly_progress_report_2026-05-18.md`, replace with explicit cite (e.g., `P1-2` per weekly report line 318 = "v0.6 retroactive ralplan").
- If already precise (audit script discovers it has been fixed), skip this sub-item; document in commit footer.

**Bulk P-tag-add work to weekly_progress_report DROPPED.** Already closed per Critic §B.3 verification (36 P-tag matches at HEAD).

**Files.**
- `scripts/audit_ptags.py` (NEW).
- `docs/process/ptag_audit.json` + `docs/process/ptag_audit.md` (NEW; generated by first run of audit script, committed as snapshot).
- `docs/release/v0.6.0/RELEASE_NOTES_EN.md:111` (conditional precision pass per (b)).
- `CONTRIBUTING.md` (if missing, create section): "P-tag audit snapshot — `docs/process/ptag_audit.json` must be reviewed in any PR that touches `weekly_progress_report_*.md` or adds new P-tags. Snapshot diff should be empty OR explained in PR description (per Architect AN-1)."

**Test.** Run `python3 scripts/audit_ptags.py` once during v0.7 cycle; commit the output snapshots. The audit-only run does NOT block on orphan counts (we accept the current orphan baseline as v0.7 snapshot). Regression detection is via snapshot diff in future PRs.

**Acceptance.**
- `scripts/audit_ptags.py` exists, runs to completion, produces both `docs/process/ptag_audit.json` and `.md` snapshots.
- Snapshots committed as v0.7 baseline.
- `CONTRIBUTING.md` has the snapshot-diff invariant (per AN-1).
- `RELEASE_NOTES_EN.md:111` precision pass landed OR explicit "already precise" note in commit footer.

### Item #8 — Critic LOW + carry: `runtime_demote_strikes_` saturation + B.1 belt-and-suspenders + open-questions cleanup _(unchanged spec; cleanup of duplicated old Item #7 above per iter-2 rewrite)_

**Problem.** Critic §D LOW-2 — `runtime_demote_strikes_.fetch_add(1, ...)` at `BinauralMonitor.cpp:476` has no saturation guard. In a pathological scenario (audio thread accumulating strikes for hours without ever resetting because the demote latch CAS lost a race), the counter could theoretically wrap. Open questions in `open-questions.md` need a v0.7 entry block.

**Fix.**

(a) **Saturation guard.** Cap `runtime_demote_strikes_.fetch_add` with a soft ceiling:

```cpp
const int current = runtime_demote_strikes_.load(std::memory_order_acquire);
if (current >= kRuntimeDemoteStrikesSaturationCeiling) {
    // Already saturated; don't bump further. The demote latch should have
    // fired by now; this branch is unreachable in correct runs.
    return;
}
strikes = runtime_demote_strikes_.fetch_add(1, std::memory_order_acq_rel) + 1;
```

With `kRuntimeDemoteStrikesSaturationCeiling = 1000` (≫ any reasonable `effective_strikes`).

(b) **B.1 belt-and-suspenders.** Critic §B.1 noted: P1-4 kill-switch `clear ↔ isRuntimeDemoted` race is theoretical (production code path never calls `clearForTest`). v0.7 adds a static_assert or runtime check that `clearRuntimeDemoteForTest()` is only callable from test builds (gate behind `#ifdef SPATIAL_ENGINE_BUILD_TESTS`).

(c) **Open-questions cleanup.** `.omc/plans/open-questions.md` v0.5.1 V051-OQ1 (telemetry cadence 1 Hz) — CLOSED via D-S3 (event-driven, not 1 Hz). V051-OQ2 (Q5 commitment level) — CLOSED via v0.6 ship. New v0.7 block added with carry-forward open questions for v0.8.

**Files.**
- `core/src/output_backend/BinauralMonitor.cpp`: 5-line saturation guard.
- `core/src/output_backend/BinauralMonitor.h`: new constant `kRuntimeDemoteStrikesSaturationCeiling = 1000`. Test-only API gated behind `#ifdef`.
- `core/src/output_backend/CMakeLists.txt` (or top-level): define `SPATIAL_ENGINE_BUILD_TESTS=1` when building unit tests.
- `.omc/plans/open-questions.md`: CLOSE v0.5.1 OQs, add v0.7 block.

**Test.** New ctest scenario in `test_b2_runtime_underrun_auto_demote.cpp`: call `recordB2BlockTiming` with over-budget elapsed_ns 2000 times in a row without invoking initialize() (use a back-door that skips the demote latch via reflection — actually, just CAS-fail the demote latch in a controlled way). Assert `runtime_demote_strikes_.load()` ≤ saturation ceiling. (Note: with the demote latch firing at effective_strikes ≤ 30, saturation is genuinely unreachable in correct runs — the test exists to prove the guard *is* in place.)

**Acceptance.** Saturation ctest PASS; ifdef gate prevents `clearRuntimeDemoteForTest()` link errors in non-test builds (verified by attempting a `SPATIAL_ENGINE_BUILD_TESTS=0` build pulling `BinauralMonitor.h` — should fail to find the symbol). open-questions.md has v0.7 block.

---

## §3. Test deltas _(iter-2 ref: Critic §B.1 baseline corrected from 86 to 115; §B.2 pytest path corrected; §A.4 AM-4 downgrade; §C.1 heartbeat bench; §C.2 concurrent reset; ordering requirement REMOVED for existing pytest)_

**Iter-2 baseline (verified 2026-05-18):**
- `build_off/`: `ctest -N | tail -1` → `Total Tests: 115`
- `build_vst3_on/`: `ctest -N | tail -1` → `Total Tests: 115` (same test set; VST3 layer adds different scope but baseline test count happens to match)
- **Pre-existing ctest baseline = 115/115 PASS**. (Iter-1 plan's "86" was stale; corrected per Critic §B.1.)
- pytest baseline = 47 (unchanged from v0.6).

| Test name | Status | Item | Notes |
|---|---|---|---|
| `b2_runtime_underrun_user_reset` | NEW | #1 D-S1 | Cooldown + accept + already-not-demoted paths. **8-atomic reset verification per AM-1** _(iter-3 §C.2 — was 6; +2 demote-moment snapshots)_. Zero-hysteresis regression gate (single over-budget call post-reset must NOT re-demote). Critic §D.7 rate-limit invariant. |
| `b2_runtime_underrun_user_reset_concurrent` | NEW | #1 D-S1 (Critic §C.2/§D.8) | std::thread + atomic-spin-fence: Scenario A (audio-thread strike-bump vs IO-thread reset race) + Scenario B (concurrent `initialize()` + OSC reset; cooldown atomic survives invariant). |
| `bench_heartbeat_drain_latency` | NEW | (Critic §C.1) | Captures baseline heartbeat drain wall-clock cost (50-sample median, persisted to `heartbeat_drain_baseline.txt`); fails on +50% regression. Guards D-S3 against hidden heartbeat overhead. |
| `b2_runtime_underrun_auto_demote` | REVISED | #2 D-S2 + #3 D-S3 + #8 saturation | 3 new block-size scenarios (32/128/1024 sample); D-S3 max-ratio snapshot assertion; #8 saturation-cap scenario. |
| `osc_outbound_multi_producer` | REVISED | #3 D-S3 + #4 D-S4 (AS-3) | Exercises new `sendReply(addr, types, int32_t, int32_t, float)` overload; ≥2 producer model preserved for AS-3 relacy compatibility. |
| `osc_security_peer_validation` | REVISED | #1 D-S1 | New `/sys/binaural_reset_demote` peer-bind path; unauthenticated peer REJECTED at OSC layer. |
| `test_osc_outbound_relacy` | NEW (CMake flag OFF default; ≥2 producers per AS-3) | #4 D-S4 | Synthetic verification of v0.6 #9 release-store under C++11 MM. Relacy 5-green soak before CI promotion per Critic §B.7 (NOT 1-green). |
| `tests/soak_harness/test_osc_warning_channel.py` _(corrected from `tests/test_osc_warning_channel.py` per Critic §B.2)_ | REVISED (pytest) | #3 D-S3 | Extends filter set to capture `/sys/binaural_diag` + `reset_demote_*` codes. **Existing `TestBothWarningCodesObserved` test left UNCHANGED — presence-only assertion, ordering NOT asserted per Critic §A.4 AM-4 downgrade.** |
| `tests/soak_harness/test_osc_warning_channel.py::test_binaural_diag_emitted_on_demote` | NEW (pytest) | #3 D-S3 | End-to-end demote → diag emission via VST3 fixture (NOT standalone harness). Asserts presence + per-emission latency within `PER_EMISSION_LATENCY_BUDGET_MS`. Source-deterministic ordering (warning enqueued first) documented as Python comment only. |
| `tests/soak_harness/test_binaural_reset_demote_handler.py` | NEW (pytest) | #1 D-S1 | OSC inbound handler smoke test: send `/sys/binaural_reset_demote ,i 1`; assert `reset_demote_accepted` or `reset_demote_cooldown_active` arrives per state. |

**ctest count accounting (iter-2 corrected per Critic §B.1):**
- **Pre-existing baseline (verified)**: 115 tests in `build_off/` and `build_vst3_on/`.
- **Default build (relacy OFF) NEW tests**:
  1. `b2_runtime_underrun_user_reset`
  2. `b2_runtime_underrun_user_reset_concurrent`
  3. `bench_heartbeat_drain_latency`
  
  → Default build target = **115 + 3 = 118/118 PASS**.
  (Iter-1's "117" undercount: missed `b2_runtime_underrun_user_reset_concurrent` and `bench_heartbeat_drain_latency` — added in iter-2 per Critic §C.1/§C.2.)
- **Relacy ON optional build NEW tests**: +1 (`test_osc_outbound_relacy`) → **119/119 PASS**.
- **REVISED tests** (`b2_runtime_underrun_auto_demote`, `osc_outbound_multi_producer`, `osc_security_peer_validation`) gain internal scenarios but count is per registered test name → no count delta.

**Gate (NOT trivially-passable per Critic §B.1):** **default build = exactly 118/118 PASS** (115 pre-existing + 3 new). Any pre-existing test failure = ship-blocker; any new test failure = ship-blocker; ctest count regression (e.g., 117) = ship-blocker. Relacy ON build = 119/119 PASS when flag enabled.

**pytest count**: v0.6 **47 → v0.7 49** (+2: `test_binaural_diag_emitted_on_demote` + `test_binaural_reset_demote_handler`). Existing 47 pass-count gate preserved (no regression).

---

## §4. Verification target _(iter-2 ref: Critic §B.1 baseline corrected to 115; trivially-passable "≥87" wording removed)_

```bash
# OFF baseline build (v0.7 default)
$ cd build_off && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && cmake --build . -j$(nproc)
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 118
Total Test time (real) =   ≤ 4.5 sec  (regression vs v0.6.x baseline ~3.55 s acceptable up to +25% for 3 new tests + heartbeat bench)

# VST3 ON build (alternate)
$ cd build_vst3_on && cmake .. -DSPATIAL_ENGINE_NO_JUCE=OFF -DSPATIAL_ENGINE_BUILD_VST3=ON && cmake --build . -j$(nproc)
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 118  # same NEW tests apply

$ python3 -m pytest tests/ -x --tb=short
============================== 49 passed in <12s ==============================

# Relacy build (ad-hoc, signal-only in v0.7 cycle; required after 5-green + ARM64 promotion per Critic §C.4)
$ cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON
$ cmake --build . -j$(nproc) --target test_osc_outbound_relacy
$ ./test_osc_outbound_relacy
[Relacy] iterations: 1024, producers: 2, fails: 0   # AS-3: ≥2 producers verified

# GHA cross-platform.yml after 5-green soak + promotion (per §7.1 Scenario C):
linux-arm64: PASS (REQUIRED — promoted P0)
macos-arm64: PASS or FAIL (signal-only; named owner `paiiek`; promotion deferred to v0.8 per AS-6)

# GHA relacy.yml (Critic §C.4 P1 promotion order — AFTER ARM64):
relacy: PASS (signal-only in v0.7 ship; required after own 5-green + ARM64 stable)
```

**Gate (NOT trivially-passable):** **exactly 118/118 PASS** in default OFF baseline build (115 pre-existing + 3 NEW per §3). Any test failure (pre-existing OR new) OR any ctest count regression OR pytest count below 49 = ship-blocker. The "≥87" trivially-passable wording from iter-1 is REMOVED — deleting tests would pass an "≥N" gate vacuously. This iter-2 gate enumerates expected NEW tests explicitly per §3.

OFF baseline byte/symbol gate (`bytes.sha256` + symbol delta) MUST MATCH pre-v0.7 baseline EXCEPT for items #1, #2, #3, #6, #8 source files (intentional change). Relacy headers + test binary (when flag ON) are isolated by CMake guard and do NOT contribute to OFF baseline. `third_party/relacy/` directory is excluded from OFF symbol gate via existing `.gitignore`-style baseline filter (verify at first commit; add if missing).

---

## §5. Out-of-scope (explicit)

| Item | Reason |
|---|---|
| **MUSHRA-style B1 vs B2 vs reference subjective evaluation** | Requires external panel recruitment (≥10 listeners), HRTF-fidelity reference signals, and statistical analysis. Cannot be automated. Carry to v0.8 or v1.x roadmap as a research-collaboration line-item; gated on Band-1 handoff workflow (ADR 0016) maturing. |
| **Live legal counsel engagement for ADR 0016** | The team has no legal-domain expert. Item #6 documents WHERE legal review is needed and prepares the artifacts (template, limitations section), but the act of engaging a GPL-aware attorney is a user/project-lead decision and budget item. |
| **macOS hardware-handoff DAW verification (Logic / Cubase / Reaper-macOS)** | `macos-arm64-verify.md` + `daw-handson-log-macos.md` checklists are user-driven hands-on items. v0.7 keeps the CI matrix's macOS leg signal-only with named owner; promotion is user-triggered after the checklist is run. |
| **D-L1 RT-safe SPSC audio→IO ring** | Architectural redesign (~300 LOC) gated on "future high-frequency outbound channel" — current outbound load doesn't justify it. **Trigger for v0.8 revival (Critic §B.8):** any audio-thread `sendReply` call site is introduced (not just high cadence; ANY audio-thread producer). The v0.5.1 multi-producer hotfix at `OSCBackend.cpp:436-492` was driven by producer-side contention safety (control + heartbeat + future audio thread), not cadence. If a future feature pushes the audio thread to start producing OSC, the current ring's "drop-on-full" behaviour becomes a *correctness* issue, not just a latency issue. Carry to v0.8 ONLY if v0.7 telemetry (D-S3) reveals a high-frequency need OR an audio-thread producer is added. |
| **D-L2 EWMA-smoothed demote precision** | Requires telemetry data (D-S3 ships in v0.7 but no real-world data exists at v0.7 ship). Carry to v0.8 after ≥1 cycle of soak data. |
| **D-L3 automatic re-arm of demoted state** | Requires confidence in B1→B2 reverse transition (currently exercised only at prepareToPlay). v0.7 D-S1 (user-hatch) covers the UX gap; auto-re-arm is a v0.8+ refinement. |
| **D-L4 `[plan-shadow]` commit-tag policy enforcement** | Process-policy enhancement; not blocking any v0.7 user-facing capability. Carry to v0.8 if another post-hoc cycle happens. |
| **MAX_ORDER bump (3 → 5 or 7) for HOA decoders** | Separate ADR-scale work item (M2HOA-Q10 in open-questions). Not blocked on v0.7; not unblocked by v0.7. Belongs to a dedicated "HOA order parity" plan. |

---

## §6. ADR / documentation changes (companion) _(iter-2 ref: Critic §B.3/§B.4/§B.6/§C.1/§C.4)_

| File | Change | Item |
|---|---|---|
| `docs/adr/0016-external-distribution-policy.md` | **NARROW pass per Critic §B.4** — extend existing `## Limitations & legal review status` section (line 291-297) with 3-numbered-list trigger events + named owner `paiiek` (per Architect AS-6). **DO NOT touch Appendix A** (already complete at line 233-269). | #6 |
| `docs/adr/0017-v0.7-rt-safety-followthrough.md` (NEW, retrospective) | **§B (AM-3 rejection rationale).** Pin: "v0.7 D-S3 uses dedicated `sendReplyImplIIF`. Flag-extension option (a) rejected — extending `sendReplyImpl` to 9 params with 3 flags violates v0.6 #8 'thin forwarder' invariant." Plus full Decision/Drivers/Alternatives/Consequences/Follow-ups per §7.3. | #3 + retrospective |
| `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` (NEW) | Operational handoff template with "법률 자문 없이 작성" disclaimer; recipient + repo URL + tag SHA + acknowledgement + audit-log entry fields. | #6 |
| `docs/ipc_schema.md` | New `### Outbound — Diagnostic Telemetry (v0.7+)` for `/sys/binaural_diag ,iif`. New inbound entry for `/sys/binaural_reset_demote ,i`. Warning-string table extended with `reset_demote_cooldown_active`, `reset_demote_accepted`. **Document process-lifetime cooldown semantics per AS-5**. | #1, #3 |
| `docs/manual_kr/CH7_BINAURAL.md` | §7.5.4 extended with the user-reset OSC verb usage + process-lifetime cooldown paragraph (AS-5). §7.5.5 NEW: diagnostic telemetry channel for power users + **AS-2 slow-degradation limitation surface** (`/sys/binaural_diag` is event-driven, NOT periodic). | #1, #3 |
| `docs/release/v0.7.0/RELEASE_NOTES_EN.md` (NEW) | Full release notes following v0.6.0 template. Includes §"Cross-platform gating" subsection + AS-2 slow-degradation known limitation. | All items |
| `docs/release/v0.7.0/cross-platform-gating.md` (NEW) | 5-green promotion criteria; current state of each CI leg; branch-protection click-path; **AS-4 rollback procedure (3 options)**; Critic §C.4 promotion order (ARM64 P0 → Relacy P1). | #5 |
| `docs/release/v0.7.0/relacy-promotion-gate.md` (NEW) | 5-green soak procedure for Relacy CI; cppmem-fallback path per §7.1 Scenario B; **Critic §B.6 license-audit checklist** (upstream URL + commit SHA + verbatim LICENSE + transitive-dep grep recipe). | #4 |
| `docs/weekly_progress_report_2026-05-25.md` | v0.7-cycle goals tracked. **No bulk P-tag rework** (Critic §B.3 — already closed at HEAD). Named owner `paiiek` for macOS-14 leg promotion decision. | #5, #7 |
| `CHANGELOG.md` | §0.7.0 entry summarizing items #1-#8 + iter-2 amendment summary (16 fixes applied). | All |
| `docs/process/ptag_audit.{json,md}` (NEW) | Audit snapshot of P-tag graph state at v0.7 ship (one-shot run per Critic §B.3 re-scoping; no bulk repair). | #7 |
| `.omc/plans/open-questions.md` | CLOSE V051-OQ1, V051-OQ2; v0.7 block already added in iter-1; iter-2 adds V07-Q9 (relacy CI promotion order confirmation) + V07-Q10 (per-band reviewer designation deferred). | #8 |
| `CONTRIBUTING.md` | **AN-1 invariant**: snapshot diff of `docs/process/ptag_audit.json` must be empty OR explained in PR description for any PR touching `weekly_progress_report_*.md` or adding new P-tags. | #7 |
| `core/tests/core_unit/heartbeat_drain_baseline.txt` (NEW) | Baseline median wall-clock cost for `bench_heartbeat_drain_latency` regression gate per Critic §C.1. | (Critic §C.1) |
| `third_party/relacy/{LICENSE, UPSTREAM_PIN.txt}` (NEW) | Verbatim BSD-2-Clause + upstream URL/SHA pin per Critic §B.6 license audit. | #4 |

---

## §7. Deliberate-mode additions

### §7.1 Pre-mortem (5 failure scenarios — 3 original + 2 NEW per Critic §D.7/§D.8)

#### Scenario A — D-S3 telemetry causes audio-thread overhead regression _(iter-2: AM-2 mitigation-ordering INVERTED)_

**How it fails:** The `runtime_demote_max_ratio_x1000_` atomic update in `recordB2BlockTiming` adds per-block atomic traffic. On a heavily-contended core (e.g., HT-sharing the audio core with a logical-sibling running ARM-emulated x86 in a container), the atomic operation could become measurably slow and itself burn budget — causing exactly the over-budget condition we're trying to measure (self-fulfilling demote prophecy redux per v0.6 retro §A.2).

**Detection:** Existing `rt_alloc_probe` covers allocations but NOT atomic-contention cost. New benchmark `recordB2BlockTiming` wall-clock cost across 10⁶ calls — target ≤ 50 ns/call median on x86_64. Heartbeat drain benchmark `bench_heartbeat_drain_latency` (NEW per Critic §C.1) catches any heartbeat-side regression at +50% threshold.

**Mitigation — iter-2 INVERTED order per AM-2:** v0.7 ships **relaxed-load-then-store as the DEFAULT** (NOT CAS). The pattern is `int cur = atomic.load(relaxed); if (new_ratio > cur) atomic.store(new_ratio, relaxed);` (Critic §A.2 correction over Architect AM-2's `store(max(...))` wording — the latter is two non-atomic ops). Audio thread is sole producer per `SpatialEngine::audioBlock`, so no real contention. CAS promotion is the ESCALATION, deferred to v0.8 only if telemetry-informed precision need surfaces. **Iter-1's CAS-first / relaxed-as-fallback ordering was the WRONG mitigation order** — Architect §C.1 + Critic §A.2 both caught this.

#### Scenario B — Relacy false positive blocks the CI promotion (Item #4) _(unchanged)_

**How it fails:** Relacy's exhaustive interleaving exploration might flag a SAFE pattern in our outbound ring as unsafe (false positive), e.g., because relacy doesn't model `std::atomic_thread_fence` perfectly in some edge case. The CI job fails repeatedly; we either lose confidence in relacy as a verifier or hide real signal under "known noise."

**Detection:** Test-the-test step in #4 acceptance: confirm relacy PASSES on the v0.6-#9 fix AND FAILS on the pre-fix simulated revert. If the PASS direction is flaky after **5-green soak attempt** (Critic §B.7 unified standard, NOT 1-green), relacy is unreliable for our model and we drop it.

**Mitigation if it fires:** Replace relacy with `cppmem` (a smaller, more conservative formal C++11 MM verifier) OR with a hand-rolled `std::thread`-based test that uses memory barriers + sleep injection. Document the rejection rationale in #4 acceptance log + ADR 0017 §B follow-up.

#### Scenario C — Linux ARM64 GHA runner has subtly different glibc/kernel that exposes a real v0.6 #9 race _(iter-2: AS-4 rollback procedure ADDED)_

**How it fails:** Linux ARM64 hosts (ubuntu-24.04-arm) have ARMv8 with `LDAR`/`STLR` acquire/release ops, which the C++ release-store maps to. But if the GHA runner has an unusually-configured kernel that disables those ops (using `LDR`+`DMB` fallback) AND there's a real race we didn't see on x86_64, the now-required `linux-arm64` leg fails on the v0.7 promotion push and main is blocked.

**Detection:** Pre-promotion soak — run `cross-platform.yml` on a branch for **5 consecutive triggers (all green)** before flipping the `continue-on-error` off. Document this gate in `cross-platform-gating.md` (which also doc-surfaces Critic §C.4 P0/P1 ordering: ARM64 promotes FIRST, Relacy SECOND).

**Mitigation if it fires (AS-4 rollback — chicken-and-egg deadlock prevention):**
1. **Option-A (preferred)**: admin-bypass merge of revert PR. Repo admin uses GitHub's "Merge without waiting for requirements" (Settings → Branches → bypass list). Audit log entry mandatory.
2. **Option-B (fallback)**: temporary requirement removal. Repo admin removes `linux-arm64` from required status checks in branch protection, merges the revert PR, re-adds `linux-arm64`. Gap window < 15 minutes; document on weekly progress report.
3. **Re-promotion after fix**: requires fresh 5-green soak on the fix branch before re-flipping `continue-on-error`. Same gate as initial promotion.

Without AS-4, a single bad ARM64-only commit leaves main unmergeable indefinitely.

#### Scenario G NEW — D-S1 user-reset OSC handler exposes DOS via reset spam _(iter-2: Critic §D.7)_

**How it fails:** The new `/sys/binaural_reset_demote ,i 1` inbound verb is reachable from any peer that has completed the v0.5.1 handshake. Without rate limit, N>16 rapid rejected resets could back up the 16-slot outbound SPSC ring (`OSCBackend.h:254 kOutboundRingCap=16`). Cooldown-rejected resets emit `reset_demote_cooldown_active` warning — if a malicious or buggy peer spams `/sys/binaural_reset_demote ,i 1` at high rate, each call enqueues a warning packet → ring saturates → legitimate warnings (`ambivs_demoted_runtime`) drop via `outbound_drops_` counter.

**Detection:** New ctest `b2_runtime_underrun_user_reset` scenario 6 asserts the rate-limit invariant (second cooldown rejection does NOT re-arm warning latch). pytest `test_binaural_reset_demote_handler.py` smokes the wire-level handler with deliberate spam pattern.

**Mitigation (BAKED INTO Item #1 spec, not deferred):** Rate-limit `reset_demote_cooldown_active` warning to **at most once per cooldown window** via new `bool reset_cooldown_warning_emitted_` atomic in `BinauralMonitor`. Set to true on first CooldownActive emission; cleared on next Accept. Plus: OSC peer-validation already enforced by v0.5.1 P1 (`osc_security_peer_validation` ctest #115 covers — unauthenticated peers REJECTED at OSC layer before reaching the binaural handler). ADR 0016 Band-0 internal-lab assumption applies — no untrusted peers in current cycle.

**Severity:** Medium-Low. Pre-mortem documents the risk for future Band-1 readiness.

#### Scenario H NEW — Concurrent `initialize()` + OSC reset race _(iter-2: Critic §D.8)_

**How it fails:** `prepareToPlay`-driven `initialize()` (control thread; audio thread stopped per VST3 SDK contract) concurrent with OSC IO-thread `resetRuntimeDemoteFromUser`. Both paths converge on the same **8 atomics** _(iter-3 §C.2 — was 6 in iter-2; +2 D-S3 demote-moment snapshots)_ from Item #1 AM-1, but `runtime_demote_last_reset_ns_` (cooldown atomic) explicitly survives `initialize()` per AS-5 process-lifetime design. If `initialize()` accidentally clears the cooldown atomic (e.g., constructor-style reset mid-method), a malicious or buggy peer could trigger `prepareToPlay` (e.g., DAW sample-rate cycle) to escape the cooldown.

**Detection:** New ctest `b2_runtime_underrun_user_reset_concurrent` Scenario B (Critic §C.2 + §D.8): drive monitor to demote first; Thread A calls `initialize(cfg)`; Thread B calls `resetRuntimeDemoteFromUser(now_ns=now)`; repeat 100 iterations. **Invariant:** `runtime_demote_last_reset_ns_` ALWAYS retains the most recent `now_ns` value (cooldown survives initialize); other 7 atomics converge to fresh state _(iter-3 §C.2 — was 5; +2 D-S3 demote-moment snapshots)_ in both orderings.

**Mitigation (BAKED INTO Item #1 spec):** `initialize()` explicitly does NOT touch `runtime_demote_last_reset_ns_` or `reset_cooldown_warning_emitted_` (per AS-5 + AM-1 atomic enumeration). Verified by inspecting `BinauralMonitor::initialize()` post-implementation — must NOT include either atomic in its reset list.

**Severity:** Low — both paths converge to "demote cleared, cooldown unchanged or fresh." But explicit testing is required because the design intent is invisible without the scenario test.

### §7.2 Expanded test plan _(iter-2 ref: Critic §C.1 heartbeat bench, §C.2 concurrent reset)_

| Layer | New / Revised tests | Coverage target |
|---|---|---|
| **Unit (ctest)** | `b2_runtime_underrun_user_reset` (NEW), **`b2_runtime_underrun_user_reset_concurrent` (NEW per Critic §C.2/§D.8 — concurrent strike-bump vs reset; concurrent initialize + reset)**, `b2_runtime_underrun_auto_demote` (REVISED with 3 block-size scenarios + saturation cap + max-ratio snapshot), `osc_outbound_multi_producer` (REVISED with new `,iif` overload; AS-3 ≥2 producer), `osc_security_peer_validation` (REVISED with new inbound peer-bind; unauthenticated peer REJECTED), `test_osc_outbound_relacy` (NEW, CMake-flagged, AS-3 ≥2 producer model). | Strike-counter state machine + cooldown + rate-limit + saturation + telemetry snapshot + new OSC overload + concurrent reset race coverage + C++11 MM safety. |
| **Integration (ctest)** | `b2_runtime_underrun_engine_integration` (REVISED to assert D-S3 telemetry snapshot propagation through `SpatialEngine` forwarder), existing `test_vst3_dispatch_rt_safety` extended to assert no audio-thread allocation from D-S3 read-then-store update (per AM-2 relaxed pattern). | End-to-end engine path + RT-safety of the new audio-thread code. |
| **End-to-end (pytest)** | `test_binaural_diag_emitted_on_demote` (NEW, `tests/soak_harness/`), `test_binaural_reset_demote_handler` (NEW, `tests/soak_harness/`), `tests/soak_harness/test_osc_warning_channel.py` (REVISED filter set — captures `/sys/binaural_diag` + `reset_demote_*`; **NO ordering assertion added per AM-4 downgrade**). Critic §C.1 add-on: `test_osc_warning_channel.py` `PER_EMISSION_LATENCY_BUDGET_MS` regression check — run before/after on same CI runner, assert delta < 10%. | OSC wire-level demote + reset + diag round trip; pytest budget non-regression. |
| **Observability / benchmarks** | `bench_heartbeat_drain_latency` (NEW per Critic §C.1, **HARD gate at +50% vs baseline `heartbeat_drain_baseline.txt`**); `recordB2BlockTiming` ≤ 50 ns/call median benchmark (NEW, soft gate — warn-but-don't-fail; informs AM-2 CAS-vs-relaxed re-evaluation in v0.8); `soak_reports/binaural_diag_YYYYMMDD.jsonl` auto-created on demote events during soak runs; `docs/process/ptag_audit.json` snapshot diff (per AN-1 invariant). | First production-path telemetry surface; P-tag chain integrity diff-visible; D-S3 audio-thread overhead bounded; heartbeat drain overhead bounded. |
| **CI / cross-platform** | `cross-platform.yml` `linux-arm64` **REQUIRED gate after 5-green soak (P0 per Critic §C.4)**; `macos-arm64` signal-only with named owner `paiiek`; `relacy.yml` (NEW) signal-only in v0.7, **promoted P1 after ARM64 + own 5-green soak**. AS-4 rollback procedure documented in `cross-platform-gating.md` for chicken-and-egg deadlock prevention. | ARM64 regression surface promoted from "advisory" to "blocking"; relacy as upstream early-warning lane (NOT a gate ahead of hardware). |

### §7.3 ADR (Architectural Decision Record) for v0.7 ship — iter-2 amended

Drafted as `docs/adr/0017-v0.7-rt-safety-followthrough.md` (NEW) — authored at the END of v0.7 cycle as a retrospective:

- **Decision:** Ship items #1-#8 as v0.7 (RT-safety follow-through + telemetry + cross-platform gating + GPL legal-surface honesty + 16 iter-2 amendments).
- **Drivers:** (1) v0.6 retro binding recommendations (Architect §D-S1..D-S5, Critic §D-MAJOR). (2) Cumulative honesty-debt repayment per Principle 1. (3) Telemetry before tuning per Principle 4. (4) Iter-2 Critic-driven precision pass (4 spec-precision + 3 Must-fix amendments + 4 Should-have additions + 2 pre-mortem additions + 1 obsolete removal).
- **Alternatives considered:** A-1 (carry-only), A-2 (carry + 1 long-tail), A-3 (docs-only) — see §0.3. B-1 / B-2 / B-3 for telemetry shape. C-1 / C-2 / C-3 for GPL legal surface.
- **§B (AM-3 rejection rationale — iter-2 binding).** **`/sys/binaural_diag ,iif` uses dedicated `sendReplyImplIIF`, NOT a flag-extension of v0.6 #8 `sendReplyImpl`.** Option (a) — extending `sendReplyImpl(addr, types, s, have_f, f, have_i, i)` to 9 parameters with 3 boolean flags — was rejected because it forces the 3 existing v0.6 #8 overloads to spell extra `false, 0` defaults at every call site, violating the "thin forwarder" invariant. Mixed-type packets (`,iif` here, possibly `,sif/,iiif` in future) are a different SHAPE of packet than the simple typetags v0.6 #8 unified — forcing them through the same impl creates more accidental coupling than it removes. **Future maintainers: do NOT fuse `sendReplyImplIIF` with `sendReplyImpl`.** If a second mixed-type outbound emerges, add another dedicated `sendReplyImpl<shape>`; consider a small helper struct refactor only once 3+ mixed-type impls exist. (Architect §D-M3 + Critic §A.3 + iter-2 AM-3 confirmation.)
- **Why chosen:** A-1 + B-1 + C-1+C-2 — see §0.3 selection rationale per decision.
- **Consequences (iter-2 corrected per Critic §B.1):** (a) v0.7 ctest count = **118/118 in default build** (115 baseline + 3 NEW: `b2_runtime_underrun_user_reset`, `b2_runtime_underrun_user_reset_concurrent`, `bench_heartbeat_drain_latency`); 119/119 in relacy ON build. (b) Linux ARM64 becomes a REQUIRED gate after 5-green soak (P0); CI failures block merge; AS-4 rollback procedure exists for deadlock prevention. Relacy P1 promotes AFTER ARM64. (c) New OSC outbound surface (`/sys/binaural_diag ,iif`) and inbound (`/sys/binaural_reset_demote ,i`); ipc_schema.md schema_version stays at 1 (additive). (d) ADR 0016 unchanged in Appendix A / Limitations (already complete at HEAD per Critic §B.4); narrow extension to trigger-events list + named owner `paiiek`. (e) Relacy header-only dev-dep vendored under `third_party/relacy/` with verbatim BSD-2-Clause LICENSE + UPSTREAM_PIN.txt + transitive-dep audit (Critic §B.6).
- **Follow-ups (v0.8 candidates):** D-L1/L2/L3 (telemetry-informed; D-L1 trigger per Critic §B.8 = any audio-thread `sendReply` producer); MAX_ORDER bump (separate ADR); MUSHRA evaluation (research-collaboration); live legal review (user/lead-driven); macOS-14 CI promotion (after hardware-handoff); B-3 (event-summary telemetry) re-evaluation after v0.7 D-S3 produces real data; CAS vs relaxed re-evaluation for D-S3 max-ratio update (AM-2 escalation conditional on observed precision need); per-band designated reviewer for ADR 0016 trigger events (V07-Q5 / AS-6 carry-forward); relacy CI promotion to required after own 5-green soak (Critic §C.4 P1 ordering).

---

## §8. Cycle execution checklist (for `/oh-my-claudecode:autopilot`) _(iter-2 amended)_

1. **Re-read iter-2 inputs before starting each item:** `.omc/plans/architect-r-v0.7.md` §D and `.omc/plans/critic-r-v0.7.md` §A/§B/§D and this plan's §-1 changelog. v0.6 retros (`architect-r-v0.6-retro.md`, `critic-r-v0.6-retro.md`) remain the upstream context but iter-2 amendments OVERRIDE iter-1 wording wherever they conflict.
2. **Per item: write test first** (where unit-testable), make it fail, implement, make it pass, commit with footer referencing v0.7 §item-number AND any iter-2 ref tag (e.g., `iter-2 AM-1`, `iter-2 Critic §B.6 license audit`).
3. **Order of execution** (per Critic §C.4 + dependency analysis):
   - Item #2 (D-S2, no new atomics, simple) FIRST.
   - Item #1 (D-S1, defines 3 new atomics that #3 depends on) SECOND.
   - Item #3 (D-S3, depends on #1 atomics + adds 2 more reset-in-#1) THIRD.
   - Item #8 (saturation + ifdef, low-risk cleanup) FOURTH.
   - Item #6 (ADR doc) + Item #7 (ptag audit script) — docs-only, parallel to code work.
   - Item #4 (Relacy, license audit + ≥2 producer model) — independent; can run in parallel with #6/#7.
   - Item #5 (cross-platform.yml ARM64 promotion) — LAST in v0.7 cycle, AFTER 5-green soak completes; Relacy promotion is v0.7.x or v0.8 per Critic §C.4 P0/P1 ordering.
4. **After items #1, #2, #3, #8:** run full ctest (`cd build_off && ctest --output-on-failure`) + pytest locally. Verify count = **118/118 in default build** (per §3 iter-2 gate). After #4 also relacy `cd build_relacy && ./test_osc_outbound_relacy`. After #5 also `yamllint .github/workflows/*.yml`.
5. **After ALL items:** `git diff` of OFF baseline `bytes.sha256` empty except for intentional code-changing files (§2 Files blocks). `third_party/relacy/` excluded from OFF baseline (verify at first commit).
6. **License audit acceptance for #4** (Critic §B.6 BLOCKING): verify `third_party/relacy/LICENSE` is verbatim upstream BSD-2-Clause; `third_party/relacy/UPSTREAM_PIN.txt` has URL + SHA + date; `grep -rE '#include <(boost|tbb|absl)/' third_party/relacy/` returns 0 matches.
7. **Compose** `docs/release/v0.7.0/RELEASE_NOTES_EN.md` + `CHANGELOG.md` §0.7.0 entry. Include AS-2 known-limitation (slow-degradation not detected by event-driven `/sys/binaural_diag`).
8. **Tag v0.7.0** only on user explicit request (per `.claude/CLAUDE.md` policy).
9. **Post-cycle ralplan ratification (iter-3 if needed):** spawn `/oh-my-claudecode:ralplan` Architect + Critic review pass on this iter-2 plan. If Critic returns ITERATE again, repeat amendment cycle. ACCEPT triggers autopilot.

---

## §9. Iter-2 verification matrix (for Critic ratification pass)

Reference table for next Critic pass to verify all 16 items are addressed:

| # | Critic finding | Section addressed | Verification cite in this plan |
|---|---|---|---|
| 1 | Critic §B.1 ctest baseline wrong | §-1.1, §3, §4, §7.3 (e) | "115/115 baseline + 3 NEW = 118" explicit; verified via `ctest -N` |
| 2 | Critic §B.2 pytest path wrong | §-1.2, §2 Item #3 Files block, §3 table, §7.2 | Path `tests/soak_harness/test_osc_warning_channel.py` in all 4 places |
| 3 | Critic §B.3 Item #7 over-claim | §-1.3, §2 Item #7 rewritten | "Bulk P-tag-add work DROPPED" + verified 36 P-tag matches |
| 4 | Critic §B.4 Item #6 over-claim | §-1.4, §2 Item #6 rewritten | "DO NOT touch Appendix A" + line-cite to existing ADR 233-269 |
| 5 | AM-1 D-S1 strike reset explicit | §-1.5, §2 Item #1 fix steps, §3 ctest #1 step 4 | 8-atomic enumeration _(iter-3 §C.2 extension — was 6; +2 D-S3 snapshots)_; zero-hysteresis regression gate test |
| 6 | AM-2 D-S3 relaxed default | §-1.6, §2 Item #3 (a), §7.1 Scenario A | Read-then-store pattern code block; mitigation order INVERTED |
| 7 | AM-3 sendReply option (b) | §-1.7, §2 Item #3 (c), §7.3 §B | `sendReplyImplIIF` dedicated + inline comment block + ADR §B note |
| 8 | AM-4 DOWNGRADE | §-1.8, §3 table notes | Existing pytest unchanged; new test documents ordering as comment only |
| 9 | Critic §B.6 relacy license | §-1.9, §2 Item #4 (a) | Upstream URL + SHA + LICENSE verbatim + transitive-dep grep |
| 10 | Critic §B.7 5-green | §-1.10, §2 Item #4 (c), §7.1 Scenario B | "5 consecutive green" everywhere; "1-green" removed |
| 11 | Critic §C.1 heartbeat bench | §-1.11, §3 table row, §7.2 Observability | `bench_heartbeat_drain_latency` NEW HARD gate +50% |
| 12 | Critic §C.2 concurrent reset | §-1.12, §2 Item #1 Test (2), §3 table row, §7.2 Unit row | `b2_runtime_underrun_user_reset_concurrent` NEW |
| 13 | Critic §C.4 promotion order | §-1.13, §2 Item #4 (c), §2 Item #5, §7.1 Scenario C | ARM64 P0 → Relacy P1 explicit in 3 places |
| 14 | Critic §D.7 DOS via reset spam | §-1.14, §2 Item #1 fix step 2, §7.1 Scenario G NEW | `reset_cooldown_warning_emitted_` flag + rate-limit invariant test |
| 15 | Critic §D.8 init+reset race | §-1.15, §2 Item #1 Test (2) Scenario B, §7.1 Scenario H NEW | Concurrent ctest scenario; cooldown survives initialize invariant |
| 16 | Critic §B.5 AS-1 obsolete | §-1.16, §2 Item #2 Fix paragraph | "AS-1 DROPPED" + verified line 461 guard already in production |

Architect AS-2/AS-3/AS-5/AS-6 also incorporated: AS-2 (§2 Item #3 Files + §7.2), AS-3 (§2 Item #4 (b)), AS-5 (§2 Item #1 Files + Item #6), AS-6 (§2 Item #5 (b) + Item #6). AN-1/AN-2/AN-3 also incorporated: AN-1 (§6 CONTRIBUTING.md row), AN-2 (§2 Item #2 comment rewrite), AN-3 (§2 Item #4 vendor-over-submodule).
