# Critic R2 Review — spatial-engine-v0 (Round 2 plan)
Date: 2026-04-28
Verdict: **REVISE-FIRST**

## Summary
Round 2 is a substantive improvement over R1: the decode-thread invariant, ADR 0001 antithesis, bootstrap target, and pre-mortem are all real DELIBERATE-mode artifacts, not boilerplate. However, two structural defects block APPROVE: (1) ADR 0003 is still a stub while the plan claims OSC over UDP and proceeds to P5, and (2) the P9 numerical accuracy gate is self-referential (VBAP-vs-VBAP) and therefore cannot validate the ±1° acceptance criterion. Several latency-budget arithmetic and test-design issues also need tightening before this is buildable.

## Strengths
- The R1→R2 changelog is item-by-item and traceable; not just rearranged prose.
- ADR 0001 single-process Rust+egui antithesis is a genuine steelman with concrete rejection rationale and a counter-acknowledgment — exactly what DELIBERATE asks for.
- P5 decode-thread contract is well-formed: named module (`core/src/ipc/osc_decode.rs`), single SPSC type (`ObjectUpdate`), and a falsifiable boundary test (1 kHz × 9 KB malformed packets, `assert_no_alloc`).
- Coordinate convention is explicitly elevated to Principle 4 with a single helper module (`core/coords`) and a documented sign-flip rule — directly tracks the L/R inversion incidents in MEMORY.md.
- Risk #9 (port collision with TouchOSC/OSCulator defaults) is the kind of real-world friction most plans miss.
- Pre-mortem Scenario C (geometry cache drift) ties cleanly to a specific test extension in the integration suite.

## Concerns (severity-rated)

### Blocker

**B1. ADR 0003 is a stub, but P5 commits to OSC/UDP and ships the schema.**
ADR 0003 says `Alternatives considered: (to be filled after review)` and `Why chosen: (to be filled after Architect/Critic confirms or revises)` (plan lines 581–582), yet `Open Items Resolution (b)` already chose OSC/UDP, the architecture diagram pins UDP ports 9000/9001, and P5 hard-codes the schema. This is the single largest contradiction in DELIBERATE mode: you cannot claim a decision is "still under review" while simultaneously building seven phases on top of it.
- Confidence: HIGH
- Why this matters: If reviewer feedback flips B1→B2 (shm+UDS), P5/P6/P7/Risk #3/Pre-mortem A/Test plan integration tests/Risk #9 all need rework. The stub is masquerading as a TODO.
- Fix: Either (a) finalize ADR 0003 in this round with the same antithesis-strength reasoning ADR 0001 got — including the `p99 drag-to-render latency under N-object load` falsifier the open-questions file already names — and remove "stub" from the status; or (b) re-label P5+downstream phases as "OSC-track, blocked on ADR 0003," which honest-flags the dependency.

**B2. P9 numerical accuracy gate is self-referential and cannot validate ±1°.**
P9 says `compute realized acoustic-vector from VBAP gains; assert per-direction angular error ≤ 1°` (plan line 407). The unit test in line 498 says `assert that placing an object at each speaker direction produces gain == 1 on that speaker and gain == 0 on the others`. The integration test in line 507 says `assert per-channel gains match the VBAP closed form within 1e-3`. All three are checking that the engine's VBAP implementation produces the same gains as a reference VBAP implementation. That is a self-consistency / numerical-correctness check, not an accuracy check. Acceptance criterion #6 says `Apparent positional accuracy ≤ ±1°`. The numerical bar that maps to "apparent positional accuracy" is the **acoustic energy vector** (Gerzon) or **velocity vector** of the realized speaker gains projected back to a perceived direction in a *given geometry* — and the 1° claim depends on the speaker layout, not on the VBAP math. P9 as written will pass even if the speaker layout is degenerate or the energy-vector pulls perception 5° off intended.
- Confidence: HIGH
- Why this matters: This is the only CI gate for accuracy. If it's circular, the plan has no automated defense against the very acceptance criterion the user cares about most.
- Fix: P9 must compute the realized **rE / rV vector** from the VBAP gains for each test direction in the chosen geometry, project it to a perceived `(az_realized, el_realized)`, and assert `|az_intended − az_realized| ≤ 1°` AND `|rE| ≥ threshold` (otherwise localization confidence is poor). State that the numerical test depends on geometry — `lab_4ch.yaml` may not pass ±1° at every grid point, and that's a layout finding, not a code bug.

