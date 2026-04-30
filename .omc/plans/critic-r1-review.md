# Critic R1 Review — `spatial-engine-v0.md` (R1)

- **Reviewer**: Critic (oh-my-claudecode:critic, DELIBERATE mode)
- **Date**: 2026-04-28
- **Plan reviewed**: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.md` (878 lines, R1)
- **Architect review consumed**: `/home/seung/mmhoa/spatial_engine/.omc/plans/architect-r1-review.md` (REVISE-FIRST-WITH-NOTES, 4 must-address items)
- **Spec source**: `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine-v2.md` (v2.1, 16 acceptance criteria)
- **Mode**: DELIBERATE (escalated from THOROUGH after surfacing the spec-criterion miscount in §1)
- **Verdict**: **ITERATE** (8 must-address items; nothing structural — fixable with mechanical edits + 2 small ADR additions; no spec criterion is unrouted)

---

## 0. Pre-commitment predictions

Before opening the plan, I predicted the most likely Critic-shaped defects for an audio-DSP RT plan with 16 spec acceptance criteria, 12 phases, and 5 ADRs:

1. **Numerical gates stated as principles but missing falsifiable thresholds** (e.g., "no clicks" without a sample-domain criterion).
2. **Self-referential accuracy verification** (rE/rV harness using the same VBAP code that's being tested, with no analytical reference baseline).
3. **Latency gate ambiguity** — multiple instrumentation points possible, plan picks one tacitly.
4. **Multi-hour stability gate without memory growth bound or "what counts as a leak" cutoff.**
5. **Pre-mortem mitigations gesture at observability counters that nobody is required to watch** (silent counters = no defense).

Result vs predictions:
- Prediction #1 → confirmed (algorithm-swap "without click" — Architect already flagged; I add a second instance below: P12 perceptual sign-off "mean perceived az err ≤ 1°" with no data-collection methodology specified).
- Prediction #2 → confirmed in part — accuracy harness uses the engine's actual `RenderingAlgorithm::processBlock` math; for VBAP this is reasonable (closed-form gain math), but for WFS/DBAP the harness is the only check and there is no analytical baseline (see §3 testability finding).
- Prediction #3 → confirmed — see Architect's must-address #2 (BinauralMonitor budget) plus my MA #5 below (instrumentation point not pinned).
- Prediction #4 → confirmed — Risk #9 sets RSS slope thresholds (good) but the soak harness exit criterion only says "no IPC heartbeat misses" with no "what is a leak" rule outside the slope (acceptable; not a finding).
- Prediction #5 → partially confirmed — `osc_reordered_drops` and `audio_underrun_count` are exposed but no plan deliverable mandates a soak-time alert threshold ("non-zero is yellow flag" is informal) — see MA #4.

Pre-commitment activated deliberate search and produced findings I would otherwise have missed (notably MA #1 spec-criterion miscount and MA #5 latency instrumentation pin).

---

## 1. Spec → Plan Acceptance Criterion Coverage

**The spec has 16 acceptance criteria, not 14.** Plan §RALPLAN-DR Principle 2 (line 19) says "12 acceptance criteria"; plan §Acceptance Criteria Mapping (line 637) and Architect review (line 80) both say 14. Verified by `grep -cE "^- \[ \]"` over spec lines 105–122: count = **16**. Spec criteria #15 (IPC layer documented) and #16 (3U code constraints documented) are present in the spec but missing from the plan's enumerated mapping table — they appear only as a single "Cross-cutting" sentence below the table (line 658), without a row of their own. This is a coverage-presentation defect (not a coverage hole — both ARE delivered by P4 and P11 respectively), but it makes the table fail its own job: trace every criterion to a phase + test in one place.

Coverage table (re-numbered against actual spec source order):

| # | Spec criterion (paraphrased) | Phase that delivers | Test that verifies | Coverage verdict |
|---|---|---|---|---|
| 1 | 4–8ch Cartesian layout config + Dante I/O routing | P2 + P5 + P6 | `tests/dante_loopback/` (P6); accuracy harness lab_4ch/8ch (P9) | **FULL** |
| 2 | VBAP + WFS + DBAP per-object selectable; LayoutCompatibilityChecker rejects bad pairs | P3 | `tests/compat_harness/` 6 bad + 6 good pairs (P3) | **FULL** |
| 3 | 8 simultaneous objects, full per-object DSP chain, no dropouts | P3 + P5 + P11 | P11 soak `8 obj × VBAP × full chain` 30 min Xrun=0 | **FULL** |
| 4 | FDN reverb + per-object send + distance-dependent reverb amount | P7 + P12 | P7 RT60 fit + send-curve sweep; P12 perceptual | **PARTIAL — see MA #2** (no falsifiable per-object send unit test gate) |
| 5 | ReverbEngine abstraction (4 hooks) + IRConvolution drop-in proves | P7 | `IRConvolutionStub` runs cleanly | **TEST-WEAK — see MA #3** (the stub returns silence; "drop-in requires no bus/send/routing changes" is asserted by the stub running, not by an end-to-end load-real-IR test even at v0 toy level) |
| 6 | OutputBackend with SpeakerArray + BinauralMonitor simultaneous | P3 + P5 + P9 | P9 ITD test on KEMAR; SpeakerArray verified by P5 8-obj capture | **PARTIAL** (latency budget for BinauralMonitor unspecified — Architect must-address #2; reinforced) |
| 7 | ExternalControl + OSCBackend + abstract Command schema; VST3Control slot callable | P4 | OSC roundtrip; handshake; reorder defense | **PARTIAL — see MA #6** ("VST3Control slot callable" claim has no test deliverable in v0; the abstract base existing != callable proven) |
| 8 | Fractional delay + smoothing → no zipper/click on rapid object motion | P3 | PropagationDelay sweep d=1→10m over 1s + GainRamp `\|Δgain\| < 1/MAX_BLOCK` | **TEST-WEAK** (`1/MAX_BLOCK` is the *trivial-floor* per-sample increment; it asserts the ramp executes but does not assert click-freeness in any spectral or perceptual sense — see §3 below) |
| 9 | GUI top-down/3D + drag 1–4 + matrix view + noise gen + inspectors | P8 | UI integration test (drag → /sys/state heartbeat) | **PARTIAL** (no UI-side automated test for the read-only matrix view's correctness vs `/sys/matrix` payload; Architect noted UI-side coalescer test missing — I extend the gap to matrix view) |
| 10 | Collaborator can clone-build-run-modify on second machine | P12 | `docs/onboarding_timing.md` ≤60 min on machine #2 | **FULL** |
| 11 | Numerical perf gate: 8 obj × VBAP × full chain; Xrun 0/30 min, mean CPU <50%, 99p block < block_size×0.7 | P11 | Soak harness baseline | **GATE-WEAK** (Architect MA #1 — `block_size × 0.7` arithmetic error stated as `358 µs`; correct = `933 µs`) |
| 12 | <5 ms end-to-end input → Dante output | P10 | `tests/latency_harness/` p99 <5 ms | **GATE-WEAK** (Architect MA #2 + my MA #5 — instrumentation point not pinned: "input event" = Qt mouse? OSC arrival? "output" = sample written to JACK port? sample physically left DAC? Different choices change p99 by ~3 ms) |
| 13 | Positional accuracy ≤ ±1° via numerical pan-law | P9 + P3.5 + P12 | rE/rV harness for lab_8ch CI gate | **PARTIAL — see MA #7** (lab_4ch is exempted from CI gate as "math-ceiling"; this is correct, but no per-direction *pre-computed analytical reference value* exists, so the harness asserts engine math vs engine math — not a true closed-form regression check) |
| 14 | Multi-hour uninterrupted operation, no leaks/stalls/underruns | P11 | 12 h soak; RSS slope <1 MB/h core / <5 MB/h UI | **FULL** |
| 15 | IPC layer documented (transport, schema, ownership) | P4 + P12 | `proto/ipc_schema.md`, `docs/ipc_schema.md` | **FULL but un-rowed in plan table** — see MA #1 |
| 16 | 3U-rack code constraints documented | P11 | `docs/3u_rack_constraints.md` | **FULL but un-rowed in plan table** — see MA #1 |

**Summary**: 0 MISSING (good). 6 PARTIAL/TEST-WEAK/GATE-WEAK. The plan's coverage is structurally correct but its table presentation under-counts and several gates are weak in measurability.

---

## 2. Numerical / Observable Gate Audit

| Acceptance | Gate as written | Verdict | Comment |
|---|---|---|---|
| #3 8+ objects, no dropouts | "8 obj × VBAP × full chain → Xrun 0 / 30 min, mean CPU < 50%, 99p block < block_size × 0.7" (P11 line 552, 653) | **GATED-CORRECTLY** for the Xrun + CPU terms; **GATE-WEAK** for the 99p term (the 358 µs arithmetic error voids the claim until corrected) | Reinforces Architect MA #1. Also: gate is specified at `8 obj × VBAP` only — what about 8 obj × WFS or 8 obj × mixed? Spec says "8+ simultaneous sound objects" silent on algorithm. Plan picks the lightest case for the gate. Acceptable interpretation but should be called out (see MA #2 below). |
| #11 (which IS #11, my numbering) | same as above | same | |
| #12 ±1° accuracy | "\|az_intended − az_realized\| ≤ 1° AND \|rE\| ≥ 0.7 AND \|rV\| ≥ 0.7" with rE/rV computed from `RenderingAlgorithm::processBlock` math (P9 line 528) | **GATE-WEAK** | The harness uses the same engine code it is verifying. There is no analytical / external-reference check. For VBAP the speaker-pair gain has a closed-form formula that should be a separate unit-test reference; for DBAP the rolloff law is parameterized and self-consistent; for WFS the Huygens analytic formula reference IS in the unit test (line 405) at sample level — so VBAP/DBAP have weaker reference checking than WFS. **Fix in MA #7.** |
| #13 <5 ms latency | "p99 < 5 ms on lab target" (P10 line 538); harness method = `inject /obj/{id}/pos jump from speaker A to speaker B; physical loopback cable from Dante out → Dante in; cross-correlate to measure onset delta` (P10 line 536) | **GATE-WEAK** | Instrumentation timestamp definition is vague. "From `/obj/{id}/pos` UI-side mouse event" (latency budget table) ≠ "inject `/obj/{id}/pos`" (P10 deliverable). What clock samples T0? UI Qt event timestamp? OSC packet send timestamp? Core OSCReceiver arrival? — these differ by 1–4 ms (latency budget stage 1). The harness must pin: T0 = call-site of `OSC.send_message()` in the harness, T1 = first cross-correlation peak in captured Dante input. **MA #5.** |
| #14 multi-hour stability | "12 h zero xruns, system-RSS slope <1 MB/h, UI-RSS slope <5 MB/h, no IPC heartbeat misses (>10 s gap)" (P11 line 552) | **GATED-CORRECTLY** | Falsifiable, measurable, monotonically checkable. Architect did not flag; I concur it's well-specified. The 12 h vs spec's "multi-hour" is a tightening, not a gap. |

---

## 3. Testability Pitfalls (specific findings)

### 3.1 "No click" tests assert ramp execution, not click-freeness
- **Where**: P3 (line 408) and Risk #5 (line 845) and unit test (line 795): assert `per-sample max |Δgain| < 1.0/MAX_BLOCK`.
- **Problem**: `1/MAX_BLOCK` (= `1/512` = 0.00195) is the *trivial floor* — any monotonic linear ramp from any value to any value over MAX_BLOCK samples satisfies this by construction (since |Δgain|_per_sample = |Δtotal| / MAX_BLOCK and Δtotal is bounded by gain headroom ≤ 1). The test passes even if the ramp is wrong (e.g., ramps in the wrong direction, or doesn't actually ramp because the SmoothedValue never starts). It's a *form check*, not a *click check*.
- **Better gate (MA #8)**: assert (a) the *first* per-sample step is ≤ `1/MAX_BLOCK` (catches "ramp didn't start"), AND (b) the cumulative gain at sample MAX_BLOCK matches the target gain within 1e-6 (catches "ramp ended early or in wrong direction"), AND (c) FFT of the rendered block contains no spectral component above the original signal's bandwidth + ε (proxy for click-as-impulse-residual). The third is the actual click test; the first two catch implementation regressions.

### 3.2 Algorithm-swap "transitions cleanly without click" (Architect §2)
- **Architect already flagged** that the integration test (line 806) asserts "without click" without specifying mechanism or measurable criterion. I **REINFORCE**: the same fix as 3.1 above — assert FFT of the swap-window block contains no spectral content above the input signal's pre-swap bandwidth + ε. Architect's recommendation to add an explicit per-sample max |Δgain| over the swap window is necessary but not sufficient (same trivial-floor problem). MA #4 of Architect's set covers the structural ADR fix; my MA #8 covers the test-criterion fix.

### 3.3 P3.5 listener-blind smoke gate is binary on N=2
- **Where**: P3.5 exit (line 417): "both listeners (N=2: a non-operator + observer) correctly localize all 4 directions independently."
- **Problem**: N=2 with a 4-direction forced-choice has chance ceiling 25%; correct on all 4 has p ≈ 0.0039 per listener under chance, joint = 1.5e-5. Statistically tight for *catching gross sign-flips* (this is what the test is for; Architect Open Question #1 questions whether it's the right test for the right hazard). **My read**: P3.5 is appropriately scoped as a sign-flip detector, not a perceptual-accuracy check. The gate is appropriately falsifiable for THAT purpose. **Architect Open Question #1 — I respond DISAGREE with concern**: the schedule-pressure-skip risk is real but the test is appropriately *cheap* (15 minutes) so making it a P3 hard gate (which it is, per "Failure halts and revisits coords + algorithm gain tables", line 417) is sufficient. **No change requested**, but flag in `docs/onboarding.md` that P3.5 is non-skippable.

### 3.4 P12 perceptual sign-off N=12 — measurable but methodology under-specified
- **Where**: P12 deliverable (line 560): "N=12 pre-registered listeners; mean perceived az err ≤ 1° on geometry-reachable grid; Friedman test for any condition contrasts; reuse vid2spatial v3 stimulus pipeline."
- **Problem**: "mean perceived az err ≤ 1°" is the *mean over directions and listeners* without specifying confidence interval or directionality. With N=12 and 36-direction grid (lab_8ch), test-retest reliability is uncertain. Friedman is mentioned but no specific contrast is named (which conditions? VBAP vs DBAP? lab_4ch vs lab_8ch? full chain vs minimal chain?). For acceptance #13's perceptual confirmation, a tighter spec is needed.
- **Fix (MA #6 included in MA #6 below)**: P12 deliverable should specify (a) the contrasts the Friedman test is *pre-registered* to evaluate (named in `tests/perceptual/listening_test_v0/preregistration.md` before data collection), (b) the per-direction CI threshold, not just mean (e.g., "mean ≤ 1° AND 95% per-direction CI upper bound ≤ 2°"), (c) reuse-protocol pointer to the vid2spatial v3 stimulus pipeline file.

### 3.5 Heartbeat miss "no >10 s gap" — gate is too lax
- **Where**: P11 exit (line 552): "no IPC heartbeat misses (>10 s gap)."
- **Problem**: Heartbeat is 30 Hz = 33 ms period. A 10 s gap = 300 dropped heartbeats — that's already a catastrophic IPC stall, not a leading indicator. As written the gate would happily pass with 9.9 s gaps repeated every minute (300 misses × 60 = 18,000 misses per hour, none counted). 
- **Verdict**: GATE-WEAK. **Fix (MA #4)**: tighten to "no >500 ms gap" (≈ 15 missed heartbeats; catches stalls early without false-positive on lossy-publish). 10 s makes sense as a *catastrophic-stall* alarm only.

---

## 4. Risk Register Quality

The plan has 12 risks (lines 838–852). Rating:

- **Domain-specificity**: STRONG. Risks 1, 5, 6, 8, 10 are spatial-audio / RT-DSP-specific; Risks 2, 3, 12 are spec-stack-specific (JUCE GPL, Digigram driver, JUCE 7→8 bump); Risks 4, 7, 11 are SDLC-flavored but appropriately scoped.
- **Mitigation concreteness**: STRONG. Every mitigation links to a phase ID, file path, or named test (e.g., Risk #1 → P4-a/P4-b boundary tests, `RT_ASSERT_NO_ALLOC` macro; Risk #6 → `LayoutCompatibilityChecker` + `tests/compat_harness/` 6+6 pairs).
- **Severity/likelihood calibration**: MOSTLY GOOD. Three calibration concerns:
  1. **Risk #2 (JUCE GPL → commercial license) HIGH/HIGH** — Architect already raised this in Open Question #2 as a Critic question. **My answer**: Severity HIGH is correct (legal exposure at v1+ deployment), Likelihood HIGH is correct (certain at v1+), but the **mitigation is aspirational** ("v1+ kickoff item explicitly tracked in `docs/onboarding.md`"). For a HIGH/HIGH risk, the mitigation should produce v0-deliverable artifacts — at minimum: (a) v0 deliverable = procurement-cost estimate based on N developers + deployment count, (b) named owner, (c) trigger condition (when the procurement decision must be made — "before v1 design freeze" or "before first non-research user"). **MA #6 covers this.**
  2. **Risk #4 (OSC port collision) LOW/MEDIUM** — calibrated correctly; mitigation is concrete.
  3. **Risk #11 (spec scope slip) MEDIUM/MEDIUM** — under-rated likelihood IMO. Spec was just frozen yesterday (2026-04-28) after 13 rounds of vendor delta. The likelihood of further vendor pushback during a multi-month v0 build is more like HIGH. But the mitigation is correct (deep-interview re-run + new spec version) — only the rating is soft. Not blocking; flag in MA #6.
- **Residual-risk acknowledgment**: WEAK. None of the 12 risks have an explicit residual-risk note (e.g., Risk #3 Dante driver — what if Digigram support relationship doesn't materialize? Fallback to HDA is named but residual risk to acceptance #1 isn't acknowledged: HDA is not Dante PCIe). Not blocking; flag in MA #6.

**Risks I'd ADD** (max 3):
- **Risk #13 — Algorithm runtime swap click** (already proposed by Architect MA #4; concur).
- **Risk #14 — KEMAR SOFA file format / partition size assumption.** P9 names KEMAR @ `/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa`, claims "256–512 sample IRs at 48 kHz" (Open Q #3). If the actual file has different sample rate (e.g., 44.1 kHz) or different IR length (e.g., 2048 samples), the partitioned-convolution latency budget changes by 2–8×. Mitigation: P0 deliverable = `tools/sofa_inspector.py` that prints sample rate + IR length + measurement count from the named file; assert at P9 startup.
- **Risk #15 — Linux kernel + Digigram driver matrix not pinned at P0.** Already raised by Architect as MA #3; concur — should also appear in the risk register, not just the must-address list.

**Risks I'd DOWNGRADE / NOT REMOVE**: none — all 12 are reasonable.

---

## 5. Pre-Mortem Strength

Architect already audited the 3 scenarios (A/B/C) and rated them STRONG/STRONG/ADEQUATE. I **REINFORCE** all three. Architect-missed angles I add:

### Critic-perspective additions

**Pre-mortem A (FDN denormal CPU spike)** — Architect cited line 107 mitigation step (iii) as "P10 soak harness." **Confirmed plan inconsistency**: line 107 says P10, but P10 (line 534) is the *Latency Harness*; the *Soak Harness* is P11 (line 541). The denormal soak is also referenced in P7 exit criteria (line 507) and again in P11 (line 552). **MA #2 includes this fix.**

**Pre-mortem B (WFS-on-irregular silent failure)** — Architect rated STRONG. **My addition**: Pre-mortem B's diagnostic chain (P9 rE/rV showing |rE|<0.4) only catches the layout error AT TEST TIME on a layout that the operator has named WFS-incompatible (`lab_8ch_irregular`). It does NOT catch the *configurable* failure mode where someone edits `lab_8ch.yaml` to add a 9th irregular speaker and forgets to re-classify regularity — `LayoutCompatibilityChecker.regularity` field is YAML-declared, not derived. **Critic finding (MA #7-extension, but minor)**: `LayoutCompatibilityChecker` should *derive* regularity from the speaker XYZ list (RANSAC-fit a circle / line / planar grid; tolerance configurable), not trust an operator-typed `regularity:` field. Without this, the check is "self-describing config matches its own self-description" (tautological). Not a v0 blocker if the rules table is hand-curated; flag in `docs/lab_setup.md` as known gap.

**Pre-mortem C (OSC reorder under fast drag)** — Architect rated ADEQUATE; recommended UI-side coalescer test (their recommendation B). **I REINFORCE Architect's B**, and add: **the silent-drop policy itself is a debuggability hazard** (Architect Open Q #3). My answer: the drop+counter is the right policy *at the audio path* (RT-safe), but the control thread should emit `/sys/warning ,iis schema_version "osc_reorder_burst" "{count_in_last_window}"` when `osc_reordered_drops` increases by ≥ N in any 1 s window (suggested N=5). Without this, the counter is a forensic log only; with it, the UI can show a yellow status indicator. **MA #4 covers this.**

**Critic-only fourth scenario (not in plan)** — IR file format edge case when v1+ slot is exercised by P7 unit test:
- **Concrete failure**: P7's `IRConvolutionStub::loadImpulseResponse(path)` is a no-op that records the path. At v1+ when a real `IRConvolution` arrives, someone passes a 96 kHz / 24-bit / mono WAV but the engine assumes 48 kHz / float32 / first-channel. Silent sample-rate mismatch produces a 2× too-long reverb tail — perceptually obvious, but only at v1+.
- **Why catch in v0**: the abstraction promise is "drop-in requires no bus/send/routing changes." A sample-rate mismatch would require bus changes. So v0's `IRConvolutionStub` should *validate* the IR file's sample rate + channel count against engine assumptions, even though it returns silence.
- **Mitigation**: P7's stub validates IR file metadata against `engine.sampleRate` and `engine.expectedIRChannels`, returns `ReverbEngine::Result::IRSampleRateMismatch` etc.; v0 unit test feeds a bad-SR file and asserts the stub rejects it. Closes the abstraction-promise hole. **MA #3.**

---

## 6. Test Plan Coverage (DELIBERATE expanded)

| Tier | Status | Notes |
|------|--------|-------|
| **Unit** | **PRESENT, well-specified** | `core/tests/core_unit/` (CTest); ui/tests/ (pytest). Per-module, named, with hand-computed expected values where applicable. CTest framework named (line 354). Compile-time `static_assert` for `Command` POD invariant (line 794) — exemplary. |
| **Integration** | **PRESENT** | `tests/e2e/` spawns core+UI as subprocesses, uses NullBackend for headless runs (line 801). Coverage: OSC roundtrip, geometry reload, algorithm runtime change, reorder defense, decode under load, port collision, matrix reload, handshake mismatch. **Gap**: no UI→engine drag → captured-output integration test except as part of e2e (acceptable; named at line 815). |
| **E2E (local-runner)** | **PRESENT** | Latency harness (P10), accuracy harness (P9 — partly CI-runnable), soak harness (P11), Dante loopback (P6), FDN denormal soak (P11). Real-Dante-required PRs labeled `local-verify-required` (line 360). |
| **Perceptual smoke (P3.5)** | **PRESENT** | N=2, 4-direction forced choice, screen-blind, paper-recorded; appropriate scope (sign-flip detector, not statistical claim). Line 415, 823. |
| **Perceptual sign-off (P12)** | **PRESENT** but methodology under-specified (see §3.4 above) — pre-registered N=12 with Friedman, but contrasts and per-direction CI threshold not pinned. |
| **Observability** | **PRESENT, well-specified** | `/sys/metrics` enumerates counters (line 828); per-thread metrics with rolling window; xrun forensics dump on event (line 832). |
| **CI vs local-runner split** | **PRESENT, explicit** | Line 358–360. CI runs unit + e2e + accuracy + compat + Python tests. Local-runner only: latency, perceptual, soak. |

**Test plan verdict: STRONG.** No tier missing; one tier (perceptual P12) under-specified; nothing hand-waved. **Architect's must-address #4 (algorithm-swap test criterion)** + my **MA #8 (click-test criterion)** are the only test-tier defects.

---

## 7. Reinforcement / Disagreement with Architect

| Architect MA # | Item | Critic position |
|---|---|---|
| **#1** | `block_size × 0.7` arithmetic error in P11 (claimed 358 µs, correct 933 µs) | **REINFORCE.** Verified: `(64/48000) × 0.7 × 1e6 = 933.33 µs`. Plan's 358 µs corresponds to 26.9% of the block period, not 70%. This is a **falsifiability defect** — at 358 µs the gate is ~3× more conservative than spec requires; the engine would fail a passing implementation. Carry forward as Critic MA #1. |
| **#2** | BinauralMonitor latency target deferred to Open Questions | **REINFORCE.** Open Q #3 (line 864) defers the decision to Critic; Critic's role is to raise the testability concern, not to make the architectural call — but I agree with Architect that "deferred to Open Questions" is structurally wrong. Spec acceptance #6 (BinauralMonitor as v0 OutputBackend) + spec acceptance #11 (which says "input → Dante output", textually excluding BinauralMonitor) are in a gray zone the plan must close in body, not in Open Q. Carry forward as Critic MA #5. |
| **#3** | PREEMPT_RT + Digigram kernel pinning in fallback footnote | **REINFORCE.** Critically: spec acceptance #1 names "Digigram ALP-Dante PCIe" as a hard requirement; the plan must pin a *certified* Digigram driver/kernel combination at P0, not at P10 measurement time. P0 cannot deliver `docs/lab_setup.md` (acceptance #1 + #14 enabler) without this pin. Carry forward as Critic MA #6. |
| **#4** | Algorithm runtime swap RT-safety lacks ADR treatment | **REINFORCE WITH EXTENSION.** Architect's R2 (parallel-run crossfade, K=256 samples) is the right resolution. **My extension**: pair this with the click-test criterion fix in §3.1/§3.2 (my MA #8) — without the FFT-based test, even the chosen mechanism can't be falsified. Carry forward as combined Critic MA #4 + MA #8. |

Architect's recommended-not-blocking items A/B/C: I concur with all three; A and B promote to Critic MA #4 (combined with #4 above); C is cosmetic.

---

## 8. Critic-specific findings (NOT in Architect review)

### 8.1 Spec acceptance criterion miscount (Critic-only, MA #1)
- Plan §RALPLAN-DR Principle 2 says "12 acceptance criteria"; plan §Acceptance Criteria Mapping says "14 criteria"; spec source has **16**. The mapping table (line 641) enumerates only 14 rows; criteria #15 (IPC layer documented) and #16 (3U-rack code constraints documented) appear only as a "Cross-cutting" sentence on line 658, without a row of their own. Architect noted "spec actually has 14 acceptance bullets (counted in my read)" (line 80) — Architect also miscounted.
- **Why this matters**: the plan's own claim is that every acceptance criterion maps to a phase + test (Principle 2). When the table doesn't enumerate all criteria, the audit trail is broken — a future reviewer would not catch a deletion of the 3U-rack-constraints requirement.
- **Fix**: count = 16. Update Principle 2 wording to "16". Add rows #15 and #16 to the mapping table. Cross-cutting sentence becomes redundant.

### 8.2 Latency harness instrumentation point not pinned (Critic-only, MA #5)
- §Latency Budget table (line 700) measures from "UI-side mouse event"; P10 deliverable (line 536) measures from "inject `/obj/{id}/pos`". These are *different timestamps* separated by Qt event-loop tail latency (1–4 ms; latency budget table stage 1). The plan therefore measures **stages 2–6** in the harness but states the goal in **stages 1–6** terms. The harness can pass while the user-perceived end-to-end latency fails the spec.
- **Fix**: P10 must explicitly choose one and stick to it. Recommended: T0 = `osc_send_call_start_ts` (recorded by harness Python wrapper, microsecond clock); T1 = first cross-correlation peak in captured Dante input. Document that this measures "post-Qt-event-loop end-to-end latency"; latency budget stage 1 (Qt event-loop tail) must be measured *separately* in P12 with a UI-driven harness (e.g., `pyautogui` mouse + `xdotool` timestamp) and added to the P10 number for the spec's "input → output" claim. Without this two-step, the <5 ms claim is ambiguous.

### 8.3 P11 phase-name inconsistency (Critic-only, MA #2)
- Pre-mortem A mitigation (line 107) names "P10 soak harness" — but P10 is the Latency Harness (line 534) and the soak is P11 (line 541). Same error in P11 (line 552 references "FDN denormal soak (Pre-mortem A): 30 min silence after impulse, p99 per-block stays < threshold" with no explicit phase).
- **Fix**: change line 107 from "P10 soak harness" to "P11 soak harness."

### 8.4 Heartbeat miss gate too lax (Critic-only, MA #4)
- See §3.5 above. 10 s gap = 300 lost heartbeats. Tighten to 500 ms (~15 lost) for leading-indicator semantics; keep 10 s as a catastrophic-stall alarm.

### 8.5 IRConvolutionStub doesn't validate the abstraction promise (Critic-only, MA #3)
- See §5 fourth scenario above. The stub returns silence; abstraction promise is "drop-in requires no bus/send/routing code changes." A real v1+ IR file would have a sample rate / channel-count / format header that the stub today doesn't validate, so the abstraction is unproven for the failure modes that matter.
- **Fix**: P7 stub validates IR file metadata (sample rate, channel count, IR length) against engine assumptions; returns `ReverbEngine::IRSampleRateMismatch` etc. on bad headers; v0 unit test feeds a 44.1 kHz file and asserts rejection.

### 8.6 VST3Control "callable from v1+" has no v0 deliverable (Critic-only, MA #6)
- Acceptance #7 says "VST3Control v1+ slot is callable." Plan §P4 (line 422–424) ships `ExternalControl` abstract base + `OSCBackend` concrete; no test or stub demonstrates that adding `class VST3Control : public ExternalControl` would compile or wire correctly.
- **Fix**: ship a `VST3ControlStub` (analogous to `IRConvolutionStub`) at P4: empty `dispatch(Command)` that asserts compile-time `ExternalControl` signature compliance; v0 test loads the stub at the abstract base via factory and asserts no engine code change required to register it. Without this, "callable" = "we say so." Tied to Architect's stable-abstraction Principle 4.

### 8.7 Coalescer correctness UI test (Architect's recommended-not-blocking B; promoted to Critic MA #4)
- Architect named `ui/tests/test_drag_coalescer.py` — concur, but go further: assert (a) per-object coalescer drops oldest pending (the Pre-mortem C bug class), AND (b) under sustained 120 Hz drag for 1 s, coalescer's output rate is exactly 120 Hz ± 10% (catches "coalescer freezes at first packet" bug), AND (c) `seq` numbers in coalescer output are monotonically increasing per object (mirrors the engine-side reorder defense).

### 8.8 No closed-form analytical reference for VBAP/DBAP gain math (Critic-only, MA #7)
- §3 testability finding above. P9 accuracy harness uses engine math vs engine math (rE/rV computed *from* `RenderingAlgorithm::processBlock` output). For VBAP, the speaker-pair gain formula has a closed-form solution (Pulkki 1997 vector base equations) that should be the reference; for DBAP, the rolloff formula is config-parameterized and inherently self-consistent — but the baseline `Σ g_i² = 1` invariant should be a separate gate (and IS at line 788, which is good — but only at the unit level, not in the accuracy harness). For WFS, the unit test at line 405 already cross-checks against Huygens analytic — this is exemplary; replicate the pattern for VBAP.
- **Fix**: P9 accuracy harness CSV should include a `vbap_analytical_gain_i` column computed from Pulkki 1997 equations, separate from the `engine_gain_i` column; assert per-direction max `|engine_gain - analytical_gain| < 1e-6`. This converts the gate from self-referential to truly externally-anchored.

---

## 9. Verdict

**ITERATE** — 8 must-address items, none structural, all mechanical except MA #4 (which Architect already raised at structural level).

### Must-Address (numbered for Planner R2)

1. **Spec criterion count is 16, not 12 or 14.** Update Principle 2 wording (line 19) and Acceptance Criteria Mapping table (line 641): add rows for #15 (IPC layer documented → P4) and #16 (3U-rack code constraints documented → P11). Remove or shorten the "Cross-cutting" sentence (line 658) since it becomes redundant.

2. **Fix `block_size × 0.7` arithmetic error in P11 exit criteria** (Architect MA #1, reinforced). Line 552 currently says `(= 358 µs at 64 frames @ 48 kHz)`. Correct value is `933 µs` (= `(64/48000) × 0.7 × 1e6`). Also fix the related phase-name inconsistency: Pre-mortem A mitigation (line 107) says "P10 soak harness" but P10 is the Latency Harness; the soak is P11. Change line 107 to "P11 soak harness."

3. **IRConvolutionStub must validate the abstraction promise, not just return silence.** Update P7 deliverable (line 502): the stub validates IR file metadata (sample rate, channel count, IR length) against engine assumptions; returns enumerated error variants (`IRSampleRateMismatch`, `IRChannelCountMismatch`, `IRTooShort`) on bad headers; v0 unit test feeds a 44.1 kHz / 2-channel WAV and asserts rejection with correct error variant. Without this, acceptance #5's "drop-in requires no bus/send/routing changes" is unproven for the failure modes that matter.

4. **Algorithm-swap RT-safety + heartbeat miss gate + UI coalescer test + reorder counter alert** (combines Architect MA #4 + Critic §3.5/§5/§8.7). Sub-items:
   - (a) Add ADR-level treatment of algorithm runtime swap to ADR 0005 (Architect's R2: parallel-run crossfade, K=256 samples; pre-allocated scratch). Add Risk #13 `algorithm_swap_click`.
   - (b) Tighten P11 heartbeat miss gate from "no >10 s gap" to "no >500 ms gap" (15 missed heartbeats); keep 10 s only as the catastrophic-stall alarm.
   - (c) Add `ui/tests/test_drag_coalescer.py`: asserts (i) drops oldest pending per object, (ii) sustained 120 Hz output rate ± 10%, (iii) monotonic `seq` per object.
   - (d) Add Pre-mortem C operator-visible alert: control thread emits `/sys/warning ,iis "osc_reorder_burst" "{count_in_last_1s_window}"` when `osc_reordered_drops` increments by ≥ 5 in any 1 s window.

5. **Pin latency-harness instrumentation timestamps + close BinauralMonitor latency target gap** (Architect MA #2 + Critic §8.2). Sub-items:
   - (a) P10 deliverable specifies T0 = `osc_send_call_start_ts` (Python harness microsecond clock at `client.send_message()`), T1 = first cross-correlation peak in captured Dante input. Document this measures stages 2–6 (NOT stage 1 Qt event-loop tail).
   - (b) Latency budget table (line 700) updated to flag stage 1 as "measured separately at P12 via UI-driven harness; added to P10 number for spec-claim composition."
   - (c) BinauralMonitor latency target moved from Open Q #3 (line 864) into plan body: commit to relaxed target `<15 ms p99` with reasoning in `docs/latency_budget.md` AND named explicitly in v0 sign-off acceptance doc; OR commit to `<5 ms p99` with credible partitioned-convolution budget showing it fits. Either choice is acceptable; deferral is not.

6. **Pin Linux kernel + Ubuntu LTS + Digigram driver matrix at P0; lift PREEMPT_RT decision out of fallback footnote; concretize Risk #2 mitigation** (Architect MA #3 + Critic §4 calibration). Sub-items:
   - (a) `docs/lab_setup.md` (P0 deliverable, line 370) names specific Ubuntu LTS point release + kernel point version + Digigram-certified driver version. "Linux 6.x generic" without a point version is not a pin.
   - (b) PREEMPT_RT y/n decided at P0 based on the latency budget table, not at P10 based on measurement. The budget table itself (line 715) says commodity kernel barely fits.
   - (c) Risk #2 (JUCE GPL → commercial) gets a v0 deliverable: `docs/license_procurement_plan.md` with cost estimate (developers × deployments × $130/mo or perpetual), named owner, trigger condition ("before v1 design freeze").
   - (d) Add Risk #14 (KEMAR SOFA file format/IR length/sample-rate assumption — Critic §4); Risk #15 (kernel/driver matrix — covered by (a) above; can be implicit).
   - (e) Add `tools/sofa_inspector.py` to P0 deliverables; assert at P9 startup that the actual KEMAR SOFA file matches assumed sample rate + IR length.

7. **Add closed-form analytical reference for VBAP gain in accuracy harness** (Critic §3.4/§8.8). P9 accuracy harness CSV (line 528) should include a `vbap_analytical_gain_i` column computed from Pulkki 1997 vector-base-amplitude-panning equations as an external reference; assert per-direction max `|engine_gain - analytical_gain| < 1e-6`. Converts the rE/rV gate from self-referential to externally-anchored. Replicates the pattern of the existing exemplary WFS unit test (line 405). For DBAP, the existing `Σ g_i² = 1` energy invariant unit test is sufficient; flag in P9 docs that DBAP rolloff parameter is not externally anchored (it's a config-driven law).

8. **Tighten click-test criterion across GainRamp, PropagationDelay, and algorithm-swap** (Critic §3.1/§3.2). Replace the `|Δgain| < 1/MAX_BLOCK` trivial-floor check with a three-part assertion:
   - (i) first per-sample step ≤ `1/MAX_BLOCK` (catches "ramp didn't start"),
   - (ii) cumulative gain at sample N=MAX_BLOCK matches target within 1e-6 (catches "ramp ended early/in-wrong-direction"),
   - (iii) FFT of the rendered block contains no spectral component above the input signal's bandwidth + 6 dB margin (proxy for click-as-impulse-residual; the actual click-freeness test).
   Apply uniformly to: GainRamp test (line 408, 795), PropagationDelay sweep test (line 410), algorithm-swap integration test (line 806).

### Tier-3 minors (informational; not blocking)

- M1. Risk #11 (spec scope slip) likelihood under-rated; should be HIGH given fresh spec freeze. Not blocking; minor calibration.
- M2. P12 perceptual sign-off methodology (§3.4): pre-register the specific Friedman contrasts and per-direction CI threshold in `tests/perceptual/listening_test_v0/preregistration.md` *before* any data collection. This is a process discipline note, not a plan defect.
- M3. WFS aliasing inequality dimensional units in Open Q #4 (Architect noted; concur — restate as `spacing < c/(2·f_max)`).
- M4. `LayoutCompatibilityChecker.regularity` derived vs operator-typed (§5 Critic addition to Pre-mortem B). v0 acceptable to keep operator-typed; flag in `docs/lab_setup.md`.
- M5. Architect's recommended-not-blocking A (D1+D3-friendly synthesis: SoA-laid-out per-algorithm scratch + `getObjectsByAlgorithm` helper). Concur; convert v1+ migration from "rewrite ownership model" to "swap one for-loop." ~30 lines in P3.

### Open Questions (unscored, low-confidence)

- Is Architect's MA #4 hybrid (D1 dispatch surface + D3-friendly invariants) better treated as a recommended-not-blocking or as a required v0 deliverable? My read: recommended (per Architect). Critic doesn't override.
- For the perceptual P12 (N=12), should the Friedman pre-registration be a v0 hard gate or a soft commitment? My read: hard gate, but I'm not confident enough to make it MA-level.

---

## 10. Verdict justification

**ITERATE, not REJECT, not APPROVE.** The plan is architecturally and operationally sound:
- All 16 spec acceptance criteria are *delivered* by named phases with named tests (no MISSING).
- Pre-mortem trio is plausible-and-mitigated (Architect rated STRONG/STRONG/ADEQUATE; I concur and add one Critic-shaped fourth scenario).
- Test plan covers all five DELIBERATE tiers (unit/integration/e2e/perceptual/observability) with named files, frameworks, and CI/local-runner split.
- Risk register has 12 domain-specific items with concrete mitigations; calibration mostly correct.
- ADRs carry antithesis-strength reasoning (ADR 0003 IPC remains exemplary).

But the plan ships with **8 fixable defects** that, if uncorrected, would:
- Cause acceptance #11 to gate on the wrong number (MA #2 — 358 µs vs 933 µs).
- Cause acceptance #12 to be ambiguously instrumented (MA #5 — Qt event-loop stage included? excluded?).
- Cause acceptance #5's drop-in-no-changes promise to be unproven for the failure modes that matter (MA #3).
- Cause acceptance #7's "VST3Control slot callable" claim to lack a v0 deliverable (MA #6.c equivalent).
- Cause the audit trail of acceptance criteria to be presentationally broken (MA #1 — 16 vs 14 vs 12).
- Leave several click-tests as form-checks rather than substance-checks (MA #8).
- Leave one accuracy gate self-referential (MA #7).
- Leave one structural decision (algorithm-swap mechanism + ADR + risk) un-addressed (MA #4 — Architect already flagged).

These are all small mechanical edits or small ADR additions — none requires structural rework. R2 should be a 1–2 day Planner pass. After R2, Critic re-review should ACCEPT or ACCEPT-WITH-RESERVATIONS.

**Mode**: started in DELIBERATE; escalated to ADVERSARIAL when MA #1 (spec criterion miscount, both Plan and Architect missed it) confirmed systemic-not-isolated nature of the audit-table presentation gap; remained adversarial through §8 to surface MA #5 (latency instrumentation), MA #7 (analytical reference), MA #8 (click-test criterion). Realist Check downgrades: none — all 8 must-address items survive at MA severity (none rise to CRITICAL because no acceptance criterion is *unrouted*; none fall to MINOR because all 8 affect a falsifiable gate or a stated abstraction promise).

---

## References (file:line)

- Plan §RALPLAN-DR Principle 2 (12 vs 16 miscount): `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.md:19`
- Plan §Pre-Mortem A mitigation (P10 vs P11 typo): `…/spatial-engine-v0.md:107`
- Plan §P3 GainRamp click test (trivial-floor criterion): `…/spatial-engine-v0.md:408,795,845`
- Plan §P9 accuracy harness (self-referential): `…/spatial-engine-v0.md:528`
- Plan §P10 latency harness (instrumentation point not pinned): `…/spatial-engine-v0.md:536`
- Plan §P11 exit (358 µs arithmetic error + 10 s heartbeat gap): `…/spatial-engine-v0.md:552`
- Plan §Acceptance Criteria Mapping table (14 rows, 16 in spec): `…/spatial-engine-v0.md:641-658`
- Plan §Open Q #3 (BinauralMonitor latency deferred): `…/spatial-engine-v0.md:864`
- Plan §Risk #2 (JUCE GPL aspirational mitigation): `…/spatial-engine-v0.md:842`
- Plan §P7 IRConvolutionStub (returns silence; abstraction-promise not exercised): `…/spatial-engine-v0.md:502,508`
- Spec v2.1 Acceptance Criteria block (16 items): `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine-v2.md:105-122`
- Architect R1 review (REVISE-FIRST-WITH-NOTES; 4 must-address): `/home/seung/mmhoa/spatial_engine/.omc/plans/architect-r1-review.md`
- Architect MA #1 (block_size arithmetic): `…/architect-r1-review.md:127,157`
- Architect MA #2 (BinauralMonitor latency in body): `…/architect-r1-review.md:133,159`
- Architect MA #3 (PREEMPT_RT + kernel pin at P0): `…/architect-r1-review.md:124-126,161`
- Architect MA #4 (algorithm-swap ADR): `…/architect-r1-review.md:54-65,163`

— end of Critic R1 review —
