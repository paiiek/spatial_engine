# spatial_engine v0.7 — Architect review (RALPLAN-DR deliberate)

| | |
|---|---|
| **Date** | 2026-05-18 |
| **Reviewer** | Architect (omc-architect; pre-implementation lane, not retroactive) |
| **Plan under review** | `.omc/plans/spatial-engine-v0.7.md` (Planner pass, 490 lines, 8 scope items) |
| **Mode** | **DELIBERATE** — RT-safety + cross-platform CI gating + new OSC verb + GPL legal surface |
| **Companion inputs** | `.omc/plans/architect-r-v0.6-retro.md` (own retro), `.omc/plans/critic-r-v0.6-retro.md`, `.omc/plans/spatial-engine-v0.6-stability.md`, `.omc/plans/open-questions.md` V07-Q1..Q8 |
| **Verdict preview** | **APPROVE WITH RECOMMENDATIONS** — three Must-fix, four Should-have. The plan is solid in shape but carries one silent semantic bug (Item #1 strike-counter reset), one verification-ordering inversion (Item #3 D-S3 benchmark-after-ship), and one encode-API ambiguity (Item #3 `encodeOscReply` extension) that all need locking before autopilot. |

---

## §0. Scope and method

I read:

- The full Planner artifact (`spatial-engine-v0.7.md` §0–§8) end-to-end.
- My own v0.6 retro (`architect-r-v0.6-retro.md` §A–§F) — this plan is the direct discharge of its §D Should-have list, so cross-checking the §D wording is load-bearing.
- The Critic v0.6 retro (§A–§D + Open Questions). The plan claims to address Critic D-MAJOR (ADR §6, P-tag) and D-MINOR (saturation, ifdef gate) — I verified citation accuracy against the Critic source.
- The v0.6 stability plan (post-hoc) for lineage: what the v0.6 ship actually delivered vs what got deferred.
- Open-questions V07-Q1..Q8 to ensure the plan's "Decision A/B/C" framing matches the question registry the Critic will work from.
- Spot-verified the production code claims the plan depends on:
  - `core/src/output_backend/BinauralMonitor.h:244-245,488-495` — strike constants and 3 atomics.
  - `core/src/output_backend/BinauralMonitor.cpp:47-49,457-509` — `initialize()` reset, `recordB2BlockTiming`, demote latch, test hooks.
  - `core/src/ipc/OSCBackend.h:119-122,276-284` — current 3 `sendReply` overload set + flag-based `sendReplyImpl` + `encodeOscReply` static.
  - `core/src/ipc/OSCBackend.cpp:436-492` — `sendReplyImpl` body and the three 3-line forwarders.
  - `.github/workflows/cross-platform.yml:42-53` — confirms both ARM legs currently `continue-on-error: true`.

I did **not** re-read every ctest. I did **not** dig into JUCE / VST3 SDK source. Those layers are not load-bearing for the architect verdict.

Method: per-item strawman/steelman where the plan's framing is non-obvious, plus a hostile pre-mortem extension. Citations are `file:line` where claims rely on actual code.

---

## §A. Strawman / Steelman analysis (selected items)

I cover the **5 items where the plan's framing is non-trivial**: #1 (D-S1), #2 (D-S2), #3 (D-S3), #4 (D-S4), #5 (D-S5). Items #6/#7/#8 are mostly mechanical doc/audit work — covered in §D only where I have a binding recommendation.

### §A.1 Item #1 — `/sys/binaural_reset_demote` user hatch + 60 s cooldown

**Strawman:** "The user can press a reset button when they hit one transient spike. 60 s cooldown stops ping-pong. The OSC handler runs on the IO thread so it can touch atomics safely. Implementation is ~30 LOC."

**Steelman:** This is the cleanest item in the plan because it inherits two already-proven patterns: (a) the inbound OSC dispatch path from existing `/sys/state` / `/sys/binaural_status` queries, and (b) the 3-atomic-store reset pattern that `initialize()` D-M1 already validates. The cooldown atomic is process-lifetime (per the plan's explicit "NOT reset by `initialize()`" decision, §2 Item #1) which correctly defends against the "reopen project to escape cooldown" gaming. The `ResetResult` enum (`Accepted | CooldownActive | NotDemoted`) gives the heartbeat drain three distinct OSC strings to emit — three observable states for the user without ambiguity. This is the only item in v0.7 where the new control-thread atomic (`runtime_demote_last_reset_ns_`) does NOT add audio-thread cost — it is touched only on user reset, never in the per-block hot path.

**Silent semantic bug in the plan.** The plan's §2 Item #1 fix description says the reset "performs the same 3-atomic-store reset as `initialize()` D-M1 pattern." Cross-check against `BinauralMonitor.cpp:47-49`: the 3 atomics are `runtime_demote_strikes_`, `runtime_demoted_`, `runtime_demote_warning_pending_`. **But the plan never specifies what state `runtime_demote_strikes_` is in at the moment of reset.** When the demote latch fires (line 488 CAS), `runtime_demote_strikes_` is at `effective_strikes` (~8 by default, 30 at 32-sample blocks). After demote, `recordB2BlockTiming` early-returns at line 464 (`isRuntimeDemoted` true), so strikes stay frozen at the demote-trigger value. If the user resets and the strike counter is **not** explicitly cleared, the next single over-budget block bumps strikes from 8 → 9 (already past threshold) and the demote latch fires immediately on the next CAS. The user gets zero hysteresis after reset. The plan's claim of "the same 3-atomic-store reset" implicitly fixes this **only** if `runtime_demote_strikes_.store(0)` is in the reset method — and the plan does not say so explicitly in §2 Item #1 (it says it for §2 Item #8's saturation guard, but that's a different concern). **This must be made explicit in the implementation spec.** See §D Must-fix #1.

**Cooldown duration (V07-Q2):** 60 s is defensible. The two failure modes are (a) too short → user re-glitches → ping-pong, (b) too long → user can't escape after fixing root cause. 60 s sits at the boundary where a typical user's "open Activity Monitor, kill the heavy app, come back" loop completes (~30–60 s). The plan's design lets the cooldown be one constant change in `BinauralMonitor.h` if it proves wrong post-ship. **Accept 60 s.**

### §A.2 Item #2 — Block-size-aware `kRuntimeDemoteStrikes`

**Strawman:** "Scale strikes with block size so the time-window invariant (20 ms) holds at 32-sample and 1024-sample blocks. One-line derivation: `max(8, ceil(0.020 / block_seconds))`."

**Steelman:** This is the cleanest correction of a v0.6 magic-number defect. The `max(8, ...)` floor is important and correctly motivated in the plan's test #3 (1024-sample case): at very large block sizes each block IS the deadline, so short hysteresis is fine. The derivation is observable per-call (no startup cache invalidation race), and the new effective_strikes is local to `recordB2BlockTiming` — no new state, no new atomic. **One concern:** the plan says "Constant `kRuntimeDemoteStrikes = 8` remains as the floor; new derived `effective_strikes` documented inline." But `BinauralMonitor.h:244` documents `kRuntimeDemoteStrikes` as **the** demote threshold. If the constant survives unchanged with new semantics (now a floor), the comment block must be rewritten or future maintainers will misread. **This is a docs-only finding; flag for the implementation.**

**Edge case the plan doesn't address:** What about block_size=0 or sample_rate=0? `recordB2BlockTiming` parameters come from `SpatialEngine::audioBlock`, which gets them from the host. Pre-roll / silent-bypass hosts may legitimately send `prepareToPlay(samplesPerBlock=0)`. The plan's formula divides by `block_seconds`. If `block_size=0`, the formula divides by zero. Existing `recordB2BlockTiming` at `BinauralMonitor.cpp:464` early-returns on `isRuntimeDemoted` but does **not** validate block_size/sample_rate. **Should-have:** add a guard `if (block_size <= 0 || sample_rate <= 0.f) return;` before the derivation. See §D Should-have #1.

### §A.3 Item #3 — `/sys/binaural_diag` telemetry + new `sendReply` overload

This is the **highest-risk item in v0.7** and warrants the longest analysis.

**Strawman:** "Capture max-ratio per strike window into an atomic. On demote latch, snapshot it. Heartbeat drain emits `/sys/binaural_diag ,iif`. New `sendReply(addr, types, i1, i2, f1)` overload. ~40 LOC."

**Steelman:** The decision B-1 selection (event-driven, not 1 Hz) is the right call for v0.7. Principle 4 ("telemetry before tuning") is honored by shipping the channel before any threshold adjustment. The privacy story (no PII, opt-in via soak harness file presence) aligns with ADR 0016 Band-0. Reusing `drainRuntimeDemotePending()` as the trigger is structurally sound — the warning and the diag are causally linked, both emitted exactly once per demote event.

**Three concerns the plan understates:**

**Concern 1 (verification ordering inversion).** The plan §7.1 Scenario A acknowledges the audio-thread CAS-cost risk and proposes a 50-ns/call benchmark. But the plan's §2 Item #3 ships the CAS *first* and only falls back to `store(max(...), relaxed)` if the benchmark fires. **This is backwards.** The benchmark is the gate for the design decision; it must happen *before* the CAS lands in code. The fallback design ("last-writer-wins, ~1 strike of precision loss") is so cheap and so well-understood that there is no architectural reason to default to CAS without a measurement showing the precision benefit is real. **Recommendation:** Reverse the default. Land the relaxed-store implementation first; promote to CAS *only* if a v0.8 telemetry analysis shows the precision matters. See §D Must-fix #2.

**Concern 2 (encode-API ambiguity).** Current `OSCBackend::sendReplyImpl` is `(addr, types, s, have_f, f, have_i, i)` — a 5-slot flag-based signature (`OSCBackend.h:284`). Adding `/sys/binaural_diag ,iif` requires either: **(a)** extending `sendReplyImpl` to `(addr, types, s, have_f, f, have_i, i, have_i2, i2)` — combinatorial, every overload pays the price, and the 3 existing overloads now pass two extra `false, 0` defaults; **(b)** a parallel dedicated `sendReplyImplIIF` for diagnostic-shape packets — duplicates ~30 LOC of socket / ring-publish boilerplate; **(c)** a fully variadic redesign — out of scope for v0.7. The plan's §A.3 says "small, targeted, no architecture impact" but does not pick between (a)/(b). My recommendation: **(b)** for v0.7 — the diag packet is the only `,iif`-shaped reply in the schema, so duplicating ~30 LOC of mostly-mechanical socket plumbing is the lowest-blast-radius option, and the v0.6 #8 unification stays clean for the original 3 overloads. If/when v0.8 adds a second mixed-type outbound, refactor toward a small helper struct then. See §D Must-fix #3.

**Concern 3 (event-driven vs slow-degradation pattern).** B-1 misses "ratio creeping up over hours" patterns explicitly (open-questions V07-Q1 acknowledges this). The plan defers B-3 (32-block pre-demote window summary) to v0.8. This is acceptable for v0.7 *if* the v0.7 release notes name "slow-degradation pattern detection" as a known v0.7 limitation. Without the explicit limitation surface, a future demoted-but-no-diag-spike user has no documented reason for the silence. See §D Should-have #2.

### §A.4 Item #4 — Relacy race-detector dev-dep

**Strawman:** "Vendor relacy. Add CMake flag default OFF. New ctest models the OSC ring's producer/consumer state machine. Pre-fix simulated revert FAILS under relacy → validates the test."

**Steelman:** The "test-the-test" step (pre-fix simulated revert FAILS) is exactly the right validation for a synthetic verifier; the plan's §2 Item #4 acceptance section captures this. Defaulting OFF means OFF baseline byte/symbol gates stay unchanged (verified by Architect §A.4 retro's adjacent-impact analysis), so the new dev-dep is invisible to production builds. The relacy.yml CI job as signal-only-initially mirrors the proven `cross-platform.yml` rollout pattern from v0.6.

**Concern:** The plan §0.3 Decision A rationale says relacy "models multi-producer + single-consumer ring." But OSCBackend's *real* concurrency story is multi-producer (control thread + future audio thread were #4 hard-walled out, but the IO drain thread also publishes) + single-consumer (`outboundDrainLoop` at `OSCBackend.cpp:511`). The relacy model has to match this exactly — if it models only single-producer the test passes vacuously. **Should-have:** The test description in §2 Item #4 says "models multi-producer ... ring" but does not say *how many* simulated producers. Lock the number at ≥2 producers (mirroring `test_osc_outbound_multi_producer`'s existing pattern) so the failure mode is actually reachable. See §D Should-have #3.

