# Plan: spatial_engine v0 (C++ JUCE native core + PySide6 UI, OSC/UDP IPC, full per-object DSP, VBAP/WFS/DBAP, FDN reverb)

## Header
- **Project**: `spatial_engine` (real-time, object-based immersive audio rendering engine; d&b Soundscape / L-ISA family)
- **Source spec**: `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine-v2.md` (spec v2.1, vendor-augmented + foundational tech locked; **16 acceptance criteria**)
- **Planner version**: round 2 (R2; supersedes R1 in place; addresses Architect R1 + Critic R1 must-address items)
- **Date**: 2026-04-28
- **Status**: `READY-FOR-CONSENSUS-R2`
- **Working dir**: `/home/seung/mmhoa/spatial_engine/`
- **Mode**: DELIBERATE (real-time audio with hard <5 ms end-to-end latency + multi-process IPC + production-grade v0 + 3 simultaneous spatial algorithms + FDN reverb + multi-hour soak; the spec is high-stakes and requires pre-mortem and expanded test plan)
- **Predecessor**: `.omc/plans/v1-archive/` — archived because spec v1's Rust+cpal stack is invalidated by spec v2.1 R12 (C++ + JUCE locked) and R7/R8/R9/R10 vendor-delta scope. Technique-only inspiration (coordinate convention principle, latency budget decomposition, ADR pattern, position interpolation per-sample ramp idea, listener-blind perceptual smoke) is carried over; no Rust paths/crates/modules survive.

---

## R1 → R2 Changelog (item-by-item against Architect R1 4 + Critic R1 8 + Tier-3 minors)

This changelog is the **first substantive section after the Header** so reviewers can navigate it. Each item references the must-address ID from the upstream review and the section/line range in this R2 where the fix lands.

### Architect R1 must-address (4 items)

- **A1 — `block_size × 0.7` arithmetic in P11 exit criterion.**
  - R1 claimed `358 µs at 64 frames @ 48 kHz`; correct value is **`933 µs`** (`64/48000 × 0.7 = 933 µs`).
  - **Fix landed**: P11 §Exit criteria, §Acceptance Criteria Mapping table row #11, ADR 0005 follow-ups (none affected), §Risk #8 mitigation gate, integration test for FDN denormal soak (`tests/e2e/`), and §Latency Budget commentary.
  - **Audit pass**: every other arithmetic claim in the plan body re-verified against C2 (no other defects found; PropagationDelay 29.2 ms at d=10 m re-checked = `10/343 = 29.155 ms` OK; FDN delay-line set still in mutually-prime sample-count band; KEMAR partitioned-convolution numbers expanded under A2).

