# Architect R1 Review — `spatial-engine-v0.md` (R1)

- **Reviewer**: Architect (oh-my-claudecode:architect, DELIBERATE mode)
- **Date**: 2026-04-28
- **Plan reviewed**: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.md` (878 lines, R1)
- **Spec source**: `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine-v2.md` (v2.1, ambiguity 6.0%)
- **Verdict**: **REVISE-FIRST-WITH-NOTES** (4 must-address items below; otherwise architecturally sound)

---

## Summary

The plan is architecturally serious. It honors every R12/R13/R7-R11 lock from spec v2.1, the principles are real (not motherhood), the ADRs carry antithesis-strength reasoning (ADR 0003 IPC is exemplary), and the pre-mortem trio is plausible-and-mitigated rather than ceremonial. Latency budget decomposition has been performed (per critic-r2 from the v1 archive) and the term-by-term arithmetic checks out within rounding. The chosen architectural forks (two-process JUCE + PySide6, OSC/UDP only, 16-line Hadamard FDN, polymorphic algorithm dispatch) are defensible, and where they're contested (ADRs 0001/0003/0004/0005), the rejected alternatives are real and documented as future migration triggers, not strawmen.

That said, four issues warrant Planner revision before Critic. The most consequential is an **arithmetic defect in the P11 numerical performance gate** that, if shipped uncorrected, makes the Stability acceptance criterion ambiguous. The others are: an **un-asterisked BinauralMonitor latency mismatch** with spec acceptance #11; a **PREEMPT_RT decision relegated to a fallback footnote** despite the latency table showing the budget barely fits without it on commodity kernel; and a **kernel/distro pinning gap** for Digigram ALP-Dante on Linux (the plan names Linux 6.x generic without confirming Digigram's certified driver matrix).

The single most consequential antithesis worth steelmanning: **ADR 0005 (per-object algorithm dispatch).** The plan picked D1 (polymorphic v-table). I steelman D3 (bucket-by-algorithm) below — not to overturn D1 for v0, but to argue the plan should pre-bake D3-friendly invariants now, not defer the falsifier-driven migration to v1+.

---

## 1. Strongest Antithesis (Steelman D3 against ADR 0005's D1 choice)

**Target chosen**: ADR 0005 — per-object algorithm dispatch (lines 624–633). Plan picked D1 (polymorphic `RenderingAlgorithm*` virtual dispatch per object per block); the antithesis is D3 (branch-per-block: partition objects into per-algorithm buckets, then process each bucket with a homogeneous loop).

### Steelman for D3

**Argument 1 — V-table cost is *not* the relevant ordinate; SOA-vs-AOS is.** ADR 0005 dismisses D2/D3 because the v-table call (~few ns × 8 objects) is invisible against FDN + LPF + delay. This framing is wrong: the cost the bucket form actually saves is **not** the indirect-call latency — it's the loss of SIMD vectorization and the cache thrashing from interleaving three distinct algorithm scratch layouts in the same audio-thread call sequence. With D1, processing 8 objects of mixed algorithms means: load VBAP scratch → run VBAP → dirty cache → load WFS scratch → run WFS → dirty cache → repeat. The CPU's vector pipeline never gets a chance to amortize. D3 (bucket VBAP[0..k], then WFS[k..m], then DBAP[m..n]) keeps each algorithm's hot scratch in L1/L2 and lets the compiler vectorize the inner loop across objects of the same algorithm. At 8 objects this is small; at the spec's "8+" wording (and at the ~200 KB of FDN delay state already pressuring L2), it can be measurable.

**Argument 2 — The P11 falsifier comes too late.** ADR 0005's falsifier ("if v-table dispatch + virtual-call overhead is >5% of audio-thread CPU at P11, file `eval-d3-bucket-dispatch` issue and re-evaluate at v1+") is structurally weak: by P11 the entire DSP module set has been written against the D1 ownership model (each algorithm owns its scratch; engine sees only `RenderingAlgorithm*`). Migrating to D3 at v1+ requires hoisting scratch ownership back into the engine and unwinding the polymorphic dispatch — exactly the "engine-side surgery" Principle 4 is supposed to prevent. The cost of doing D3-friendly work in v0 (SoA-laid-out per-algorithm scratch arrays sized at `prepareToPlay`) is small; the cost of un-doing D1 at v1+ is large.

**Argument 3 — D1 contradicts the plan's own RT-isolation discipline at the algorithm-swap boundary.** Spec v2.1 explicitly allows runtime algorithm change (`/obj/{id}/algorithm` command, plan line 439). With D1, runtime swap means swapping `RenderingAlgorithm*` on an Object — but the new algorithm's pre-allocated scratch must already exist, which means **all three algorithm impls' scratch is allocated for every Object up to MAX_OBJECTS=16, regardless of which algorithm is currently selected**. This is ~3× over-allocation in the worst case. With D3, the per-algorithm bucket scratch is sized once at `prepareToPlay` and shared across objects; an algorithm swap is just moving an object's index from one bucket to another — strictly cheaper.

### Concession

D1 wins on three real dimensions: (a) it is the simplest C++ idiom and the lowest cognitive load for collaborator review; (b) the P3 unit tests are easier to write per-algorithm in isolation; (c) the v-table cost truly is invisible at 8 objects on modern x86 with branch prediction. **For v0 acceptance pass**, D1 is sufficient.

### Recommended synthesis (Architect's actual recommendation)

**Hybrid: D1 dispatch surface, D3-friendly invariants.** Keep the polymorphic `RenderingAlgorithm*` per-Object surface for v0 (preserves Principle 4 and ergonomics), but at `prepareToPlay`:
1. Have each `RenderingAlgorithm` impl allocate its scratch as a flat SoA array sized to `MAX_OBJECTS`, indexed by object slot. This is the layout D3 needs anyway.
2. Provide an `engine->getObjectsByAlgorithm(Algorithm)` helper that returns spans (no allocation) — used by debug tools in v0 and by D3 dispatch in v1+.
3. Write the audio-thread loop as `for (auto& obj : objects) obj.algorithm->processBlock(...)` (D1) but document explicitly in ADR 0005 that the migration to `for (auto algo : algorithms) for (auto& obj : objects_by_algo[algo]) algo.processBlock(...)` (D3) is an internal change to `SpatialEngine::renderBlock` only — no ABI change, no header change to `RenderingAlgorithm`.

This costs ~30 lines in v0 and converts the v1+ migration from "rewrite ownership model" to "swap one for-loop." It strictly dominates pure D1 on the v0→v1 invariant principle (Principle 5) without sacrificing v0 ergonomics.

**Recommendation**: Planner should add this as an explicit subsection in ADR 0005's "Follow-ups" — not as a TODO but as a concrete v0 deliverable in P3 (the SoA scratch + `getObjectsByAlgorithm` helper).

---

## 2. Real Tradeoff Tension

**Tension**: **Principle 1 (RT-safe audio thread)** vs. **spec-required runtime algorithm swap** (`/obj/{id}/algorithm`, plan line 439; spec ontology "Object.algorithm: enum(VBAP|WFS|DBAP)" with per-object selectability).

**Where it lives**: The plan's audio-thread isolation rules (lines 186–192) say "no `juce::ScopedLock`, no `std::mutex`. Crossings are SPSC FIFOs only." The Command FIFO carries `Command::ObjectUpdate`-style records that mutate Object state. Algorithm change is one such command. But changing `Object.algorithm` from VBAP to WFS at sample T means: (a) the new algorithm's scratch must be initialized (already pre-allocated, fine), (b) the old algorithm's tail (WFS has per-speaker propagation delay state; VBAP doesn't) must either be flushed (click) or crossfaded (requires running both algorithms for the crossfade window — ~2× CPU spike on that object for K ms).

**Plan's current resolution**: The plan addresses position interpolation via `GainRamp` (per-sample linear ramp on `Command::ObjectUpdate`, line 193 + Risk #5) but **does not address algorithm-change crossfade explicitly**. The integration test at line 806 ("Algorithm runtime change ... assert speaker gain pattern transitions cleanly without click") names the requirement but the design doesn't show how it's achieved.

**Architect verdict**: PARTIAL. The spec doesn't *require* zero-click algorithm swap (acceptance #2 says "per-object selectable", #8 says zipper-free for distance/position changes — algorithm change is in a gray zone). But the Risk register doesn't list "algorithm-swap click" and the test asserts it without specifying the mechanism. Two viable resolutions, Planner picks one:

- **R1** (cheap): Algorithm swap requires a one-block silent gap on that object (gain ramp to 0 → swap pointer → gain ramp back from 0). User-visible artifact: ~2.7 ms dip on that object. Document in `docs/architecture.md` as known v0 behavior. Honors Principle 1 trivially.
- **R2** (correct): Run both algorithms in parallel for K = 256 samples (~5 ms) on the affected object during the swap; crossfade their outputs. Pre-allocated scratch for both is already there (D1 over-allocation). Costs ~2× CPU on that object for K samples — well within the per-block budget headroom.

Recommend **R2** documented in ADR 0005 with the K=256 sample window pinned in `core/src/core/Constants.h`. Either way, the plan must call this out — silent click on algorithm swap is a P12 perceptual finding nobody wants.

---

## 3. Synthesis (when possible)

The D1↔D3 synthesis is in §1 above (D1 dispatch surface + D3-friendly invariants). The Principle-1↔algorithm-swap synthesis is in §2 (R2 crossfade with pre-allocated parallel-run scratch). Both are concrete, both add ≤50 lines to v0, both prevent v1+ engine-core surgery.

---

## 4. Principle-by-Principle Audit

Plan declares 5 principles (lines 17–22). Verdicts:

| # | Principle | Verdict | Evidence / Gap |
|---|-----------|---------|----------------|
| 1 | RT-safe audio thread (alloc-free, lock-free, syscall-free, log-free) | **HONORED** with one PARTIAL spot | Lines 186–193 codify the discipline; `RT_ASSERT_NO_ALLOC` in P1 (line 354, 378); `juce::ScopedNoDenormals` (line 192, ADR 0004); `TraceRing` for log; pre-allocation at `prepareToPlay` (line 187). **PARTIAL spot**: algorithm runtime swap (§2 above) — current test asserts click-free without specifying the mechanism. |
| 2 | 12 acceptance criteria as inviolable design pressure | **HONORED** | Acceptance Criteria Mapping table (lines 641–656) exhaustively maps spec criteria → phases. Note: spec actually has 14 acceptance bullets (counted in my read), and the plan's table indexes 1–14 — consistent. Numerical performance gate (#11), latency gate (#12), positional accuracy (#13), stability (#14) each have dedicated harnesses (P11/P10/P9/P11). |
| 3 | Coordinate frames explicit and tested at every boundary | **HONORED — exemplary** | The §Coordinate Convention Module (lines 724–778) is best-in-class: 5 frames defined, helper signatures with hand-computed test points (NOT round-trip-only), explicit ANTI-TEST against the 2026-03-01 baseline_pan inversion bug from MEMORY.md. This is precisely the regression defense the historical bug pattern needs. |
| 4 | `OutputBackend` / `RenderingAlgorithm` / `ReverbEngine` / `ExternalControl` stable from day one | **HONORED** with one PARTIAL spot | Two `OutputBackend` impls in v0 (SpeakerArray P3+P5, BinauralMonitor P9); three `RenderingAlgorithm` impls in P3; FDN + IRConvolutionStub in P7 satisfying the 4 hooks; OSCBackend with VST3Control as the abstract callable slot. **PARTIAL spot**: ADR 0005's D1 over-allocates per-algorithm scratch on every Object (§1 above) — preserves the abstraction but at a cost the synthesis avoids. |
| 5 | IPC schema is the v0→v1 invariant | **HONORED** | Single Command schema (lines 426–463); `schema_version` u16 in-band on every command + `/sys/protocol_version` handshake (ADR 0003); `proto/command_table.json` as language-agnostic source of truth with CI sync check (line 359). External controllers (TouchOSC, future text2traj OSC adapter) join the same schema. |

**Net**: **5 honored / 2 with PARTIAL spots / 0 violated.** No principle is contradicted by the plan body; two have specific narrow gaps the synthesis section addresses.

---

## 5. Pre-Mortem Audit

Three scenarios at plan lines 99–119.

### Scenario A — FDN denormal CPU spike at idle
- **Plausibility**: HIGH. FDN denormal cliff on x86 without FTZ/DAZ is a textbook hazard (Jot 1991 and every subsequent FDN paper warns about it).
- **Diagnostic signals**: Strong. Per-block-time histogram bimodality is the right signal; `audio_underrun_count` rising under silence is the smoking gun; `perf` time-in-`_mm_mul_ss` is the confirmation.
- **Mitigation traceability**: STRONG. ADR 0004 specifies (i) `juce::ScopedNoDenormals` on every callback (line 612), (ii) per-line ±1e-20 DC offset injection (line 612), (iii) P7 unit test (line 507) and P11 idle-with-tails soak (line 552) explicitly gate on this. Phase IDs are concrete.
- **Verdict**: STRONG. No revision needed.

### Scenario B — Layout/algorithm silent mismatch (WFS on irregular array)
- **Plausibility**: HIGH. WFS on irregular arrays really does produce non-zero output that sounds chaotic, not silent — the canonical "fails closed silently in the worst possible way" failure mode.
- **Diagnostic signals**: Strong. P9 rE/rV harness would show |rE| collapse <0.4 (line 112); P3.5 listener-blind shows direction confusion >50%.
- **Mitigation traceability**: STRONG. P3 ships `LayoutCompatibilityChecker::validate(layout, algorithm)` with the rules table at line 113 and config-load-time enforcement; CI compat harness with 6 known-bad + 6 known-good pairs (line 113, line 407, line 644). Phase ID concrete (P3 + `tests/compat_harness/`).
- **Verdict**: STRONG. **One small note**: the WFS aliasing rule in Open Question #4 (line 866) is presented with a dimensionally garbled inequality (`0.5 × 8000 = 4000 > c/2 = 171.5` mixes m·Hz vs m/s). Math intent is right (0.5 m spacing fails `c/(2·f_max)` = 21.4 mm at f_max=8 kHz by ~23×). Restate as `spacing × f_max ≤ c/2 → 0.5 × 8000 = 4000 m·Hz, c/2 = 171.5 m·Hz, ratio 23×` for clarity. Not load-bearing; cosmetic.

### Scenario C — OSC packet reordering / coalescer bug under fast drag
- **Plausibility**: MEDIUM. Loopback UDP reordering is rare but real under load; a coalescer dropping the *newest* instead of *oldest* is a real bug class.
- **Diagnostic signals**: Adequate. Sequence-number monotonicity check in soak; `osc_reordered_drops` counter — both observable. **Gap**: the scenario describes both a kernel reordering AND a coalescer-direction bug as failure modes; the mitigation (sequence-number drop) defends against reordering, but not against the coalescer-direction bug (UI-side; mitigated only by UI test). The plan's UI section (line 287) names `controllers/drag.py` with "120 Hz rate-limit, last-write-wins coalesce per object" but no unit test on coalescer correctness is listed.
- **Mitigation traceability**: PARTIAL. P5 reorder-defense unit test (line 472) covers the engine side; **the UI-side coalescer correctness test is missing from `ui/tests/`** (line 290 doesn't list one).
- **Verdict**: ADEQUATE. **Minor revision**: add UI-side test under `ui/tests/` that asserts the drag coalescer drops oldest, not newest, on per-object pending. ~10 lines of pytest. Tied to Pre-mortem C completeness.

---

## 6. Latency Budget Audit

Plan §Latency Budget (lines 700–720). Term-by-term audit:

### Arithmetic
- **Re-summed**: stages 1+2+3+4+5+6 lo bounds = 1+0+1+0+0+(1.33+1) = 4.33 ms. Hi bounds = 4+0.2+3+1.33+0.5+(1.33+3) = 13.36 ms.
- **Plan claims**: 4.4–13.0 ms (Linux 6.x generic + ALP-Dante) and 3.3–8.0 ms (HDA fallback).
- **Verdict**: Arithmetic checks out within rounding. ✅

### Hardware/kernel assumption credibility
- **JACK period 64 frames @ 48 kHz**: realistic on modern CPUs with PipeWire-JACK. Standard pro-audio configuration. ✅
- **Linux 6.x generic**: this is the **load-bearing assumption** and is **partially credible but under-pinned**. The plan asserts (line 702) "Linux 6.x generic kernel (NOT PREEMPT_RT for v0)." But:
  - **Digigram ALP-Dante's certified driver matrix is not pinned in the plan.** Digigram historically certifies specific kernel versions for their ALSA drivers; "Linux 6.x generic" is not a version. `docs/lab_setup.md` (P0 deliverable, line 370) is named but the plan doesn't pre-commit which Ubuntu LTS / kernel point release will be tested. **Risk**: at P6, Dante I/O fails on the actual chosen kernel, and the team discovers Digigram supports e.g. 5.15 LTS only.
  - **The PREEMPT_RT decision is a footnote, not a structural choice.** Stage 3 (control thread schedule jitter) is the dominant variable term (1–3 ms p99 commodity vs <0.5 ms PREEMPT_RT). The plan says PREEMPT_RT is "ruled out for v0 baseline; can be adopted post-P10 if measurements show >5 ms p99" (line 717). Architect read: this defers a decision that affects P0 (`docs/lab_setup.md`), P1 (audio device bringup), and P10 (latency harness target) until *after* P10 — which is exactly the late-failure pattern DELIBERATE mode is supposed to prevent.
- **`block_size × 0.7` exit criterion arithmetic error**: Line 552 (P11 exit criteria) states "p99 per-block time < `block_size × 0.7` (= 358 µs at 64 frames @ 48 kHz)." This is **wrong arithmetic**: `(64/48000) × 0.7 = 933 µs`, not 358 µs. (358 µs ≈ block_size × 0.27.) **This is a falsifiability defect in the plan body** — must-address. The spec (line 116) says "99-percentile per-block processing time < block_size × 0.7" — the correct value is 933 µs.

### Fallback plausibility
- **PREEMPT_RT fallback**: real and effective; this is the standard pro-audio Linux escape hatch. Credible.
- **Smaller buffer (32 frames)**: doubles xrun risk; real but only viable on PREEMPT_RT. Credible characterization.
- **Relax to <10 ms p99 with explicit asterisk**: honest. The plan says "this plan does NOT silently relax" (line 718) — good.
- **BinauralMonitor latency relaxation (Open Question #3)**: plan correctly identifies that KEMAR partitioned convolution at 64-frame partitions adds ≥1.33 ms partition latency on top of stage 4. Plan's read (line 864) is that BinauralMonitor's budget can be relaxed because the spec's #11 latency gate is "input → Dante output" (the SpeakerArray path). **Architect verdict**: this read is *defensible but un-asterisked*. The spec's acceptance #11 doesn't textually distinguish output backends, and BinauralMonitor is a v0 acceptance criterion (#6, two simultaneous OutputBackends) — so a reader could legitimately argue BinauralMonitor must hit <5 ms too. **Must-address**: either (a) Planner includes BinauralMonitor in the latency target with a credible budget showing it fits, or (b) Planner gets explicit acknowledgment in the plan body (not just an Open Question) that BinauralMonitor has a relaxed target (e.g., <15 ms p99) with the reasoning captured in `docs/latency_budget.md` and explicitly called out in the v0 sign-off acceptance doc — *not* deferred to Architect/Critic to decide.

---

## 7. Open Questions for Critic

These are 3 questions Critic is best positioned to evaluate (testability + risk framing, which is Critic's lane):

1. **Is the P12 perceptual sign-off sufficient as the *only* listening test, or does P3.5 listener-blind smoke (N=2) at P3 risk being skipped under schedule pressure?** The plan ties P3.5 to a concrete halt criterion ("Failure halts and revisits coords + algorithm gain tables", line 417) but N=2 is inherently weak; the test is more "check for sign-flip" than "check for spatial accuracy." Critic should evaluate whether this is the right test for the right hazard.

2. **Risk #2 (JUCE GPL → commercial license)** is rated HIGH severity / HIGH likelihood (line 842) but the mitigation is "v1+ kickoff item." Is "tracked at v1+ kickoff" a credible mitigation, or does this need a v0 cost estimate + procurement plan now (e.g., $130/mo × N developers × deployment count)? Critic's testability: can the Planner show what triggers the procurement and who owns it?

3. **The sequence-number reorder defense (Pre-mortem C) drops out-of-order packets silently and increments a counter.** Is silent drop the right policy, or should the engine emit `/sys/error` (or at least a `/sys/warning`) when `osc_reordered_drops` exceeds a threshold? Critic should evaluate from the operations / debuggability angle: a silent counter that nobody watches is no defense.

4. **Algorithm-swap crossfade (§2 Tradeoff Tension above)** — Critic should validate whether the integration test at line 806 ("transitions cleanly without click") is an effective test of *whatever mechanism* the Planner picks. A test that asserts "no click" without specifying the audio-domain criterion (e.g., per-sample max |Δgain| < threshold over the swap window) is a weak gate.

---

## 8. Verdict

**REVISE-FIRST-WITH-NOTES.** The plan is architecturally sound and most of the work is already done correctly. Four must-address items below are scoped to small, mechanical revisions that don't require structural rework. Once patched, the plan is ready for Critic.

### Must-address (4 items)

1. **Fix the `block_size × 0.7` arithmetic in P11 exit criteria.** Line 552 currently says "= 358 µs at 64 frames @ 48 kHz." Correct value is **933 µs** (= 1333 µs × 0.7). This is a falsifiability defect — uncorrected, the test passes/fails ambiguously. (Tied to: P11, Acceptance #11.)

2. **Make the BinauralMonitor latency target explicit in the plan body, not in Open Questions.** Either fold BinauralMonitor into the <5 ms target with a credible partitioned-convolution budget showing it fits (probable: 64-frame partitions on top of the SpeakerArray budget gives ~6.5 ms p99 — too high), OR commit in the plan body to a relaxed target (e.g., <15 ms p99) with the reasoning captured in `docs/latency_budget.md` and named explicitly in the v0 sign-off acceptance doc. Don't defer the decision. (Tied to: §Latency Budget, Acceptance #6 + #11, Open Question #3.)

3. **Pin the kernel + Ubuntu LTS + Digigram driver matrix in `docs/lab_setup.md` as a P0 deliverable, and lift the PREEMPT_RT decision out of the fallback footnote into a structural choice.** Currently P0 says "Linux 6.x generic" without a point version. Confirm Digigram ALP-Dante's certified driver/kernel combination (vendor docs) and pin it. Decide PREEMPT_RT yes/no at P0 based on the latency budget table rather than at P10 based on measurement — the budget table itself shows commodity kernel barely fits and the RT fallback adds bringup complexity better absorbed at P0 than P10. (Tied to: P0, P10, Risk #3, §Latency Budget.)

4. **Add ADR-level treatment of algorithm runtime swap (the §2 tension).** Either commit to the silent-gap (R1) or crossfade (R2) approach in ADR 0005 (preferred: R2 with K=256 samples and parallel-run scratch). Add `algorithm_swap_click` as Risk #13 with the chosen mitigation. Update the integration test at line 806 to assert the audio-domain criterion (per-sample max |Δgain| over the swap window), not just "without click." (Tied to: ADR 0005, Risk register, P3 integration test.)

### Recommended-but-not-blocking (2 items)

A. **Adopt the D1+D3-friendly synthesis in §1 above.** Add to ADR 0005 follow-ups: SoA-laid-out per-algorithm scratch arrays at `prepareToPlay`; `engine->getObjectsByAlgorithm(Algorithm)` helper. Converts the v1+ migration from "rewrite ownership model" to "swap one for-loop." ~30 lines in P3.

B. **Add a UI-side coalescer correctness test** (`ui/tests/test_drag_coalescer.py`): assert per-object coalescer drops oldest pending, not newest. Closes the Pre-mortem C mitigation gap on the UI side (~10 lines).

C. **Restate the WFS aliasing inequality in Open Question #4** with consistent units (`spacing × f_max [m·Hz] vs c/2 [m·Hz=m/s? no]` — actually express it as `spacing < c/(2·f_max)`, i.e., 0.5 m vs 21.4 mm at f_max=8 kHz). Cosmetic.

---

## References (file:line)

- Plan §RALPLAN-DR Summary, Principles 1–5: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.md:18-22`
- Plan §Pre-Mortem Scenarios A/B/C: `…/spatial-engine-v0.md:103-119`
- Plan §Architecture Overview, two-process model: `…/spatial-engine-v0.md:127-174`
- Plan §Audio-thread isolation rules: `…/spatial-engine-v0.md:186-193`
- Plan ADR 0001 (process model): `…/spatial-engine-v0.md:570-579`
- Plan ADR 0003 (IPC OSC/UDP) — exemplary antithesis structure: `…/spatial-engine-v0.md:593-603`
- Plan ADR 0004 (FDN topology) — denormal handling: `…/spatial-engine-v0.md:605-622`
- Plan ADR 0005 (algorithm dispatch) — D1 chosen, D3 falsifier: `…/spatial-engine-v0.md:624-633`
- Plan §Latency Budget table: `…/spatial-engine-v0.md:700-720`
- Plan §P11 exit criteria with arithmetic error: `…/spatial-engine-v0.md:552`
- Plan §Coordinate Convention Module — exemplary: `…/spatial-engine-v0.md:724-778`
- Plan §Risk Register: `…/spatial-engine-v0.md:838-852`
- Plan §Open Questions for Architect: `…/spatial-engine-v0.md:856-868`
- Spec v2.1 §Acceptance Criteria: `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine-v2.md:106-122`
- Spec v2.1 §Constraints/Output (algorithm + reverb): `…/deep-interview-spatial-engine-v2.md:33-47`
- Spec v2.1 §Stack (R12 JUCE + R13 OSC): `…/deep-interview-spatial-engine-v2.md:74-79`
- Archived critic-r2 latency budget call-out (now satisfied): `/home/seung/mmhoa/spatial_engine/.omc/plans/v1-archive/critic-r2-review.md:36-44`
- Archived critic-r2 position-interpolation gap (now satisfied): `…/v1-archive/critic-r2-review.md:97`

— end of Architect R1 review —