**B3. End-to-end <5 ms latency budget arithmetic is not constructed; the 64-frame buffer claim is necessary but nowhere near sufficient.**
P8 / Risk #2 specifies `64 frames @ 48 kHz = 1.33 ms one-way`. That's the audio-output buffer only. End-to-end as defined in acceptance #4 is *input → speaker*. The plan never decomposes:
- UI mouse event → Qt main thread tick (typical Qt event-loop tail latency on commodity Linux: ~1–4 ms, depending on frame pacing)
- `/obj/{id}/pos` over UDP loopback (sub-ms, OK)
- core control thread schedule wake → SPSC push (depends on scheduler; on a non-PREEMPT_RT kernel, jitter to ~1–3 ms is realistic)
- audio thread next callback boundary (up to 1.33 ms wait)
- DSP block compute (small)
- output buffer + DAC (1.33 ms output buffer + interface IO ~1–3 ms typical USB-class)

Sum p99 on commodity Linux 6.x generic (NOT PREEMPT_RT) is plausibly 6–10 ms before any tuning. The plan asserts <5 ms in P8 verification but doesn't lay out the budget that gets it there, and Risk #2 specifically rules out PREEMPT_RT for v0. Pre-mortem Scenario A talks about latency creep from 3.2 → 12 ms, but the 3.2 ms baseline is presented as an assumption, not a derivation.
- Confidence: MEDIUM-HIGH
- Why this matters: This is the headline acceptance numeric. If the budget is infeasible without PREEMPT_RT, the plan must say so or adjust the target. Discovering this at P8 is exactly the kind of late-failure DELIBERATE mode is supposed to prevent.
- Fix: Add a "Latency budget decomposition" subsection under Risk #2 or P8 that lists each contributor with measured-or-estimated p99 numbers. Either (i) show how the sum stays under 5 ms with the named hardware/kernel, or (ii) propose a fallback (PREEMPT_RT, smaller buffer at cost of more xruns, or relax to <10 ms with an asterisk). Note: the spec acceptance number is 5 ms; the user can re-litigate, but the plan must not silently assume it works.

### Major