- **A2 — BinauralMonitor latency target promoted from Open Questions to plan body / ADR-level treatment.**
  - **Fix landed**: new ADR-level treatment in §Latency Budget §"BinauralMonitor sub-budget" + new entry in §Acceptance Criteria Mapping row #6 (asterisk on spec criterion #11 mapping for BinauralMonitor path) + concrete partition-strategy decision in P9 deliverables. KEMAR IR length confirmed at P0 deliverable `tools/sofa_inspector.py` (Risk #14 / Critic MA #6.e).
  - **Decision**: BinauralMonitor uses **`juce::dsp::Convolution` zero-latency partitioned uniform-partition** (head 64 + tail at increasing partition sizes) → expected p99 **<10 ms** (asterisked relative to spec criterion #11 which textually scopes "input → Dante output"). The asterisk is documented in `docs/latency_budget.md` and the v0 sign-off acceptance doc.
  - **Falsifier**: at P9 measurement, if `juce::dsp::Convolution` partitioned mode produces >12 ms p99 latency on the lab target with KEMAR-actual IR length, switch to **uniform-partition with zero-latency front + 64-frame predelay on SpeakerArray** to align both backends; re-measure both.

- **A3 — PREEMPT_RT + Digigram ALP-Dante kernel pinning moved from fallback footnote into P0 first-class deliverable.**
  - **Fix landed**: P0 deliverables expanded with explicit kernel/distro pinning **spike** (since Digigram's published Linux compatibility matrix is the load-bearing input and is not currently inventoried by the team); §Latency Budget body now references the pinned combination instead of "PREEMPT_RT optional"; Risk #15 added to register; ADR addition in §"P0 kernel/driver matrix decision" subsection.
  - **Decision shape**: P0 publishes `docs/lab_setup.md` naming **specific Ubuntu LTS point release + specific kernel point version + specific Digigram-certified ALSA driver version**, and **decides PREEMPT_RT y/n at P0 based on the latency budget table**, not at P10 based on measurement. Default disposition: **adopt PREEMPT_RT for v0 baseline** (the budget table shows commodity kernel barely fits and Digigram's certified-driver matrix on PREEMPT_RT is non-zero; Linux 6.x generic is preserved as fallback).

- **A4 — Algorithm runtime swap RT-safety: ADR 0006 added.**
  - **Fix landed**: new ADR 0006 — Algorithm Runtime Swap Mechanism (Decision / Drivers / Alternatives / Why / Consequences / Falsifier). Decision: pre-allocate algorithm-specific scratch state per object at scene-load time; audio thread reads stable `algorithm_id` field set by atomic publish from control thread; **R2 crossfade with K=256 samples (~5.33 ms @ 48 kHz) of parallel-run** between old and new algorithm during swap, both backed by pre-allocated scratch. Risk #13 (`algorithm_swap_click`) added to register.

### Critic R1 must-address (8 items)

- **C1 — Spec acceptance criterion miscount.**
  - R1 said "12 criteria" (Principle 2) and "14 criteria" (mapping table). Spec v2.1 has **16**.
  - **Fix landed**: Principle 2 wording updated to "16"; §Acceptance Criteria Mapping table now has **16 rows** (rows #15 IPC-layer-documented + #16 3U-rack-code-constraints-documented added; "Cross-cutting" sentence dropped as redundant).

- **C2 — Latency-budget arithmetic audit (Critic-reinforced A1).**
  - **Fix landed**: A1 corrected; full plan-body arithmetic re-audited; FDN delay-line lengths in samples vs ms re-checked; PropagationDelay numbers re-checked; KEMAR partition strategy numbers (A2) expanded.

- **C3 — Latency harness instrumentation point ambiguity.**
  - R1 had two different timestamp definitions (`/obj/{id}/pos` UI mouse event vs OSC inject). Different by 1–4 ms.
  - **Fix landed**: P10 deliverable §pin specifies **T0 = OSC packet receive timestamp at native core's `juce::OSCReceiver` callback entry (recorded by core, microsecond clock)**, **T1 = sample written to Dante PCIe buffer (just before kernel hand-off to ALSA period buffer; instrumented inside `DanteBackend::audioDeviceIOCallbackWithContext` via a hardware-timestamp probe)**. Stage 1 (Qt event-loop tail) is **measured separately at P12** with a UI-driven harness and added to the P10 number for the spec's "input → Dante output" claim. **Mean vs p99**: spec says "<5 ms"; we interpret as **p99**. Documented in P10 + Acceptance Mapping row #12.

- **C4 — Algorithm-swap ADR specificity (Critic-reinforced A4).**
  - **Fix landed**: ADR 0006 falsifier specifies "a profiled allocation or vtable churn observed inside the audio thread during an algorithm swap under `RT_ASSERT_NO_ALLOC` instrumentation, OR a click event during the swap window detected via FFT spectral spike >10 dB above neighboring frequencies (per C8)."

- **C5 — JUCE GPL license trigger concretization.**
  - R1 said "v1+ industrial deployment" abstractly.
  - **Fix landed**: Risk #2 expanded with: (a) v0 is GPL-compliant for lab/research/internal use; (b) **trigger event = first external distribution of the binary OR first commercial deployment outside the user's research lab**; (c) procurement action = purchase JUCE Indie/Pro license at trigger T (named owner: project lead; delivery: `docs/license_procurement_plan.md` filed at P12 alongside v0.1.0 tag). `docs/lab_setup.md` reminds contributors that v0 source is GPL-licensed and PRs must be compatible.

- **C6 — Heartbeat-miss gate strengthening.**
  - R1: "no >10 s gap" was a catastrophic-stall alarm masquerading as a leading indicator.
  - **Fix landed**: heartbeat changed from 30 Hz to **10 Hz (100 ms period)**; miss threshold = **3 consecutive missed beats (300 ms)**; on miss, **control thread** (never audio thread) emits `/sys/heartbeat_miss ,iis schema_version "{stream}" "{count_in_last_window}"`. Documented in P4 (IPC) + P11 (observability) + Risk register.

- **C7 — VBAP/DBAP accuracy harness self-reference.**
  - R1's harness compared "engine math vs engine math".
  - **Fix landed**: P9 §Numerical Accuracy Harness now uses **closed-form analytical baselines**: VBAP closed-form three-loudspeaker-arc gain solution (Pulkki 1997 vector base) computed independently from engine code; DBAP published distance-based amplitude formula (Lossius et al. 2009) computed independently; assert realized-rE-direction ≤ ±1° **from the analytic value**. CSV column scheme expanded with `_analytical_gain_i` columns separate from `_engine_gain_i` columns; per-direction max `|engine_gain - analytical_gain| < 1e-6`.

- **C8 — Click-test substance vs form (FFT spectral check).**
  - R1's `|Δgain| < 1/MAX_BLOCK` was a trivial-floor form check.
  - **Fix landed**: every click test (GainRamp, PropagationDelay sweep, algorithm swap, scene swap, position jump) now asserts the three-part criterion:
    1. first per-sample step ≤ `1/MAX_BLOCK` (catches "ramp didn't start");
    2. cumulative gain at sample N=MAX_BLOCK matches target within 1e-6 (catches "ramp ended early or in wrong direction");
    3. **FFT of the rendered block around the discontinuity event contains no spectral component >10 dB above neighboring frequencies** (the substance check).
  - Applied uniformly in P3, P5, P9 unit + integration tests.

### Tier-3 minors (Critic R1 §M1–M5)

- **M1 — Risk #11 (spec scope slip) likelihood**: **APPLIED**. Likelihood escalated from MEDIUM to **HIGH** given fresh-spec-freeze.
- **M2 — P12 perceptual sign-off pre-registration**: **APPLIED**. P12 deliverable now requires `tests/perceptual/listening_test_v0/preregistration.md` filed *before any data collection*, naming the specific Friedman contrasts (VBAP vs DBAP on lab_8ch; lab_4ch vs lab_8ch on VBAP) and per-direction CI threshold (`mean ≤ 1° AND 95% per-direction CI upper bound ≤ 2°`).
- **M3 — WFS aliasing inequality units in Open Q #4**: **APPLIED**. Restated as `spacing < c/(2 · f_max)` (i.e., `0.5 m vs 21.4 mm at f_max = 8 kHz`).
- **M4 — `LayoutCompatibilityChecker.regularity` derived vs operator-typed**: **DEFERRED**. v0 keeps operator-typed; flagged in `docs/lab_setup.md` as a known v1+ refinement (RANSAC fit). Reason: derivation adds geometry-fit code path the v0 hand-curated rules table doesn't need.
- **M5 — Architect's recommended-not-blocking A (D1 + D3-friendly invariants)**: **APPLIED** as a v0 P3 deliverable. SoA-laid-out per-algorithm scratch arrays at `prepareToPlay`; `engine->getObjectsByAlgorithm(Algorithm)` helper added. Documented in ADR 0005 follow-ups + ADR 0006 (the swap mechanism uses the same SoA scratch).

### Architect R1 recommended-not-blocking (B/C from Architect §"Recommended-but-not-blocking")

- **Architect-rec-B — UI-side coalescer correctness test (`ui/tests/test_drag_coalescer.py`)**: **APPLIED**. Test asserts (i) drops-oldest-pending per-object (Pre-mortem C bug class), (ii) sustained 120 Hz output rate ± 10% over 1 s sustained drag, (iii) monotonic `seq` per object in coalescer output.
- **Architect-rec-C — WFS aliasing inequality dimensional units restatement**: superseded by C-tier M3 above (same fix).

### Net R2 outcome

- **12 must-address items** (4 Architect + 8 Critic) all addressed.
- **Tier-3 minors**: 4 of 5 applied; 1 deferred with documented reason.
- **ADR count**: **6** (0001–0006).
- **Spec criterion mapping**: **16 rows** (was 14).
- **Risk register**: **15 items** (was 12; added #13 algorithm-swap-click, #14 KEMAR-SOFA-format, #15 kernel-driver-matrix).
- **Heartbeat semantics**: **10 Hz / 300 ms miss gate** (was 30 Hz / 10 s gap).

---

## RALPLAN-DR Summary

### Principles (5)
1. **Real-time audio thread is allocation-free, lock-free, syscall-free, log-free.** No `new`/`malloc`/`std::vector::push_back` in steady state, no `std::mutex`, no I/O. JUCE pre-allocation (`AudioBuffer<float>` of fixed `MAX_BLOCK`, `juce::dsp::ProcessorChain` configured at `prepareToPlay`, `juce::AbstractFifo` SPSC for control crossings). Python never sees an audio sample.
2. **The 16 acceptance criteria of spec v2.1 are inviolable design pressure, not aspiration.** Every architectural decision (process model, IPC schema, FDN topology, algorithm dispatch, OutputBackend abstraction) traces to ≥1 acceptance criterion; conversely every acceptance criterion maps to ≥1 phase that delivers it (see §Acceptance Criteria Mapping with 16 rows).
3. **Coordinate frames are explicit and tested at every boundary.** `(az_rad, el_rad, dist_m)` internal; pipeline (RIGHT=+az), AmbiX/SOFA (LEFT=+az), VBAP/WFS/DBAP layout-frame, image-y-down ↔ listener-frame elevation. The `coords` namespace is the *only* place sign-flips happen, and each (frame, sign) pair has a hand-computed expected-value test (see §Coordinate Convention Module). MEMORY.md catalogs ≥4 historical L/R-or-elevation sign-flip incidents in the sibling vid2spatial repo; this principle is the regression defense.
4. **`OutputBackend`, `RenderingAlgorithm`, `ReverbEngine`, `ExternalControl` are stable abstractions from day one.** v0 ships two simultaneous `OutputBackend` impls (`SpeakerArray` + `BinauralMonitor`), three `RenderingAlgorithm` impls (VBAP/WFS/DBAP), one `ReverbEngine` impl (FDN with 4 v1+ hooks), one `ExternalControl` impl (`OSCBackend`); v1+ adds `IRConvolution` reverb and `VST3Control` external control by *one new subclass*, not by engine-core surgery.
5. **The IPC schema is the v0→v1 invariant.** Single `Command` schema serves UI IPC + external OSC + v1+ VST3Control. v0 UI is throw-away; v0 audio core is preserved into v1; the schema bridges both. Schema version handshake on connect; UI mismatch = explicit error, never silent drift.

### Decision Drivers (top 3)
1. **Sub-5 ms end-to-end input → Dante output latency on lab Linux + Digigram ALP-Dante PCIe.** Dominates: process model (no Python in audio path), IPC choice (UDP loopback < shm only if measured-cheaper), buffer size (64 frames @ 48 kHz), kernel/governor (R2: **PREEMPT_RT** lab baseline + `performance` governor + PipeWire-JACK; Linux 6.x generic preserved as fallback), and FDN delay-line size (no oversampled diffusion).
2. **v0→v1 transition cost.** Spec v2.1 explicitly preserves the audio core into v1; only the UI is throw-away. So the C++/JUCE core must be the production-grade choice now (it is — that's R12 lock); IPC schema must survive (single `Command` schema across UI + OSC + v1+ VST3Control); abstractions must support drop-in (the 4 hooks for `IRConvolution`, the `ExternalControl` slot for `VST3Control`).
3. **Three simultaneous rendering algorithms (VBAP/WFS/DBAP) selectable per-object, with full per-object DSP chain.** Drives: per-object `RenderingAlgorithm*` polymorphism vs table dispatch (chosen: D1 polymorphic + D3-friendly SoA invariants — see ADR 0005); per-object algorithm-runtime-swap mechanism (ADR 0006, K=256 sample crossfade); `LayoutCompatibilityChecker` design (rejects WFS-on-irregular-array at config-load time, not at first sample); pre-allocation strategy (each algorithm pre-allocates its own per-object scratch at `prepareToPlay`).

### Viable Options for Major Architectural Forks (≥2 each, with bounded pros/cons)

#### (a) Process model
- **Option A1 — Two processes: JUCE host (audio core, C++) + PySide6 UI (Python), OSC/UDP loopback IPC.**
  - Pros: Python GIL/GC/Qt-event-loop tail latency *cannot* leak into the audio path; UI throw-away surface is process-isolated; matches spec v2.1 R13 lock.
  - Cons: serialization tax (~0.05 ms; well inside latency budget); two binaries to ship; schema versioning required to prevent core/UI drift.
- **Option A2 — Single process: JUCE host with embedded CPython interpreter (`Py_Initialize` + `pybind11`); PySide6 runs in a Qt thread inside the same process.**
  - Pros: zero IPC; direct memory sharing for state mirror; one binary.
  - Cons: GIL acquisition from any C++ → Python edge serializes; Python GC pauses can co-occur with the audio thread's CPU and cause cache thrash; embedded CPython + Qt is a build-system tarpit on Linux (Conda vs system Python vs uv resolution); spec v2.1 IPC R13 is *single transport OSC*, which only works cleanly across a process boundary; UI throw-away rewrite at v1 becomes harder if the Python is welded into the C++ host. **Invalidated** for v0 by spec R13 + Driver #2 (UI must be throw-away at process boundary).
- **Recommendation: A1.** Spec v2.1 R13 effectively pre-decides this; we document it explicitly because the alternative is the kind of "obvious efficiency win" a future maintainer might propose.

#### (b) IPC schema versioning
- **Option B1 — In-band `schema_version` u16 as first OSC argument on every command + `/sys/protocol_version` handshake on connect.**
  - Pros: forward/backward compatibility detectable on first packet; UI refuses to send commands on mismatch with explicit error; cheap (2 bytes per command); externally visible (TouchOSC users can debug with `oscdump`).
  - Cons: every command's argument list is shifted by one slot; minor wire-format ceremony.
- **Option B2 — Out-of-band capability negotiation: connect handshake exchanges full feature set; commands are unversioned afterward.**
  - Pros: no per-command ceremony; richer capability description.
  - Cons: stateful; harder to debug a single packet in isolation; if state drifts mid-session (e.g., core hot-reloaded), commands silently misalign.
- **Recommendation: B1.** Per-packet self-describing wins for debugging; the cost is negligible; matches OSC ergonomics.

#### (c) FDN topology
- **Option C1 — 16-line FDN with Hadamard mixing matrix + per-line one-pole tone control + frequency-dependent T60.**
  - Pros: 16 lines give dense modal coverage at lab-scale RT60 (0.2–2.0 s); Hadamard is allocation-free, branchless, and SIMD-friendly (16×16 = 4 cascaded butterfly stages). `juce::dsp::DelayLine` with linear interpolation per line; `juce::dsp::IIR::Filter` for tone control. Industry-standard pattern (Jot 1991, Schroeder-derived).
  - Cons: 16 delay lines × N samples per object's reverb send = larger memory footprint than 8-line; modal density may be more than v0 lab listening rooms need.
- **Option C2 — 8-line FDN with Householder reflection matrix + shared global tone control.**
  - Pros: half the delay memory; Householder is also allocation-free and uses ~2× FLOPs less than Hadamard for 8 lines.
  - Cons: 8 lines at lab RT60 produce audibly sparse modal density on transient sources; tail flutter risk; would need denser per-line tone shaping to compensate.
- **Option C3 — Configurable {8, 16} at config-load with default 16.**
  - Pros: optionality; falsifiable in lab listening.
  - Cons: doubles test surface; tone-control coefficients need separate tuning per N.
- **Recommendation: C1 (16 lines, Hadamard, per-line one-pole tone control, frequency-dependent T60 via per-line lowpass).** ADR 0004 details. The 16-line memory cost is small (16 × ~3000 samples @ 48 kHz × 4 bytes ≈ 192 KB) and modal density matters more than the marginal CPU saved.

#### (d) Per-object algorithm dispatch
- **Option D1 — Polymorphic `RenderingAlgorithm*` per `Object`; virtual `processBlock(ObjectFrame&, OutputBus&)` called once per object per block.**
  - Pros: straightforward C++ idiom; each impl owns its scratch; `LayoutCompatibilityChecker` validates pairs at config time so the v-table call is always safe.
  - Cons: virtual call overhead per object per block (~few ns; negligible at 8 objects); inhibits some auto-vectorization across objects.
- **Option D2 — Table-driven dispatch: enum `Algorithm{VBAP,WFS,DBAP}`; `switch` per object per block selects pre-allocated scratch + algorithm function pointer.**
  - Pros: no v-table; cache-friendly if all 8 objects share an algorithm.
  - Cons: scratch ownership becomes the engine's problem instead of the algorithm's; growing the enum touches the engine.
- **Option D3 — Branch-per-block: at start of each block, partition the object set into per-algorithm buckets, then process each bucket with a homogeneous loop.**
  - Pros: SIMD-friendly within a bucket; minimizes branch mispredictions; pays only when partitioning changes.
  - Cons: bucket bookkeeping; harder to reason about per-object SOA layout if the partitioning shuffles.
- **R2 recommendation: D1 dispatch surface + D3-friendly invariants** (Architect synthesis adopted as P3 deliverable; M5). Keep polymorphic `RenderingAlgorithm*` per-Object surface; at `prepareToPlay`, each `RenderingAlgorithm` impl allocates SoA scratch sized to `MAX_OBJECTS`; `engine->getObjectsByAlgorithm(Algorithm)` helper returns spans. ADR 0005 details. v1+ migration to D3 is "swap one for-loop in `SpatialEngine::renderBlock`" — no header change.

#### (e) AudioInput v0 source mix
- **Option E1 — File playback only.**
  - Pros: simplest; deterministic test signals; no ALSA capture bringup.
  - Cons: doesn't exercise live-input path; spec doesn't mandate live mic in v0 but the lab demo will eventually need it.
- **Option E2 — File + offline-rendered synth (test-tone bank generated once via `tools/render_test_signals.py`).**
  - Pros: deterministic + diverse content (sines, noise, transients for latency, broadband for spatial perception); decouples content from I/O.
  - Cons: still no live mic.
- **Option E3 — File + synth + live mic (ALSA/JACK input).**
  - Pros: full input matrix.
  - Cons: scope creep before Dante I/O is verified; mic feedback risks during lab demos.
- **Recommendation: E2 in P0–P5 bring-up, then add live mic at P6 (after Dante I/O is verified at P4).** Phased.

#### (f) Audio I/O abstraction layer
- **Option F1 — JUCE `juce::AudioIODeviceType::createAudioIODeviceType_JACK()` exclusively (PipeWire-JACK on modern Ubuntu).**
  - Pros: low latency; routes to Dante via JACK ports; spec R12 named JACK.
  - Cons: JACK service must be running; PipeWire-JACK vs JACK1/JACK2 install-time choice.
- **Option F2 — JUCE `createAudioIODeviceType_ALSA()`.**
  - Pros: no JACK daemon; talks directly to ALSA PCM.
  - Cons: harder to share Dante PCIe with other clients; xrun recovery less robust than JACK.
- **Option F3 — Both, selectable at config.**
  - Pros: optionality.
  - Cons: doubles test surface for marginal benefit in v0.
- **Recommendation: F1 (PipeWire-JACK) for v0 lab.** Pinned in `docs/lab_setup.md` (P0 deliverable). F2 retained as fallback; selectable via `configs/default.yaml::audio.backend`.

---

## Pre-Mortem (DELIBERATE)

Three failure scenarios, each genuinely plausible for *this* C++/JUCE + PySide6 + OSC + Dante PCIe design. Each names what goes wrong concretely, observable diagnostic signals, and the in-plan mitigation that should have caught it.

### Scenario A — FDN denormal-induced CPU spike at idle
- **Concrete failure**: Eight objects are loaded but no audio is playing into them (all sources at silence). After ~30 seconds the audio thread's per-block compute time spikes from ~0.4 ms to ~3.0 ms; xrun counter starts ticking; user reports "silence is louder than sound." `htop` shows the audio thread pegged.
- **Why it happens**: FDN delay lines hold residual energy from a previous transient; in the absence of new excitation, samples decay exponentially toward zero and pass through the IEEE-754 denormal range (|x| < ~1e-38). On x86 without flush-to-zero (FTZ) / denormals-are-zero (DAZ), denormal arithmetic is 50–200× slower than normal float math. 16 delay lines × N samples × per-line one-pole filter × per-block FFT-domain mixing = compute cliff.
- **Diagnostic signals**: per-block-time histogram (P11 observability) shows bimodal tail; `audio_underrun_count` rises monotonically when no input is playing; `perf` shows >40% time in `_mm_mul_ss` on the audio thread despite zero observable amplitude.
- **In-plan mitigation**: ADR 0004 specifies (i) `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)` + `_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON)` set on every audio-thread entry (JUCE provides `juce::ScopedNoDenormals`), (ii) per-line DC offset injection of ±1e-20 to keep state away from denormal range, (iii) **P11 soak harness** explicitly runs an *idle-with-tails* scenario (load FDN, feed an impulse, then silence for 30 min) and gates on per-block time **p99 < `block_size × 0.7 = 933 µs` at 64 frames @ 48 kHz** (C2 / A1 corrected). Catches the regression before it ships.

### Scenario B — Layout/algorithm mismatch silently produces wrong panning
- **Concrete failure**: An engineer authors `configs/lab_5ch_irregular.yaml` (5 speakers, irregular Cartesian) and assigns `algorithm: WFS` to an object. The engine starts; no error is thrown; audio plays. P3.5 perceptual smoke is skipped because "the engine works." Two weeks later P12 listener-blind formal test reports "sources are everywhere, nowhere, and nothing localizes." Engineering team chases ghost bugs in `WFSRenderer::processBlock` for a week before realizing WFS requires a regular planar array (line/grid) and a 5-speaker irregular layout breaks the WFS spatial-aliasing assumption catastrophically.
- **Why it happens**: WFS's underlying physics (Huygens principle on a continuous secondary-source distribution) produces correct localization only when the synthesis array is regular (linear, circular, or planar grid) with inter-speaker spacing `< c/(2·f_max)` (M3 corrected statement). Applied to an irregular array, WFS still produces *some* output, but the wavefronts add chaotically. VBAP and DBAP, by contrast, work on any layout (with quality degradation, but not catastrophic failure). The engine's `LayoutCompatibilityChecker` is the only line of defense.
- **Diagnostic signals**: P9 rE/rV harness for the `(lab_5ch_irregular, WFS)` pair would show |rE| collapse to <0.4 across most of the grid; P3.5 listener-blind test on this geometry would show >50% direction-confusion.
- **In-plan mitigation**: P3 implements `LayoutCompatibilityChecker::validate(layout, algorithm) -> Result<void, IncompatibilityReason>` called at config-load time *before* any audio starts. Rules table: `WFS` requires `layout.regularity ∈ {LINEAR, CIRCULAR, PLANAR_GRID}` AND `layout.maxSpacing_m < c/(2·f_max)` (i.e., 21.4 mm at f_max=8 kHz); `VBAP` requires `layout.dimensionality ∈ {2D_HORIZONTAL, 3D}` AND `layout.speaker_count ≥ 3`; `DBAP` requires `layout.speaker_count ≥ 2`. Failed validation produces a UI error dialog naming the offending pair AND refuses to start the audio thread. Test: P3 unit test feeds 6 known-bad pairs and asserts each is rejected with the correct reason; 6 known-good pairs and asserts each accepts.

### Scenario C — OSC packet reordering / coalescing bug causes position jitter under fast drag
- **Concrete failure**: User drags one object rapidly across the screen; perceived position appears to "stutter" — front, then briefly back, then front again — even though the visual UI shows a smooth path. Listeners report "sources jump backward when I move them fast."
- **Why it happens**: UDP doesn't guarantee ordering. UI sends `/obj/0/pos az=0.0 t=10ms`, then `/obj/0/pos az=0.5 t=20ms`, then `/obj/0/pos az=1.0 t=30ms`. If the kernel reorders them on loopback (rare on localhost but documented under load), the engine applies them in the wrong order. Worse: under sustained drag, the UI's coalescing (one packet per object per 8.3 ms) is meant to drop the *oldest* per-object pending packet; if the coalescer instead drops the *newest* due to a bug, the engine receives stale positions.
- **Diagnostic signals**: P5-a load test (8-object 120 Hz drag, 1 h soak) shows a per-object `applied_seq` counter that goes backward in any window; P3.5 listener-blind smoke does not catch this (the test directions are static); P9 rE/rV does not catch this (dynamic, not static); only the soak + a per-object position-monotonicity invariant + the UI-side coalescer correctness test (Architect-rec-B / Critic-MA #4.c, applied) catches it.
- **In-plan mitigation**: Each `/obj/{id}/pos` carries a sequence number `seq` (u32, monotonic per object). Engine maintains `last_applied_seq[id]`; if incoming `seq <= last_applied_seq[id]`, the packet is dropped (last-write-wins by sequence, not by arrival time) and `osc_reordered_drops` counter increments. **Operator-visible alert (Critic MA #4.d)**: when `osc_reordered_drops` increases by ≥ 5 in any 1 s window, control thread emits `/sys/warning ,iis schema_version "osc_reorder_burst" "{count_in_last_window}"`; UI shows yellow status indicator. P5 verification adds a property test: send 1000 `/obj/0/pos` packets with sequence numbers in deliberate scrambled order; assert engine applies the highest-seq value as final state and `osc_reordered_drops` reports the correct count. **UI-side coalescer correctness test** (`ui/tests/test_drag_coalescer.py`): asserts (i) drops oldest pending per object, (ii) sustained 120 Hz output rate ± 10% over 1 s sustained drag, (iii) monotonic `seq` per object in coalescer output. P11 soak monitors `osc_reordered_drops` non-zero as a yellow flag.

---

## Architecture Overview

### Process model
**Two processes** (per spec R13 lock + ADR 0001):

```
┌─────────────────────────────────────────────────────────────────┐
│ Process B: PySide6 UI (Python; throw-away v0)                   │
│  - Qt main thread (top-down + 3D speaker view, drag, panels)    │
│  - IPC client thread (python-osc; rate-limit drag at 120 Hz)    │
│  - State mirror (ObjectModel, GeometryModel, MatrixModel)       │
│  - NoiseGenerator panel, Matrix read-only view, scene mgmt      │
└────────────────────────────┬────────────────────────────────────┘
                             │ OSC over UDP (loopback or LAN)
                             │ default ports: 9100 cmd, 9101 state
                             │ overrides: --osc-cmd-port / --osc-state-port
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│ Process A: Native Audio Core (C++ + JUCE; production-grade v0)  │
│                                                                 │
│  ┌───────────────────────┐  ┌─────────────────────────────┐     │
│  │ Audio Thread (RT)     │◄─│ juce::AbstractFifo (SPSC)   │     │
│  │  juce::AudioIODevice  │  │ Command queue (control→audio│     │
│  │   callback            │  └────────────▲────────────────┘     │
│  │  AudioMatrix routing  │               │                      │
│  │  Per-object DSP chain │  ┌────────────┴────────────────┐     │
│  │   × 8+ objects:       │  │ Control Thread              │     │
│  │   EQ→Delay→Pan→       │  │  juce::OSCReceiver (cmd)    │     │
│  │   distGain→HF LPF→    │  │  juce::OSCSender (state)    │     │
│  │   propDelay→reverbSend│  │  CommandDecoder             │     │
│  │  FDNReverb            │  │  StateModel owner           │     │
│  │  OutputBackend.render │  │  HeartbeatPublisher (10 Hz) │     │
│  │   ├─ SpeakerArray     │  │  GeometryLoader             │     │
│  │   └─ BinauralMonitor  │  │  LayoutCompatibilityChecker │     │
│  └──────┬───────┬────────┘  └────────────▲────────────────┘     │
│         │       │                        │                      │
│         │       │                        │ (config files)       │
│         ▼       ▼                        │                      │
│       Dante  KEMAR-HRTF              configs/*.yaml             │
│       (4–8ch via SOFA via             {lab_4ch, lab_8ch,        │
│        ALP-Dante     juce::dsp::      noise_gen, matrix,        │
│        PCIe via      Convolution      reverb, fdn_topology}     │
│        JACK)                                                    │
│                                                                 │
│  ┌───────────────────────┐  ┌─────────────────────────────┐     │
│  │ Decoder Pool (N thrds)│  │ NoiseGenerator              │     │
│  │  one decoder per file │  │  (white/pink, direct→matrix)│     │
│  │  source; mlock buffer │  └─────────────────────────────┘     │
│  │  fills per-object FIFO│                                      │
│  └───────────────────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

### Thread model (Process A — JUCE host)
Threads are explicit; cross-thread crossings use `juce::AbstractFifo` SPSC ring of fixed-size POD `Command` records.

| Thread | JUCE class / mechanism | Responsibilities | Allocates? |
|--------|------------------------|------------------|------------|
| **Audio thread (RT)** | `juce::AudioIODeviceCallback::audioDeviceIOCallbackWithContext` | drain Command FIFO; run AudioMatrix routing; per-object DSP chain ×N; FDNReverb; OutputBackend.render | NO (`juce::ScopedNoDenormals` + pre-allocated everything; FTZ/DAZ on entry) |
| **Control thread** | dedicated `juce::Thread` | OSC receive (juce::OSCReceiver listener callback runs here); decode bytes → `Command`; push to FIFO; receive `/sys/state` from StateModel and send via juce::OSCSender at **10 Hz heartbeat (C6)**; LayoutCompatibilityChecker; geometry YAML reload; emits `/sys/heartbeat_miss` and `/sys/warning` on operator-visible events | YES (control-path only) |
| **Decoder pool (N threads)** | `juce::Thread` per active file source (or pool of `min(N_objects, hw_threads/2)`) | open file (libsndfile via `juce::AudioFormatReader`); mlock decoded sample buffer; produce into per-object `juce::AbstractFifo` of f32 frames | YES (decoder-path only) |
| **Message thread** | `juce::MessageManager` (default JUCE GUI thread; here: command-line tool with no GUI but kept alive for `juce::OSCReceiver` listeners that JUCE attaches to MessageManager by default) | optionally hosts juce::OSCReceiver listener callbacks; we explicitly redirect listeners to the named control thread via `juce::OSCReceiver::registerFormatErrorHandler` + custom dispatch to avoid coupling to MessageManager | low |

**Audio-thread isolation rules** (Principle 1, codified):
- Pre-allocate at `prepareToPlay(sampleRate, maxBlockSize=512)`: all per-object scratch buffers (`juce::AudioBuffer<float>` of `MAX_BLOCK = 512` × max-channel-count); all `juce::dsp::ProcessorChain` nodes (4-band EQ, delay, pan, distance gain, HF LPF, propagation delay, reverb send); FDN delay lines; OutputBackend channel buffers; **per-algorithm SoA scratch arrays** (M5 / ADR 0005 follow-up: VBAP/WFS/DBAP each pre-allocate flat arrays sized `MAX_OBJECTS`, indexed by object slot); **algorithm-swap parallel-run scratch** (ADR 0006: each Object slot can simultaneously hold "old" + "new" algorithm scratch during a K=256-sample crossfade).
- `MAX_OBJECTS = 16` (twice spec's 8 hard requirement; pre-allocated even when unused, marked active/inactive).
- `MAX_BLOCK = 512` frames; if `juce::AudioIODevice` reports a buffer size > `MAX_BLOCK` at startup, refuse to start with explicit `BackendError::BlockSizeExceedsMax`.
- No `juce::String` construction on the audio thread (logging uses pre-allocated `juce::AbstractFifo<TraceEvent>` ring drained by control thread).
- No `juce::ScopedLock`, no `std::mutex`. Crossings are SPSC FIFOs only.
- Denormal flush: `juce::ScopedNoDenormals` on every audio callback entry (Pre-mortem A mitigation).
- Per-object position interpolation: per-sample linear ramp of pan-gain across the audio block on `Command::ObjectUpdate` apply, eliminating clicks on discrete jumps (carried over technique from archived plan; spec acceptance: "no zipper/click artifacts on rapid object movement", "Position interpolation: per-sample linear gain ramp across audio block").

### Module breakdown (C++, namespace `spe::` inside `core/`)

```
core/                                      # CMake-managed C++ project (JUCE module-based)
├── CMakeLists.txt
├── JuceLibraryCode/                       # auto-generated by JUCE CMake
└── src/
    ├── audio_io/
    │   ├── AudioDeviceManager.h           # juce::AudioDeviceManager wrapper
    │   ├── DanteBackend.h                 # config-driven JACK device pick (Digigram ALP-Dante)
    │   └── NullBackend.h                  # offline rendering (CI null-audio mode)
    ├── core/
    │   ├── SpatialEngine.h                # top-level orchestrator
    │   ├── Object.h                       # id, position(az,el,dist), gain, algorithm_id (atomic), EQ params, delay, distance model
    │   ├── ObjectFrame.h                  # SOA struct passed to RenderingAlgorithm
    │   ├── AlgorithmSwapState.h           # ADR 0006: per-object {old_algo, new_algo, crossfade_pos, crossfade_K=256}
    │   └── Constants.h                    # MAX_OBJECTS=16, MAX_BLOCK=512, SOUND_C=343.0, ALGO_SWAP_K=256
    ├── dsp/
    │   ├── EQ4Band.h                      # juce::dsp::IIR cascade
    │   ├── DelayLine.h                    # juce::dsp::DelayLine (per-object insert)
    │   ├── DistanceGain.h                 # 1/r or configurable rolloff with floor
    │   ├── DistanceLPF.h                  # 1-pole LPF, fc ∝ 1/r
    │   ├── PropagationDelay.h             # fractional delay (Lagrange or Thiran) + smoothing
    │   ├── GainRamp.h                     # per-sample linear ramp on ObjectUpdate
    │   └── PerObjectChain.h               # composed processor chain
    ├── render/
    │   ├── RenderingAlgorithm.h           # abstract base
    │   ├── VBAPRenderer.h                 # 2D / 3D VBAP, triangulation pre-computed at config-load
    │   ├── WFSRenderer.h                  # delay+gain per speaker for plane-wave / point-source synthesis
    │   ├── DBAPRenderer.h                 # distance-based amplitude panning, all speakers contribute
    │   ├── LayoutCompatibilityChecker.h   # validates (layout, algorithm) pairs (Pre-mortem B mitigation)
    │   └── AlgorithmAnalyticReference.h   # C7: closed-form Pulkki-1997 (VBAP) + Lossius-2009 (DBAP) gain math, NOT engine code
    ├── reverb/
    │   ├── ReverbEngine.h                 # abstract base; block process(), getLatencySamples(), capability traits
    │   ├── FDNReverb.h                    # 16-line Hadamard FDN with per-line tone control (ADR 0004)
    │   ├── IRConvolutionStub.h            # v1+ slot, compiles, returns silence (acceptance #5)
    │   └── SupportsIRLoading.h            # capability trait for IR loading
    ├── output_backend/
    │   ├── OutputBackend.h                # abstract: channel_count(), prepare(), render(), shutdown()
    │   ├── SpeakerArray.h                 # 4–8ch via Dante (sum of object renders)
    │   └── BinauralMonitor.h              # KEMAR SOFA HRTF convolution, simultaneous side-output
    ├── matrix/
    │   ├── AudioMatrix.h                  # input-ch → object-slot, rendered-ch → physical-Dante-out
    │   └── MatrixConfig.h                 # YAML/JSON schema mirror
    ├── ipc/
    │   ├── ExternalControl.h              # abstract: dispatch(Command)
    │   ├── OSCBackend.h                   # juce::OSCReceiver/Sender + CommandDecoder + StateModel publisher
    │   ├── Command.h                      # POD enum-tagged variant; THE ONLY audio-thread crossing type
    │   ├── CommandDecoder.h               # OSC byte → Command on control thread
    │   ├── StateModel.h                   # control-thread-owned authoritative state; mirrored to UI via heartbeat
    │   ├── HeartbeatPublisher.h           # 10 Hz lossy publish of /sys/state (C6)
    │   ├── HeartbeatMonitor.h             # detects 3+ consecutive missed beats (300 ms); emits /sys/heartbeat_miss
    │   ├── ProtocolVersion.h              # schema_version u16 + handshake
    │   └── VST3ControlStub.h              # C-MA #6.f: empty ExternalControl impl proves abstraction shape (callable from v1+)
    ├── input/
    │   ├── AudioInput.h                   # abstract: pull(N frames) into per-object FIFO
    │   ├── FileInput.h                    # libsndfile via juce::AudioFormatReader; decoder thread; mlock
    │   ├── SynthInput.h                   # offline-rendered test tones, played as files in v0
    │   └── LiveMicInput.h                 # ALSA capture via juce::AudioIODevice input channel (added at P6)
    ├── coords/
    │   ├── Coords.h                       # explicit (frame, sign) conversions; the ONLY place sign-flips happen
    │   └── CoordsTests.h                  # hand-computed expected values for every (frame, sign) pair
    ├── geometry/
    │   ├── SpeakerLayout.h                # Cartesian XYZ + channel map; YAML loader; regularity classification
    │   └── LayoutLoader.h                 # parses configs/lab_*.yaml
    ├── util/
    │   ├── NoiseGenerator.h               # white/pink + per-channel gain, direct-to-matrix
    │   ├── XrunCounter.h                  # exposed via /sys/xruns
    │   ├── ClickDetectorFFT.h             # C8: FFT spectral spike detector around discontinuity events (test build only)
    │   └── TraceRing.h                    # lock-free SPSC of TraceEvent, drained by control thread
    └── bin/
        └── spatial_engine_core.cpp        # JUCE console app entry; instantiates SpatialEngine; runs forever
```

### Module breakdown (Python, package `spatial_engine_ui/` inside `ui/`)

```
ui/
├── pyproject.toml                         # uv-managed; deps: pyside6, python-osc, numpy
└── spatial_engine_ui/
    ├── __init__.py
    ├── app.py                             # QApplication entry; CLI: --osc-cmd-port, --osc-state-port
    ├── ipc/
    │   ├── osc_client.py                  # python-osc client; rate-limit; coalesce; sequence numbers
    │   ├── protocol.py                    # mirror of proto/ipc_schema.md; schema_version constant
    │   └── state_listener.py              # juce-state receiver in a QThread; signals to main thread
    ├── state/
    │   ├── object_model.py                # per-object: pos, gain, EQ, delay, dist, algorithm
    │   ├── geometry_model.py              # mirrors /sys/geometry response
    │   └── matrix_model.py                # mirrors /sys/matrix response (read-only v0)
    ├── views/
    │   ├── topdown.py                     # QGraphicsView of speakers + objects (Cartesian XY)
    │   ├── view3d.py                      # Q3DScatter via QtDataVisualization (or simpler 3D view)
    │   ├── matrix_view.py                 # QTableView read-only mirror of AudioMatrix (acceptance #9)
    │   ├── noise_panel.py                 # NoiseGenerator panel (white/pink + per-channel gain)
    │   ├── object_panels.py               # per-object: pos sliders, gain, EQ, delay, distance model
    │   └── status_indicator.py            # yellow flag on /sys/warning osc_reorder_burst (Pre-mortem C alert)
    ├── controllers/
    │   └── drag.py                        # mouse → (az, dist); 120 Hz rate-limit; coalesce per-object
    └── scene/
        └── scene_io.py                    # save/load object positions to JSON
```

```
ui/tests/                                  # pytest
├── test_drag_coalescer.py                 # Architect-rec-B / C-MA #4.c: drops-oldest, sustained 120 Hz, monotonic seq
├── test_matrix_view_sync.py               # mirror correctness vs /sys/matrix payload
└── test_protocol_version.py               # handshake mismatch dialog
```

### Repo layout (top-level)

```
spatial_engine/
├── core/                                  # C++ JUCE audio core (above)
├── ui/                                    # Python PySide6 UI (above)
├── proto/                                 # shared schema artifacts (language-agnostic)
│   ├── ipc_schema.md                      # Command schema + schema_version + handshake
│   ├── command_table.json                 # machine-readable mirror (used by tests + future codegen)
│   ├── geometry_schema.json               # JSON-schema for configs/lab_*.yaml
│   ├── matrix_schema.json                 # JSON-schema for configs/matrix.yaml
│   └── trajectory_schema.json             # mirrors vid2spatial format [{t, az_deg, el_deg, dist_m}]
├── configs/
│   ├── lab_4ch.yaml                       # 4 speakers, horizontal, regular ring or rectangle
│   ├── lab_8ch.yaml                       # 8 speakers, horizontal + 2 elevated rings; WFS-compatible variant
│   ├── lab_8ch_irregular.yaml             # 8 speakers, irregular (DBAP-only — Pre-mortem B negative test)
│   ├── matrix.yaml                        # AudioMatrix routing
│   ├── reverb_fdn.yaml                    # 16 lines, Hadamard, RT60 ranges
│   ├── binaural.yaml                      # KEMAR SOFA path: /home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa
│   ├── noise_gen.yaml                     # default white/pink params
│   └── default.yaml                       # selects audio.backend=jack, layout=lab_8ch, etc.
├── tests/
│   ├── core_unit/                         # CMake CTest; runs in null-audio mode
│   ├── e2e/                               # spawn core+UI; OSC roundtrips; pytest
│   ├── latency_harness/                   # impulse-injected onset measurement (P10)
│   ├── soak_harness/                      # multi-hour Xrun + RSS sampling (P11)
│   ├── accuracy_harness/                  # rE/rV vs analytic-reference; per layout (P9)
│   ├── compat_harness/                    # LayoutCompatibilityChecker red/green pair tests (P3)
│   └── perceptual/
│       ├── p3_5_smoke/                    # listener-blind 4-direction (technique from archive)
│       └── listening_test_v0/             # P12 perceptual sign-off, N=12 pre-registered
│           └── preregistration.md         # M2: filed before any data collection
├── tools/
│   ├── render_test_signals.py             # offline content (sine, noise, transients, music)
│   ├── osc_debug_console.py               # stdin → OSC; /sys/state → stdout; works against any OSC core
│   ├── matrix_yaml_validator.py           # validates configs/matrix.yaml against schema
│   └── sofa_inspector.py                  # C-MA #6.e / Risk #14: print SR/IR-length/measurement-count of KEMAR SOFA
├── docs/
│   ├── architecture.md
│   ├── ipc_schema.md                      # human-readable Command schema
│   ├── coordinate_convention.md           # every (frame, sign) pair, hand-computed
│   ├── lab_setup.md                       # PREEMPT_RT + Ubuntu LTS + Digigram driver pinning (A3 / P0)
│   ├── license_procurement_plan.md        # C5: JUCE Indie/Pro trigger + procurement plan
│   ├── onboarding.md                      # bootstrap + ephemeral-port discovery + lab gotchas
│   ├── onboarding_timing.md               # measured fresh-clone bringup time (P12 artifact)
│   ├── latency_budget.md                  # term-by-term p99 decomposition (this plan §Latency Budget); BinauralMonitor sub-budget asterisk (A2)
│   ├── ui_modification_examples.md        # three concrete UI-only modifications (acceptance #10)
│   ├── 3u_rack_constraints.md             # acceptance #16 documentation
│   └── adr/
│       ├── 0001-process-model.md
│       ├── 0002-native-core-cpp-juce.md
│       ├── 0003-ipc-osc-udp.md
│       ├── 0004-fdn-topology.md
│       ├── 0005-algorithm-dispatch.md
│       └── 0006-algorithm-runtime-swap.md  # NEW R2 (A4 / C4)
├── CMakeLists.txt                         # workspace root
├── pyproject.toml                         # uv root for ui/
├── justfile                               # just bootstrap / build / run / soak / accuracy / latency
├── bootstrap.sh                           # non-interactive: apt deps, JUCE submodule, cmake build, uv sync, pre-commit
├── .pre-commit-config.yaml                # clang-format, clang-tidy, ruff, pyright, yamllint
├── .gitignore
├── LICENSE.md                             # GPL v3 for v0 (JUCE GPL clause); contributor reminder per C5
└── README.md
```

### Build / dev tooling
- **C++**: CMake ≥3.20; JUCE 7.x as git submodule under `core/JUCE/`; `find_package(juce)` via JUCE's CMake integration; pinned compiler = `gcc-11` or `clang-15`+ (via `bootstrap.sh`); `assert_no_alloc`-equivalent: a custom `RT_ASSERT_NO_ALLOC` macro that overrides `operator new` to abort during a scoped audio-thread call (test build only; uses `juce::AbstractFifo`-style sentinel). **C8 click-detector**: `ClickDetectorFFT` (`util/ClickDetectorFFT.h`) — test-build-only utility; FFT-derived spectral spike detection around discontinuity timestamps; threshold +10 dB above neighboring bins.
- **Python**: `uv` with `pyproject.toml`; `uv.lock` committed.
- **Task runner**: `justfile`: `just bootstrap`, `just build`, `just run`, `just test`, `just soak`, `just measure-latency`, `just accuracy`, `just compat`, `just sofa-inspect`.
- **Pre-commit**: `clang-format`, `clang-tidy --warnings-as-errors=performance-*,bugprone-*,cppcoreguidelines-pro-type-*`, `ruff`, `pyright`, `yamllint`.
- **CI vs local-runner split**:
  - **CI (GitHub Actions Linux)**: unit tests (CTest), e2e tests using `NullBackend` (writes deterministic WAV to `/tmp` instead of opening a JACK device), accuracy harness (P9 rE/rV math vs analytic reference, no audio device), compat harness (P3, no audio device), Python pytest. Schema validation: `proto/command_table.json` schema is checked against `core/src/ipc/Command.h` enum at CI time via a small generated header check.
  - **Local runner only**: latency harness (P10), perceptual P3.5 / P12 (P12 perceptual), soak harness (P11) — these need real Dante hardware. PRs that touch real-audio paths require `local-verify-required` label with results posted by the lab-machine reviewer.

---

## Phased Plan (P0..P12; 13 phases)

Phases are ordered to build acceptance criteria incrementally — no acceptance criterion depends on a single late phase.

### P0 — Bootstrap & Lab Pinning (A3 first-class)
- **Goal**: Repo skeleton, `bootstrap.sh` non-interactive setup, lab machine pinning artifact `docs/lab_setup.md` written *before* any measurement phase. **A3 spike**: identify the actual kernel/distro combination Digigram ALP-Dante targets; pin it; decide PREEMPT_RT y/n at P0.
- **Deliverables**:
  - `CMakeLists.txt`, `core/CMakeLists.txt` skeleton with JUCE submodule, `ui/pyproject.toml`, `justfile`, `bootstrap.sh`, `.pre-commit-config.yaml`, `README.md`.
  - **`docs/lab_setup.md`** (A3 first-class deliverable): pins **specific Ubuntu LTS point release** (default disposition: Ubuntu 22.04 LTS unless Digigram's published Linux compatibility matrix indicates a different LTS); **specific kernel point version** with **PREEMPT_RT** (e.g., `linux-image-rt-amd64` from official PREEMPT_RT patchset; Linux 6.x generic preserved as fallback if Digigram's certified driver-kernel matrix forecloses RT for the chosen ALP-Dante driver version); **specific Digigram-certified ALSA driver version** (filed by P0 spike: contact Digigram support OR consult `getdante.com/product/alp-dante/` published Linux requirements; if unknown after the spike, document the gap and proceed with closest-feasible PREEMPT_RT-compatible kernel and file `digigram-driver-matrix-confirmation` issue to track resolution before P6 Dante I/O verification); `cpufreq=performance` governor; PipeWire-JACK pinned; JACK period = 64 frames @ 48 kHz target. Also includes contributor reminder: **v0 source is GPL-licensed; PRs must be GPL-compatible; commercial redistribution requires JUCE Indie/Pro license per C5**.
  - `tools/sofa_inspector.py` (C-MA #6.e / Risk #14): scripts that prints sample rate + IR length + measurement count of `/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa`. Result drives BinauralMonitor partition-strategy parameters (A2).
  - `docs/license_procurement_plan.md` (C5): JUCE Indie ($40–$130/mo) / Pro / Perpetual options; trigger event = first external distribution OR first commercial deployment outside research lab; named owner = project lead; cost estimate per developer × deployment count.
  - ADR 0001/0002/0003/0004/0005/0006 *first drafts* (filled by P5).
- **Files / modules**: top-level + `docs/lab_setup.md`, `docs/onboarding.md` skeleton, `docs/license_procurement_plan.md`, `tools/sofa_inspector.py`.
- **Dependencies**: none.
- **Exit criteria**: `just bootstrap` runs to completion non-interactively on a clean Ubuntu 22.04 box; `just build` succeeds with empty `main`; `ctest` and `pytest` produce zero tests but green; pre-commit hooks pass on no-op commit; `docs/lab_setup.md` reviewed (kernel + Digigram driver pinned, PREEMPT_RT decision recorded with rationale); `tools/sofa_inspector.py` runs against the real KEMAR file and prints metadata (recorded in `docs/lab_setup.md` under "BinauralMonitor sub-budget assumptions").
- **Spec acceptance criteria progress**: scaffolds #1 (Linux + Digigram driver pinned), #15 (IPC layer scaffolding), #16 (3U-compatible code constraints documented).

### P1 — Native Core Skeleton + AudioDeviceManager + NullBackend
- **Goal**: JUCE host process opens an audio device (PipeWire-JACK), runs callbacks at 64 frames @ 48 kHz, emits silence, exposes `XrunCounter`. NullBackend for CI.
- **Deliverables**: `core/src/audio_io/{AudioDeviceManager,DanteBackend,NullBackend}.{h,cpp}`, `core/src/core/SpatialEngine.{h,cpp}` (skeleton), `core/src/util/{XrunCounter,TraceRing,ClickDetectorFFT}.{h,cpp}`, `core/src/bin/spatial_engine_core.cpp`, `core/src/core/Constants.h` (MAX_OBJECTS=16, MAX_BLOCK=512, ALGO_SWAP_K=256), CTest unit test for `AbstractFifo` SPSC roundtrip, `RT_ASSERT_NO_ALLOC` macro.
- **Files / modules**: `audio_io/`, `core/SpatialEngine`, `util/XrunCounter`, `util/TraceRing`, `util/ClickDetectorFFT`.
- **Dependencies**: P0.
- **Exit criteria**: binary runs 60 s with 0 xruns on lab machine; `MAX_BLOCK` boundary test (force-set buffer to 1024, expect refusal); audio callback wrapped in `RT_ASSERT_NO_ALLOC` test reports zero allocations under steady silence.
- **Spec acceptance**: contributes to #1 (audio device + Dante), #11 (numerical performance gate scaffolding).

### P2 — Coordinate Convention Module + Geometry Loader
- **Goal**: `coords` namespace with explicit (frame, sign) helpers + every-pair hand-computed unit test; YAML geometry loader; `LayoutCompatibilityChecker` skeleton.
- **Deliverables**: `core/src/coords/{Coords,CoordsTests}.{h,cpp}`, `core/src/geometry/{SpeakerLayout,LayoutLoader}.{h,cpp}`, `core/src/render/LayoutCompatibilityChecker.{h,cpp}` (rules table only, no algorithms yet), `configs/lab_4ch.yaml`, `configs/lab_8ch.yaml`, `configs/lab_8ch_irregular.yaml`, `proto/geometry_schema.json`, `docs/coordinate_convention.md`.
- **Files / modules**: `coords/`, `geometry/`, `render/LayoutCompatibilityChecker`.
- **Dependencies**: P1.
- **Exit criteria**: every (frame, sign) pair test passes (pipeline RIGHT=+az; AmbiX/SOFA LEFT=+az; elevation UP=+el listener-frame; image-y-down → listener-frame; VBAP-frame audio-azimuth-from-front, RIGHT=+az, degrees in YAML); YAML loader rejects malformed configs with named errors; `LayoutCompatibilityChecker` rule table compiles and a placeholder `validate()` returns `Compatible` for any pair (algorithm-specific rules added in P3).
- **Spec acceptance**: contributes to #2 (layout config loads + compatibility check scaffold), #16 (coordinate convention documented).

### P3 — Per-Object DSP Chain + RenderingAlgorithm Trio + Layout Compatibility Rules + SoA Scratch + Algorithm-Swap Crossfade
- **Goal**: All three rendering algorithms implemented; per-object full DSP chain wired; LayoutCompatibilityChecker enforces real rules; SoA per-algorithm scratch (M5); algorithm runtime swap with K=256 crossfade (ADR 0006).
- **Deliverables**:
  - `core/src/dsp/{EQ4Band,DelayLine,DistanceGain,DistanceLPF,PropagationDelay,GainRamp,PerObjectChain}.{h,cpp}` — chain order per spec: Source → 4-Band EQ → Delay → Pan → distance gain → distance HF rolloff (1-pole LPF, fc = 22050 / (1 + dist_m * k_hf)) → distance propagation delay (fractional delay, Lagrange or `juce::dsp::DelayLine` with linear interp + `SmoothedValue` ramp) → reverb send.
  - `core/src/render/{RenderingAlgorithm,VBAPRenderer,WFSRenderer,DBAPRenderer,AlgorithmAnalyticReference}.{h,cpp}`.
    - **D1 + D3-friendly invariants (M5)**: each `RenderingAlgorithm` impl pre-allocates SoA scratch arrays sized `MAX_OBJECTS` at `prepareToPlay`. `engine->getObjectsByAlgorithm(Algorithm)` returns spans (no allocation). v0 audio-thread loop: `for (auto& obj : objects) obj.algorithm->processBlock(...)`. v1+ migration to D3 is a single-loop swap to `for (auto algo : algorithms) for (auto& obj : objects_by_algo[algo]) algo.processBlock(...)`.
    - **Algorithm runtime swap (ADR 0006 / A4)**: `Object` carries `std::atomic<AlgorithmId> algorithm_id`; control thread allocates new algorithm's scratch in non-RT, publishes pointer atomically, audio thread next block reads new id; **K=256 sample crossfade**: during the swap window (5.33 ms @ 48 kHz), audio thread runs **both** algorithms in parallel for that object and crossfades their outputs (linear ramp from 0→1 on new, 1→0 on old); old state freed by control thread next safe window.
  - VBAP: 2D for `lab_4ch`, 3D for `lab_8ch`; triangulation pre-computed at `prepareToPlay`; `juce::dsp::SmoothedValue<float, juce::dsp::SmoothedValueLinear>` per-speaker per-object for gain ramp.
  - WFS: per-speaker delay (r/c) + 1/√r gain (point-source) or planar-wave variant; spatial-aliasing edge-fade beyond `c/(2 * f_max * speaker_spacing)`.
  - DBAP: per-speaker amplitude `g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))` with rolloff `a = 1.5..2.0` config-tunable; all speakers contribute.
  - **`AlgorithmAnalyticReference` (C7)**: closed-form Pulkki-1997 VBAP gain (vector base amplitude panning, three-loudspeaker triangle) and Lossius-2009 DBAP rolloff math, **independent from engine code** — exists solely as the analytic baseline for P9's accuracy harness.
  - `LayoutCompatibilityChecker` real rules (Pre-mortem B mitigation): VBAP requires layout dimensionality ≥ 2D + ≥3 speakers; WFS requires layout regularity ∈ {LINEAR, CIRCULAR, PLANAR_GRID} AND `max_spacing < c/(2·f_max)` (M3 corrected); DBAP requires ≥2 speakers (always passes).
  - Per-object position interpolation: `GainRamp` does per-sample linear interpolation of pan-gain across the audio block on `Command::ObjectUpdate` apply (technique from archive).
- **Dependencies**: P2.
- **Exit criteria**:
  - VBAP unit test: place object at each speaker direction, assert closed-form gain match vs `AlgorithmAnalyticReference::vbap_gain()` (1e-6).
  - WFS unit test: place object behind a regular linear array, assert wavefront synthesis at sample level matches Huygens analytic formula (1e-3).
  - DBAP unit test: assert speaker gains sum-of-squares = 1.0 across all positions, AND match `AlgorithmAnalyticReference::dbap_gain()` Lossius-2009 formula (1e-6).
  - LayoutCompatibilityChecker: 6 known-bad pairs (`lab_8ch_irregular × WFS`, etc.) rejected with correct reason; 6 known-good pairs accepted; CI gate (`tests/compat_harness/`).
  - **C8 click test (3-part, applied to GainRamp)**: jump from front to right at mid-block; assert (i) first per-sample step ≤ `1.0/MAX_BLOCK`, (ii) cumulative gain at sample MAX_BLOCK matches target within 1e-6, (iii) **FFT of rendered block contains no spectral spike >10 dB above neighboring bins** (via `ClickDetectorFFT`).
  - **C8 click test applied to PropagationDelay sweep**: d sweeps 1→10 m over 1 s; assert no spectral spike >10 dB during the sweep; smoothing via `juce::SmoothedValue` target.
  - **C8 click test applied to algorithm swap (ADR 0006)**: spawn object with `algorithm=VBAP`; runtime change to `algorithm=DBAP` via `/obj/{id}/algorithm`; during K=256 crossfade window, assert (i) `RT_ASSERT_NO_ALLOC` reports zero allocations on audio thread, (ii) per-sample max `|Δgain|` ≤ `1/256` per speaker, (iii) FFT of swap-window block contains no spectral spike >10 dB above neighboring bins (the substance check).
  - Distance HF rolloff: at d=1 m, fc ≈ 22 kHz; at d=10 m, fc < 5 kHz (config-driven k_hf).
  - Propagation delay: at d=10 m, delay ≈ 29.155 ms; assert no zipper artifact when d sweeps 1→10 m over 1 s.
- **Spec acceptance**: #2 (all three algorithms + compat check), #3 (per-object full chain processes), #8 (fractional delay + smoothing, no zipper).

### P3.5 — Listener-Blind Perceptual Smoke
- **Goal**: Catch direction-flip regressions (L/R inversion class from MEMORY.md) before P9 numerical and P12 perceptual.
- **Deliverables**: `tests/perceptual/p3_5_smoke/script.md` (4-direction randomized: front 0°, right +90°, back 180°, left −90°; lab_8ch geometry; listener cannot see UI screen; written response on paper before operator reveal); `docs/p3_5_smoke.md` results log; technique imported from archive.
- **Dependencies**: P3.
- **Exit criteria**: both listeners (N=2: a non-operator + observer; if user participates, must be listener-only with screen blind) correctly localize all 4 directions independently. Failure halts and revisits `coords` + algorithm gain tables. **`docs/onboarding.md` flags P3.5 as non-skippable**.
- **Spec acceptance**: early-warning gate for #13 (positional accuracy).

### P4 — IPC Layer (OSC + Command Schema + Handshake) + StateModel + HeartbeatPublisher (10 Hz, C6) + VST3ControlStub
- **Goal**: Single Command schema; OSC over UDP (juce::OSCSender/Receiver ↔ python-osc); `schema_version` handshake; sequence-number reordering defense (Pre-mortem C); `assert_no_alloc` boundary test; **10 Hz heartbeat with 300 ms miss gate (C6)**; **VST3ControlStub proves abstraction shape (C-MA #6.f)**.
- **Deliverables**:
  - `core/src/ipc/{ExternalControl,OSCBackend,Command,CommandDecoder,StateModel,HeartbeatPublisher,HeartbeatMonitor,ProtocolVersion,VST3ControlStub}.{h,cpp}`.
  - `proto/ipc_schema.md` (every command + reply documented; schema_version=1).
  - `proto/command_table.json` (machine-readable; CI checks header sync).
  - Command schema (verbatim wire format below):

    ```
    UI → Core (commands; first arg always schema_version u16):
      /sys/protocol_version   ,i           schema_version
      /sys/subscribe          ,ii          schema_version client_state_port
      /sys/unsubscribe        ,ii          schema_version client_state_port
      /obj/{id}/spawn         ,iiisss      schema_version id source_kind source_path algorithm
                                            algorithm ∈ {"VBAP","WFS","DBAP"}
                                            source_kind ∈ {"file","synth","mic"}
      /obj/{id}/despawn       ,ii          schema_version id
      /obj/{id}/pos           ,iiifff      schema_version id seq az_rad el_rad dist_m
      /obj/{id}/gain          ,iif         schema_version id gain_lin
      /obj/{id}/algorithm     ,iis         schema_version id algorithm   # runtime change → ADR 0006 K=256 crossfade
      /obj/{id}/eq            ,iiffff…     schema_version id band1_freq Q gain  band2_… (4 bands)
      /obj/{id}/delay         ,iif         schema_version id delay_ms
      /obj/{id}/distance      ,iiff        schema_version id rolloff_a hf_k
      /obj/{id}/reverb_send   ,iif         schema_version id send_gain
      /sys/load_geometry      ,is          schema_version yaml_path
      /sys/load_matrix        ,is          schema_version yaml_path
      /sys/noise_gen          ,iisff       schema_version channel kind  amp_lin   # kind ∈ "white","pink","off"
      /sys/list_devices       ,i           schema_version
      /sys/get_geometry       ,i           schema_version
      /sys/get_matrix         ,i           schema_version
      /sys/scene_save         ,is          schema_version path
      /sys/scene_load         ,is          schema_version path

    Core → UI (state + replies):
      /sys/protocol_version   ,i           server_schema_version    (handshake reply)
      /sys/state              ,ib          schema_version <blob: object list summary> (10 Hz, lossy — C6)
      /sys/devices            ,ib          schema_version <blob>
      /sys/geometry           ,ib          schema_version <blob: SpeakerLayout>
      /sys/matrix             ,ib          schema_version <blob: AudioMatrix>
      /sys/ack                ,iiis        schema_version cmd_seq id_or_zero "ok"
      /sys/error              ,iis         schema_version cmd_seq message
      /sys/warning            ,iis         schema_version "{kind}" "{count_in_window}"   # Pre-mortem C alert (C-MA #4.d)
      /sys/heartbeat_miss     ,iis         schema_version "{stream}" "{count_in_window}" # C6: 3+ consecutive misses → 300 ms
      /sys/xruns              ,ii          schema_version count_since_start
      /sys/metrics            ,ib          schema_version <blob: per-block-time p99, ipc_queue_depth, cpu_pct>
    ```
  - **Heartbeat (C6 corrected)**: **10 Hz (100 ms period)** lossy publish (overwriting single-slot staging buffer; non-blocking `sendto`; `EAGAIN` = drop). **Miss threshold: 3 consecutive missed beats (300 ms)**; on miss, **control thread** (NOT audio thread) emits `/sys/heartbeat_miss` event. 10-second-gap retained as catastrophic-stall alarm only.
  - Sequence-number reordering defense (Pre-mortem C): per-object monotonic `seq`; engine maintains `last_applied_seq[id]`; reorder drops counted in `osc_reordered_drops`; **operator-visible alert (C-MA #4.d)**: when `osc_reordered_drops` increases by ≥ 5 in any 1 s window, control thread emits `/sys/warning ,iis schema_version "osc_reorder_burst" "{count}"`.
  - **`VST3ControlStub` (C-MA #6.f)**: empty `class VST3ControlStub : public ExternalControl` with `dispatch(Command)` no-op; v0 unit test loads the stub at the abstract base via factory and asserts no engine-code change required to register it. Compile-time `static_assert` on signature compliance.
- **Files / modules**: `ipc/`, `proto/{ipc_schema.md,command_table.json}`, `tools/osc_debug_console.py`.
- **Dependencies**: P3.
- **Exit criteria**:
  - `osc_debug_console.py` spawns 2 objects, drives one across 360° via `/obj/0/pos`, observes `/sys/state` heartbeat at 10 Hz, sees xrun counter == 0.
  - Handshake test: UI sends mismatched schema_version, core replies `/sys/error`; UI refuses to send commands.
  - **Boundary test split (technique from archive)**: (P4-a) **valid-packet drain saturation** — flood `/obj/{id}/pos` at 10 kHz across 8 active objects for 60 s, force max FIFO fill, audio thread RT_ASSERT_NO_ALLOC-clean, zero xruns. (P4-b) **malformed-packet flood** — send 1 kHz of malformed 9 KB OSC packets for 60 s, control thread reject path doesn't propagate, audio thread RT_ASSERT_NO_ALLOC-clean.
  - **Reorder defense test (Pre-mortem C)**: send 1000 `/obj/0/pos` with deliberately scrambled `seq`; assert highest-seq is final state; `osc_reordered_drops` matches expected. **Alert burst test**: inject 10 reordered packets in 200 ms; assert `/sys/warning osc_reorder_burst` emitted within next 1 s window.
  - **Heartbeat miss test (C6)**: kill control-thread heartbeat publisher for 350 ms; assert `/sys/heartbeat_miss` emitted with count=3+; assert audio thread did not block.
  - **VST3ControlStub registration test (C-MA #6.f)**: factory loads stub, registers as second `ExternalControl`, sends command via stub, assert engine state unchanged but dispatch path compiled cleanly.
  - ADR 0003 finalized in this phase; ADR 0006 specifies the IPC side of algorithm swap.
- **Spec acceptance**: #6 (OutputBackend handles commands), #7 (ExternalControl + OSCBackend + VST3 slot callable), #15 (IPC layer documented).

### P5 — File AudioInput + Synth Test Signals + AudioMatrix
- **Goal**: Multi-source playback with file-based AudioInput + offline synth bank; AudioMatrix DSP routing v0 core.
- **Deliverables**:
  - `core/src/input/{AudioInput,FileInput,SynthInput}.{h,cpp}` — per-object decoder thread; mlock decoded buffer; per-object `juce::AbstractFifo<float>` of decoded frames; audio thread reads non-blocking; underflow → silence + `audio_underrun_count.per_object[id]++`.
  - `core/src/matrix/{AudioMatrix,MatrixConfig}.{h,cpp}` — input-channel→object-slot routing AND rendered-channel→Dante-output routing, sample-path; runtime change via `/sys/load_matrix` with per-channel `juce::SmoothedValue` crossfade (Open Q #5 resolution: hot-swap with crossfade, not stop-reload-restart).
  - `core/src/util/NoiseGenerator.{h,cpp}` — white/pink + per-channel gain, direct-to-matrix bypass.
  - `tools/render_test_signals.py` — sine 440/1000/4000/10000 Hz, white/pink noise, transient clicks (for latency harness), broadband music excerpt.
  - `configs/matrix.yaml`, `configs/noise_gen.yaml`.
- **Dependencies**: P4.
- **Exit criteria**: 8 file-source objects, each panned to fixed positions, captured to multi-channel WAV via `NullBackend`; assert 8 distinct sources audible per channel via energy analysis. Decoder underflow test: pause decoder thread, assert audio thread emits silence + per-object underrun counter increments, no xrun, no panic. AudioMatrix runtime reload (`/sys/load_matrix matrix_alt.yaml`) routing change applied within 1 block. **C8 click test on matrix reload**: capture FFT of output during reload window; assert no spectral spike >10 dB.
- **Spec acceptance**: #1 (4–8ch routing), #3 (8 simultaneous objects), partial #8 (matrix routing core).

### P6 — Dante I/O Verification + Live Mic
- **Goal**: Verify Digigram ALP-Dante PCIe roundtrip; add live-mic AudioInput (now that Dante is up).
- **Deliverables**: `core/src/audio_io/DanteBackend.{h,cpp}` final (JACK port discovery → Dante channels), `core/src/input/LiveMicInput.{h,cpp}`, `docs/lab_setup.md` updated with **measured Dante driver version + JACK config snapshot + PREEMPT_RT verification (matches A3 P0 pinning)**, `tests/dante_loopback/` (impulse out → physical loopback cable → impulse in; verify channel mapping).
- **Dependencies**: P5.
- **Exit criteria**: 8-channel impulse-out captured with correct channel order on Dante; live mic captures 1 ch and routes to object 0; soak Dante for 30 min, 0 xruns. **Validation gate**: kernel + Digigram driver actually match the P0-pinned versions; if not, file `kernel-driver-deviation` issue and update `docs/lab_setup.md` before P10.
- **Spec acceptance**: #1 (Dante PCIe + 4–8ch).

### P7 — FDN Reverb + IRConvolution Stub (with metadata validation)
- **Goal**: 16-line Hadamard FDN with per-line tone control + frequency-dependent T60; per-object send; `IRConvolutionStub` v1+ slot **with format validation** (Critic §8.5 / C-MA #3 reinforced).
- **Deliverables**:
  - `core/src/reverb/{ReverbEngine,FDNReverb,IRConvolutionStub,SupportsIRLoading}.{h,cpp}`.
  - FDNReverb: 16 delay lines (lengths chosen as mutually prime to maximize echo density: e.g., {1499, 1693, 1801, 1933, 2069, 2207, 2347, 2477, 2647, 2789, 2917, 3079, 3203, 3361, 3491, 3613} samples @ 48 kHz; tunable from `configs/reverb_fdn.yaml`); Hadamard mixing matrix (4 cascaded butterfly stages, branchless); per-line one-pole LPF for HF damping (T60 frequency-dependent); per-line gain for global RT60 control via `g_i = 10^(-3 × m_i / (T60 × fs))`.
  - Per-object reverb send bus: send-gain curve `s(d) = s_min + (s_max - s_min) × min(1, d/d_max)`; sums into the FDN input bus; FDN output sums back into the matrix output bus.
  - **Per-object send unit test gate (Critic §1 row #4)**: assert that `s(d)` is monotonic non-decreasing in `d` per object, and changing `s_max` runtime via `/obj/{id}/reverb_send` produces measurable output amplitude change ≥ 1 dB at the FDN bus over the reverb-tail energy.
  - `juce::ScopedNoDenormals` + per-line ±1e-20 DC offset injection (Pre-mortem A mitigation).
  - **`IRConvolutionStub` with metadata validation (C-MA #3)**: implements `ReverbEngine::process()` returning silence; implements `SupportsIRLoading::loadImpulseResponse(path)` with **strict metadata validation** — checks `sample_rate == engine.sampleRate`, `channel_count == engine.expectedIRChannels`, `length_samples >= MIN_IR_LEN && length_samples <= MAX_IR_LEN`; returns enumerated error variants `IRSampleRateMismatch` / `IRChannelCountMismatch` / `IRLengthOutOfRange` on bad headers; v0 unit test feeds (a) a 44.1 kHz / 2-channel WAV and asserts `IRSampleRateMismatch`, (b) an 8-channel WAV and asserts `IRChannelCountMismatch`, (c) a 48 kHz / 1-channel matching WAV and asserts no error (no actual convolution; stub still returns silence).
- **Dependencies**: P5.
- **Exit criteria**:
  - FDN unit test: feed an impulse, capture 5 s of decay, fit RT60 via energy decay curve (Schroeder integration), assert measured RT60 within ±10% of `configs/reverb_fdn.yaml::rt60`.
  - Distance-dependent reverb send: drive object from d=1 m to d=10 m over 5 s, capture FDN output, assert send-gain envelope matches `s(d)` curve. Per-object send gate (above).
  - Denormal soak (Pre-mortem A): impulse + 30 min silence, assert per-block-time p99 stays < `block_size × 0.7 = 933 µs` throughout (C2/A1 corrected).
  - `IRConvolutionStub` with format validation: 3-test suite above passes; loading a valid path doesn't crash; loading a bad path returns named error variant.
- **Spec acceptance**: #4 (FDN reverb + distance-dependent send + per-object gate), #5 (ReverbEngine abstraction + IR slot + 4 hooks; metadata validation proves the abstraction shape).

### P8 — UI: PySide6 Skeleton + IPC + Top-Down View + Drag + Matrix Read-Only View + Noise Generator Panel
- **Goal**: Minimal v0 UI satisfying acceptance #9 (drag 1–4 objects, top-down/3D view, matrix read-only view, NoiseGenerator panel, parameter inspectors).
- **Deliverables**:
  - `ui/spatial_engine_ui/app.py` (CLI flags, QApplication entry).
  - `ui/spatial_engine_ui/ipc/{osc_client,protocol,state_listener}.py`.
  - `ui/spatial_engine_ui/state/{object_model,geometry_model,matrix_model}.py`.
  - `ui/spatial_engine_ui/views/{topdown,view3d,matrix_view,noise_panel,object_panels,status_indicator}.py`.
  - `ui/spatial_engine_ui/controllers/drag.py` (120 Hz rate-limit, last-write-wins coalesce per object).
  - `ui/spatial_engine_ui/scene/scene_io.py` (scene save/load).
  - `ui/tests/test_drag_coalescer.py` (Architect-rec-B / C-MA #4.c): asserts (i) drops oldest pending per object, (ii) sustained 120 Hz output rate ± 10% over 1 s, (iii) monotonic `seq` per object.
  - `ui/tests/test_matrix_view_sync.py` (Critic §1 row #9): asserts `matrix_model` mirrors `/sys/matrix` payload byte-for-byte after load.
  - `ui/tests/test_protocol_version.py`: handshake mismatch dialog renders.
  - `status_indicator.py`: yellow flag triggered by `/sys/warning osc_reorder_burst` events.
- **Dependencies**: P4 + P3 + P5.
- **Exit criteria**: `python -m spatial_engine_ui --osc-cmd-port 9100 --osc-state-port 9101` launches; receives `/sys/state` heartbeat at 10 Hz; renders 8 spawned objects; drag moves an object and pan tracks audibly; matrix view shows current routing read-only; noise generator panel routes white/pink to selected channel; `/sys/protocol_version` handshake mismatch shows explicit error dialog. All three coalescer tests pass.
- **Spec acceptance**: #9 (GUI + drag + matrix view + noise gen + inspectors), #6 (Binaural Monitor; partial — UI exists, KEMAR side-output in P9).

### P9 — BinauralMonitor (KEMAR SOFA, partitioned convolution per A2) + Numerical Accuracy Harness (rE/rV vs Analytic Reference per C7)
- **Goal**: Simultaneous binaural side-output via KEMAR SOFA (acceptance #6); numerical positional-accuracy verification using rE/rV projection compared against **closed-form analytic reference**, not engine code (C7).
- **Deliverables**:
  - `core/src/output_backend/BinauralMonitor.{h,cpp}` — SOFA loaded from `/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa`; **per-object HRTF convolution via `juce::dsp::Convolution` zero-latency partitioned uniform-partition (A2)**: head 64 frames at zero added latency (matches block size); tail at increasing partition sizes (128, 256, 512); **expected p99 latency <10 ms** (asterisked relative to spec criterion #11); AmbiX/SOFA sign convention applied at the binaural backend (negate az before HRTF lookup, per MEMORY.md fix). Simultaneous with `SpeakerArray` (both implementations register and process every block). At P9 startup, `tools/sofa_inspector.py` validates that the actual KEMAR SOFA file matches assumed sample rate (48 kHz) + IR length (256 or 512 samples); if not, `BinauralMonitor::initialize` returns named error and logs the discrepancy.
  - **Falsifier check (A2)**: at P9, measure BinauralMonitor end-to-end p99 (using same harness as P10 but instrumenting BinauralMonitor output); if >12 ms, switch to uniform-partition with zero-latency front + 64-frame predelay on SpeakerArray to align both backends; document in `docs/latency_budget.md`.
  - `tests/accuracy_harness/` (CI-runnable; uses NullBackend; **C7 redesign**): for each (layout, algorithm) pair, compute speaker gains `g_i_engine` from the engine's `RenderingAlgorithm::processBlock` AND `g_i_analytic` from the closed-form `AlgorithmAnalyticReference`. Assert `|g_i_engine − g_i_analytic| < 1e-6` per direction (the **externally-anchored gate** that was missing in R1). Then compute energy vector `rE = Σ_i (g_i² · u_i) / Σ_i g_i²` and velocity vector `rV` from analytic gains; project to `(az_realized, el_realized)`; assert `|az_intended − az_realized| ≤ 1°` AND `|rE| ≥ 0.7` AND `|rV| ≥ 0.7`. Grid: lab_4ch = 36 az × 1 el (horizontal); lab_8ch = 36 az × 5 el `[-15°, 0°, +15°, +30°, +45°]`. Per-geometry caveats: lab_4ch may not deliver ±1° everywhere; report as layout finding, not code bug. **CSV columns**: `(layout, algorithm, az_intended, el_intended, az_realized_engine, az_realized_analytic, |rE|, |rV|, az_err_deg_engine_vs_intended, az_err_deg_engine_vs_analytic, max_abs_gain_diff_engine_vs_analytic)`. **For DBAP**: rolloff parameter is config-driven so the analytic baseline is the same Lossius-2009 formula with the same parameter; engine vs analytic gate still meaningful. P9 docs flag that DBAP rolloff parameter is config-driven, not externally anchored to physics.
  - Binaural verification: localize 4 cardinal directions through KEMAR HRTF; assert ITD (interaural time difference) signs match the (frame, sign) convention (LEFT=+az for AmbiX/SOFA, so left source → ITD positive in left ear arrival).
- **Dependencies**: P3 + P6.
- **Exit criteria**: `just accuracy` produces CSV per (layout, algorithm) per the column scheme above; CI gate for lab_8ch (where math says ≤1° is reachable); for lab_4ch reports per-direction ceiling; **engine-vs-analytic gate** must pass for VBAP and DBAP at all in-CI directions. Binaural side-output: ITD test passes; perceptually plausible head-tracking-free localization on headphones during P3.5 re-run; A2 falsifier check produces measured BinauralMonitor latency in `docs/latency_budget.md`.
- **Spec acceptance**: #6 (Binaural Monitor v0 — with A2 latency asterisk), #13 (positional accuracy ±1° numerical gate, externally anchored).

### P10 — Latency Harness (C3 instrumentation pinned)
- **Goal**: Measure end-to-end input → Dante output latency at p99 with **explicitly pinned instrumentation points**.
- **Deliverables**: `tests/latency_harness/` (Python + lab harness).
  - **C3 instrumentation pin**:
    - **T0 = OSC packet receive timestamp at native core's `juce::OSCReceiver` callback entry** (recorded in core, microsecond clock; passed back to harness via `/sys/metrics`).
    - **T1 = sample written to Dante PCIe buffer just before kernel hand-off to ALSA period buffer** (instrumented inside `DanteBackend::audioDeviceIOCallbackWithContext` via a hardware-timestamp probe; recorded per block with the sample index).
    - This measures **stages 2–6** of the latency budget (NOT stage 1 Qt event-loop tail).
    - **Stage 1 (Qt event-loop tail) is measured separately at P12** with a UI-driven harness using `pyautogui` mouse + `xdotool` timestamp; added to the P10 number for the spec's "input → output" claim composition.
  - Harness method: inject `/obj/{id}/pos` jump from speaker A to speaker B at known T0; physical loopback cable from Dante out → Dante in; measure T1 via cross-correlation with reference impulse; cross-correlate to get sample-precise T1; compute T1−T0 in microseconds; report.
  - **Mean vs p99 interpretation**: spec criterion #11 says "<5 ms". This plan interprets as **p99 < 5 ms**. Documented in P10 + Acceptance Mapping row #12.
  - Measure p50, p95, p99 over 1 h with 8 active objects (the ADR 0003 falsifier soak); output to `tests/latency_harness/baseline.json` (records kernel, governor, JACK period, Dante driver version, audio interface, machine model, **PREEMPT_RT y/n per A3 P0 pinning**).
- **Dependencies**: P6 + P4.
- **Exit criteria**: p99 < 5 ms (T1−T0, stages 2–6) on lab target with 64-frame @ 48 kHz buffer + PipeWire-JACK + `performance` governor + **PREEMPT_RT kernel per A3 P0 pinning** + Digigram-certified driver version. If p99 ≥ 5 ms: trigger budget-vs-measurement reconciliation per §Latency Budget; pre-authorized fallbacks: smaller buffer (32 frames @ 48 kHz with PREEMPT_RT only), or relaxed acceptance with explicit asterisk in v0 sign-off doc (not silent).
- **Spec acceptance**: #12 (latency gate <5 ms; p99; stages 2–6 instrumented).

### P11 — Multi-Hour Soak Harness + Observability + 3U Constraints Doc
- **Goal**: 8 objects × VBAP × full chain → Xrun 0 / 30 min, mean CPU < 50%, **99-percentile per-block time < `block_size × 0.7 = 933 µs` at 64 frames @ 48 kHz** (A1/C2 corrected); multi-hour stability; UI-process RSS slope; observability counters; `docs/3u_rack_constraints.md`.
- **Derivation (explicit per A1)**: `block_size_samples / sample_rate × 0.7 = 64 / 48000 × 0.7 = 933.33 µs`. The R1 figure of 358 µs was incorrect (corresponded to `× 0.27`).
- **Deliverables**:
  - `tests/soak_harness/` — 8-object random-walk for ≥4 h pilot, then 12 h full; samples RSS, per-block time, xruns, IPC FIFO depth, CPU pct, `osc_reordered_drops`, `audio_underrun_count`, **heartbeat-miss events (10 Hz publish, 300 ms miss threshold per C6)** every 10 s.
  - UI-process RSS sub-suite: `psutil.Process(ui_pid).memory_info().rss` sampled every 60 s under 4 h synthetic 120 Hz drag; threshold <5 MB/h passes, >20 MB/h fails (Qt's QGraphicsView is a known leak vector).
  - Observability metrics exposed via `/sys/metrics`: `audio_underrun_count`, `ipc_queue_depth`, `osc_packet_rate`, `osc_reject_count`, `osc_reordered_drops`, `osc_reorder_burst_count_1s`, `cpu_pct_audio_thread`, `per_block_time_p99_us`, `geometry_cache_version`, `heartbeat_miss_count`.
  - Structured logs (control thread; audio thread uses `TraceRing` drained by control): `juce::Logger`-based.
  - Optional Xrun dump: on xrun, control thread dumps last N=4 audio blocks (pre-allocated ring) to disk for postmortem.
  - `docs/3u_rack_constraints.md` — documents code constraints: no passive/low-power-GPU dependency (the engine is CPU-only; no CUDA, no Metal, no Vulkan); low-noise (no spawning processes during real-time; no GUI on the audio host machine in v1+); ECC-friendly memory patterns (avoid scattered allocations in steady state; pre-allocated SOA buffers; no growing containers).
  - Memory anchors: core ~80 MB steady-state RSS (JUCE host overhead higher than Rust core; 16 objects × per-object scratch + FDN delay lines + KEMAR SOFA partitions + algorithm-swap parallel-run scratch per ADR 0006) with slope <1 MB/h; UI ~150 MB (PySide6 baseline + scene + state mirror) with slope <5 MB/h.
- **Dependencies**: P10 + P9.
- **Exit criteria**:
  - 30-min `8 obj × VBAP × full chain` baseline: Xrun 0, mean CPU < 50%, **p99 per-block time < `block_size × 0.7` = 933 µs at 64 frames @ 48 kHz** (A1/C2 corrected; derivation: `64 / 48000 × 0.7 = 933 µs`).
  - 12-h full soak: zero xruns, system-RSS slope <1 MB/h, UI-RSS slope <5 MB/h.
  - **Heartbeat-miss gate (C6)**: zero `/sys/heartbeat_miss` events at the 300 ms threshold over the full soak. The 10-second-gap legacy criterion is retained only as catastrophic-stall alarm (any single 10 s gap = soak failure regardless of count).
  - FDN denormal soak (Pre-mortem A): 30 min silence after impulse, p99 per-block stays < 933 µs throughout (A1 corrected).
  - `osc_reorder_burst_count_1s` reported per soak; non-zero is a yellow-flag artifact, not a fail; trended over time.
- **Spec acceptance**: #11 (numerical performance gate, corrected 933 µs threshold), #14 (multi-hour stability), #16 (3U code constraints documented).

### P12 — Documentation, Onboarding, Tag v0.1.0, Perceptual Sign-Off (N=12) + Stage-1 Latency
- **Goal**: Ship v0.1.0; verify acceptance #10 (engineer collaborator can clone-build-run-modify) on a second machine; perceptual sign-off N=12 with **pre-registration filed before data collection (M2)**; **measure stage 1 of the latency budget (C3) and compose with P10's stages 2–6 for the spec's "input → output" claim**.
- **Deliverables**:
  - `README.md`, `docs/architecture.md`, `docs/coordinate_convention.md` (full), `docs/ipc_schema.md` (full), `docs/onboarding.md` (full), `docs/onboarding_timing.md` (measured times — ≤60 min hard target, two machines), `docs/ui_modification_examples.md` (three concrete UI-only mods: per-object gain meter widget; drag sensitivity / smoothing tau; recolor / reicon objects).
  - Second-machine definition: same Ubuntu 22.04 LTS, different USB controller (e.g., AMD vs Intel chipset), different audio interface (Built-in HDA via PipeWire-JACK if lab uses Dante; vice-versa).
  - **`tests/perceptual/listening_test_v0/preregistration.md` (M2)**: filed *before* any data collection; specifies (a) Friedman contrasts: VBAP vs DBAP on lab_8ch, lab_4ch vs lab_8ch on VBAP; (b) per-direction CI threshold: `mean ≤ 1° AND 95% per-direction CI upper bound ≤ 2°`; (c) pointer to vid2spatial v3 stimulus pipeline file.
  - `tests/perceptual/listening_test_v0/` — N=12 pre-registered listeners; reuse vid2spatial v3 stimulus pipeline; mean perceived azimuth error within ±1° on geometry-reachable grid; Friedman test for the named contrasts.
  - **Stage-1 latency harness (C3)**: `tests/latency_harness_stage1/` uses `pyautogui` + `xdotool` to measure Qt event-loop tail latency from synthesized mouse event → `python-osc.send_message()` call site; report p99 stage-1 contribution; compose with P10's p99 stages-2–6 to produce the full spec-claim number; document in `docs/latency_budget.md`.
  - `docs/license_procurement_plan.md` finalized (C5) — JUCE Indie/Pro/Perpetual options + cost estimate + named owner + trigger conditions.
  - Git tag `v0.1.0`.
- **Dependencies**: P11.
- **Exit criteria**: ≤60 min on a clean Ubuntu 22.04 box (machine #1) + second-machine repeat ≤60 min, recorded in `docs/onboarding_timing.md` with date, machine model, USB controller, audio interface, kernel, network, blockers. If either >60 min, file bootstrap-friction issue and revise `bootstrap.sh` before tag. Perceptual sign-off: mean perceived az err ≤ 1° on geometry-reachable grid; per-direction 95% CI upper bound ≤ 2°. Stage-1 latency added to budget table; full input→output composed p99 figure published in `docs/latency_budget.md`.
- **Spec acceptance**: #10 (collaborator clone-build-run-modify), #13 (positional accuracy perceptual sign-off), #15 (IPC documented).

---

## ADRs

### ADR 0001 — Process model: two processes (JUCE host + PySide6 UI), OSC over UDP loopback IPC
- **Decision**: v0 uses two processes — JUCE C++ audio core (Process A) and PySide6 Python UI (Process B) — communicating over OSC/UDP loopback. v1 keeps the audio core, rewrites the UI in production stack (likely C++/JUCE-native or web-via-OSC-bridge); the IPC schema survives.
- **Drivers**: D1 latency budget (Python's GIL/GC tail latency cannot leak across a process boundary into the audio thread); D2 v0→v1 transition cost (UI is throw-away; core survives; the IPC schema is the bridge); D3 spec v2.1 R13 lock (single OSC transport, single Command schema).
- **Alternatives considered**:
  - **Single process: JUCE host with embedded CPython interpreter (`Py_Initialize` + `pybind11`); PySide6 in a Qt thread inside the same process.** *Steelman*: zero IPC, direct memory share for state mirror, one binary. *Why rejected*: GIL acquisition on every C++→Python edge serializes; Python GC pauses co-occur with audio-thread CPU and cause cache thrash; embedded CPython + Qt + JUCE is a build-system tarpit (Conda vs system Python vs uv); spec R13's "single transport OSC" only works clean across a process boundary; UI throw-away rewrite at v1 is harder if Python is welded into the C++ host.
  - **Two processes with shared-memory state mirror + UDS control socket (ZeroMQ-on-UDS or hand-rolled).** *Steelman*: lowest possible state-mirror latency. *Why rejected for v0*: adds tooling complexity collaborators can't `tcpdump`; the latency budget table shows IPC contributes <0.2 ms (well inside 5 ms); spec R13 lock; we'd lose free TouchOSC compatibility. *Migration target*: if ADR 0003 falsifier fires (p99 drag-to-render IPC stages > 3.0 ms for ≥1% windows under 8-object 120 Hz drag in 1 h), v1 starts with shm+UDS migration as a workstream.
- **Why chosen**: only this combination satisfies all three Drivers simultaneously. The audio path is production-grade from day one (Principle 1); the UI is throw-away by design (Principle 5 from spec); the schema survives the rewrite.
- **Consequences**: + real-time-safe C++/JUCE audio thread; + Python iteration speed for throw-away UI; + free external-controller compatibility (TouchOSC, OSCulator, future text2traj OSC adapter); − two binaries to ship; − schema versioning required to detect drift between core HEAD and stale UI checkout; − Python→OSC serialization tax (~0.05 ms; in budget).
- **Falsifier**: not applicable — this is a structural choice spec R13 effectively pre-decided. Re-litigation would require revisiting spec R13.
- **Follow-ups**: confirm Linux audio stack (PipeWire-JACK pinned in P0 `docs/lab_setup.md`); JUCE commercial license procurement (C5: trigger event = first external distribution OR first commercial deployment outside research lab; cost estimate + named owner in `docs/license_procurement_plan.md`).

### ADR 0002 — Native audio core language: C++ + JUCE (reaffirmation)
- **Decision**: native audio core is C++ (C++17 minimum, C++20 preferred) using JUCE 7.x as the framework. `juce::dsp` modules (Biquad/IIR, Convolution, SmoothedValue, AudioBuffer, ProcessorChain) are used where applicable. License: GPL v3 for v0 lab/research; commercial license required at trigger T per C5 (~$130/mo Indie or perpetual Pro).
- **Drivers**: spec v2.1 R12 lock (industry-standard audio framework; first-class VST3 SDK integration for v1+ VST3Control; mature `juce::dsp` modules accelerate per-object DSP chain implementation; `juce::AudioIODeviceType` handles Dante PCIe via JACK on Linux); D2 v0→v1 transition cost; D1 latency budget.
- **Alternatives considered**:
  - **Rust + `cpal` + custom DSP + `nih-plug` for v1 plugin path.** *Steelman*: memory safety in real-time path; modern toolchain; `cpal`'s callback model + `crossbeam` SPSC matches Principle 1 by construction; reproducible cargo builds. The archived v1-plan chose this. *Invalidation rationale*: (i) spec v2.1 R12 explicitly locks JUCE; (ii) `nih-plug` is the only practical Rust path for v1+ VST3Control plugin and remains pre-1.0; JUCE has 20-year track record; (iii) the spec's vendor delta (R7 + R8 + R10) is implementable in either, but JUCE's `juce::dsp` library gives pre-tested partitioned-convolution-capable building blocks; (iv) collaborator pool skews JUCE-fluent.
  - **C + PortAudio (raw)**. *Why rejected*: re-invents wheels; no DSP primitives.
  - **Zig + raw audio APIs**. *Why rejected*: ecosystem too young.
- **Why chosen**: spec R12 lock + `juce::dsp` accelerator for the spec v2.1 vendor-delta scope.
- **Consequences**: + pre-tested DSP primitives; + production-grade audio framework; + clean v1+ VST3Control path; − GPL v0 license requires commercial license at trigger T per C5; − C++ exposes RT path to UB risks Rust forecloses (mitigation: `RT_ASSERT_NO_ALLOC` macro + `juce::ScopedNoDenormals` + clang-tidy rule set).
- **Falsifier**: at P11, profile audio thread with `perf` for 60 s under 8-object full-chain load; if `juce::*` frames dominate inclusive >50% AND dominant function is not user-overrideable, file `re-evaluate-juce-framework` issue.
- **Follow-ups**: pin JUCE submodule at P0; track JUCE commercial license budget per C5 trigger.

### ADR 0003 — IPC transport: OSC over UDP, single transport, single Command schema (per spec R13 lock)
- **Decision**: v0 uses **OSC over UDP loopback** between JUCE core (Process A) and PySide6 UI (Process B). Same transport and same `Command` schema serve UI IPC + external OSC + v1+ VST3Control. Default ports 9100 (cmd) / 9101 (state); overridable via `--osc-cmd-port` / `--osc-state-port`. Every command carries a `schema_version` u16 first argument; startup `/sys/protocol_version` handshake gates connection. Heartbeat **10 Hz / 300 ms miss threshold (C6)**.
- **Drivers**: D1 latency (UDP loopback adds <0.2 ms to budget); D2 schema-as-invariant; D3 spec R13 lock; D4 external-controller compatibility (TouchOSC).
- **Alternatives considered (steelmanned)**:
  - **Shared-memory ring buffer (state mirror) + Unix Domain Socket control channel.** *Steelman*: lowest-possible-latency state mirror. *Why rejected for v0*: more code than the latency budget needs; POSIX shm is OS-specific and harder to inspect; Python `multiprocessing.shared_memory` + PySide6 + Qt event loop = GIL/GC tarpit; external-controller story breaks. *Migration target on falsifier trigger*.
  - **ZeroMQ over `ipc://` or `tcp://127.0.0.1`**. *Why rejected*: framing overhead vs raw UDP; opaque to engineers; still need schema choice.
  - **Cap'n Proto over UDS**. *Why rejected*: codegen complicates `bootstrap.sh` 60-min target; less L-ISA-idiomatic than OSC; no external-controller story.
- **Why chosen**: only OSC/UDP simultaneously satisfies D1, D2, D3, D4. shm+UDS antithesis preserved as v1 migration target conditional on falsifier.
- **Falsifier (operational)**: **`p99 drag-to-render latency in IPC-dominant stages > 3.0 ms for ≥1% of windows under sustained 8-object 120 Hz drag in a 1 h soak`**. Operationalization: P10 latency harness extended to 1 h drag soak; measure end-to-end input-OSC → speaker-onset latency in 1 s windows (3600 windows total); compute p99 of the IPC + decode + audio-callback-wait portion (latency table stages 2–4). Trigger: ≥36 of 3600 windows with p99 > 3.0 ms in IPC-dominant stages → file `migrate-to-shm-uds` issue.
- **Consequences**: + IPC contribution <0.2 ms; + free external-controller support; + plain-text schema, `tcpdump`/`osc_debug_console.py` debuggable; + schema versioning in-band; − UDP best-effort (mitigations: 120 Hz coalescing at UI source; sequence-number reorder defense per Pre-mortem C; 10 Hz lossy heartbeat for resync); − default ports 9100/9101 *moved off* TouchOSC defaults.
- **Follow-ups**: P4 ships `--osc-cmd-port` / `--osc-state-port` overrides; P4 ships `/sys/protocol_version` handshake + `schema_version` field on every command + 10 Hz heartbeat with 300 ms miss gate (C6); P10 latency harness produces the 1 h 8-object 120 Hz drag soak.

### ADR 0004 — FDN topology: 16-line Hadamard FDN with per-line one-pole tone control + frequency-dependent T60
- **Decision**: FDNReverb implements a 16-line Feedback Delay Network with: 16×16 Hadamard mixing matrix; 16 mutually-prime delay-line lengths at 48 kHz; per-line one-pole LPF for HF damping; per-line gain `g_i = 10^(-3 × m_i / (T60 × fs))`; **denormal handling** (Pre-mortem A): `juce::ScopedNoDenormals` + per-line ±1e-20 DC offset injection.
- **Drivers**: D1 latency budget (FDN must fit per-block compute); Principle 1 (allocation-free, branchless); spec acceptance #4 + #5.
- **Alternatives considered**:
  - **8-line FDN with Householder + shared global tone control**. *Why rejected*: sparse modal density at lab RT60.
  - **Configurable {8, 16}**. *Why rejected*: doubles test surface.
  - **Schroeder (4 series allpass + 4 parallel comb)**. *Why rejected*: metallic coloration on dense input.
  - **Moorer (early reflections + comb + allpass)**. *Why rejected*: requires geometry-aware early-reflection design; v1+ enhancement.
- **Why chosen**: 16 lines + Hadamard is the modern algorithmic-reverb floor (Jot 1991); SIMD-friendly; 4 v1+ hooks unchanged.
- **Consequences**: + dense modal coverage; + branchless Hadamard; − ~192 KB in core RSS; − denormal hazard requires explicit FTZ/DAZ + DC offset.
- **Falsifier**: not applicable in measurement-by-budget sense; perceptual at P12 (if listeners report "metallic" / "ringy", re-tune mutually-prime set before doubling line count).
- **Follow-ups**: ship `configs/reverb_fdn.yaml` at P7; P7 unit test fits Schroeder energy decay to verify RT60 ±10%.

### ADR 0005 — Per-object algorithm dispatch: D1 polymorphic + D3-friendly invariants (M5 synthesis)
- **Decision**: each `Object` carries a `RenderingAlgorithm*` pointer (D1 dispatch surface). At `prepareToPlay`, **each `RenderingAlgorithm` impl pre-allocates SoA scratch arrays sized `MAX_OBJECTS`** (D3-friendly invariant per Architect Recommendation A / M5). `engine->getObjectsByAlgorithm(Algorithm)` returns spans (no allocation; used by debug tools in v0 and by D3 dispatch in v1+). Audio-thread loop is `for (auto& obj : objects) obj.algorithm->processBlock(...)` (D1) but the v1+ migration to D3 is a single-loop swap to `for (auto algo : algorithms) for (auto& obj : objects_by_algo[algo]) algo.processBlock(...)` — no header change.
- **Drivers**: Principle 4 (RenderingAlgorithm is a stable abstraction); spec ontology (`Object.algorithm` per-object selectable); D2 (v1+ adds new algorithm impls without engine surgery); ergonomics; **forward-compatibility with v1+ N≥32-object scaling** (M5).
- **Alternatives considered**:
  - **D2 (table-driven dispatch)**. *Why rejected*: scratch ownership in engine (Principle 4 violation); v-table cost dwarfed by FDN+LPF+delay anyway.
  - **D3 (bucket-by-algorithm) v0 directly**. *Why rejected for v0*: bucket bookkeeping; complexity not justified at 8 objects. *Migration target* with SoA invariant pre-baked.
- **Why chosen**: D1 dispatch surface preserves Principle 4 ergonomics; D3-friendly SoA invariant + `getObjectsByAlgorithm` helper means the v1+ migration is "swap one for-loop", not "rewrite ownership model" (Architect synthesis).
- **Falsifier**: at P11 soak, profile audio thread with `perf` for 60 s under 8-object full-chain mixed-algorithm load (e.g., 4 VBAP + 2 WFS + 2 DBAP); if v-table dispatch + virtual-call overhead is >5% of audio-thread CPU, file `eval-d3-bucket-dispatch` issue and re-evaluate at v1+.
- **Consequences**: + simple, idiomatic, encapsulated (D1); + each algorithm owns scratch (D1) AND scratch is SoA-laid-out for v1+ D3 migration (M5); + ~3× over-allocation at MAX_OBJECTS=16 across all three algorithms is small (~few hundred KB; in budget); − virtual-call overhead per object per block (~few ns × 8 = invisible); − SoA layout adds ~30 lines vs pure D1.
- **Follow-ups**: at P11 record `perf` flame graph as baseline; document v1+ D3 migration recipe in `docs/architecture.md`.

### ADR 0006 — Algorithm runtime swap mechanism (NEW R2; A4 / C4)
- **Decision**: Per-object algorithm runtime change (`/obj/{id}/algorithm` command) executes via:
  1. **Pre-allocation**: at scene-load (or whenever a new algorithm is chosen for an object), control thread allocates the new algorithm's scratch state from the per-algorithm SoA arrays (ADR 0005 / M5). Pre-allocation happens in non-realtime context.
  2. **Atomic publish**: `Object` carries `std::atomic<AlgorithmId> algorithm_id`. Control thread sets `pending_algorithm_id` and a swap-armed flag via single-writer atomic publish.
  3. **Audio-thread swap**: at next block boundary, audio thread reads the swap-armed flag; if armed, it begins a **K=256-sample crossfade window (~5.33 ms @ 48 kHz)** during which **both** the old and the new algorithm run on that object in parallel. Old output crossfades 1→0 via linear ramp; new output crossfades 0→1. Both are backed by pre-allocated scratch (Principle 1 alloc-free preserved).
  4. **Cleanup**: after K samples, audio thread atomically sets `algorithm_id = pending_algorithm_id` and clears the swap-armed flag. Control thread observes the flag clear at next safe window and frees old algorithm's scratch slot back to its SoA pool.
- **Drivers**: A4 (RT-safety vs spec-required runtime selectability — Principle 1 vs spec ontology `/obj/{id}/algorithm` runtime command); spec acceptance #2 (per-object selectable, runtime-changeable); C4 (audio-domain falsifiable click test).
- **Alternatives considered**:
  - **R1 silent-gap**: ramp object gain to 0 → swap pointer → ramp back. *Why rejected*: ~2.7 ms audible dip on the object during swap; user-visible artifact unfit for performance/exhibition target.
  - **No crossfade, instantaneous swap**: *Why rejected*: discontinuity at swap sample → click; algorithm-specific scratch (e.g., WFS per-speaker propagation-delay state) different from VBAP, so the discontinuity is not just a gain step.
  - **K=64 (one-block) crossfade**: *Why rejected*: too short for some perceptually significant filter-state differences (e.g., the WFS delay-line tail that VBAP doesn't have).
  - **K=512 (eight-block) crossfade**: *Why rejected*: runs both algorithms in parallel for 10.7 ms, which doubles per-object CPU for that window — at 8 objects all swapping, 2× p99 spike risk at P11 gate. K=256 is the chosen middle.
- **Why chosen**: K=256 balances click-freeness (5.33 ms is enough for typical filter-state differences to be perceptually masked) against the per-block CPU spike (parallel-run cost on a single object at a single swap event is well within budget).
- **Consequences**: + click-free runtime swap; + RT-safe (audio thread reads atomic, never allocates); + falsifiable test (C8 FFT spectral check around the swap window); − 2× scratch per object slot during swap (in budget; <50 KB extra); − control thread responsible for free-after-clear (standard SPSC pattern); − a future operator-issued "swap all 8 objects simultaneously" command produces a brief 8× CPU spike during K=256 window — documented as known v0 behavior in `docs/architecture.md`.
- **Falsifier**: a profiled allocation or vtable churn observed inside the audio thread during an algorithm swap under `RT_ASSERT_NO_ALLOC`-style instrumentation, OR a click event during the K=256 swap window detected via `ClickDetectorFFT` spectral spike >10 dB above neighboring frequencies (per C8). Either failure mode → file `algorithm-swap-mechanism-redesign` issue.
- **Follow-ups**: P3 implements the swap mechanism; P3 integration test (`tests/e2e/`) asserts the C8 three-part criterion during the swap; Risk #13 in register tracks `algorithm_swap_click`.

---

## Acceptance Criteria Mapping (spec v2.1's **16** criteria → phase verification) (C1)

Spec v2.1 §Acceptance Criteria (16 criteria, counted in source order):

| # | Acceptance bullet (paraphrased from spec) | Verifying phase(s) / harness | Verdict |
|---|-------------------------------------------|------------------------------|---------|
| 1 | Custom Cartesian 4–8ch speaker layout config loads + routes correctly on lab Linux + Digigram ALP-Dante PCIe. | P0 (lab/kernel/driver pinning per A3) + P2 (geometry loader) + P5 (matrix routing) + P6 (Dante I/O verified) | FULL |
| 2 | All three rendering algorithms (VBAP, WFS, DBAP) implemented; each Object selects its algorithm at runtime; LayoutCompatibilityChecker rejects incompatible pairs with clear error. | P3 (algorithm trio + checker + ADR 0006 K=256 crossfade for runtime change) + `tests/compat_harness/` | FULL |
| 3 | Per-object full DSP chain (4-Band EQ → Delay → Pan → distance gain → distance HF rolloff → distance propagation delay → reverb send) processes 8 simultaneous objects without dropouts. | P3 (chain) + P5 (8-object capture) + P11 (8-object soak Xrun 0; gate corrected to p99 < 933 µs per A1/C2) | FULL |
| 4 | FDN-based algorithmic reverb operates with per-object send modulation; distance-dependent reverb amount audibly increases with distance. | P7 (FDN + send curve unit test + per-object send gate) + P12 (perceptual sign-off) | FULL |
| 5 | ReverbEngine interface uses block-based process(), getLatencySamples(), optional SupportsIRLoading capability, extensible parameter-ID table — future IR drop-in requires no bus/send/routing code changes. | P7 (`IRConvolutionStub` + 4 hooks; **metadata validation per C-MA #3** proves the abstraction shape for the failure modes that matter) | FULL |
| 6 | OutputBackend abstraction has two simultaneous v0 implementations: SpeakerArray (Dante PCIe, 4–8ch) + BinauralMonitor (KEMAR SOFA, headphones). | P3 + P5 (SpeakerArray) + P9 (BinauralMonitor + simultaneous side-output + A2 partitioned convolution) | FULL (BinauralMonitor latency relaxed to <10 ms p99 per A2 — see footnote\*) |
| 7 | ExternalControl abstraction with OSCBackend v0 implementation; abstract Command schema covers all object-position / object-gain / scene-control operations; VST3Control v1+ slot is callable. | P4 (OSCBackend + Command schema + protocol_version handshake + **VST3ControlStub registration test per C-MA #6.f** proves "callable") | FULL |
| 8 | Distance-dependent propagation delay uses fractional delay + smoothing/crossfade — no zipper/click artifacts when object moves rapidly. | P3 (PropagationDelay sweep + **C8 three-part click test** including FFT spectral spike check) | FULL |
| 9 | GUI: top-down/3D speaker view, drag-to-move 1–4 objects in real time, read-only audio-matrix view, noise-generator panel, parameter inspectors. | P8 (full UI + `ui/tests/test_drag_coalescer.py` per Architect-rec-B + `ui/tests/test_matrix_view_sync.py` per Critic §1) | FULL |
| 10 | Engineer collaborator can clone, build, run engine + GUI on their Linux machine and modify a UI element end-to-end. | P12 (≤60 min on second machine + three concrete UI-only modifications documented) | FULL |
| 11 | Numerical performance gate: 8 obj × VBAP × full chain sustains Xrun 0 / 30 min, mean CPU < 50%, **99-percentile per-block time < block_size × 0.7 = 933 µs** (corrected per A1/C2: `64/48000 × 0.7 = 933 µs`). | P11 (soak harness baseline; corrected gate) | FULL (corrected) |
| 12 | Latency gate: end-to-end input → Dante output < 5 ms (interpreted as **p99**; T0 = OSC packet receive at core, T1 = sample written to Dante PCIe buffer per C3; stage 1 Qt event-loop tail measured separately at P12). | P10 (latency harness with C3-pinned instrumentation) + P12 (stage-1 composition) | FULL (instrumentation pinned per C3) |
| 13 | Positional accuracy gate: numerical pan-law verification ≤ ±1° apparent angle error. | P9 (rE/rV harness with **closed-form analytic reference per C7** for VBAP and DBAP; CI gate for lab_8ch; lab_4ch reports ceiling) + P3.5 (early-warning smoke) + P12 (perceptual sign-off N=12 with M2 pre-registration) | FULL (externally anchored per C7) |
| 14 | Stability gate: multi-hour uninterrupted operation without memory leaks, GC/IPC stalls, audio underruns. | P11 (12 h soak: zero xruns, RSS slope thresholds for both core and UI, **heartbeat-miss gate at 300 ms per C6**) | FULL (heartbeat gate tightened) |
| 15 | IPC layer between native audio core and PySide6 UI documented (transport, message schema, state ownership). | P4 (`proto/ipc_schema.md`, `proto/command_table.json`) + P12 (`docs/ipc_schema.md`) | FULL (was un-rowed in R1; now explicit per C1) |
| 16 | Code constraints for 3U-rack target documented (no passive/low-power-GPU dependency, low-noise, ECC-friendly memory patterns). | P11 (`docs/3u_rack_constraints.md`) + P0 (`docs/lab_setup.md` GPU/CPU constraints scaffolded) | FULL (was un-rowed in R1; now explicit per C1) |

\* **Footnote on row #6 / spec criterion #11 mapping**: BinauralMonitor's latency target is **<10 ms p99** per ADR-level treatment in §Latency Budget BinauralMonitor sub-budget (A2). Spec criterion #11 textually scopes "input → Dante output" (the SpeakerArray path); BinauralMonitor's HRTF convolution adds ≥1.33 ms partition latency on top of the SpeakerArray path. Asterisk is documented in `docs/latency_budget.md` and the v0 sign-off acceptance doc.

(Cross-cutting sentence from R1 dropped as redundant; rows #15 and #16 now explicit.)

---

## Numerical Verification Methodology (Acceptance #13; C7 redesign)

**Goal**: prove ≤ ±1° apparent positional accuracy via numerical pan-law verification, per-geometry-per-algorithm, **with closed-form analytic reference baseline (C7)**.

**Method**: energy-vector (rE) + velocity-vector (rV) projection (Gerzon 1992) **plus engine-vs-analytic gain comparison (C7)**.

For each test direction `(az_intended, el_intended)`:

1. Compute `g_i_engine` from the engine's `RenderingAlgorithm::processBlock` (called offline through NullBackend).
2. Compute `g_i_analytic` from `core/src/render/AlgorithmAnalyticReference.h`:
   - **VBAP**: closed-form Pulkki-1997 vector base amplitude panning solution for the three nearest speakers (or two on the horizontal plane). Independent code path; not built on engine.
   - **DBAP**: closed-form Lossius-2009 distance-based amplitude formula `g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))` — same formula as engine but computed independently as a re-implementation cross-check; engine vs analytic should match to 1e-6 (since the formula is config-driven, the gate proves engine math is correct, not that physics chose the formula).
3. **Gate 1 (engine correctness)**: assert `max_i |g_i_engine - g_i_analytic| < 1e-6` per direction.
4. For each speaker `i`, let `u_i = (cos(el_i)*cos(az_i), cos(el_i)*sin(az_i), sin(el_i))` (unit vector listener→speaker).
5. Compute (using **analytic gains**, not engine gains):
   - `rE = Σ_i (g_i_analytic² · u_i) / Σ_i g_i_analytic²`
   - `rV = Σ_i (g_i_analytic · u_i) / Σ_i g_i_analytic`
6. Project to angles: `(az_realized, el_realized) = direction_of(rE)`.
7. **Gate 2 (positional accuracy)**: assert
   - `|az_intended − az_realized| ≤ 1°` AND
   - `|el_intended − el_realized| ≤ 1°` AND
   - `|rE| ≥ 0.7` AND
   - `|rV| ≥ 0.7`.

**Test grid**: lab_4ch = 36 az × 1 el (36 directions); lab_8ch = 36 az × 5 el (180 directions); lab_8ch_irregular not in CI accuracy harness (used only for `LayoutCompatibilityChecker` red/green tests).

**Per-algorithm coverage**:
- VBAP on lab_4ch + lab_8ch (compat-passing).
- DBAP on lab_4ch + lab_8ch + lab_8ch_irregular (DBAP works on any layout).
- WFS on a regular WFS-compatible layout (planar grid variant of lab_8ch). For WFS, the analytic baseline is the existing per-sample Huygens cross-check at the unit-test level (P3 line 405 equivalent).

**What the harness reports**: CSV `(layout, algorithm, az_intended, el_intended, az_realized_engine, az_realized_analytic, |rE|, |rV|, az_err_deg_engine_vs_intended, az_err_deg_engine_vs_analytic, max_abs_gain_diff_engine_vs_analytic)`; pass/fail per row; aggregate p50/p95/p99 of `az_err_deg_engine_vs_intended`; per-direction failures listed with reason (math-ceiling vs code-bug vs engine-vs-analytic mismatch).

**Coupled with perceptual sign-off (P12 listener test, N=12 pre-registered per M2)**: numerical gate is necessary not sufficient.

---

## Latency Budget Decomposition (Acceptance #12; <5 ms p99 end-to-end input → Dante output)

Term-by-term p99 estimates from `/obj/{id}/pos` UI-side mouse event to first-altered-sample-at-Dante-output. Hardware/kernel assumptions (per A3 P0 pinning): lab machine = x86_64 desktop, **PREEMPT_RT kernel** (Linux 6.x generic preserved as fallback if Digigram driver matrix forecloses RT), `cpufreq=performance` governor, PipeWire-JACK with period = 64 frames @ 48 kHz, Digigram ALP-Dante PCIe with **vendor-certified driver version** (pinned in `docs/lab_setup.md`).

| Stage | Component | p99 estimate (PREEMPT_RT) | p99 (commodity 6.x fallback) | Notes / dominating term |
|-------|-----------|--------------------------|------------------------------|-------------------------|
| 1 | Qt event loop tick (UI mouse → `python-osc.send_message`) | **1–4 ms** | 1–4 ms | Measured **separately at P12** via `pyautogui` + `xdotool` (C3); not in P10 harness. |
| 2 | UDP loopback (`sendto` → core's `OSCReceiver`) | **<0.2 ms** | <0.2 ms | Local UDP loopback ≈ memcpy + scheduler hop. |
| 3 | Core control thread wake → OSC decode → SPSC push | **<0.5 ms** | 1–3 ms | PREEMPT_RT scheduler compresses jitter; commodity tail can reach 3 ms. |
| 4 | Audio callback wait (next JUCE callback boundary) | **0–1.33 ms** | 0–1.33 ms | At 64 frames @ 48 kHz. Avg 0.67 ms; p99 ≈ 1.33 ms. |
| 5 | DSP block compute (8 objects × full chain + FDN + matrix + 2 OutputBackends) | **<0.5 ms** | <0.5 ms | Measured at P11; p99 ≈ 500 µs at 64 frames. |
| 6 | Output buffer fill + DAC out (Dante PCIe) | **1.33 + 1–3 ms** | 1.33 + 1–3 ms | 1.33 ms output buffer queue (JACK) + Dante interface IO ~1–3 ms. |
| **Sum p99 (PREEMPT_RT, P10 measures stages 2–6 per C3)** | | **≈ 3.0–6.5 ms (stages 2–6)** | | |
| **Sum p99 + Stage 1 (P12 composition)** | | **≈ 4.0–10.5 ms (full input→output)** | | |
| **Sum p99 (commodity 6.x fallback, stages 2–6)** | | **≈ 3.5–9.5 ms (stages 2–6)** | | |

**P10 measures stages 2–6 (T0 = OSC packet receive at core, T1 = sample at Dante PCIe buffer per C3); spec-claim composition adds Stage 1 from P12 separately. Spec criterion #12 is interpreted as p99 < 5 ms.**

### BinauralMonitor sub-budget (A2 — promoted from Open Q to plan body)

BinauralMonitor uses **`juce::dsp::Convolution` zero-latency partitioned uniform-partition** (head 64 frames at zero added latency, tail at increasing partition sizes). Partition latency budget at the Critic-flagged KEMAR-actual IR length (256 to 512 samples; finalized at P0 by `tools/sofa_inspector.py`):

| Stage | BinauralMonitor add-on |
|-------|-------------------------|
| Convolution head partition (64 frames) | 0 ms (zero latency by design) |
| Tail partitions overlap-add (128/256/512 frames) | +1.33 to +5 ms |
| **BinauralMonitor p99 target** | **<10 ms** (asterisked relative to spec criterion #11 SpeakerArray path) |

**Convolution length / partition strategy**: zero-latency partitioned uniform-partition; head 64; tail 128/256/512 increasing partitions. **Falsifier (A2)**: at P9 measurement, if BinauralMonitor end-to-end p99 >12 ms on lab target, switch to uniform-partition with zero-latency front + 64-frame predelay on SpeakerArray to align both backends; document in `docs/latency_budget.md`.

### Pre-authorized fallbacks

If P10 measurement shows p99 (stages 2–6) >5 ms even with PREEMPT_RT pinned per A3:
1. **Smaller audio buffer** (32 frames @ 48 kHz = 0.67 ms one-way) — saves ~1.3 ms but doubles xrun risk; only viable on PREEMPT_RT.
2. **Relax acceptance to <10 ms p99** (asterisked in v0 sign-off doc) — keeps PREEMPT_RT posture; user re-litigation required; this plan does NOT silently relax.

P10 reports p50/p95/p99; if p99 > 5 ms, P10 output triggers a budget-vs-measurement reconciliation, then a documented decision about which fallback to apply. Documented in `docs/latency_budget.md` (P0 deliverable updated through P12).

---

## Coordinate Convention Module (Principle 3)

The `coords` namespace inside `core/src/coords/` is the *only* place sign-flips happen. Every (frame, sign) pair has an explicit named function with hand-computed expected values; round-trip identity tests are non-primary.

**Frames in v0**:
| Frame name | Convention | Used by |
|------------|------------|---------|
| **Pipeline / vid2spatial-native** | `(az, el)` rad; `az = atan2(x_listener, z_listener)`; **RIGHT = +az**; `el = arcsin(-y_image)`; UP = +el listener-frame. | OSC commands `/obj/{id}/pos`; engine internal state. |
| **AmbiX / SOFA** | `(az, el)` rad; **LEFT = +az**; UP = +el. | KEMAR SOFA HRTF lookup in BinauralMonitor; future FOA encoders. |
| **VBAP layout-frame (engine-internal)** | Cartesian XYZ in `configs/*.yaml`; per-speaker `{az_deg, el_deg}` audio-azimuth-from-front, RIGHT=+az, in degrees. | SpeakerLayout YAML schema; VBAPRenderer triangulation. |
| **Image-y-down (vid2spatial vision)** | image pixel y grows downward; converted by `el = arcsin(-y_image_normalized)`. | future v1+ vid2spatial trajectory adapter; not used in v0 audio path. |

**Helper functions (every test has hand-computed expected values, NOT only round-trip)**:

```cpp
namespace spe::coords {
  inline auto pipeline_to_ambix(float az_pipe, float el_pipe) -> std::pair<float,float>;
    // TEST: pipeline_to_ambix(+M_PI/4, 0) == (-M_PI/4, 0)
    // TEST: pipeline_to_ambix(0, +M_PI/2) == (0, +M_PI/2)        // straight up unchanged
    // TEST: round-trip pipeline_to_ambix(ambix_to_pipeline(p)) == p   // SECONDARY check

  inline auto ambix_to_pipeline(float az_ambix, float el_ambix) -> std::pair<float,float>;
    // TEST: ambix_to_pipeline(+M_PI/4, 0) == (-M_PI/4, 0)

  inline auto cartesian_to_pipeline(float x, float y, float z) -> std::tuple<float,float,float>;
    // TEST: cartesian_to_pipeline(1, 0, 1) == (atan2(1,1), 0, sqrt(2)) == (+π/4, 0, √2)
    // TEST: cartesian_to_pipeline(-1, 0, 1) == (-π/4, 0, √2)
    // TEST: cartesian_to_pipeline(0, 1, 0) == (0, +π/2, 1)
    // TEST: cartesian_to_pipeline(0, -1, 0) == (0, -π/2, 1)

  inline auto image_y_to_listener_el(float y_image_normalized) -> float;
    // TEST: image_y_to_listener_el(+0.5) == arcsin(-0.5) == -π/6
    // TEST: image_y_to_listener_el(0) == 0
    // TEST: image_y_to_listener_el(-1) == arcsin(1) == +π/2

  inline auto yaml_speaker_to_cartesian(float az_deg, float el_deg, float dist_m=1) -> std::array<float,3>;
    // TEST: yaml_speaker_to_cartesian(+90, 0, 1) == (1, 0, 0)
    // TEST: yaml_speaker_to_cartesian(0, 0, 1)   == (0, 0, 1)
    // TEST: yaml_speaker_to_cartesian(180, 0, 1) == (0, 0, -1)
    // TEST: yaml_speaker_to_cartesian(0, +90, 1) == (0, 1, 0)

  inline auto stereo_pan_from_pipeline_az(float az_pipe) -> float;
    // TEST: stereo_pan_from_pipeline_az(+π/2) > 0
    // TEST: stereo_pan_from_pipeline_az(-π/2) < 0
    // ANTI-TEST: assert NOT sin(-az) — locks against the 2026-03-01 baseline_pan inversion bug
}
```

**Acceptance rule**: every (frame, sign) pair gets at least 3 hand-computed test points (cardinal directions + one off-axis); round-trip identity is a tie-breaker secondary check, never primary.

---

## Test Plan (DELIBERATE expanded: unit / integration / e2e / observability)

### Unit tests (CTest in `core/tests/core_unit/`, pytest in `ui/tests/`)

- **VBAP gain calculation**: for `lab_4ch` / `lab_8ch`, per-speaker-direction gain == 1, others 0 (within 1e-6); engine vs `AlgorithmAnalyticReference::vbap_gain()` Pulkki-1997 (within 1e-6) **per C7**.
- **WFS gain + delay calculation**: regular linear array; per-speaker delay = `r_i / c` (within 1 sample @ 48 kHz); per-speaker amplitude = `1/√r_i` (within 1e-3); Huygens analytic cross-check at sample level.
- **DBAP gain calculation**: `Σ g_i² = 1` within 1e-6; engine vs `AlgorithmAnalyticReference::dbap_gain()` Lossius-2009 within 1e-6 **per C7**.
- **LayoutCompatibilityChecker**: 6 known-bad pairs rejected with correct reason (e.g., `(lab_8ch_irregular, WFS) → IncompatibilityReason::WFS_REQUIRES_REGULAR_ARRAY`); 6 known-good pairs accepted.
- **Coordinate frame conversions**: every (frame, sign) pair, per §Coordinate Convention Module above; hand-computed expected values; round-trip secondary.
- **OSC decode parser**: valid → expected `Command` enum variant; malformed → `Err`, never panics; oversized → bounded-time rejection; mismatched `schema_version` → `/sys/error` reply.
- **YAML schema validation**: valid `lab_4ch.yaml` parses; missing channel count, duplicate speaker IDs, out-of-range angles, unsupported regularity → fail with descriptive error.
- **OutputBackend trait conformance**: `SpeakerArray`, `BinauralMonitor`, future `IRConvolutionStub` satisfy `channel_count`/`prepare`/`render`/`shutdown`; compile-time test via generic harness.
- **`Command` POD invariant**: `static_assert(sizeof(Command) <= 64)` and `static_assert(std::is_trivially_copyable_v<Command>)`.
- **C8 click test (3-part) on GainRamp**: jump position from `(az=0)` to `(az=+π/2)` mid-block; assert (i) first per-sample step ≤ `1.0/MAX_BLOCK`, (ii) cumulative gain at sample MAX_BLOCK matches target within 1e-6, (iii) FFT of rendered block contains no spectral spike >10 dB above neighboring bins.
- **Distance gain rolloff**: configured `1/r^a` law with floor; monotonic-decreasing.
- **PropagationDelay fractional + C8 click sweep**: feed an impulse at d=10 m; assert delay = `10/343` s = 29.155 ms within ±0.5 sample (Lagrange precision); sweep d 1→10 m over 1 s; assert no spectral spike >10 dB during sweep.
- **FDN RT60 measurement**: feed impulse, capture 5 s, fit Schroeder energy decay; assert measured T60 within ±10% of configured.
- **NoiseGenerator**: white = flat power spectrum within ±1 dB; pink = -3 dB/oct slope within ±1 dB.
- **IRConvolutionStub format validation (C-MA #3)**: feeds 44.1 kHz / 2-channel → assert `IRSampleRateMismatch`; feeds 8-channel → assert `IRChannelCountMismatch`; feeds matching 48 kHz / 1-channel → no error.
- **VST3ControlStub registration (C-MA #6.f)**: factory loads stub; registers via abstract base; engine-code change required = zero (compile-time verified).
- **Heartbeat-miss test (C6)**: simulate 350 ms publisher pause; assert `/sys/heartbeat_miss` event with count=3+; assert audio thread not blocked.
- **UI drag coalescer (Architect-rec-B / C-MA #4.c, in `ui/tests/test_drag_coalescer.py`)**: drops oldest pending; sustained 120 Hz output ± 10% over 1 s; monotonic `seq` per object.
- **UI matrix view sync** (`ui/tests/test_matrix_view_sync.py`): matrix_model mirrors `/sys/matrix` byte-for-byte after load.

### Integration tests (`tests/e2e/`; spawns core+UI as subprocesses; uses NullBackend)

- **OSC roundtrip**: UI writes `/obj/0/pos`, reads back via `/sys/state`; echoed position matches within sequence-number tolerance.
- **SpeakerArray known-position output**: capture multi-channel buffer; per-channel gains match VBAP closed form within 1e-3.
- **Geometry YAML reload**: load `lab_4ch.yaml`, then `/sys/load_geometry lab_8ch.yaml`; assert `/sys/geometry` reports 8 channels; UI updates; in-flight positions clamped without xrun.
- **Algorithm runtime change with C8 click test (ADR 0006)**: spawn object with `algorithm=VBAP`; runtime change to `algorithm=DBAP`; assert (i) `RT_ASSERT_NO_ALLOC` zero allocations during K=256 swap window, (ii) per-sample max `|Δgain|` ≤ `1/256` per speaker during crossfade, (iii) FFT of swap-window block contains no spectral spike >10 dB.
- **Reorder defense + alert burst (Pre-mortem C, C-MA #4.d)**: send 1000 `/obj/0/pos` with scrambled `seq`; assert highest-seq is final state; `osc_reordered_drops` matches expected; inject 10 reorders in 200 ms; assert `/sys/warning osc_reorder_burst` emitted within next 1 s.
- **Heartbeat miss (C6)**: kill heartbeat publisher 350 ms; assert `/sys/heartbeat_miss` emitted with count≥3.
- **Decode-thread invariant under load**: 1 kHz of malformed 9 KB OSC packets for 60 s; audio thread RT_ASSERT_NO_ALLOC zero allocations and zero xruns.
- **Port-collision recovery**: `--osc-cmd-port 0`; UI reads chosen port from core stderr; both connect successfully.
- **AudioMatrix runtime reload + C8 click test**: load `matrix_alt.yaml`; routing change applied within 1 block; FFT of reload window contains no spectral spike >10 dB.
- **Handshake mismatch**: UI sends `schema_version=2`; core (v0 = 1) replies `/sys/error`; UI shows explicit dialog.
- **VST3ControlStub end-to-end**: stub registered as second `ExternalControl`; sends test command; assert engine state unchanged; stub `dispatch(Command)` invoked.

### End-to-end tests (real hardware on local runner)

- **Full bootstrap → drag → measured speaker gains**: per-channel gains match expected curve within 1° angular tolerance.
- **Latency harness (P10) with C3 instrumentation**: T0 = OSC packet receive at core, T1 = sample at Dante PCIe buffer; report p50/p95/p99 over 1 h; assert p99 <5 ms (stages 2–6); `tests/latency_harness/baseline.json` records kernel/governor/JACK period/Dante driver/audio interface/PREEMPT_RT y/n.
- **Stage-1 latency harness (P12)**: `pyautogui` + `xdotool` measures Qt event-loop tail; composed with P10 number for spec-claim total.
- **Accuracy harness (P9, C7 redesign)**: rE/rV projection with closed-form analytic reference; CI gate for lab_8ch; lab_4ch reports ceiling.
- **Soak harness (P11) with corrected gates**: 4 h pilot + 12 h full; core RSS slope <1 MB/h; UI <5 MB/h; zero xruns; **zero `/sys/heartbeat_miss` at 300 ms threshold per C6**; **p99 per-block < 933 µs per A1/C2**.
- **Dante loopback (P6)**: 8-ch impulse out → physical loopback → impulse in; channel mapping correct; 30 min Dante soak with 0 xruns; kernel + Digigram driver match P0 pinning.
- **FDN denormal soak (Pre-mortem A) with corrected gate**: feed impulse, then 30 min silence; per-block-time **p99 stays < 933 µs throughout (A1/C2 corrected)**.

### Perceptual tests
- **P3.5 listener-blind smoke**: N=2 listeners; randomized 4-direction; screen-blind; both correct = pass. `docs/onboarding.md` flags non-skippable.
- **P12 perceptual sign-off**: N=12 pre-registered listeners; **`tests/perceptual/listening_test_v0/preregistration.md` filed before data collection per M2**, naming Friedman contrasts (VBAP vs DBAP on lab_8ch; lab_4ch vs lab_8ch on VBAP) and per-direction CI threshold (`mean ≤ 1° AND 95% per-direction CI upper bound ≤ 2°`); reuse vid2spatial v3 stimulus pipeline.

### Observability (`/sys/metrics`)

- **Counters**: `audio_underrun_count`, `osc_packet_rate`, `osc_reject_count`, `osc_reordered_drops` (Pre-mortem C), `osc_reorder_burst_count_1s` (C-MA #4.d), `ipc_queue_depth`, `geometry_cache_version`, `matrix_cache_version`, `heartbeat_miss_count` (C6).
- **Per-thread metrics**: `cpu_pct_audio_thread`, `per_block_time_p50_us`, `per_block_time_p95_us`, `per_block_time_p99_us` (rolling 60 s window).
- **UI-side metrics**: `ui_frame_time_p99_ms`, `ui_process_rss_mb`.
- **Structured logging**: `juce::Logger` to `logs/spatial_engine_core.log`; audio thread → pre-allocated `TraceRing` → control thread.
- **Xrun forensics**: on xrun, dump last 4 audio blocks + last 32 Commands + per-block-time history to `logs/xrun_forensics_{timestamp}.json`.
- **Debug stderr endpoint**: `--debug-stderr-metrics` flag prints metrics every 1 s.

---

## Risk Register (15 items per R2)

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|------------|------------|
| 1 | **Real-time allocation in C++ paths** (Principle 1 violation) | HIGH | MEDIUM | `RT_ASSERT_NO_ALLOC` macro overrides `operator new` to abort during scoped audio-thread call; P4 boundary tests; clang-tidy `cppcoreguidelines-pro-type-*` + `bugprone-*` rules; ADR-required policy for changes to `core/`, `dsp/`, `render/`, `reverb/`, `ipc/`; P1 unit test wraps callback under steady silence for 60 s with `RT_ASSERT_NO_ALLOC`. |
| 2 | **JUCE GPL license trigger at commercial deployment (C5 concretized)** | HIGH (legal) | HIGH (certain at trigger T) | `LICENSE.md` declares GPL v3 for v0; **trigger event = first external distribution of the binary OR first commercial deployment outside the user's research lab**; **named owner = project lead**; v0 deliverable = `docs/license_procurement_plan.md` with cost estimate (developers × deployments × Indie/Pro/Perpetual fee) and procurement timeline; `docs/lab_setup.md` reminds contributors that v0 is GPL and PRs must be GPL-compatible. |
| 3 | **Dante PCIe driver maturity on Linux for Digigram ALP-Dante** | HIGH | MEDIUM | P0 spike pins specific kernel + Digigram-certified driver version (A3); P6 dedicated to Dante I/O verification (impulse loopback, channel mapping, 30 min soak); fallback to built-in HDA via PipeWire-JACK for development if Dante driver delays acquisition; `docs/lab_setup.md` pins Dante driver version + Dante Controller config; vendor support relationship (Digigram) flagged for project kickoff. **Residual risk**: HDA fallback is not Dante PCIe; if vendor relationship fails entirely, acceptance #1 cannot be met for the production path — escalate to spec re-litigation. |
| 4 | **OSC port collision** | LOW | MEDIUM | v0 default ports 9100/9101 (off TouchOSC); P4 ships `--osc-cmd-port` / `--osc-state-port` overrides; `docs/onboarding.md` ephemeral-port-discovery section. |
| 5 | **Position interpolation click on discrete jumps** | HIGH | HIGH (default behavior without mitigation) | P3 implements per-sample linear gain ramp (`GainRamp.h`); **C8 three-part click test** including FFT spectral spike check (substance, not just form); PropagationDelay uses `juce::SmoothedValue`. |
| 6 | **Layout/algorithm mismatch silently producing wrong panning** (Pre-mortem B) | HIGH | MEDIUM | `LayoutCompatibilityChecker` with explicit rule table (P3); validates at config-load *before* audio starts; UI shows error dialog; CI compat harness 6+6 pairs. |
| 7 | **JUCE ↔ python-osc protocol drift** | MEDIUM | HIGH (default in async dev) | `schema_version` u16 + `/sys/protocol_version` handshake; `proto/command_table.json` source-of-truth; CI sync check. |
| 8 | **FDN denormal-induced CPU spike** (Pre-mortem A) | HIGH | MEDIUM | ADR 0004: `juce::ScopedNoDenormals` + per-line ±1e-20 DC offset; P11 idle-with-tails soak gate; **p99 < 933 µs per A1/C2 corrected**. |
| 9 | **Multi-hour memory growth in either process** | HIGH | MEDIUM | P11 soak with explicit RSS slope thresholds; RSS anchors documented; `tracemalloc` snapshots in soak harness. |
| 10 | **OSC packet reordering causing position jitter** (Pre-mortem C) | MEDIUM | LOW | Per-object monotonic `seq` + `last_applied_seq[id]` gate; `osc_reordered_drops` counter; **operator-visible `/sys/warning osc_reorder_burst` alert when ≥5 in any 1 s window per C-MA #4.d**; UI yellow flag indicator. |
| 11 | **Spec scope slip during v0** (M1: likelihood escalated) | MEDIUM | **HIGH** (M1 — escalated from MEDIUM given fresh-spec-freeze 2026-04-28 and 13 vendor-delta rounds; further pushback during multi-month build is materially likely) | Spec v2.1 frozen; scope addition requires deep-interview re-run + new spec version; ADR-required for changes to `OutputBackend`, `RenderingAlgorithm`, `ReverbEngine`, `ExternalControl`. |
| 12 | **JUCE 7.x → 8.x breaking change during v0** | MEDIUM | LOW | JUCE submodule pinned at P0; bump = explicit task with regression test. |
| 13 | **Algorithm runtime swap click (NEW R2 per A4 / ADR 0006)** | MEDIUM | MEDIUM (default behavior without mitigation) | ADR 0006: K=256 sample crossfade with parallel-run scratch (pre-allocated, RT-safe); C8 three-part click test asserts FFT spectral spike <10 dB during swap window; falsifier = `RT_ASSERT_NO_ALLOC` violation OR FFT spike. |
| 14 | **KEMAR SOFA file format/IR length/sample-rate assumption (NEW R2 per Critic §4 / C-MA #6.e)** | MEDIUM | LOW | P0 ships `tools/sofa_inspector.py`; prints SR + IR length + measurement count from real KEMAR file; result documented in `docs/lab_setup.md`; BinauralMonitor partition strategy (A2) finalized at P9 from this metadata; engine startup validates actual file matches assumed config and refuses to load on mismatch. |
| 15 | **Linux kernel + Digigram driver matrix not pinned at P0 (NEW R2 per A3 / Critic §4)** | HIGH | MEDIUM | P0 spike resolves vendor-certified driver/kernel combination; `docs/lab_setup.md` pins specific Ubuntu LTS point release + kernel point version + Digigram driver version + PREEMPT_RT y/n decision; P6 validation gate confirms actual environment matches P0 pin; deviation = `kernel-driver-deviation` issue + plan revision before P10. |

---

## Open Questions for Architect (R2)

These are 5 design questions where the Planner sees genuine tension and wants Architect R2 input. **Most R1 Open Qs are now resolved in body** (BinauralMonitor latency → A2 in body; algorithm dispatch → ADR 0005 with M5; algorithm-swap mechanism → ADR 0006 with K=256). Remaining:

1. **Single FDN bus vs per-algorithm reverb routing** (R1 Open Q #2 carried forward): Planner's P7 has one global FDNReverb with per-object send. Alternative: each `RenderingAlgorithm` routes to its own FDN tap. Spec is silent on per-algorithm tail-shaping; my read = keep one global FDN; validate.

2. **WFS regularity rule strictness** (R1 Open Q #4 with M3 unit fix): rule is now `max_spacing < c/(2·f_max)`. With f_max=8 kHz this fails most realistic lab geometries (spacing > 21.4 mm). Planner's read: enforce strict at v0 and document; v1+ adds hybrid mode. Validate.

3. **AudioMatrix v0 hot-swap with crossfade** (R1 Open Q #5 → resolved as YES in body): Planner's P5 implements hot-swap with `juce::SmoothedValue` per-channel crossfade. Validate that hot-swap is correct interpretation of "runtime change via OSC API".

4. **PREEMPT_RT y/n decision input from Digigram (A3 follow-up)**: P0 spike requires Digigram-certified driver/kernel combination. If vendor only certifies non-RT, the latency budget table reverts to commodity-fallback column. Architect R2 should validate the spike's outcome documentation and PREEMPT_RT decision before P10.

5. **ADR 0006 K=256 crossfade window**: chosen middle between K=64 (too short) and K=512 (2× CPU spike too costly). Architect R2 should validate K=256 against perceptual literature on filter-state crossfades.

---

## Style notes (per requested style)
- Concrete file paths used throughout.
- Spec v2.1 acceptance criterion IDs cited per phase (numbering matches spec §Acceptance Criteria source order; **16 criteria** per C1).
- ADRs (now **6**) carry antithesis-strength reasoning.
- Pre-mortem scenarios genuinely plausible for this stack.
- All 12 must-address items from Architect R1 + Critic R1 addressed in this R2; tier-3 minors applied or documented as deferred.

— end of plan R2 —