**Pre-mortem Scenario B (false positive) is well-handled** in §7.1. The fallback to cppmem is realistic — cppmem is more conservative and would be a defensible substitute.

### §A.5 Item #5 — cross-platform.yml Linux ARM64 promotion

**Strawman:** "Drop `continue-on-error: true` from Linux ARM64 leg only. macOS stays signal-only with named owner. Pre-promotion soak = 5 consecutive green triggers."

**Steelman:** The split-promotion strategy (ARM64 to required, macOS stays signal-only) is the right risk shape. ARM64 GHA runners have been at GA pricing for over a year; promotion delay would be pure pessimism. The macOS-14 leg's "named owner" gate addresses the v0.6 Critic's MEDIUM-2 concern (release validation hides the deferral) by giving it a named exit criterion. **The 5-green-trigger gate is reasonable** — 1 is too few (we've already had 1 green via the v0.6 cycle), 10 delays without proportionate confidence. Open-questions V07-Q4 accepts 5.

**Concern:** Pre-mortem Scenario C (ARM glibc/kernel exposes a real race) is well-modeled. But the **rollback procedure is underspecified.** If `linux-arm64` becomes a required check and then a real race surfaces, "revert the promotion (re-add `continue-on-error: true`)" means a workflow-file PR. If main is protected with the new required check, that revert PR itself must pass the failing required check — chicken-and-egg. **Must-fix-adjacent:** The `cross-platform-gating.md` doc must spell out the *unblock procedure* when the promoted gate goes red — typically "admin-bypass merge of revert PR" or "temporarily delete required-status entry, merge revert, re-add." See §D Should-have #4.

---

## §B. Decision points review

### §B.1 Decision A — Scope shape (A-1 / A-2 / A-3)

**Strawman A-1 (selected):** Carry-only. All five §D Should-haves + Critic MAJORs + saturation + open-questions cleanup.
**Strawman A-2:** A-1 + 1 long-tail (D-L1 RT-safe SPSC, or D-L2 EWMA, or D-L3 auto-re-arm).
**Strawman A-3:** Docs-only.

**My recommendation: APPROVE A-1.** The Planner's rationale is correct on its own terms but I add three architectural reasons:

1. **D-L1 (SPSC ring) cost-benefit is genuinely unfavorable in v0.7.** The current outbound channel max load is 1 binaural_status/sec + on-demand warnings + (new in v0.7) on-demote binaural_diag. Steady-state cadence is ≤2 Hz. The heartbeat-drain pattern services this at ≤100 ms latency. Adding a 300-LOC SPSC ring buys us nothing measurable until a new high-frequency outbound channel exists. v0.7 explicitly *creates* one such channel (binaural_diag) but its cadence is **event-driven on demote** — not high-frequency. Defer correctly to v0.8 if D-L4 head-tracking telemetry ever gets scheduled.
2. **D-L2 (EWMA) requires D-S3 telemetry data we won't have until v0.7 ships.** Ordering is forced.
3. **D-L3 (auto-re-arm) requires the B1→B2 reverse transition direction to be exercised first.** D-S1 user hatch covers the UX gap in the meantime and *also* gives D-L3 its prerequisite — a code path that performs the reverse transition out of a real demote. Sequencing D-S1 (v0.7) → D-L3 (v0.8) is the right path.

A-3 rejection rationale (Planner's): correct. The Should-haves came with explicit "binding for v0.7" framing in the retro; slipping invalidates the retro's recommendation framing.

### §B.2 Decision B — Telemetry channel shape (B-1 / B-2 / B-3)

**Strawman B-1 (selected):** Event-driven on demote latch. One OSC verb. Local log file.
**Strawman B-2:** Continuous 1 Hz.
**Strawman B-3:** Pre-demote-window summary (32-block min/max/p95).

**My recommendation: APPROVE B-1, but with the §A.3 Must-fix #2 (relaxed-store default) and §D Should-have #2 (slow-degradation limitation surface).**

B-2 rejection (Planner's): correct. 1 Hz × hours = log explosion + privacy / storage burden. The opt-in-default-off gate is a UX trap that defaults the population to zero data.

B-3 rejection (Planner's): "prematurely optimizes for a not-yet-measured failure pattern." I add: B-3's ring buffer of 32 doubles on the audio thread is a **second** per-block atomic write site (alongside the strike counter), doubling the per-block atomic-write cost. The plan's §7.1 Scenario A risk is already non-trivial for one atomic; B-3 would multiply it.

### §B.3 Decision C — ADR 0016 GPL §6 surface (C-1 / C-2 / C-3)

**Strawman C-1 (selected):** Appendix A obligation mapping + Limitations section.
**Strawman C-2 (selected):** C-1 + `docs/legal/BAND_1_HANDOFF_TEMPLATE.md`.
**Strawman C-3:** Block all Band-1 handoffs until legal review.

**My recommendation: APPROVE C-1+C-2 paired.** The Planner's rationale is correct: C-1 alone is operationally inert; C-2 gives the user a working artifact. C-3 is over-engineered for current Band-1 count = 0.

**One refinement.** The "trigger events" listed in the Limitations section (Item #6 (c)) need an **owner per trigger**, not just trigger names. Open-questions V07-Q5 catches this. My recommendation: name the project lead as default owner for all three triggers in v0.7, and surface the question "should each band have a designated reviewer?" as a v0.8 carry-forward open question. This is a doc-only addition to Item #6 (c).

---

## §C. Pre-mortem adequacy

The plan's §7.1 lists 3 scenarios. I evaluate each, then propose **2 additional scenarios** the plan misses.

### §C.1 Scenario A (D-S3 telemetry overhead regression) — adequate but mitigation-order wrong

Detection (50-ns/call benchmark) is concrete. Mitigation (CAS → relaxed-store) is correct as a *fallback*. As noted in §A.3 Concern 1, the plan ships CAS first; the mitigation order should reverse. The pre-mortem itself is well-modeled.

### §C.2 Scenario B (relacy false positive) — adequate

Detection (test-the-test step) is concrete. Mitigation (cppmem or hand-rolled `std::thread` test) is realistic. The "one cycle of unstable PASS direction" exit criterion is right-sized.

### §C.3 Scenario C (Linux ARM64 silent race) — adequate but rollback procedure missing

Detection (5-green soak) is concrete. Mitigation is sketched but the **rollback procedure when the promoted gate goes red** is not in the plan. See §A.5 / §D Should-have #4.

### §C.4 NEW Scenario D — ABI / state-format drift from D-S1 reset semantics

**How it fails:** D-S1 introduces a new in-process state machine (cooldown atomic). If a user opens a project saved during a cooldown window, then closes and reopens, the cooldown atomic resets to `INT64_MIN` on the new process — first reset always accepted. This is the *correct* behavior (cooldown is process-lifetime), but it's invisible to users. A user could repeatedly close-reopen-reset to bypass the cooldown. **Severity:** Low (defeats only the anti-glitch-loop protection, not safety). **Detection:** Document the process-lifetime cooldown explicitly in CH7 §7.5.4 + ipc_schema.md as the *documented* behavior so users don't perceive the close-reopen-reset as a bug. **Mitigation if it fires:** Persist `runtime_demote_last_reset_ns_` to the state v4 schema. **Recommendation:** No code change — explicit doc surface only. See §D Should-have #5.

### §C.5 NEW Scenario E — Audit script (Item #7) divergence between strict and lenient modes

**How it fails:** Item #7 (c) makes `scripts/audit_ptags.py --strict` an *optional* pre-commit hook. The plan §2 Item #7 acceptance says "if strict mode is too aggressive, the lenient pass exits 0 with ≤ N pre-existing orphans documented." But the lenient mode's "N pre-existing orphans" count isn't pinned anywhere — it's whatever the audit reports at the v0.7 ship snapshot. If a future PR adds 1 new orphan, lenient mode's `≤ N` doesn't detect it (because the count didn't grow past the docs threshold). **Severity:** Low — Item #7 acceptance is doc-only and the orphan graph is checked into `docs/process/ptag_audit.json` as a snapshot, so the *diff* of that snapshot is the real regression signal. **Recommendation:** Make the snapshot-diff-must-be-empty-or-explained the explicit invariant in `CONTRIBUTING.md` (Item #7 (c)). One-liner addition. See §D Nice-to-have #1.

### §C.6 NEW Scenario F (the dangerous one) — D-S2 + D-S3 interaction breaks existing soak harness expectations

**How it fails:** v0.6's `tests/test_osc_warning_channel.py` was built when `ambivs_demoted_runtime` was the *only* outbound on demote events. v0.7 adds `/sys/binaural_diag` to the *same* drain (Item #3 plan §2 wording: "heartbeat drain extended to emit `/sys/binaural_diag` immediately after `ambivs_demoted_runtime` on the same drain"). If the existing pytest's wait-for-message logic depends on packet count or strict ordering, the new diag packet arriving in the same drain pass may break message-count assertions OR may arrive before the warning packet (depending on which `sendReply` call returns first under contention). **Severity:** Medium — pytest regressions are catchable in CI but only on the v0.7 cycle's first push. **Detection:** Plan §3 already lists `test_osc_warning_channel.py` as REVISED. **Mitigation:** Item #3's pytest revision must explicitly document the expected packet ordering (warning *then* diag, same drain pass) and assert it. See §D Must-fix #4.

---

## §D. Binding recommendations

### §D.1 Must-fix (block autopilot until resolved)

| # | Item | Section | Effort | Why must-fix |
|---|---|---|---|---|
| **AM-1** | **Item #1 D-S1 spec must explicitly include `runtime_demote_strikes_.store(0)` in `resetRuntimeDemoteFromUser`.** Without it, the post-reset state has stale strikes ≈ `effective_strikes`, and the next single over-budget block re-demotes immediately (zero hysteresis). The plan says "same 3-atomic-store reset as `initialize()` D-M1 pattern" but the implementation spec in §2 Item #1 lists only the cooldown/rejection atomics, not the strike counter. Make it explicit: "reset clears `runtime_demote_strikes_=0`, `runtime_demoted_=false`, `runtime_demote_warning_pending_=false`, snapshots the new `runtime_demote_last_reset_ns_=now`." | §A.1 | S (1-line plan amendment, ~0 LOC change to the eventual implementation if the implementer reads §2 Item #1 carefully — but planning should not depend on implementer carefulness for correctness invariants) | Silent semantic bug; documented contract ("60s cooldown for sticky-demote recovery") fails on second over-budget block after reset. |
| **AM-2** | **Item #3 D-S3: invert default — land `store(max(...), relaxed)` first, promote to CAS only if a v0.8 telemetry analysis proves precision matters.** The plan currently ships CAS and falls back to relaxed only if a 50-ns/call benchmark fails. This is backwards: CAS is the higher-risk choice (self-fulfilling-demote-prophecy redux per Architect §A.2 retro), and the precision benefit is unmeasured. Relaxed-store last-writer-wins loses at most 1 strike's worth of ratio per demote event, which is below the precision needed for any v0.8 threshold-tuning decision. | §A.3, §C.1 | S (1-line plan amendment + 5-line implementation difference) | Mitigation-ordering inversion. Plan defaults to the riskier choice without measurement. |
| **AM-3** | **Item #3 D-S3: pick the `sendReply` encode path (option a / b / c) explicitly in the plan.** The plan says "small, targeted, no architecture impact" but does not pick. The decision affects (i) future `sendReply` overload extensibility, (ii) ~30 LOC of new socket-plumbing duplication risk if option (b), (iii) `sendReplyImpl` signature creep if option (a). My recommendation is option (b) — a dedicated `sendReplyImplIIF` for the diagnostic packet shape — because the existing 3 overloads stay clean and the only `,iif`-shaped packet is binaural_diag. Lock this in §2 Item #3 before autopilot. | §A.3 | S (plan-only decision; ~30 LOC implementation either way) | Encode-API ambiguity. Without a pick, the implementer chooses under time pressure. |
| **AM-4** | **Item #3 D-S3: pytest revision must lock packet ordering (warning *then* diag, same drain pass) as an explicit assertion.** Otherwise the existing `test_osc_warning_channel.py` may pass / fail non-deterministically depending on which `sendReply` call returns first. The plan §3 lists the test as REVISED but does not specify the ordering invariant. | §C.6 | S (1 docstring + 1 assertion line in pytest spec) | Adjacent regression risk on existing pytest. |

### §D.2 Should-have (lock in plan; can land as part of autopilot)

| # | Item | Section | Effort |
|---|---|---|---|
| **AS-1** | Item #2 D-S2: add `if (block_size <= 0 || sample_rate <= 0.f) return;` guard at the top of `recordB2BlockTiming` *before* the new derivation. Defends against pre-roll / silent-bypass hosts that send `prepareToPlay(samplesPerBlock=0)`. | §A.2 | S (1-line guard, 1 ctest scenario) |
| **AS-2** | Item #3 D-S3: surface "slow-degradation pattern (ratio creeping up over hours) is NOT detected by event-driven `/sys/binaural_diag`" as a v0.7 *known limitation* in `docs/release/v0.7.0/RELEASE_NOTES_EN.md` + CH7 §7.5.5. Open-questions V07-Q1 already names this; the limitation needs the user-facing surface, not just the open-questions log. | §A.3 Concern 3 | S (1 paragraph in release notes + 1 paragraph in CH7) |
| **AS-3** | Item #4 D-S4: pin the relacy test producer count at ≥2 (matching `test_osc_outbound_multi_producer`'s real concurrency model). Without this, a single-producer model may pass vacuously. | §A.4 | S (test design only; relacy test code spec gains 1 line) |
| **AS-4** | Item #5 D-S5: `cross-platform-gating.md` must include the *rollback procedure when the promoted gate goes red* (admin-bypass merge of revert PR, OR temporary delete-required-status-entry + merge revert + re-add). Without this, a real ARM64 race after promotion leaves the team in chicken-and-egg deadlock. | §A.5, §C.3 | S (1 doc section, ~20 lines) |
| **AS-5** | Item #1 D-S1: document the *process-lifetime* cooldown semantics explicitly in CH7 §7.5.4 + ipc_schema.md. A user who closes and reopens a project resets the cooldown atomic — this is correct behavior but invisible. Doc surface prevents user perception of "the cooldown timer is buggy on reopen." | §C.4 | S (1 paragraph each) |
| **AS-6** | Item #6 ADR 0016 Limitations section: name the project lead as default owner for all three trigger events (per V07-Q5). Surface "per-band designated reviewer" as a v0.8 carry-forward question. | §B.3 | S (doc addition only) |

### §D.3 Nice-to-have (defer to Critic or v0.7.x)

| # | Item | Section | Effort |
|---|---|---|---|
| **AN-1** | Item #7: `CONTRIBUTING.md` should make "snapshot-diff of `docs/process/ptag_audit.json` must be empty or explained in the PR description" the explicit invariant, not just lenient-mode `≤ N orphans` (which doesn't detect new-orphan regressions). | §C.5 | S |
| **AN-2** | Item #2 D-S2: rewrite the comment block above `kRuntimeDemoteStrikes` (BinauralMonitor.h:244) — current comment describes the constant as the demote threshold; v0.7 makes it the floor. Future maintainer trap if not rewritten. | §A.2 | S (doc-only) |
| **AN-3** | Item #4 D-S4: relacy vendoring vs submodule (V07-Q6) — plan default is vendor + commit hash. Defensible but locks the precedent for future verification tools. Architect-side recommendation: vendor for v0.7 (matches the "zero network dependency at clone time" property of every other dev-dep in the repo), but flag in the v0.7 ADR 0017 that v0.8+ may revisit if cppmem or other verifiers get added. | (V07-Q6) | S (ADR text only) |

---

## §E. Verdict

**APPROVE WITH RECOMMENDATIONS.**

The plan's overall shape is correct: A-1 + B-1 + C-1+C-2 is the right scope/telemetry/legal-surface mix for v0.7. The pre-mortem is non-trivial and the test-layer coverage in §7.2 is appropriate. The plan honestly self-discloses its limits (B-1 misses slow degradation; macOS-14 stays signal-only; relacy may false-positive). The Item #1 / #2 / #5 / #6 / #7 / #8 items are well-scoped and have a clear failure-mode → mitigation chain.

**Conditional on:** all four Must-fix items (AM-1..AM-4) being applied to the plan text **before autopilot starts**. AM-1 (D-S1 strike reset) is the highest-priority — it is a silent semantic bug that would make the user-hatch UX functionally broken (immediate re-demote on next over-budget block). AM-2 (D-S3 relaxed-default) is the second-highest — the plan currently ships the riskier-than-necessary default choice. AM-3 / AM-4 are spec-precision gaps that prevent the implementer from guessing.

The 6 Should-have items (AS-1..AS-6) can land as part of autopilot but should be locked in the plan text first so they're not silently dropped under time pressure.

The 3 Nice-to-have items are Critic-pass material.

---

## §F. Open questions for Critic

These are items where the Architect verdict is APPROVE but the Critic should independently scrutinize:

1. **AM-1 silent-bug analysis adequacy.** I claim the post-reset strikes-stale case is a real bug. Critic should verify against `BinauralMonitor.cpp:464-509` whether the early-return at line 464 actually freezes the strike counter at the demote-trigger value, OR whether some other code path I missed clears it. If the latter, AM-1 downgrades to Should-have.
2. **AM-3 sendReply encode-API choice (a/b/c).** I recommend option (b) — dedicated `sendReplyImplIIF`. Critic should consider whether option (a) (extend the existing flag-based signature) is in fact cleaner because it preserves "one impl, many overloads" as an invariant. The choice affects v0.8+ extensibility patterns.
3. **§C.6 Scenario F (pytest packet-ordering invariant).** I treat this as Must-fix. If the existing `test_osc_warning_channel.py` does not in fact assert packet ordering (only packet *presence*), the risk is lower and AM-4 downgrades to Should-have. Critic with pytest-source-reading access can confirm/refute.