**M1. P3.5 perceptual smoke with N=2 including the user is not double-blind and has a bias hazard.**
Line 401: `have N=2 listeners (the user + one engineer) confirm the apparent direction matches the intended direction`. The user knows the intended direction (they wrote the plan, they're reading it on the screen, they might be the one driving the OSC commands). One non-user listener is also vulnerable — they can hear the visual UI cue or read the screen. P3.5 is supposed to catch L/R inversion; but if the listener can see "intended right" on the GUI, confirmation bias defeats the test. MEMORY.md shows L/R inversion bugs were the recurring pain point, so this test exists for a real reason — but in this form it would not catch the v2 bugs it's meant to prevent.
- Confidence: HIGH
- Why this matters: P3.5 is offered as the early-warning gate that prevents direction-flip regressions from surviving to P13. If P3.5 is not bias-controlled, Pre-mortem Scenario C's mitigation chain has a weak link.
- Fix: Run P3.5 with **listener blind to intended direction** — randomized 4-direction sequence, listener writes down perceived direction without seeing screen, scored after the fact. N=2 is fine for a smoke test; randomization + screen-blind is what makes it cheap-but-valid.

**M2. The "single source of truth for coordinates" claim is over-stated; there are at least three invariants.**
Principle 4 says `(az, el) radians + dist_m; az = atan2(x, z) (RIGHT = az>0). Sign-flip to AmbiX/SOFA conventions is centralized in one helper`. But MEMORY.md catalogs at least three sign issues: (a) `az` pipeline vs AmbiX flip; (b) `el = arcsin(-y)` vs `arcsin(y)` (the 2026-03-06 elevation sign bug); (c) stereo pan baseline `sin(az)` vs `sin(-az)` (the 2026-03-01 baseline_pan inversion). Plus a fourth from VBAP-land: speaker positions in `configs/lab_*.yaml` need a documented convention (degrees? radians? ISO 7010 azimuth-from-north or audio-azimuth-from-front?). The plan's `core/coords` design with one helper risks repeating the v0 vid2spatial mistake of "single helper that everyone calls but in the wrong direction."
- Confidence: HIGH
- Why this matters: The recurring user-pain pattern in MEMORY.md is exactly this class of bug. A `core/coords` API that exposes only a "negate az" helper will not prevent (b), (c), or geometry-config sign mismatches.
- Fix: Expand `docs/coordinate_convention.md` (P12) **and** the `core/coords` test plan to enumerate every (frame, sign) pair: pipeline (RIGHT=+az), AmbiX/SOFA (LEFT=+az), elevation (UP=+el or image-y-down), VBAP (whatever the engine chooses). Each pair gets a test with a hand-computed expected value, not just a round-trip. Unit test must include a *known* (cartesian) → angle case that catches sign flips, not just `f(g(x)) == x` round-trips (which would pass with a double-flip bug).

**M3. P9 grid is not specified, P13 listener count is hedged ("N≥6 or N≥12 if effect size is small").**
Line 515 says `grid scan 36 azimuths × 5 elevations` — 5 elevations including the listener-plane and ±something? What ±? `lab_4ch.yaml` is plausibly horizontal-only, in which case 5 elevations is wrong for that geometry. Line 411 / open-questions item #2 says listener count is N≥6 with possible escalation to N≥12. DELIBERATE mode does not allow "we'll decide at the data" for an acceptance gate. Effect-size-conditional sample size is a HARK risk.
- Confidence: HIGH
- Why this matters: P9 is the CI gate; P13 is the perceptual sign-off. Both have under-specified verification design.
- Fix: P9: pin the grid to the *geometry's reachable directions* (e.g., for lab_4ch horizontal: 36 azimuths × 1 elevation; for lab_8ch with elevation: 36 × 5 with named el values). P13: pre-register N (suggest N=12) before the test runs.

**M4. `OutputBackend::render` signature implies SoA but plan never specifies block size or per-object audio ownership.**
Line 224: `objects: &[ObjectFrame]` with `// SOA: positions + gains + per-obj audio block`. Who owns the per-object audio block lifetime? The audio thread cannot allocate, so `ObjectFrame` must hold a `&[f32]` borrow into a pre-allocated, control-thread-owned slab — but if the control thread mutates that slab while the audio thread is mid-`render`, it's a data race. The plan says SPSC carries `ObjectUpdate` only, so position/gain are fine; but the *audio sample block per object* (file-source decoded samples in P4) crosses the same boundary and isn't accounted for. The plan never says how many frames `render()` produces per call (cpal block size? a fixed `MAX_BLOCK`?), which determines pre-allocation sizes.
- Confidence: MEDIUM
- Why this matters: This is exactly the kind of detail that turns a "rust real-time-safe by construction" claim into a UB hazard if not specified upfront.
- Fix: Specify (a) max block size constant (`MAX_BLOCK` in addition to `MAX_OBJECTS`), (b) the ownership model for per-object audio blocks (e.g., file-source uses a per-object SPSC of decoded sample frames pre-fed by a dedicated decode thread; audio thread reads N samples without blocking; if empty, output silence and increment underrun counter), (c) what `render()`'s buffer size argument is and whether it's fixed or variable per call.

**M5. P5 verification "send a malformed 9 KB OSC packet at 1 kHz for 60 s" is the right shape but doesn't test the contract that matters.**
The contract is "audio thread does not allocate." Sending malformed OSC packets at 1 kHz exercises the *control thread's* decode error path, not the audio thread. A control-thread bug that swallows the packet entirely would still cause the audio-thread `assert_no_alloc` test to pass — because the audio thread sees nothing. The test that would catch a real boundary violation is: send VALID `/obj/{id}/pos` packets at high rate, force the audio thread to drain `ObjectUpdate` ring at saturation, AND simultaneously have a code path that adds/removes objects (which is the realistic scenario in which someone might add a `Vec` allocation per pre-mortem A).
- Confidence: HIGH
- Why this matters: Pre-mortem Scenario A is the single highest-stakes failure mode. The test that's supposed to prevent it tests adjacent behavior.
- Fix: Restructure P5 boundary test as two distinct cases: (i) valid-packet drain saturation (audio thread sees max ring fill, no alloc), (ii) malformed-packet flood (control thread reject path, no propagation across SPSC). Keep both; they protect different invariants.

**M6. ADR 0002 `nih-plug` 1.0 falsifier is too late and too coarse.**
Line 575: trigger condition is `if nih-plug 1.0 has not been released by v1 kickoff (target date: v0 acceptance + 6 months), open an issue tagged re-evaluate-juce`. v1 kickoff is at minimum 6 months out from v0 acceptance, which itself is months out from now. A falsifier that only fires at v1 kickoff doesn't help if `nih-plug` is showing maturity warning signs *during* v0 (e.g., maintainership goes inactive, breaking 0.x → 0.y migration). The `nih-plug` 1.0 release isn't even on the public roadmap at the time of this plan — there's no semver commitment from upstream that "1.0 by date X" is a defensible target.
- Confidence: MEDIUM
- Why this matters: A falsifier that can't fire is no falsifier.
- Fix: Add an interim checkpoint at the end of P11 (binaural+DAW stub backends): a 1-day spike confirming `nih-plug` HEAD compiles, runs a hello-world plugin, and surveys upstream activity (commit frequency, last release, issue close rate). That spike's findings inform whether the v1 kickoff trigger remains 6 months out or moves earlier.

### Minor

- The architecture diagram says `port 9000 cmd, 9001 state` (line 175) but Risk #9 mitigation says use `--osc-cmd-port 0` for OS-assigned ports. The diagram should note these are defaults overridable at startup, otherwise readers will hard-code them.
- `MAX_OBJECTS = 16` is "8 hard requirement + 2× headroom" (line 194). 16 = 2×8, so headroom is 1× not 2×; either the math or the prose is off by one.
- Plan lines 369–370: pre-commit names `pyright (or mypy)` — pick one. Two type checkers in pre-commit is a recipe for drift.
- P11 says `ADR 0003 finalized` (line 409). ADR 0003 is the IPC ADR. Why is the IPC ADR being finalized in P11 (binaural/DAW stubs phase) rather than P5 (the IPC phase)? Likely a typo carried over.
- `tests/perceptual/p3_5_smoke/` is not in the Repository Layout listing on lines 304–364, but P3.5 outputs reference it. Add it.
- The trajectory schema placeholder `proto/trajectory_schema.json` is mentioned but never said what schema it mirrors. State explicitly that it mirrors `vid2spatial`'s `[{t, az_deg, el_deg, dist_m}]` so a future v1 import doesn't recreate the format.
- Line 81 of spec says `JACK on Linux for low-latency multi-channel routing`; plan Risk #2 mitigation says JACK or PipeWire-JACK. Reconcile and pick one for v0 in `docs/lab_setup.md`.
- `assert_no_alloc` is named throughout but never pinned to a crate version or vendored; if the crate goes unmaintained, the test infrastructure has a single-point-of-failure. Add a follow-up note.

## What's Missing (gap analysis)

- **No explicit treatment of position interpolation across audio blocks.** When `/obj/0/pos` jumps the position discretely, the audio thread will produce a click at the next callback boundary unless it crossfades or interpolates per-sample. The plan asserts "without click/dropout artifacts" (acceptance #3) but doesn't specify the smoothing model. MEMORY.md shows the user previously hit smoothing issues in vid2spatial (asymmetric one-pole IIR; az unwrap). This is a known footgun; the plan should specify per-sample gain ramp on `ObjectUpdate` apply.
- **No spec for what `dist_m` does in v0.** The spec lists `(az, el, dist_m)` but VBAP is direction-only. Does v0 ignore `dist_m`? Apply an inverse-square gain? The plan never says. MEMORY.md shows distance-handling has been a chronic weakness (`bbox_area per-clip normalize bug`).
- **No bring-up plan for the second machine.** P12 says "repeat on a second machine; if either run >60 min, file a bootstrap-friction issue." But there's no list of what "second machine" means (different USB controller? different audio interface? both Ubuntu 22.04?). Without this, the second-machine test is a nominal box-tick.
- **No CI-vs-local test split policy.** P9 is supposed to gate CI. Does CI have audio hardware? If not, the latency harness (P8) and accuracy harness (P9) must be either headless-friendly (write to WAV via cpal null backend) or marked as local-only. Plan doesn't say.
- **No backpressure semantics for `/sys/state` heartbeat.** If the UI's `python-osc` listener falls behind 30 Hz, what does the core do? Drop heartbeats? Buffer them (allocates)? Log? Plan says "heartbeat consumer is non-blocking" but core-side behavior under slow UI is unspecified.
- **No version compatibility plan between core and UI.** A collaborator may pull `core` HEAD but have stale `ui` checkout. There's no `/sys/protocol_version` handshake. With the IPC schema as the v0→v1 invariant, this matters.
- **Threading model for the file-source `AudioInput` is undefined.** P4 says "mmap-loaded at config time, ring-fed to audio thread." Who's filling the ring? A dedicated decoder thread? If the audio thread reads from an mmap'd file directly, that's a page-fault hazard at audio-thread priority — page faults are syscalls, syscalls violate Principle 1.
- **No explicit IPC-schema versioning policy.** ADR 0003 should specify a `schema_version` field on every command, with a documented compatibility window. Plan currently has none.
- **No memory-budget sketch.** Soak harness has slope thresholds (RSS) but no absolute target. What's the expected steady-state RSS for core and UI? Without an anchor, "creep" is hard to define for a process that grew 200 MB at startup.

## Ambiguity Risks

- "Multi-hour" in acceptance #7 → Plan reads as "≥4 h pilot + 12 h full run." Spec just says "multi-hour." A reviewer could read multi-hour as "8 h workday." Pin: multi-hour = 12 h continuous, full-load.
- "Apparent positional accuracy ±1°" → Pre-mortem C says the *perceived* azimuth drift is what matters. Acceptance #6 hedges with "or numerical pan-law check." Plan defaults to numerical for CI. Two interpretations: (A) numerical-passes-then-perceptual-confirms (current); (B) only-perceptual-counts. Pin (A) explicitly so P9 isn't downgraded later.
- "Modify a UI element end-to-end" → The plan's three example modifications are concrete (gain meter widget; drag sensitivity; recolor). But a collaborator might think "add a per-object reverb send" qualifies — that's a core-touching change, not a UI-only one. Define "end-to-end" as "no Rust changes required."

## Specific items the next Planner round MUST address

1. **Fill ADR 0003 in this round.** Use the same antithesis-strength reasoning as ADR 0001. Steelman shm+UDS and ZeroMQ; reject with reasons; nominate the migration falsifier (the open-questions file already names `p99 drag-to-render latency under N-object load`, use it). Strip `(stub)` from line 579. (Tied to: ADR 0003, Open Items §b, P5.)
2. **Replace P9's self-referential VBAP check with an energy-vector / velocity-vector test in the chosen geometry.** State per-geometry expected accuracy; do not promise ±1° on geometries that mathematically cannot deliver it (lab_4ch with 4 horizontal speakers will not give ±1° everywhere). (Tied to: P9, Acceptance #6, Test Plan §integration.)
3. **Decompose the <5 ms latency budget end-to-end with named contributors.** Add a section under Risk #2 or P8 that sums Qt event tick + UDP loopback + control-thread schedule jitter + SPSC handoff + audio callback wait + DSP block + DAC out, with measured-or-estimated p99 numbers. If commodity Linux 6.x generic cannot deliver, name the fallback (PREEMPT_RT optional? relax to <10 ms?). (Tied to: P8, Risk #2, Pre-mortem A.)
4. **Make P3.5 listener-blind.** Randomized direction sequence, listener cannot see UI screen, score after the fact. N=2 is fine; the screen-blindness is what makes it valid. (Tied to: P3.5, Risk #7.)
5. **Pin the P13 listener count at N=12** (see Open items below) and remove the conditional escalation phrasing. (Tied to: P13, Open Items §f, open-questions #2.)
6. **Specify per-sample position interpolation / gain-ramp on `ObjectUpdate` apply** in P3 or `core/dsp/`. Without this, acceptance #3 ("without click/dropout artifacts") has no implementation contract. (Tied to: P3, P5, Acceptance #3.)
7. **Define `dist_m` semantics in v0** — either "ignored, position is direction-only" or "applied as inverse-square or 1/r gain with rolloff X." Currently silent. (Tied to: P3, IPC schema, OSC `/obj/{id}/pos`.)
8. **Add `MAX_BLOCK` constant + per-object audio-block ownership model.** Specify the file-source decoder thread that owns the producer end of a per-object audio SPSC ring; audio thread reads, never decodes. Eliminates the page-fault hazard. (Tied to: P2, P4, audio thread isolation rules.)
9. **Restructure P5 boundary test into two cases**: valid-packet drain saturation (proves audio-thread no-alloc under load) AND malformed-packet flood (proves control-thread error path doesn't propagate). Current test only exercises the latter. (Tied to: P5 verification.)
10. **Add interim `nih-plug` health checkpoint at P11** (1-day spike, finding logged in ADR 0002), not only the v1-kickoff trigger 6+ months out. (Tied to: ADR 0002, P11.)
11. **Add `schema_version` to IPC commands and a startup `/sys/protocol_version` handshake.** Version skew between core and UI is real once collaborators work async. (Tied to: ADR 0003, IPC schema, P5, Risk #4.)
12. **Specify CI-vs-local split for P8/P9/P10.** Either provide a null-audio cpal backend for headless CI or mark these as local-runner-required and explain how PRs that touch them get verified. (Tied to: P8, P9, P10, build/dev tooling section.)
13. **Coords helper test plan must include hand-computed expected angles for each (frame, sign) pair**, not only round-trip identity tests. (Tied to: `core/coords/`, Test Plan §unit, Risk #7.)
14. **Reconcile JACK vs PipeWire-JACK for v0**, pin one in `docs/lab_setup.md`, and in P8's baseline artifact. (Tied to: Risk #2, P8, spec §Technical Context.)
15. **Move "ADR 0003 finalized" out of P11** — it belongs in P5 (the IPC phase). Likely typo. (Tied to: P11.)

## Open items I take a position on (vs. leaving for user)

- **ADR 0003 IPC transport: OSC over UDP for v0, with shm+UDS named as the v1 migration target conditional on a falsifier.** Falsifier: `p99 drag-to-render latency exceeds 3.0 ms with 8 objects + 120 Hz drag for ≥1% of windows over a 1 h soak`. If yes at v0 sign-off, v1 starts with shm+UDS migration; if no, v1 keeps OSC. This makes ADR 0003 fillable now.
- **P13 listener count: N=12.** MEMORY.md is unambiguous: "User N=3 only informal user validation; N≥12 needed for Friedman test." User has previously held themselves to that bar for paper-grade claims, and a perceptual sign-off for a multi-quarter "main project" engine is paper-grade-adjacent. Pre-register N=12 before P13 runs; if recruitment is the blocker, schedule longer, don't lower N.
- **Geometry-edit Non-Goal: keep option (b) (Non-Goal).** Three concrete UI-modification examples are sufficient and avoid scope creep. Geometry editing is a v1 feature.
- **DELIBERATE vs SHORT mode: keep DELIBERATE.** Real-time audio + multi-process IPC + production-grade-from-v0 + multi-hour soak + perceptual sign-off is exactly the workload class DELIBERATE was designed for.
- **`nih-plug` 1.0 falsifier window: add interim checkpoint at P11.** v1-kickoff-only is too late. 1-day spike at P11 logs `nih-plug` upstream activity + hello-world plugin compile.
- **Lab machine pinning: deferred to P2 (not P8).** `docs/lab_setup.md` should be a P2 deliverable so that P8 measures *against* the pinned spec, not *into* it.

## Verdict rationale

REVISE-FIRST, not REJECT and not APPROVE.

Not REJECT because R2's substantive content (decode-thread invariant, ADR 0001 antithesis, bootstrap target, pre-mortem, expanded test plan) is the real DELIBERATE-mode work, not theater. The plan is salvageable in one more round.

Not APPROVE because: (i) ADR 0003 is structurally a stub while seven phases depend on its outcome (B1); (ii) the P9 numerical accuracy gate cannot validate the headline acceptance criterion as currently designed (B2); (iii) the <5 ms latency budget is asserted, not constructed (B3); (iv) at least three Major findings (P3.5 bias, coords API scope, render signature ownership) require structural changes, not just clarifications.

**Path to APPROVE**: Round 3 must finalize ADR 0003 (no stub), fix P9's accuracy methodology, decompose the latency budget with named contributors, address listener-blind P3.5, pin P13 N=12, and resolve at least items 1, 2, 3, 4, 6, 7, 8, 9 from "Specific items the next Planner round MUST address" above.
