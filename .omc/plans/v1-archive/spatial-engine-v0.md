# Plan: spatial_engine v0 (hybrid native+Python real-time object-based spatial audio engine)

## Header
- **Project**: `spatial_engine` (real-time, object-based spatial audio engine; L-ISA / d&b Soundscape family)
- **Source spec**: `/home/seung/mmhoa/spatial_engine/.omc/specs/deep-interview-spatial-engine.md`
- **Planner version**: round 3 (consensus-loop revised draft)
- **Date**: 2026-04-28
- **Status**: `READY-FOR-CONSENSUS-R3`
- **Working dir**: `/home/seung/mmhoa/spatial_engine/`
- **Mode**: DELIBERATE (escalated by Architect Round 1 — real-time audio + multi-process IPC + production-grade-from-v0 + measured fresh-clone time). Pre-mortem and expanded test plan are included as required top-level sections.

- **Round 2 → Round 3 changelog (Critic-R2-driven)** — item-by-item against Critic R2's 15 numbered "MUST address" items + Tier 3 gaps + Tier 4 minors:
  1. **B1 / Item 1 (ADR 0003 stub)**: ADR 0003 is now FILLED with antithesis-strength reasoning (steelmanned shm+UDS, ZeroMQ, Cap'n Proto; rejected with concrete reasons). Migration falsifier named: `p99 drag-to-render latency > 3.0 ms for ≥1% of windows under 8-object 120 Hz drag in 1 h soak`. "(STUB)" stripped.
  2. **B2 / Item 2 (P9 self-referential)**: P9 acceptance check replaced with energy-vector (rE) / velocity-vector (rV) projection. For each test direction we compute realized `(az_realized, el_realized)` from rE projection of the realized speaker gains; assert `|az_intended − az_realized| ≤ 1°` AND `|rE| ≥ 0.7`. Per-geometry caveat documented: lab_4ch may not deliver ±1° everywhere, that's a layout finding, not a code bug.
  3. **B3 / Item 3 (latency budget)**: New "Latency budget decomposition (end-to-end)" subsection inserted under Risk #2 with term-by-term p99 estimates (Qt event tick, UDP loopback, control-thread schedule jitter, audio callback wait, DSP block compute, DAC out). Sum + named hardware/kernel + fallback (PREEMPT_RT optional, smaller buffer at xrun cost, or relaxed <10 ms target with explicit asterisk).
  4. **M1 / Item 4 (P3.5 listener-blind)**: P3.5 verification rewritten — randomized 4-direction sequence, listener cannot see UI screen, score after the fact. N=2 retained, screen-blindness is the validity-maker.
  5. **M2 / Item 13 (coords frames enumerated)**: `core/coords` test plan now enumerates every (frame, sign) pair with hand-computed expected values: pipeline (RIGHT=+az), AmbiX/SOFA (LEFT=+az), elevation (UP=+el or image-y-down), VBAP convention. Round-trip identity tests demoted to a non-primary check.
  6. **M3 / Item 5 (P9 grid + P13 N pinned)**: P9 grid pinned to *geometry-reachable* directions: lab_4ch = 36 az × 1 el (horizontal); lab_8ch = 36 az × 5 el with named el values `[-15°, 0°, +15°, +30°, +45°]`. P13 listener count pinned at **N=12** (no conditional escalation phrasing).
  7. **M4 / Item 8 (`MAX_BLOCK` + per-object audio-block ownership)**: `MAX_BLOCK = 512` constant added. Per-object audio-block ownership specified: dedicated decoder thread per object owns producer end of a per-object SPSC ring of decoded `f32` frames; audio thread reads N samples non-blocking; if empty, output silence + increment `audio_underrun_count`. File-source uses mlocked sample buffer (no page faults).
  8. **M5 / Item 9 (P5 boundary test split)**: P5 verification restructured into TWO distinct cases: (i) valid-packet drain saturation (audio thread sees max ring fill, no alloc) AND (ii) malformed-packet flood (control thread reject path, no propagation). Both retained; they protect different invariants.
  9. **M6 / Item 10 (`nih-plug` interim checkpoint)**: 1-day `nih-plug` health spike added at end of P11 (HEAD compile + hello-world plugin + upstream activity survey). Findings logged in ADR 0002 follow-up. Trigger condition `re-evaluate-juce` now fires at P11+6 months, not v1 kickoff.
  10. **Item 6 (position interpolation)**: Per-sample linear gain ramp across audio block on `ObjectUpdate` apply specified in P3 / `core/dsp/`. Eliminates click on discrete position jumps. Acceptance #3 ("without click/dropout artifacts") now has an implementation contract.
  11. **Item 7 (`dist_m` v0 semantics)**: `dist_m` v0 semantics chosen — `gain = 1 / max(dist_m, 0.1)^0.5` (-3 dB per doubling amplitude, soft 1/√r rolloff with floor at 0.1 m; chosen over the steeper -6 dB/doubling `1/r` law because v0 lab geometries are small enough that a sharper rolloff would mute mid-distance objects). No LPF in v0; air-absorption LPF deferred to v1. Documented in P3 + IPC schema + `core/src/dsp/distance.rs`.
  12. **Item 11 (IPC `schema_version`)**: `schema_version` field added to every command; startup `/sys/protocol_version` handshake. Documented in ADR 0003 + IPC schema. UI refuses to connect on mismatch with explicit error.
  13. **Item 12 (CI-vs-local split)**: P8/P9/P10 marked local-runner-required (no audio hardware in CI). CI runs unit tests + accuracy harness in null-audio-backend mode (cpal "null" sink writing to WAV). Documented in build/dev tooling section.
  14. **Item 14 (JACK vs PipeWire-JACK)**: Reconciled — recommend **PipeWire-JACK** for v0 (modern Ubuntu defaults, no JACK1/JACK2 install conflict). Pinned in `docs/lab_setup.md` (now P2 deliverable, not P8).
  15. **Item 15 (ADR 0003 in P5)**: "ADR 0003 finalized" moved out of P11 → into P5 (the IPC phase) — typo fix.
  - **Tier 3 gaps filled**: heartbeat backpressure semantics (lossy publish, last-write-wins, never blocks); second-machine definition for P12 (Ubuntu 22.04 LTS + different USB controller + different audio interface OR built-in HDA); memory-budget anchors (core ~50 MB, UI ~150 MB steady-state RSS); file-source AudioInput threading (dedicated decoder thread per object, mlocked buffer, audio thread reads only).
  - **Tier 4 minors cleaned up**: architecture diagram annotated with port-override note; `MAX_OBJECTS = 16 = 2× of 8` prose corrected; pre-commit pinned to `pyright` (drop "or mypy"); `tests/perceptual/p3_5_smoke/` added to repo layout; `proto/trajectory_schema.json` explicitly stated to mirror vid2spatial format `[{t, az_deg, el_deg, dist_m}]`; `assert_no_alloc` crate version pin noted in Cargo.toml + maintainership follow-up; `docs/lab_setup.md` deliverable moved from P8 to P2 (lab pinning artifact must precede measurement).

- **Round 1 → Round 2 changelog (Architect-driven, preserved for audit trail)**:
  1. P5 contract — OSC decode pinned to control thread; SPSC `ObjectUpdate` is the only audio-crossing type; `assert_no_alloc` boundary test mandated.
  2. ADR 0001 — added single-process Rust + `egui` as a considered-and-rejected alternative with concrete reasons.
  3. Acceptance #10 — added `bootstrap.sh` / `just bootstrap` in P1; P12 hard target ≤60 min on clean Ubuntu 22.04 with `docs/onboarding_timing.md` artifact.
  4. P3.5 — informal 4-direction perceptual smoke (N=2) inserted between P3 and P4.
  5. T3 geometry edit — explicitly chose option (b): geometry editing is a v0 Non-Goal; three concrete UI-modification examples added.
  6. Risk #2 — lab kernel (Linux 6.x generic, NOT PREEMPT_RT for v0), audio device class, and P8 buffer-size target (64 frames @ 48 kHz = 1.33 ms one-way) named.
  7. Risk #6 — UI-process RSS soak sub-test added in P10 with explicit thresholds and tooling.
  8. ADR 0002 — `nih-plug` 1.0 falsifier trigger condition written in.
  - Bonus: Risk #9 (IPC port collision) added; `proto/trajectory_schema.json` placeholder added to repo layout.
  - DELIBERATE additions: Pre-mortem (3 scenarios) + Expanded test plan (unit/integration/e2e/observability) added as new top-level sections.

---

## RALPLAN-DR Summary

### Principles (5)
1. **Audio path is real-time-safe end-to-end** — the audio thread does no heap allocation, no blocking syscalls, no locks beyond lock-free queues; Python never touches an audio sample.
2. **`OutputBackend` is an explicit, stable abstraction from day one** — `SpeakerArray` (v0), `BinauralRenderer` (v1+), `DAWPlugin` (v1+) plug in without engine-core changes (per spec §Acceptance Criteria, §Constraints v0/v1+).
3. **Pre-production-grade numerics apply to v0 already** — 8+ objects, ±1°, <5 ms, multi-hour. The v0 audio core is preserved into v1; only the UI is throw-away (per spec §Constraints, §Round 5/6 resolution).
4. **Coordinate convention is one inherited contract** — `(az, el)` radians + `dist_m`; `az = atan2(x, z)` (RIGHT = az>0). Sign-flip to AmbiX/SOFA conventions is centralized in one helper, never re-derived per call site (per spec §Technical Context, MMHOA memory).
5. **UI iteration speed beats UI sophistication** — Python/PySide6 UI optimizes for engineer-collaborator clone-build-modify in under one afternoon; throw-away by design (per spec §Constraints v0).

### Decision Drivers (top 3)
1. **Latency budget <5 ms end-to-end on commodity Linux audio I/O.** Dominates language choice, IPC choice, buffer-size choice, and OS audio stack (JACK vs PulseAudio vs PipeWire-JACK).
2. **v0→v1 transition cost.** v1 keeps the audio core, rewrites the UI. So the *core* must be the production-grade choice now; UI may be sloppy.
3. **Engineer iteration speed (clone → build → modify a UI element end-to-end in one afternoon).** Required by spec acceptance criterion ("engineer collaborator can clone, build, run, and modify a UI element end-to-end").

### Viable Options for the 6 Open Items

#### (a) Native audio core language
- **Option A1: Rust + `cpal` + `crossbeam`**
  - Pros: memory safety in real-time path; mature `cpal`/`jack`/`coreaudio-sys` crates; cargo build reproducibility; engineers can `cargo build && cargo run` with one command; matches modern audio dev trends.
  - Cons: VST3/AU plugin tooling is less mature than C++ (revisit in v1 for `DAWPlugin` backend); fewer ready-made DSP primitives vs JUCE; smaller pool of audio DSP devs.
- **Option A2: C++ + JUCE**
  - Pros: industry-reference framework; VST/AU/AAX path is built in; massive DSP primitive library; large pool of contributors familiar with JUCE.
  - Cons: heavier toolchain (CMake + Projucer/CMake+JUCE module system); higher risk of UB / leaks in real-time path; slower iteration (`cmake --build`) vs `cargo`; license complexity for closed-source v1+.
- **Option A3: C + PortAudio (raw)**
  - Pros: minimal dependency surface; lowest possible toolchain weight; portable.
  - Cons: re-invents wheels (no DSP primitives, no FFI helpers); higher risk in real-time safety; weak ergonomics for a 6+ month codebase; v1 path to plugins is harder than JUCE and not better than Rust.

#### (b) IPC transport (native core ↔ Python UI)
- **Option B1: OSC over UDP**
  - Pros: matches L-ISA / d&b Soundscape control idiom (per spec §Technical Context); rich tooling (`python-osc`, `rosc`); message-oriented; future external-controller compatibility comes free.
  - Cons: UDP is best-effort (packet loss for `set-object-position` during fast drag must be coalesced/redelivered at app level); no native back-pressure; no easy pub/sub of full state without custom layer.
- **Option B2: Shared-memory ring buffer (state mirror) + UDS control socket (commands/acks)**
  - Pros: lowest possible latency for state mirroring; bounded zero-copy; clean separation of "current truth" (shm) from "request/response" (UDS).
  - Cons: more code to write and verify; OS-specific (POSIX shm); harder for an engineer to inspect with off-the-shelf tools; Python `multiprocessing.shared_memory` introduces GIL/GC concerns for the reader.
- **Option B3: ZeroMQ (PUB/SUB + REQ/REP over IPC or TCP)**
  - Pros: rich patterns out of the box; back-pressure and high-water-mark; cross-language bindings; loopback over `ipc://` is fast.
  - Cons: another runtime dep on both sides; opaque to engineers without ZMQ experience; serialization choice (msgpack/protobuf) is still a separate decision.
- **Option B4: Cap'n Proto over Unix Domain Socket**
  - Pros: schema-first messaging with codegen for both sides; fast serialization; UDS is local-only and reliable.
  - Cons: schema compiler in build pipeline; less L-ISA-idiomatic than OSC; no obvious external-controller story for v1+; team unfamiliar with Cap'n Proto raises the learning-curve tax.

#### (c) Engineer collaboration cadence
- **Option C1: Async PR review + weekly sync**
  - Pros: scales to N>1 engineers; review trail in git; matches typical OSS rhythm; non-blocking.
  - Cons: feedback latency 1–2 days; UI tweaks (the throw-away part) feel heavyweight under PR ceremony.
- **Option C2: Pair-programming sessions + direct push**
  - Pros: fastest iteration for a 1–2 person team; perfect for UI/UX co-design.
  - Cons: poor audit trail; doesn't scale past 2; no review pass for the audio-core code where review actually matters.
- **Option C3: Design-doc-first then async impl** (ADR per non-trivial change in `docs/adr/`)
  - Pros: forces the v0→v1 transition risks to surface in writing; builds an institutional memory; matches the consensus-loop ethos.
  - Cons: front-loads ceremony; can stall trivial UI changes.
- **Option C4 (recommended composite): Async PR review + weekly sync, with ADR-required only for changes that touch (audio core, IPC schema, OutputBackend trait, coordinate convention).**
  - Pros: review where it matters (real-time path, public interfaces); freedom where it doesn't (UI tweaks merge fast); ADR trail satisfies v0→v1 transition record.
  - Cons: requires policing the "ADR-required" surface; review reviewer must be assigned.

#### (d) v0 audio source
- **Option D1: File playback only (WAV/FLAC, looped)**
  - Pros: simplest; no I/O hardware coupling; reproducible test signals.
  - Cons: doesn't validate live-input real-time path; no interactive demos.
- **Option D2: File + offline-rendered synth (test tones generated from `synth3d`/MMAudio offline, played as files)**
  - Pros: keeps v0 simple; gets diverse content (transients, sustained tones, broadband noise) without live-mic complexity; deterministic for the verification harness.
  - Cons: still doesn't validate live-input path; v1 will need a separate live-input bringup.
- **Option D3: File + live mic (ALSA capture)**
  - Pros: validates the live-input real-time path now; most realistic demo.
  - Cons: doubles I/O hardware bringup work; mic feedback risks during lab demos; harder to test deterministically.
- **Option D4: All three (file + offline-rendered synth + live mic)**
  - Pros: covers the full input space.
  - Cons: scope creep for v0; live-mic was *not* a hard requirement in spec §Constraints.

#### (e) Speaker geometry input
- **Option E1: Hardcoded constants in source**
  - Pros: zero infra; simplest possible.
  - Cons: any geometry change is a recompile; collaborators must edit source to try a new room; violates the "modify a UI element end-to-end" acceptance bar.
- **Option E2: YAML/JSON config file (loaded by both core and UI at startup)**
  - Pros: one source of truth; checked into git; collaborators edit text; easy to ship multiple lab presets (`lab_4ch.yaml`, `lab_8ch.yaml`).
  - Cons: change at runtime requires restart; risk of core/UI drift if the file is parsed twice with two schemas — must be parsed once and shipped over IPC.
- **Option E3: In-GUI editor with live application**
  - Pros: best UX; supports drag-arrange of speakers.
  - Cons: pure scope creep for v0; not on the acceptance list.
- **Option E4 (recommended composite): Config file as source of truth, parsed by core only, mirrored to UI over IPC `list-devices`/`get-geometry` message.**
  - Pros: single parser; UI is read-only display of geometry in v0; leaves the in-GUI editor as a v1 feature.
  - Cons: requires geometry message in IPC schema from day one (acceptable — it must be there anyway).

#### (f) Positional-accuracy verification method
- **Option F1: Numerical only (compare intended panning vector to realized speaker gains)**
  - Pros: deterministic; CI-friendly; fast; gives a single ±° number per test point; sufficient for VBAP correctness.
  - Cons: doesn't capture perceptual phantom-image accuracy in a real room (room reflections, speaker mismatch).
- **Option F2: Perceptual listening test only**
  - Pros: matches the spec's "apparent positional accuracy" language directly.
  - Cons: slow; needs N≥6 listeners for any signal; not CI-runnable; user memory shows N=3 is "informal validation" only.
- **Option F3: Both** — numerical for CI/regression, perceptual for v0 sign-off
  - Pros: numerical catches regressions on every commit; perceptual gives the spec-aligned ±1° claim weight.
  - Cons: double the harness work; perceptual test schedule is the long-pole.
- **Option F4: Numerical primary in v0; perceptual as a separately-scheduled v0 sign-off event (not a CI gate)**
  - Pros: keeps CI fast; defers perceptual scheduling; still produces a perceptual number for the final v0 acceptance review.
  - Cons: a few weeks gap between code-complete and perceptual-validated.

---

## Open Items Resolution

### (a) Native audio core language → **Rust + `cpal` + `crossbeam` (Option A1)**
**Rationale.** Driver #2 (v0→v1 transition cost) demands the *core* be the production-grade choice now. Rust's memory-safety guarantees on the real-time path directly serve Principle 1 (no heap, no locks, no blocking) — `cpal`'s callback model + `crossbeam` SPSC ring gives us a lock-free, allocation-free audio thread by construction. JUCE's only decisive advantage is VST3/AU plugin packaging, which spec §Non-Goals explicitly defers ("DAW plugin packaging — architecturally reachable but not built in v0"). C with PortAudio loses on ergonomics over a multi-quarter codebase. Driver #3 (engineer iteration) is satisfied: `cargo build && cargo run` is a one-command bringup. **Architect attention**: if v1 prioritizes a VST3 plugin over a binaural backend, Rust→`nih-plug` is the path; flag if Architect believes `nih-plug` maturity is a concern.

### (b) IPC transport → **OSC over UDP (loopback) for v0; revisit at v1 (Option B1)**
**Rationale.** Drivers #1 and #3 both favor OSC for v0. Spec §Technical Context explicitly cites the L-ISA/Soundscape OSC idiom. OSC's tooling depth means an engineer can plug a TouchOSC iPad in on day one as a free debug controller. Latency on UDP loopback is sub-millisecond (well inside our 5 ms budget). The legitimate concerns — packet loss during fast drag, no back-pressure — are mitigated by (i) coalescing `set-object-position` to a max rate (~120 Hz) at the UI side, (ii) sequence numbers for last-write-wins on the core side, (iii) periodic full-state heartbeat. Shared-mem (B2) is the right v1 answer once we know real bottlenecks; doing it now is premature optimization.

**Decode-thread pinning (Architect Round 1, Item 1 — Principle 1)**: OSC byte-buffer decoding (`rosc::decode` invocation) is pinned to the named control thread inside `core/src/ipc/osc_decode.rs`. The audio thread never sees raw OSC bytes. The only type that crosses the SPSC boundary into the audio thread is `ObjectUpdate` (a fixed-size `enum` with no heap-allocating variants). This boundary is verified by an `assert_no_alloc`-wrapped property test on the audio-path side (see P2 / P5 verification). The architecture diagram annotation makes this explicit.

### (c) Engineer collaboration cadence → **C4: Async PR review + weekly sync, ADR-required for protected surfaces**
**Rationale.** Driver #2 (v0→v1 transition) demands a written record of decisions on the surfaces that survive the rewrite (audio core, IPC schema, `OutputBackend` trait, coordinate convention). Pure async-PR (C1) misses this; pair-programming (C2) doesn't scale and skips review where it matters most; doc-first-everywhere (C3) over-taxes UI iteration. C4 is the minimum-friction policy that protects the surfaces v1 will inherit while leaving UI churn unblocked. ADRs live in `docs/adr/` with monotonic numbering.

### (d) v0 audio source → **File + offline-rendered synth (Option D2)**
**Rationale.** Principle 5 (UI iteration over UI sophistication) and Driver #3 say minimize bringup work that doesn't advance the audio core. File+offline-synth gives the verification harness deterministic test signals (sine, white noise, transient clicks for latency measurement) and gives demos diverse content without live-mic complexity. Live-mic (D3) is a v1 add (one new `AudioInput` impl behind the existing trait — no architectural rewrite). All-three (D4) is scope creep relative to spec §Constraints.

### (e) Speaker geometry input → **Config file (YAML), parsed by core only, mirrored to UI via IPC (Option E4)**
**Rationale.** Principle 4 (one source of truth for coordinate convention) generalizes: one source of truth for *any* geometry the core and UI share. A single parser in the core eliminates the drift risk between two YAML readers. The UI gets geometry from `get-geometry` IPC at startup. Hardcoding (E1) fails the "modify end-to-end" criterion; in-GUI editor (E3) is v1 scope. Configs ship as `configs/lab_4ch.yaml`, `configs/lab_8ch.yaml`.

### (f) Positional-accuracy verification → **F4: Numerical primary in v0 CI; perceptual as scheduled v0 sign-off event**
**Rationale.** Driver #1 (latency) and Driver #3 (iteration) both want a fast CI-runnable accuracy check. Numerical pan-vector check (F1) gives ±° per test point in milliseconds. The spec's `apparent positional accuracy ±1°` language, however, has a perceptual flavor and the user memory (`vid2spatial` listening test v3) shows the team takes perceptual evaluation seriously. F4 splits the timeline: numerical gates every commit; one perceptual session (N≥6, probably reusing `vid2spatial` listening-test infra) gates v0 final sign-off. **Architect attention**: confirm whether N=6 is acceptable for v0 sign-off or whether N≥12 (the user's stated bar for paper-grade claims) is required here too.

---

## Architecture

### Process model
```
┌────────────────────────────────────────────────────────────┐
│ Process A: Native Audio Core (Rust)                        │
│                                                            │
│  ┌──────────────────────┐    ┌────────────────────────┐    │
│  │ Audio Thread (RT)    │◄───│ Lock-free SPSC ring    │    │
│  │  - cpal callback     │    │ (control updates in)   │    │
│  │  - DSP graph tick    │    └────────────▲───────────┘    │
│  │  - per-obj pan/gain  │                 │                │
│  │  - OutputBackend.tick│    ┌────────────┴───────────┐    │
│  └──────────┬───────────┘    │ Control Thread         │    │
│             │                │  - OSC server (UDP)    │    │
│             │                │  - osc_decode (parse)  │    │
│             │                │  - state model owner   │    │
│             │                │  - heartbeat publisher │    │
│             ▼                │  - device enumeration  │    │
│        speaker outs          └────────────▲───────────┘    │
│                                                            │
│  NOTE: OSC byte→struct decoding runs on the control thread │
│  only. SPSC ring carries `ObjectUpdate` enum (fixed-size,  │
│  no heap variants) — the *only* type crossing into audio.  │
│                                           │                │
└───────────────────────────────────────────┼────────────────┘
                                            │ OSC/UDP loopback
                                            │ (DEFAULTS: port 9000 cmd, 9001 state;
                                            │  override via --osc-cmd-port /
                                            │  --osc-state-port at startup, see Risk #9)
                                            ▼
┌────────────────────────────────────────────────────────────┐
│ Process B: Python UI (PySide6)                             │
│                                                            │
│  ┌──────────────────────┐    ┌────────────────────────┐    │
│  │ Qt Main Thread       │◄───│ IPC Client Thread      │    │
│  │  - Top-down view     │    │  - python-osc client   │    │
│  │  - Drag controller   │    │  - state mirror        │    │
│  │  - Param panels      │    │  - rate-limit/coalesce │    │
│  │  - Scene mgmt        │    └────────────────────────┘    │
│  └──────────────────────┘                                  │
└────────────────────────────────────────────────────────────┘
```

**Audio thread isolation rules** (Principle 1):
- No `Box::new` / `vec![]` / `String::new` after init.
- No `Mutex`, no `RwLock`. Only lock-free queues (`crossbeam::queue::ArrayQueue` SPSC).
- No `println!`, no `log::` macros (use a pre-allocated lock-free ring for trace events, drained by control thread).
- Pre-allocate all per-object DSP state at config-load time; cap `MAX_OBJECTS = 16` (8 hard requirement × 2 = 1× headroom for transient spawn/despawn churn during demos; if 16 proves tight in practice, raise to 24 in a follow-up — the constant is a single `pub const` in `core/src/lib.rs`).
- **`MAX_BLOCK = 512`** (Critic R2 M4 / Item 8): per-call upper bound on `render()` block size in frames. cpal callback block size is observed at `prepare()` time and must not exceed `MAX_BLOCK`. All per-object scratch buffers (gain ramps, sample reads, pan-vector temporaries) pre-allocated to `MAX_BLOCK` frames at init. If cpal reports a callback size > `MAX_BLOCK`, the engine refuses to start with a clear error (configuration mismatch).
- **Per-object audio-block ownership model** (Critic R2 M4 / Item 8):
  - Each `Object` owns a per-object **SPSC ring of decoded `f32` frames** (capacity = `4 × MAX_BLOCK` frames per channel). The audio thread is the consumer; a dedicated **decoder thread** per object (or a small pool, one thread per `MAX_OBJECTS / pool_size` objects) is the producer.
  - File-source decoder threads `mlock` the underlying sample buffer (no page faults at audio-thread priority — addresses the gap "audio thread reads from mmap directly = page-fault hazard"). Decoded sample fills happen on the decoder thread, NOT the audio thread.
  - Audio thread reads N = `block_size` samples per object per callback, non-blocking. If the per-object ring is empty (decoder underflow), audio thread emits silence for that object's contribution and increments `audio_underrun_count.per_object[id]`.
  - Per-object position/gain (the `ObjectFrame` SOA fields) come from a separate SPSC of `ObjectUpdate` messages owned by the OSC decode/control thread (already specified in P5).
  - The `ObjectFrame` slice passed to `render()` holds `&[f32]` borrows into the per-object ring's read window — borrows are valid for the duration of the call only; the audio thread releases them before yielding.
- `cpal` callback returns within deadline or we underrun — measured by xrun counter exposed to control thread.
- **Decode-thread invariant** (Architect R1, Item 1): OSC byte parsing happens *only* in `core/src/ipc/osc_decode.rs`, which runs on the named control thread. The audio thread reads only the SPSC `ObjectUpdate` enum (a fixed-size, no-heap-variant message type). Verified by `assert_no_alloc` test wrapping the audio callback during integration tests.

### Native core module boundaries (Rust)
```
core/
├── audio_io/        # cpal device open, callback wiring, xrun counter
├── dsp/             # per-object pan/gain, low-pass, peak-limit; SOA layout
├── graph/           # DSP graph: AudioInput → Object → Mixer → OutputBackend
├── output_backend/  # trait OutputBackend; impls: SpeakerArray, BinauralRenderer (stub), DAWPlugin (stub)
├── state/           # control-thread-owned ObjectState, GeometryState; SPSC pub to audio
├── ipc/             # OSC server (control-thread); see osc_decode.rs
│   ├── mod.rs
│   ├── osc_decode.rs   # Architect R1, Item 1: byte→ObjectUpdate parse runs on control thread ONLY
│   ├── object_update.rs # SPSC enum: the ONLY type crossing to audio thread
│   ├── rate_limit.rs
│   └── heartbeat.rs
├── geometry/        # SpeakerLayout loader (from configs/*.yaml), VBAP triangulation
├── coords/          # one helper for az atan2/AmbiX sign-flip (Principle 4)
└── bin/             # spatial_engine_core binary entry
```

**`OutputBackend` trait (Rust pseudocode):**
```rust
pub trait OutputBackend: Send {
    fn channel_count(&self) -> usize;
    fn prepare(&mut self, sample_rate: u32, max_block: usize, geometry: &SpeakerLayout) -> Result<(), BackendError>;
    fn render(
        &mut self,
        objects: &[ObjectFrame],   // SOA: positions + gains + per-obj audio block
        out_channels: &mut [&mut [f32]], // length == channel_count()
    );
    fn shutdown(&mut self);
}
```
Concrete impls planned:
- `SpeakerArray` (v0) — VBAP gains → multi-channel out.
- `BinauralRenderer` (v1+) — per-object HRTF convolution (KEMAR SOFA reuse from vid2spatial).
- `DAWPlugin` (v1+) — wraps a `nih-plug` adapter exposing object positions as plugin params.

A new backend is added by `impl OutputBackend for MyBackend {}` and registering in a `match backend_type` table in `output_backend/mod.rs` — the engine core, the IPC layer, the state model, and the UI all stay untouched.

### Python UI module boundaries
```
ui/
├── spatial_engine_ui/
│   ├── app.py            # QApplication entry
│   ├── ipc/              # python-osc client; rate-limit drag at 120 Hz; coalesce by obj-id
│   ├── state/            # ObjectModel, GeometryModel; mirrors core state
│   ├── views/
│   │   ├── topdown.py    # QGraphicsView of top-down speaker layout + objects
│   │   └── panels.py     # per-object gain/source param panels
│   ├── controllers/
│   │   └── drag.py       # mouse → (az, dist) for the dragged object
│   └── scene/            # scene save/load (object positions to JSON)
├── pyproject.toml
└── README.md
```

### IPC schema (OSC pseudocode, address-pattern style)

**Schema versioning (Critic R2 Item 11)**: every command carries a `schema_version` *u16* as its first argument (after the address pattern). v0 ships `schema_version = 1`. The startup handshake `/sys/protocol_version` is exchanged before any data flows: UI sends `/sys/protocol_version ,i 1` immediately after `/sys/subscribe`; core replies with its supported version. On mismatch, UI logs an explicit error and refuses to send commands. v0→v1 IPC migration bumps the version; v0 cores reject v1 commands and vice versa.

**`dist_m` semantics in v0 (Critic R2 Item 7)**: `dist_m` is applied as a soft-rolloff gain `g_dist = 1 / max(dist_m, 0.1)^0.5` (i.e. -3 dB per doubling of distance amplitude, soft 1/√r rolloff with a floor at 0.1 m to avoid singularity; the gentler-than-`1/r` rolloff is chosen because v0 lab geometries are small enough that a -6 dB/doubling law would mute mid-distance objects). No low-pass filter is applied in v0; air-absorption LPF is deferred to v1 (per Risk #2 budget). VBAP direction (the (az, el) part) and `g_dist` (the distance part) are independent: VBAP gives unit-norm speaker gains, then `g_dist` scales the per-object source gain before feeding the VBAP mix.

**Client → Server (UI → Core):**
```
/sys/protocol_version ,i    schema_version              # handshake; FIRST message after subscribe
/obj/{id}/pos        ,ifff  schema_ver az_rad el_rad dist_m  # rate-limited 120 Hz, coalesced last-write-wins per id
/obj/{id}/gain       ,if    schema_ver gain_lin
/obj/{id}/source     ,isi   schema_ver source_kind source_path_or_idx
/obj/{id}/spawn      ,iiss  schema_ver id source_kind source_path
/obj/{id}/despawn    ,ii    schema_ver id
/sys/subscribe       ,ii    schema_ver client_port      # UI declares state-receive port
/sys/unsubscribe     ,ii    schema_ver client_port
/sys/list_devices    ,i     schema_ver                  # request → triggers /sys/devices reply
/sys/get_geometry    ,i     schema_ver                  # request → triggers /sys/geometry reply
/sys/load_geometry   ,is    schema_ver yaml_path
```

**Server → Client (Core → UI):**
```
/sys/protocol_version ,i    server_schema_version       # reply to UI handshake
/sys/state           ,ib    schema_ver <flatbuffer-or-json blob: full object list + positions + gains>
                                 # heartbeat at 30 Hz; full state always; LOSSY (see backpressure note)
/sys/devices         ,ib    schema_ver <list of available audio devices>
/sys/geometry        ,ib    schema_ver <SpeakerLayout: channel_count, positions[(az,el,dist)...]>
/sys/ack             ,iis   schema_ver seq_num msg      # ack of last-received command
/sys/error           ,iis   schema_ver seq_num msg
/sys/xruns           ,ii    schema_ver count_since_start # exposes audio-thread health
```

**Rate-limit / coalescing notes**:
- UI drags mouse → 1 `/obj/{id}/pos` per 8.3 ms (120 Hz). UI coalesces faster events to one per object per tick.
- Core control thread: bounded SPSC queue size = 256 entries; on overflow, drop oldest *position* updates for the same object (last-write-wins semantics).
- `/sys/state` heartbeat at 30 Hz lets the UI re-sync if it ever falls behind.
- Sequence numbers on every command; last-applied seq per object echoed in `/sys/ack`.

**Heartbeat backpressure semantics (Critic R2 Tier 3)**: `/sys/state` is **lossy publish** — if the UI's `python-osc` listener falls behind 30 Hz, the core drops the *oldest* pending heartbeat (last-write-wins on system state). The core never blocks waiting for the UI to drain. The control thread maintains a single-slot heartbeat staging buffer overwritten on each tick; the OSC send path is non-blocking UDP `sendto` with `EAGAIN` treated as a drop. UI must be prepared to receive heartbeats with arbitrary gaps and resync on next-arrived state. This is consistent with the spec's "GC stalls must not propagate to the audio path" requirement: even an unresponsive UI cannot back-pressure the core.

---

## Reuse Decisions for Sibling MMHOA Assets

| Asset | Decision | Why |
|-------|----------|-----|
| **vid2spatial: KEMAR SOFA** (`/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa`) | **Link, defer use to v1** | `BinauralRenderer` is a v1+ backend; v0 has only a stub. Path is recorded in `configs/binaural.yaml` so v1 plugs in without re-discovery. |
| **vid2spatial: FOA encoders** (`encode_mono_to_foa`) | **Re-derive in Rust if/when needed; do not link Python** | Audio path is real-time-safe Rust. FOA encoding is trivial (4 SH coeffs); cheaper to re-port than to FFI. Coordinate-convention sign-flip (memory: "negate az before FOA encoding") becomes a unit test in `core/coords`. |
| **vid2spatial: trajectory JSON schema** (`{t, az_deg, el_deg, dist_m}`) | **Reuse as future input format** (v1+) | Spec §Non-Goals: "trajectory generation modules — kept as future input plugins." v0 supports only manual drag; v1 adds a `TrajectoryPlayer` `AudioInput`/control source that consumes this schema unmodified. |
| **vid2spatial: coordinate convention** (`az = atan2(x, z)`, RIGHT = az>0; AmbiX flip) | **Reuse exactly; centralize in `core/coords`** | Principle 4. Single helper, unit-tested, cited in inline doc comments. |
| **text2hoa** | **Out of scope for v0; informational reference for v1+ HOA backend** | v0 is multi-speaker VBAP. HOA-out backend is plausibly a v2 backend, not part of the v0 OutputBackend stub set. |
| **text2traj_v3** (text → keyframe list) | **Deferred to v1+** | Spec §Non-Goals lists it as future input plugin. v0's IPC schema has `/obj/{id}/pos` so plugging text2traj in v1 means writing a small adapter that publishes positions over OSC. No engine changes needed. |
| **MMAudio** | **Deferred to v1+; consider for offline-rendered synth source files** | We can pre-render diverse test content with MMAudio for the file-source `AudioInput`; this is a one-off content task, not a runtime dependency. |
| **vid2spatial listening-test v3 infra** | **Reuse for v0 perceptual sign-off** | Open Item (f) F4 plan: schedule a perceptual session reusing the v3 stimulus / config / scoring pipeline. |

---

## Repository Layout (`/home/seung/mmhoa/spatial_engine/`)

```
spatial_engine/
├── core/                        # Rust audio core (cargo workspace member)
│   ├── Cargo.toml
│   └── src/
│       ├── audio_io/
│       ├── dsp/
│       ├── graph/
│       ├── output_backend/
│       ├── state/
│       ├── ipc/
│       ├── geometry/
│       ├── coords/
│       ├── lib.rs
│       └── bin/
│           └── spatial_engine_core.rs
├── ui/                          # Python UI (PySide6)
│   ├── spatial_engine_ui/
│   ├── pyproject.toml
│   └── tests/
├── proto/                       # Shared schema (OSC address conventions, JSON envelopes)
│   ├── ipc_schema.md            # protocol_version handshake + schema_version field on every command
│   ├── geometry_schema.json     # JSON-schema for configs/*.yaml
│   └── trajectory_schema.json   # MIRRORS vid2spatial format `[{t, az_deg, el_deg, dist_m}]` (Critic R2 Tier 4)
│                                # so a future v1 TrajectoryPlayer input plugin reuses the format unchanged
├── configs/
│   ├── lab_4ch.yaml
│   ├── lab_8ch.yaml
│   ├── binaural.yaml            # placeholder for v1 KEMAR SOFA path
│   └── default.yaml             # selects a layout + audio device + IPC ports
├── tests/
│   ├── core_unit/               # cargo test (lives inside core/)
│   ├── e2e/                     # spawns core+UI, sends OSC, verifies output
│   ├── latency_harness/         # loopback latency measurement (impulse → onset)
│   ├── soak_harness/            # multi-hour run + memory/xrun sampling
│   ├── accuracy_harness/        # rE-vector vs intended-direction (Critic R2 B2)
│   └── perceptual/
│       ├── p3_5_smoke/          # listener-blind 4-direction smoke test (Critic R2 M1)
│       └── listening_test_v0/   # P13 perceptual sign-off, N=12 (reuses vid2spatial v3 infra)
├── tools/
│   ├── render_test_signals.py   # offline content (sine/noise/transients) for v0 sources
│   └── osc_debug_console.py     # stdin → OSC, /sys/state → stdout
├── docs/
│   ├── README.md
│   ├── architecture.md
│   ├── ipc_schema.md
│   ├── coordinate_convention.md
│   ├── lab_setup.md              # P2 deliverable (Critic R2 Tier 4): kernel + governor + audio device + PipeWire-JACK pinning
│   ├── onboarding.md             # bootstrap walkthrough + ephemeral-port-discovery section (Architect R1 bonus, Risk #9)
│   ├── onboarding_timing.md      # measured fresh-clone bringup time on Ubuntu 22.04 (Architect R1 Item 3, P12 artifact)
│   ├── p3_5_smoke.md             # listener-blind 4-direction perceptual smoke results log (Critic R2 M1)
│   ├── ui_modification_examples.md # three concrete "modify a UI element" examples for acceptance #10 (Architect R1 Item 5)
│   ├── latency_budget.md         # term-by-term p99 budget decomposition (Critic R2 B3)
│   └── adr/
│       ├── 0001-stack-hybrid-native-python.md
│       ├── 0002-native-language-rust.md
│       └── 0003-ipc-transport.md      # FILLED in round 3 (Critic R2 B1)
├── Cargo.toml                   # workspace root
├── pyproject.toml               # uv-managed Python tooling root (or pixi)
├── justfile                     # one-command tasks: just bootstrap / just build / just run / just soak
├── bootstrap.sh                 # non-interactive fresh-clone bringup: rustup → cargo build → uv sync → pre-commit install (Architect R1 Item 3)
├── .pre-commit-config.yaml
├── .gitignore
└── README.md
```

### Build / dev tooling
- **Rust**: `cargo` workspace at root; pinned toolchain in `rust-toolchain.toml` (stable, bumped quarterly). `assert_no_alloc` crate version pinned in `core/Cargo.toml` (Critic R2 Tier 4); follow-up issue tracks the crate's maintenance status quarterly — if maintenance lapses, the test infrastructure has a single-point-of-failure and we either vendor or replace.
- **Python**: `uv` (project manager) with `pyproject.toml`; lockfile committed (`uv.lock`).
- **Task runner**: `justfile` exposing `just build`, `just run`, `just test`, `just soak`, `just measure-latency`, `just accuracy`.
- **Pre-commit**: `cargo fmt`, `cargo clippy -- -D warnings`, `ruff`, `pyright`, `yamllint` for `configs/*.yaml`. (Critic R2 Tier 4: `pyright` only — drop "or mypy" to avoid two-typechecker drift.)
- **CI vs local-runner split (Critic R2 Item 12)**:
  - **CI (GitHub Actions Linux)** runs: all unit tests (`cargo test`, `pytest`); all integration tests that don't need real audio hardware; the **accuracy harness in null-audio backend mode** — `cpal` configured with a "null" sink that writes deterministic WAVs to `/tmp` instead of opening a device, so VBAP gain math + rE-vector projection can be verified deterministically without hardware.
  - **Local runner only (lab machine)** runs: latency harness (P8); perceptual sign-off (P13); soak harness (P10) — these need real audio interfaces. PRs that touch real-audio paths are gated by a `local-verify-required` label, with results posted as a comment by the lab machine's reviewer (until/unless we set up a self-hosted lab runner).
  - The cpal "null" sink is implemented in `core/src/audio_io/null_backend.rs` behind `#[cfg(test)]` and a `--null-audio` CLI flag for offline runs.

### README outline (top-level bullets)
- What the engine is (one paragraph + diagram).
- Acceptance numerics (8+ obj / ±1° / <5 ms / multi-hour).
- Quickstart: `just bootstrap && just build && just run` (bootstrap performs non-interactive rustup + cargo + uv + pre-commit install).
- Architecture summary + link to `docs/architecture.md`.
- IPC schema link.
- How to add a new `OutputBackend`.
- How to define a new speaker geometry (`configs/`).
- **What "modify a UI element end-to-end" means in v0** (acceptance #10 interpretation, Architect R1 Item 5):
  - Geometry editing (drag-arrange speakers, change channel count, edit lab YAML through the GUI) is **explicitly a v0 Non-Goal**; geometry remains config-file-and-restart in v0 (extends spec §Non-Goals reflection).
  - Three concrete example modifications a collaborator should be able to do in one afternoon:
    1. Add a per-object gain meter widget to the right-side parameter panel.
    2. Change the drag sensitivity / smoothing time constant in `controllers/drag.py`.
    3. Recolor an object dot or swap its icon in the top-down view.
  - Detail and walkthroughs live in `docs/ui_modification_examples.md`.
- Engineer collaboration policy (PR review + ADR rules).
- Pointers to sibling repos (vid2spatial, text2traj_v3, etc.).
- License + status.

---

## Implementation Phases

| # | Goal | Outputs | Verification | Depends on |
|---|------|---------|--------------|------------|
| **P1** | Bootstrap repo + tooling: cargo workspace, uv-managed UI, justfile, pre-commit, README skeleton, ADRs 0001/0002, ADR 0003 stub. **Ship `bootstrap.sh` / `just bootstrap`** (Architect R1 Item 3) that performs full fresh-clone setup non-interactively (rustup install + toolchain pin → `cargo build` → `uv sync` → `pre-commit install`). | `Cargo.toml`, `core/`, `ui/pyproject.toml`, `justfile`, `bootstrap.sh`, `.pre-commit-config.yaml`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/adr/0003-*.md` (stub), `docs/onboarding.md` (skeleton with ephemeral-port-discovery section per Risk #9), `README.md`. | `just bootstrap` runs to completion non-interactively on a clean container; `just build` succeeds; `cargo test` and `pytest` produce zero tests but green; pre-commit hooks pass on a no-op commit. | — |
| **P2** | Native audio core skeleton: `OutputBackend` trait, audio thread with cpal, lock-free SPSC ring, pre-allocated `ObjectState[MAX_OBJECTS]`, `MAX_BLOCK = 512` constant, per-object SPSC ring of decoded `f32` frames, NO-OP backend that emits silence. **Lab machine pinning artifact `docs/lab_setup.md` ships in P2** (Critic R2 Tier 4 — moved from P8) so subsequent measurement phases measure *against* the pinned spec, not *into* it. PipeWire-JACK pinned for v0 (Critic R2 Item 14). | `core/src/output_backend/mod.rs`, `core/src/audio_io/`, `core/src/dsp/`, `core/src/graph/`, `core/src/bin/spatial_engine_core.rs`, `docs/lab_setup.md` (kernel + governor + audio device + PipeWire-JACK pinning). | Binary opens an audio device, runs 60 s, reports 0 xruns to stderr; `cargo test` passes audio-thread no-alloc property tests (`assert_no_alloc`-wrapped audio callback); `MAX_BLOCK` boundary asserted (cpal callback size ≤ `MAX_BLOCK`, else refuse to start with explicit error). | P1 |
| **P3** | `SpeakerArray` impl with VBAP from `configs/lab_4ch.yaml` and `lab_8ch.yaml`; geometry loader; `coords` helper. **Per-sample linear gain ramp on `ObjectUpdate` apply** (Critic R2 Item 6): when an `ObjectUpdate` arrives mid-block, the audio thread linearly interpolates per-channel speaker gain from previous to new value across the audio block (sample-by-sample), eliminating the click that would arise from a discrete jump. Ramp duration = full callback block (≤ `MAX_BLOCK`); next callback starts from the post-ramp value. **`dist_m` v0 semantics** (Critic R2 Item 7): `g_dist = 1 / max(dist_m, 0.1)^0.5` applied per-object before VBAP mix; no LPF in v0; documented in `core/src/dsp/distance.rs` and `proto/ipc_schema.md`. | `core/src/output_backend/speaker_array.rs`, `core/src/geometry/`, `core/src/coords/`, `core/src/dsp/distance.rs`, `core/src/dsp/gain_ramp.rs`, `configs/lab_4ch.yaml`, `configs/lab_8ch.yaml`. | Unit tests: pan a single object at 0°/±90°/180° and assert speaker gains match VBAP closed-form within 1e-6; coord-convention enumerated (frame, sign) tests with hand-computed expected values (Critic R2 M2); `gain_ramp` test: jump position from front to right at mid-block and assert per-sample interpolation produces no discontinuity > 1/`MAX_BLOCK` per sample (proxy for click-free); `dist_m` rolloff test: assert -6 dB ± 0.1 dB at doubling. | P2 |
| **P3.5** | **Listener-blind 4-direction perceptual smoke** (Critic R2 M1, was Architect R1 Item 4). Drive a single object to four cardinal directions (front 0°, right +90°, back 180°, left −90°) using the lab 8ch config in **randomized order**. Listeners (N=2: a participant who is *not* operating the OSC console + one observer who runs the test) **cannot see the UI screen**: the laptop is closed or screen-off, listener writes down perceived direction on paper *before* operator reveals the intended direction; results scored after the full sequence is done. Catches gross VBAP-direction-flip bugs (and the recurring L/R-inversion class from MEMORY.md) without confirmation-bias contamination. The user may participate as listener only if a different operator drives the OSC and the user genuinely cannot see the screen during the trial — otherwise user is operator only. | `tests/perceptual/p3_5_smoke/script.md` (4-direction randomized sequence + scoring sheet), `docs/p3_5_smoke.md` (results log: trial order, listener responses, intended vs perceived). | Pass: both listeners correctly localize all 4 directions independently with screen blind. Fail: any L/R inversion, any front/back confusion → halt and revisit `coords` + VBAP gain table + AmbiX sign-flip helper before proceeding to P4. | P3 |
| **P4** | File-source `AudioInput` (WAV/FLAC). **Threading model (Critic R2 Tier 3)**: a dedicated **decoder thread per object** mlocks its sample buffer at startup (no page faults during playback), pre-feeds the per-object SPSC ring of decoded `f32` frames; the audio thread reads N samples per callback non-blocking and never touches the file or mmap. Pre-render synth content via `tools/render_test_signals.py`. | `core/src/graph/audio_input/file.rs` (decoder thread + mlock'd buffer + per-object SPSC producer), `tools/render_test_signals.py`, `tests/fixtures/sine_440.wav`, etc. | Run engine with 8 file objects panned to fixed positions; capture multi-channel output to WAV; assert 8 distinct sources are audible per channel via energy analysis. Decoder underflow test: pause the decoder thread; assert audio thread emits silence + per-object underrun counter increments, no xrun, no panic. | P3.5 |
| **P5** | IPC layer: OSC server (control thread), schema per `proto/ipc_schema.md` with `schema_version` + `/sys/protocol_version` handshake (Critic R2 Item 11), rate-limit/coalesce, sequence numbers, lossy heartbeat publisher (Critic R2 Tier 3), `/sys/state` and `/sys/xruns` exposure. **Architect R1 Item 1 contract**: `core/src/ipc/osc_decode.rs` parses OSC bytes on the control thread *only*; the SPSC `ObjectUpdate` enum is the *only* type crossing into the audio thread. `--port` CLI override implemented for both ports (Risk #9 mitigation). **ADR 0003 finalized in this phase** (Critic R2 Item 15 — moved from P11 where it was a typo). | `core/src/ipc/{mod.rs, osc_decode.rs, object_update.rs, rate_limit.rs, heartbeat.rs, protocol_version.rs}`, `proto/ipc_schema.md`, `tools/osc_debug_console.py`, `docs/adr/0003-ipc-transport.md` (FILLED). | `osc_debug_console.py` can spawn 2 objects, drag one across 360° via `/obj/0/pos`, observe `/sys/state` heartbeats, and see xrun counter == 0. End-to-end OSC→audio loop verified with capture. Handshake test: UI sends mismatched `schema_version`, core rejects with `/sys/error`. **Boundary test split (Critic R2 M5 / Item 9)**: TWO distinct cases: **(P5-a) valid-packet drain saturation** — flood `/obj/{id}/pos` at 10 kHz across 8 active objects for 60 s, force max SPSC ring fill, audio thread must be `assert_no_alloc`-clean and have zero xruns (proves no-alloc under realistic load); **(P5-b) malformed-packet flood** — send 1 kHz of malformed 9 KB OSC packets for 60 s, control thread reject path must not propagate to audio thread, audio thread `assert_no_alloc`-clean (proves error path doesn't leak). Both gates required. | P4 |
| **P6** | Python UI skeleton with object model + top-down view + IPC client (no drag yet). | `ui/spatial_engine_ui/{app.py,state/,views/,ipc/}`. | `python -m spatial_engine_ui` launches; receives `/sys/state` heartbeat; renders 2 spawned objects on the top-down view; subscribes/unsubscribes cleanly on shutdown. | P5 |
| **P7** | Drag-to-move: `controllers/drag.py`, mouse → `(az, dist)` mapping consistent with `coords` helper, 120 Hz rate-limit, last-write-wins coalescing. | `ui/spatial_engine_ui/controllers/drag.py`. | Manual smoke: drag object, hear pan move; automated: simulate Qt mouse events, assert at most 1 OSC message per object per 8.3 ms window. Spec acceptance "drag to move 1–4 objects in real time" met. | P6 |
| **P8** | Latency measurement harness (`tests/latency_harness/`): inject impulse via OSC `set-position` jump from speaker A to B, measure onset delta on captured loopback. **Architect R1 Item 6 specifics**: lab target = Linux 6.x generic kernel (NOT PREEMPT_RT for v0; v1 may revisit), USB class-compliant audio interface (e.g., Focusrite Scarlett family) OR built-in HDA via PipeWire-JACK bridge; buffer-size target = **64 frames @ 48 kHz = 1.33 ms one-way** (with DSP + I/O headroom <5 ms achievable end-to-end); `cpufreq` set to `performance` governor; `rtirq` script applied if available, otherwise documented absence in setup notes. | `tests/latency_harness/` Python script + analysis, `docs/lab_setup.md` (kernel/governor/device notes). | Reports end-to-end position-change → speaker-onset latency in ms; assert <5 ms on lab target with 64-frame @ 48 kHz buffer. Output committed as `tests/latency_harness/baseline.json` (records measured kernel version, audio device, buffer size, governor). | P7 |
| **P9** | Accuracy verification harness — **energy-vector (rE) projection, not VBAP self-consistency** (Critic R2 B2 / Item 2). For each intended direction in the geometry's reachable grid, compute the VBAP speaker gains, then compute the realized **energy vector** `rE = Σ_i (g_i^2 · u_i) / Σ_i g_i^2` where `u_i` is the unit vector from listener to speaker `i`; project `rE` to angles `(az_realized, el_realized) = direction_of(rE)`. Assert `|az_intended − az_realized| ≤ 1°` AND `|rE| ≥ 0.7` (low magnitude = spread/imprecise localization, perceptually unusable even if angle is "correct"). **Grid pinned to geometry-reachable directions** (Critic R2 M3 / Item 5): lab_4ch = **36 azimuths × 1 elevation** (horizontal only, listener-plane); lab_8ch = **36 azimuths × 5 elevations [-15°, 0°, +15°, +30°, +45°]** (named explicitly). Per-geometry caveat: lab_4ch with 4 horizontal speakers may not deliver ±1° everywhere — that's a **layout finding**, not a code bug; report failures as such. The test confirms the engine math is sound for the chosen layout, and exposes layout limits where the layout itself cannot deliver the acceptance criterion. | `tests/accuracy_harness/` Rust+Python (rE-vector projection in Rust, CSV+plotting in Python). | `just accuracy` produces a CSV of (intended_az, intended_el, realized_az, realized_el, rE_magnitude, err_deg); assert max angular err ≤ 1° AND min `|rE|` ≥ 0.7 across the geometry-reachable grid; CI-gateable for lab_8ch (where math says it's reachable); for lab_4ch, the harness reports per-direction limits and flags the geometry-induced ceiling explicitly rather than failing CI on an unreachable spec. | P3, P5 |
| **P10** | Multi-hour soak harness: `tests/soak_harness/` runs 8 objects with random walks for ≥4 h, samples RSS / xruns / IPC queue depth every 10 s. **Architect R1 Item 7 sub-test**: a separate 4-hour synthetic 120 Hz drag scenario tracks the *UI process* RSS specifically (Qt's `QGraphicsView` is a known leak vector), distinct from system RSS. Tooling: `psutil.Process(pid).memory_info().rss` sampled every 60 s. Threshold: UI-process RSS slope <5 MB/h is acceptable; >20 MB/h fails the soak. | `tests/soak_harness/` script + dashboard JSON, `tests/soak_harness/ui_rss_drag/` sub-suite. | 4 h pilot, then 12 h full run: zero xruns, system-RSS slope <1 MB/h, UI-process RSS slope <5 MB/h, no IPC heartbeat misses. Output `tests/soak_harness/run_*.json` (system) and `tests/soak_harness/ui_rss_drag/run_*.json` (UI process). | P8 |
| **P11** | `BinauralRenderer` and `DAWPlugin` stub backends — compile, return silence, register in `OutputBackend` table. **`nih-plug` health spike (Critic R2 M6 / Item 10)**: 1-day spike at end of P11 — clone `nih-plug` HEAD, compile, build a hello-world plugin (gain plugin or similar), survey upstream activity (commit frequency over last 90 days, last release version + date, open vs closed issue count, recent PR merge cadence). Findings logged as a follow-up note appended to ADR 0002. The follow-up determines whether the v1-kickoff `re-evaluate-juce` trigger fires earlier (e.g. if maintenance is dormant, fire trigger immediately at P11+0; if healthy, keep the P11+6mo trigger). | `core/src/output_backend/binaural_stub.rs`, `core/src/output_backend/dawplugin_stub.rs`, `docs/adr/0002-*.md` follow-up section (nih-plug spike findings), `docs/nih_plug_spike.md` (one-page report). | `cargo build --features stub_backends` succeeds; integration test selects each backend by config, runs 5 s, confirms zero crashes. nih-plug HEAD compiles and the hello-world plugin loads in a host (REAPER or `nih_plug_xtask bundle` test runner); upstream-activity numbers recorded. Spec acceptance "OutputBackend explicit abstraction with stub binaural and DAWPlugin" met. | P5 |
| **P12** | README + run instructions + collaborator-onboarding doc; cut a `v0.1.0` tag once P1–P11 green. **Architect R1 Item 3 measurable target**: time a fresh clone → `just bootstrap` → `just build` → `just run` → "modify a UI element end-to-end" (one of the three example modifications from `docs/ui_modification_examples.md`). **Second-machine definition (Critic R2 Tier 3)**: same Ubuntu 22.04 LTS, **different USB controller** (e.g., AMD vs Intel chipset), and **different audio interface** OR built-in HDA (e.g., if lab uses Focusrite, the second machine uses Behringer / Motu / built-in HDA via PipeWire-JACK). Same kernel major version (6.x) but exact subversion captured per-run. | `README.md`, `docs/architecture.md`, `docs/coordinate_convention.md`, `docs/ipc_schema.md`, `docs/onboarding.md` (full), `docs/onboarding_timing.md` (measured times — both machines), `docs/ui_modification_examples.md`, git tag. | **Hard target: ≤60 min on a clean Ubuntu 22.04 box (machine #1), recorded in `docs/onboarding_timing.md` with date, machine model, USB controller, audio interface, kernel version, network speed, and any blockers.** This artifact verifies acceptance bullet #10. **Second machine** per the definition above; if either run >60 min, file a bootstrap-friction issue and revise `bootstrap.sh` before tagging v0.1.0. | All prior |
| **P13 (sign-off, scheduled)** | Perceptual listening test (F4 perceptual leg) reusing vid2spatial v3 stimulus pipeline, **N=12 listeners pre-registered** (Critic R2 M3 / Item 5; matches user's vid2spatial v3 bar for Friedman-test statistical significance). No conditional escalation — N is locked before the test runs; if recruitment is the blocker, schedule longer rather than lower N. | `tests/perceptual/listening_test_v0/` config + results JSON, pre-registration doc filed before any data collection. | Mean perceived azimuth error within ±1° on the geometry-reachable grid (matching P9's grid); results entered in v0 sign-off doc; Friedman test for any condition contrasts that the design exposes. | P12 |

---

## Acceptance Criteria

(Verbatim from spec §Acceptance Criteria.)

- [ ] 4–8 channel speaker array output runs on lab hardware in real time.
- [ ] GUI displays a spatial view (top-down or 3D) and supports mouse drag to move 1–4 sound objects in real time during v0 bringup.
- [ ] Object position changes propagate to speaker routing within the latency budget without click/dropout artifacts.
- [ ] End-to-end input → speaker latency measures under 5 ms on the lab target machine.
- [ ] 8 simultaneous objects render without dropouts under sustained load.
- [ ] Apparent positional accuracy ≤ ±1° (verified by listening test or numerical pan-law check, method TBD).
- [ ] Multi-hour uninterrupted operation: no memory leaks, no GC/IPC stalls, no audio underruns over a multi-hour session.
- [ ] `OutputBackend` is an explicit abstraction with at least one stub implementation showing how a `BinauralRenderer` and a `DAWPlugin` backend would plug in.
- [ ] IPC layer between native audio core and Python UI is documented (transport, message schema, ownership of state).
- [ ] An engineer collaborator can clone, build, and run the engine + GUI on their machine and modify a UI element end-to-end.

### Verification mapping

| # | Acceptance bullet | Verifying phase / harness |
|---|-------------------|--------------------------|
| 1 | 4–8 ch speaker array runs in real time | P3 (`SpeakerArray`) + P4 (multi-object capture) |
| 2 | GUI top-down view + drag 1–4 objects | P6 (UI skeleton) + P7 (drag) |
| 3 | Position changes propagate without clicks/dropouts | P5 (IPC) + P8 (latency harness) + per-block crossfade tests in P3 |
| 4 | End-to-end latency < 5 ms | P8 (latency harness) |
| 5 | 8 simultaneous objects, no dropouts | P10 (soak harness) under 8-object load |
| 6 | Positional accuracy ≤ ±1° | P3.5 (informal smoke) + P9 (numerical, CI gate) + P13 (perceptual sign-off) |
| 7 | Multi-hour: no leaks, no GC/IPC stalls, no underruns | P10 (soak harness, including UI-process RSS sub-test) |
| 8 | `OutputBackend` abstraction + Binaural + DAWPlugin stubs | P2 (trait) + P11 (stubs) |
| 9 | IPC layer documented | P5 + `proto/ipc_schema.md` + `docs/ipc_schema.md` |
| 10 | Collaborator clone-build-run-modify | P12 (fresh-clone bootstrap timed ≤60 min, artifact `docs/onboarding_timing.md`) — geometry editing explicitly excluded as v0 Non-Goal; allowed modifications enumerated in `docs/ui_modification_examples.md` |

---

## Risks & Mitigations

1. **Python UI GIL stalls leak into IPC and starve `/sys/state` heartbeats during heavy drag.**
   - *Mitigation*: IPC client runs in a dedicated `QThread` (not the Qt main thread); drag rate-limited at 120 Hz at the source; heartbeat consumer is non-blocking; UI never blocks waiting for a `/sys/ack`.
2. **OS audio buffer underruns (xruns) on commodity Linux audio I/O at <5 ms target.**
   - *Mitigation*: lab target uses **PipeWire-JACK** (Critic R2 Item 14, modern Ubuntu default; pinned in `docs/lab_setup.md`) with explicit period size matching the budget; `cpal` JACK backend; xrun counter exposed to UI; soak harness gates on zero xruns; ADR 0002 records why a Rust real-time-safe audio thread (no alloc, no locks) is the floor of the budget, not the ceiling.
   - *Specificity (Architect R1 Item 6)*: lab kernel = Linux 6.x **generic** (NOT PREEMPT_RT for v0; v1 may revisit). Audio device class = USB class-compliant interface (e.g., Focusrite Scarlett family) OR built-in HDA via PipeWire-JACK bridge. Buffer-size target = **64 frames @ 48 kHz = 1.33 ms one-way**, leaving DSP + I/O headroom inside the 5 ms end-to-end budget. `cpufreq` set to `performance`; `rtirq` script applied if available, otherwise documented absence in `docs/lab_setup.md`. Pinned in P2 deliverable, measured in P8.

   - **Latency budget decomposition (end-to-end, p99 estimates) — Critic R2 B3 / Item 3**. Documented in full in `docs/latency_budget.md`. Term-by-term, from `/obj/{id}/pos` mouse event to first-altered-sample-at-DAC:

| Stage | Component | p99 estimate (commodity Linux 6.x generic) | Notes |
|-------|-----------|--------------------------------------------|-------|
| 1 | Qt event loop tick (UI mouse → `python-osc.send_message`) | **1–4 ms** | Qt 6 event-loop tail latency on a non-stressed desktop. Dominated by repaint pacing; non-deterministic. Coalescing at 120 Hz (8.3 ms tick) means worst case is 1 tick of latency at the source side, but the *distribution* tail can stretch under GC pressure. |
| 2 | UDP loopback (`sendto` → core's `recvfrom`) | **<0.2 ms** | Local UDP loopback is essentially memcpy + scheduler hop; sub-ms. |
| 3 | Core control thread wake → OSC decode → SPSC push | **1–3 ms** | Non-PREEMPT_RT scheduler jitter dominates here. On a quiet system, ~0.3 ms; under load (other Qt processes, browser tabs), the p99 tail can reach 3 ms. PREEMPT_RT compresses this to <0.5 ms but is ruled out for v0. |
| 4 | Audio callback wait (next cpal callback boundary) | **0–1.33 ms** | At 64 frames @ 48 kHz, the SPSC `ObjectUpdate` is consumed at the next callback. Average 0.67 ms; p99 ≈ 1.33 ms. |
| 5 | DSP block compute (8 objects × VBAP + gain ramp + per-channel sum) | **<0.3 ms** | Rust + SOA + per-sample ramp at 64 frames; budget on a modern x86_64 core is comfortable. Measured number recorded in P8 baseline. |
| 6 | Output buffer fill + DAC out (audio interface) | **1.33 + 1–3 ms** | 1.33 ms output buffer queue (cpal-side) + USB-class audio interface IO ~1–3 ms typical. Built-in HDA can be faster (~0.5–1 ms). |
| **Sum p99 (commodity Linux 6.x generic, USB interface)** | | **≈ 5.2–13.0 ms** | |
| **Sum p99 (built-in HDA, idle system)** | | **≈ 3.3–8.0 ms** | |

   - **Reading**: the headline 5 ms target is **achievable on the optimistic edge** (built-in HDA, idle system, low-tail Qt), but the p99 tail under realistic load **cannot be guaranteed** without scheduler help. Three fallback strategies are pre-authorized:
     1. **PREEMPT_RT kernel** (most effective, drops stages 3 + 6 jitter to ~0.5 ms each) — adds bringup complexity and was ruled out for v0 baseline; *can be adopted post-P8 if measurements show >5 ms p99*.
     2. **Smaller audio buffer** (32 frames @ 48 kHz = 0.67 ms one-way) — saves ~1.3 ms but doubles xrun risk; only viable on PREEMPT_RT.
     3. **Relax acceptance to <10 ms p99** (asterisked in v0 sign-off doc) — keeps commodity-kernel posture; user re-litigation required, this plan does NOT silently relax.
   - **What P8 measures vs what this budget predicts**: P8 reports p50, p95, p99 onset latency on the lab target. If p99 > 5 ms, P8 output triggers a budget-vs-measurement reconciliation (which contributor exceeded its p99 estimate?), then a documented decision about which fallback to apply. Pre-mortem Scenario A's "3.2 → 12 ms creep" is now grounded: 3.2 ms is *the optimistic edge of the budget table above* (built-in HDA + idle), and the creep represents drift through the table's fallbacks rather than an unmoored assumption.
3. **IPC backpressure during fast drag (UDP packet flood, queue overflow).**
   - *Mitigation*: 120 Hz coalescing at UI; bounded SPSC at core control thread with last-write-wins drop; sequence numbers + 30 Hz full-state heartbeat make eventual consistency observable.
4. **v0→v1 UI rewrite tax: PySide6 idioms creep into the IPC schema and bind v1 to Python-isms.**
   - *Mitigation*: `proto/ipc_schema.md` is *language-agnostic OSC*; ADR-required for any change to the schema (collaboration policy C4); v1 UI rewrite must reuse the schema, not redesign it.
5. **Speaker geometry config drift between core and UI.**
   - *Mitigation*: only the core parses `configs/*.yaml`; UI receives geometry via `/sys/get_geometry`; integration test asserts UI-rendered speaker count == core-reported channel count.
6. **Multi-hour memory creep from event allocations or log strings; specifically, Python UI-process RSS creep** (Qt's `QGraphicsView` with hundreds of repaints/sec is a known leak vector).
   - *Mitigation*: audio thread is enforced no-alloc by `assert_no_alloc` in tests; control thread uses bounded buffers; UI runs `tracemalloc` snapshots in soak harness; soak gate on system-RSS slope.
   - *UI-specific soak (Architect R1 Item 7)*: P10 sub-test runs a 4-hour synthetic 120 Hz drag and tracks `psutil.Process(ui_pid).memory_info().rss` sampled every 60 s. Threshold: <5 MB/h slope passes; >20 MB/h fails the soak. Distinguishes UI-process growth from system-wide noise.
   - **Memory-budget anchors (Critic R2 Tier 3)**: steady-state RSS targets are now anchored, not just slope-thresholded:
     - **Core (Rust audio core)**: ~50 MB steady-state RSS (after init, with 8 objects spawned, file-source decoder threads running). Slope threshold <1 MB/h system-RSS over 12 h soak. Failure mode: any trace of unbounded queue growth or audio-thread allocation.
     - **UI (Python/PySide6)**: ~150 MB steady-state RSS (PySide6 baseline ~120 MB + scene + state mirror). Slope threshold <5 MB/h UI-process RSS over 4 h synthetic-drag soak. Failure mode at 20 MB/h: known QGraphicsView item-leak / cached pixmap retention pattern.
     - These anchors make "creep" definable for processes that grow at startup; the soak harness reports both the absolute steady-state and the slope, and either a steady-state >2× anchor or a slope above threshold fails the soak.
7. **Coordinate convention regression.** (Memory shows multiple historical L/R-inversion bugs in vid2spatial.)
   - *Mitigation*: `core/coords` is the single source; unit tests cover atan2 + AmbiX-flip + pixel-roundtrip; documented in `docs/coordinate_convention.md` with prose examples; P3.5 perceptual smoke catches gross direction-flip regressions before they reach P13.
8. **Numerical-only accuracy passes while perceptual reality fails** (room reflections, speaker mismatch).
   - *Mitigation*: Open Item (f) resolution F4 — perceptual sign-off (P13) is required for v0 closure, even though only numerical (P9) gates CI. P3.5 informal smoke (Architect R1 Item 4) provides an early-warning gate so P13 isn't the first time perception is checked.
9. **IPC port collision on dev machines** (UDP 9000/9001 are TouchOSC / OSCulator defaults; collaborators running those tools will see silent OSC drops or crosstalk; Architect R1 bonus).
   - *Mitigation*: P5 ships `--port` CLI override on both core (`spatial_engine_core --osc-cmd-port N --osc-state-port M`) and UI (`spatial_engine_ui --osc-cmd-port N --osc-state-port M`). `docs/onboarding.md` includes an "ephemeral port discovery" section showing how to pick free ports (`ss -lnu` or pass `--osc-cmd-port 0` for OS-assigned, with the chosen port logged at startup and offered to the UI via env-var or CLI passthrough).

---

## Pre-mortem (DELIBERATE mode, Architect R1 escalation)

Imagine that v0 has failed acceptance. For each scenario, the description names the failure, traces the most-likely root cause, and points at the specific in-plan mitigation that should have caught it.

### Scenario A — Latency budget creep
- **Failure**: Six months in, end-to-end latency has crept from a measured 3.2 ms (P8 baseline) to 12 ms. Acceptance bullet #4 fails on the lab target.
- **Root cause**: The OSC → SPSC → audio path accreted complexity. A maintainer relaxed the decode-thread invariant ("just this once") to handle a special-case OSC bundle on the audio thread side; a second maintainer added a small `Vec` allocation in the audio callback to handle a "harmless" debug-event path; cumulative drift past `assert_no_alloc`'s coverage caused jitter that the OS scheduler smeared into block-size jumps.
- **Mitigation already in plan that should have prevented this**: The Architect R1 Item 1 boundary contract — `core/src/ipc/osc_decode.rs` decode-on-control-thread invariant, plus `assert_no_alloc` test wrapping the audio callback in CI on every commit (P5 verification). Plus ADR-required policy (collaboration C4) gates any change to `output_backend`, `state`, `ipc/object_update.rs`, or `coords` behind written rationale.

### Scenario B — Bootstrap stalls
- **Failure**: A new engineer clones the repo and cannot reproduce <5 ms latency on their machine. They spend three days debugging kernel/driver/PipeWire-JACK config; UI-iteration velocity dies because nobody else can verify their PRs.
- **Root cause**: The lab machine has a specific `cpufreq` governor + `rtirq` configuration + an audio-interface driver quirk that was not captured in `bootstrap.sh`; the engineer's clean Ubuntu 22.04 box uses the `schedutil` governor by default and a different USB controller driver path.
- **Mitigation already in plan that should have prevented this**: P12's measured ≤60 min fresh-clone target on a *clean* Ubuntu 22.04 box (Architect R1 Item 3) — repeating it on a *second machine* is required before tagging v0.1.0. `docs/onboarding_timing.md` and `docs/lab_setup.md` (P8 / Risk #2 specificity, Architect R1 Item 6) capture exactly the kernel + governor + audio-device requirements. If P12 is taken seriously, this scenario is caught at tag time, not in the field.

### Scenario C — Perceptual sign-off failure on geometry-specific drift
- **Failure**: P13 perceptual sign-off fails. Listeners report apparent azimuth drift of ~3–4° on the lab 8ch geometry, even though P9 numerical accuracy passes the ±1° gate. The drift is most pronounced on rear-quadrant objects.
- **Root cause**: The geometry YAML on the core side and the geometry view on the UI side fell out of sync — the UI was caching a stale `/sys/geometry` snapshot from before a `load_geometry` call, so the engineer dragged objects against an incorrect mental model of where the speakers actually were. Risk #5 manifested despite the "core parses YAML, UI mirrors via IPC" mitigation, because the UI side cached.
- **Mitigation already in plan that should have prevented this**: Risk #5 mitigation specifies that *only* the core parses `configs/*.yaml` and the UI receives geometry via `/sys/get_geometry`; the integration test that asserts UI-rendered speaker count == core-reported channel count needs to be extended (per the expanded test plan below) to also assert UI-rendered speaker *positions* match `/sys/geometry` after every `load_geometry`. P3.5 (Architect R1 Item 4) is the early-warning gate that should catch gross direction-flip versions of this; subtle drift requires the integration assertion.

---

## Expanded Test Plan (DELIBERATE mode, Architect R1 escalation)

Tests grouped by tier; each tier names artifacts and what they protect.

### Unit tests (`cargo test` in `core/`, `pytest` in `ui/tests/`)
- **VBAP gain calculation** (`core/src/output_backend/speaker_array.rs`): for the lab 4ch and 8ch configs, assert that placing an object at each speaker direction produces gain == 1 on that speaker and gain == 0 on the others (within 1e-6). *Note:* this is a numerical-correctness check, not the accuracy gate — the accuracy gate is the rE-vector projection test in P9 (Critic R2 B2).
- **Coordinate frame conversion — enumerated (frame, sign) pairs** (`core/src/coords/`, Critic R2 M2 / Item 13). Each pair gets a hand-computed expected-value test, **not just round-trip identity**:
  - **Pipeline frame** (vid2spatial native): RIGHT=+az. Test: `(x=1, y=0, z=1)` → `az = atan2(1, 1) = +π/4`; `(x=-1, y=0, z=1)` → `az = -π/4`. Assert sign convention.
  - **AmbiX/SOFA frame** (target FOA + binaural): LEFT=+az. Test: helper `pipeline_to_ambix(az_pipe)` applied to `+π/4` returns `-π/4`; applied twice returns identity (round-trip secondary check).
  - **Elevation, listener-frame** (UP=+el): test `(x=0, y=0, z=1)` (front horizon) → `el = 0`; `(x=0, y=1, z=0)` (above) → `el = +π/2`; `(x=0, y=-1, z=0)` (below) → `el = -π/2`.
  - **Elevation, image-y-down convention** (vid2spatial vision pipeline): test `arcsin(-y_image)` for `y_image=+0.5` (below image center, listener-frame above-horizon when camera is forward-pointing) → `el = -π/6`. Assert the sign-flip helper is named `image_y_to_listener_el` and tested explicitly. (Reference: MEMORY.md elevation sign bug 2026-03-06.)
  - **VBAP frame** (engine-internal): pinned in `configs/lab_*.yaml` schema as **audio-azimuth-from-front, RIGHT=+az, in degrees**. Test: speaker config `{ az_deg: +90, el_deg: 0 }` parses to listener-frame `(x=+1, y=0, z=0)` (right of listener). Assert that loading lab_4ch.yaml and listing speaker positions matches a hand-computed Cartesian array.
  - **Stereo pan baseline** (audit reference, not v0 use): `pan = sin(az_pipeline)` should give `pan > 0` for az>0 (right). Test asserts this against the v0-incorrect alternative `sin(-az)` to lock against the 2026-03-01 baseline_pan inversion. (Even if v0 doesn't ship a stereo pan path, the test-named convention prevents future regressions if/when one is added.)
  - **Round-trip identity tests** are retained as a *secondary* check (`inverse(forward(p)) == p` within tolerance) — they catch composition bugs but would silently pass under a double-flip, which is exactly the failure pattern MEMORY.md catalogs. The hand-computed pair tests above are the primary defense.
- **OSC decode parser** (`core/src/ipc/osc_decode.rs`): valid `/obj/{id}/pos` packet with correct `schema_version` → expected `ObjectUpdate` enum variant; malformed packet → `Err`, never panics; oversized packet (>8 KB) → bounded-time rejection; mismatched `schema_version` → `Err::ProtocolVersionMismatch` reply via `/sys/error`.
- **YAML schema validation** (`core/src/geometry/`): valid `lab_4ch.yaml` parses; missing channel count, duplicate speaker IDs, out-of-range angles each fail with a descriptive error.
- **`OutputBackend` trait conformance** (`core/src/output_backend/`): each impl (`SpeakerArray`, `BinauralRenderer` stub, `DAWPlugin` stub) satisfies all four required methods (`channel_count`, `prepare`, `render`, `shutdown`); compile-time test via a generic harness.
- **`ObjectUpdate` no-heap invariant**: a `static_assert`-style test that `std::mem::size_of::<ObjectUpdate>()` is fixed and `<= N` bytes; ensures variants never accreted heap-allocating fields silently.
- **Per-sample gain ramp** (`core/src/dsp/gain_ramp.rs`, Critic R2 Item 6): jump position from `(az=0)` to `(az=+π/2)` mid-block; assert per-sample max `|Δgain|` < `1.0 / MAX_BLOCK` for each speaker channel (proxy for click-free).
- **`dist_m` rolloff** (`core/src/dsp/distance.rs`, Critic R2 Item 7): assert `g(2) / g(1) ≈ 1/√2` (i.e., -3 dB per doubling under the `1/r^0.5` rule); assert `g(0.05)` is clamped to `g(0.1)` (floor); assert `g` is monotonic-decreasing on `(0.1, ∞)`.

### Integration tests (`tests/e2e/` — spawn core + UI as subprocesses)
- **OSC roundtrip**: UI writes `/obj/0/pos`, then reads back via `/sys/state` heartbeat; assert echoed position matches within sequence-number tolerance.
- **`SpeakerArray` known-position output (numerical correctness, NOT accuracy)**: send a single object to a known direction, capture multi-channel buffer, assert per-channel gains match the VBAP closed form within 1e-3. *Note:* this is a numerical-correctness check verifying that VBAP math is implemented correctly. The **accuracy gate** is the rE-vector projection in P9 (Critic R2 B2), which projects realized speaker gains back to a perceived direction.
- **Geometry YAML reload**: load `lab_4ch.yaml`, then `/sys/load_geometry lab_8ch.yaml`; assert (a) `/sys/geometry` reply now reports 8 channels, (b) UI's rendered speaker positions match `/sys/geometry`, (c) any in-flight per-object positions are clamped to the new geometry without xrun (Risk #5 / Pre-mortem C mitigation).
- **Decode-thread invariant under load** (P5 verification, Architect R1 Item 1): send 1 kHz of malformed 9 KB OSC packets for 60 s; audio thread `assert_no_alloc` wrapper logs zero allocations and zero xruns.
- **Port-collision recovery** (Risk #9): start core with `--osc-cmd-port 0` (OS-assigned); UI reads chosen port from core stderr/env; assert both connect successfully on a non-default port.

### End-to-end tests (`tests/e2e/`, `tests/latency_harness/`, `tests/accuracy_harness/`)
- **Full bootstrap → drag → measured speaker gains**: run `bootstrap.sh` on a clean container; launch core+UI; simulate Qt mouse drag from speaker A to speaker B; capture multi-channel output; assert per-channel gains match expected VBAP curve within 1° angular tolerance.
- **Latency harness** (P8): measure input-OSC → speaker-onset for 60 s; report p50, p95, p99; assert p99 <5 ms on lab target (kernel/buffer/governor recorded in artifact).
- **Accuracy harness** (P9, Critic R2 B2 / M3): rE-vector projection. Grid pinned to geometry-reachable directions: lab_4ch = 36 az × 1 el (horizontal); lab_8ch = 36 az × 5 el `[-15°, 0°, +15°, +30°, +45°]`. Assert per-direction `|az_intended − az_realized| ≤ 1°` AND `|rE| ≥ 0.7`. CI gate for lab_8ch; lab_4ch reports limits as a layout finding without failing CI.
- **Soak harness** (P10): 4 h pilot + 12 h full run; core steady-state RSS ~50 MB target with system RSS slope <1 MB/h, UI steady-state RSS ~150 MB target with UI-process RSS slope <5 MB/h, zero xruns, zero IPC heartbeat misses (lossy publish never blocks; "miss" here means UI's last-received heartbeat age >10 s).
- **P3.5 perceptual smoke** (Critic R2 M1, listener-blind): N=2 listeners, randomized 4-direction sequence, screen-blind responses, scored after the fact; documented in `docs/p3_5_smoke.md`.
- **P13 perceptual sign-off** (formal, Critic R2 M3): **N=12** pre-registered listeners; mean perceived azimuth error within ±1° on the geometry-reachable grid; Friedman test for any condition contrasts.

### Observability (in-engine, runtime-visible signals)
- **Structured logging** in core via the `tracing` crate; spans for `audio_callback`, `control_thread.osc_decode`, `control_thread.heartbeat`; events drained from a pre-allocated lock-free ring (audio-thread side) into the control thread, then to stderr or a log file (NOT inside the audio callback).
- **Metrics exposed via `/sys/state` heartbeat and a debug stderr sink**:
  - `audio_underrun_count` (xruns since start) — already in spec via `/sys/xruns`.
  - `ipc_queue_depth` (current SPSC `ObjectUpdate` ring fill) — for backpressure diagnosis.
  - `osc_packet_rate` (packets/sec received and decoded on control thread) — for drag-storm visibility.
  - `ui_frame_time_p99` (UI-side, reported via `/sys/ui_health` or stderr) — for QGraphicsView leak / repaint-storm diagnosis.
  - `ui_process_rss_mb` (UI-side, sampled at 60 s by the soak harness) — Risk #6.
- **Debug endpoint**: a single `--debug-stderr-metrics` flag on core that prints metrics every 1 s to stderr; future v1 can graduate to an HTTP endpoint without changing the metric set.

---

## ADRs

### ADR 0001 — Stack: hybrid native (Rust) audio core + Python/PySide6 UI (FILLED)

- **Decision**: v0 uses a hybrid stack — native real-time audio core in Rust + a separate Python/PySide6 UI process communicating over OSC/UDP loopback. v1 keeps the core, rewrites the UI in the production stack.
- **Drivers**: latency budget <5 ms (Driver #1); v0→v1 transition cost (Driver #2 — preserve the core, throw away the UI); engineer iteration speed (Driver #3 — Python+Qt is the fastest UI iteration loop the user has access to).
- **Alternatives considered**:
  - Pure Python + Qt with measure-only audio. *Invalidated*: cannot meet <5 ms / no-GC-stall under sustained 8-object load (spec Round 6 Simplifier resolution).
  - JUCE C++ from day one. *Invalidated for v0*: heavier toolchain, slower iteration, and v0 explicitly does not need plugin packaging (spec §Non-Goals); but JUCE is reconsidered if v1 prioritizes a DAW plugin form factor.
  - Web-stack (WebAudio) UI. *Invalidated*: cannot make the latency budget; OS-level audio routing not in reach.
  - **Single-process Rust + `egui` (no IPC) — antithesis steelman, considered-and-rejected** (Architect R1 Item 2). *Genuine appeal*: collapses two languages into one, eliminates the entire OSC schema / rate-limiter / coalescer / sequence-numbers / heartbeat / `osc_debug_console.py` surface, and would directly invalidate Risks #1, #3, #4, #5, #6, and #9 (six of the nine named risks). `cargo run` becomes the entire bringup story. `egui` is what `nih-plug` itself uses for plugin GUIs, so the v0 UI would be *closer* to the v1 production form factor than a Python UI is. *Reasons for rejection (real, not strawman)*:
    1. Engineers iterating on UI/UX with the user need a familiar GUI stack. The user's collaborator pool has Python+Qt fluency (per spec §Constraints "Collaboration mode" and the user's wider research ecosystem); `egui`'s immediate-mode model and Rust ownership ergonomics are not yet a reachable substrate for those collaborators.
    2. Choosing `egui` for v0 *risks locking the v1 production GUI choice prematurely*. The plan deliberately wants the UI to be throw-away precisely so the engine core does not get coupled to a particular GUI's iteration churn; collapsing into a single process ties UI choice to engine choice.
    3. `egui`'s ergonomics for a 2D top-down spatial scene with drag interaction (custom `QGraphicsItem`-equivalent painting + hit-testing) are workable but more code than `QGraphicsView` for the same v0 features; iteration cost on the *throw-away* surface goes up, not down.
    4. The IPC schema is part of what the project wants to keep across the v0→v1 rewrite (Driver #2). A single-process design has no reason to specify and version a schema, and v1 would have to reconstruct it from scratch when external controllers (TouchOSC, OSCulator, future text2traj/vid2spat input plugins per spec §Non-Goals) re-enter scope.
    - *Counter-acknowledgment*: rejecting this antithesis is **a deliberate bet** that "UI-throwaway via process boundary + Python iteration ergonomics" is worth carrying Risks #1/#3/#4/#5/#6/#9. If at v1 kickoff the team finds those risks dominated cost more than projected, this ADR should be revisited and `egui` (or another single-process Rust GUI) reconsidered for the v1 production GUI form factor.
- **Why chosen**: only this combination satisfies all three Drivers simultaneously. The audio path is production-grade from day one (Principle 3); the UI is throw-away by design (Principle 5).
- **Consequences**:
  - +: real-time-safe Rust core, fast UI iteration, clean v0→v1 boundary.
  - +: free external-controller path via OSC.
  - −: two languages, two build tools, one IPC schema to maintain.
  - −: v1 UI rewrite is now an explicit budgeted activity (covered by Risk #4).
- **Follow-ups**:
  - Confirm Linux audio stack (JACK vs PipeWire-JACK) on lab target machine (P2).
  - Decide ADR 0003 (IPC transport) — currently a stub pending Architect/Critic feedback.

### ADR 0002 — Native audio core language: Rust + `cpal` + `crossbeam` (FILLED)

- **Decision**: the native audio core is written in Rust 2021 edition, using `cpal` for audio I/O, `crossbeam` for lock-free SPSC queues, and `assert_no_alloc` (test-only) to enforce zero heap-allocation in the audio thread.
- **Drivers**: v0→v1 transition cost (Driver #2 — the core survives the rewrite); latency budget (Driver #1); engineer iteration speed (Driver #3 — `cargo build && cargo run` is one command).
- **Alternatives considered**:
  - C++ + JUCE. *Invalidated for v0*: JUCE's decisive advantage is plugin packaging, which spec §Non-Goals defers; meanwhile C++ exposes the real-time path to UB and leak risks Rust forecloses.
  - C + PortAudio. *Invalidated*: re-invents wheels (no DSP primitives, no FFI helpers); ergonomics fail over a multi-quarter codebase.
  - Zig + raw audio APIs. *Invalidated*: ecosystem too young; no equivalent of `cpal`/`nih-plug` maturity for v1+ plugin path.
- **Why chosen**: Rust is the only choice that satisfies Principle 1 (no heap, no locks, no UB) by construction *and* gives a credible v1+ DAW-plugin path (`nih-plug`).
- **Consequences**:
  - +: memory-safe real-time path; reproducible builds via cargo lockfile.
  - +: no manual lifetime ceremony in the audio callback (SPSC handles ownership).
  - −: smaller pool of contributors who are simultaneously DSP-fluent and Rust-fluent.
  - −: v1 plugin packaging via `nih-plug` (rather than JUCE) is a bet on `nih-plug` maturity — flagged for Architect attention.
- **Follow-ups**:
  - Pin `cargo` toolchain version in `rust-toolchain.toml` (P1).
  - Spike on `nih-plug` for the v1 `DAWPlugin` backend (out of v0 scope; record findings in a future ADR).
  - **`nih-plug` falsifier (Architect R1 Item 8)**: the choice of Rust over JUCE relies on `nih-plug` reaching production maturity for the v1 DAW-plugin path. *Trigger condition*: if `nih-plug` 1.0 has **not** been released by v1 kickoff (target date: v0 acceptance + 6 months), open an issue tagged `re-evaluate-juce` in the project tracker and re-litigate JUCE for the v1 DAW-plugin path. Re-litigation is *constrained* to the plugin path — the v0 audio core (cpal + crossbeam + assert_no_alloc) stays regardless, since Principle 3 ("v0 audio core preserved into v1") binds the engine core, not its v1 plugin packaging.

### ADR 0003 — IPC transport: OSC over UDP loopback for v0; shm+UDS as v1 migration target conditional on a measured falsifier (FILLED — Critic R2 B1 / Item 1)

- **Decision**: v0 uses **OSC over UDP loopback** between the Rust audio core (Process A) and the Python/PySide6 UI (Process B). Default ports 9000 (cmd) / 9001 (state), overridable via `--osc-cmd-port` / `--osc-state-port`. Every command carries a `schema_version` u16 and a startup `/sys/protocol_version` handshake gates the connection. v1 keeps OSC unless a measured falsifier (defined below) fires, in which case v1 starts with **shared-memory ring + UDS control socket** as the migration target.

- **Drivers**:
  - **D1 (Latency budget <5 ms end-to-end)**. UDP loopback is sub-millisecond; the IPC term in the latency budget table (Risk #2 decomposition) is <0.2 ms. OSC adds nothing measurable to the budget.
  - **D2 (v0→v1 transition cost — preserve the audio core; UI is throw-away)**. The IPC schema is what survives the rewrite. OSC's address-pattern + type-tag schema is plain text, language-agnostic, and trivially reproducible in any v1 UI stack (Rust+egui, C++/JUCE GUI, web UI). Versioning is in-band (`schema_version`).
  - **D3 (Engineer iteration speed)**. `python-osc` (UI) and `rosc` (core) are one-line dependencies. A collaborator can plug in TouchOSC or a `tools/osc_debug_console.py` REPL and drive the engine without touching the schema. Wireshark/`osculate` are off-the-shelf debug aids.
  - **D4 (External-controller forward compatibility)**. Spec §Technical Context cites L-ISA / d&b Soundscape, which expose OSC control surfaces. Choosing OSC means future text2traj/vid2spat input adapters and TouchOSC-style hardware controllers plug in for free.

- **Alternatives considered (steelmanned, Critic R2 B1)**:

  - **B2: Shared-memory ring buffer (state mirror) + Unix Domain Socket control channel (commands/acks)**. *Steelman*: lowest-possible-latency state mirroring (memcpy-only, no kernel network stack); bounded zero-copy; clean separation of "current truth" (shm) from "request/response" (UDS); the production-grade pattern used by professional audio middleware (e.g., JACK's own internal client/server). Process B reads the shm region with a single mmap and gets effectively-instant state without a network stack. UDS is reliable, not lossy, so command acks are deterministic. *Reasons for rejection in v0 (real, not strawman)*:
    1. **More code to write and verify than the latency budget needs**. The latency table shows IPC at <0.2 ms; shm+UDS would cut this to ~0.01 ms, which is invisible in a 5 ms end-to-end budget where Qt event-loop tail (1–4 ms) and DAC (1–3 ms) dominate. We'd be optimizing the cheapest term.
    2. **POSIX shm is OS-specific and harder for a collaborator to inspect.** OSC packets show up in Wireshark, in stderr (`tools/osc_debug_console.py`), in `tcpdump`. Shm requires writing a custom inspector, which is throw-away work that doesn't survive into v1 if the schema is what survives.
    3. **Python `multiprocessing.shared_memory` has GIL/GC interaction with PySide6 that is non-trivial.** Reading shm under Qt's event loop without blocking the main thread requires a watcher thread + signal marshaling, which is the same complexity OSC already pays — we'd buy nothing.
    4. **External-controller story is broken.** TouchOSC / OSCulator / future text2traj adapters speak OSC; they do not speak our shm layout. v1 with shm would need an OSC bridge process anyway, so we'd carry both.
    *When B2 wins*: when the latency budget proves untenable on UDP and the bottleneck is provably IPC. The falsifier (below) measures exactly this.

  - **B3: ZeroMQ (PUB/SUB + REQ/REP) over `ipc://` or `tcp://127.0.0.1`**. *Steelman*: rich messaging patterns out of the box (PUB/SUB for `/sys/state` heartbeat; REQ/REP for command/ack); built-in high-water-mark + backpressure semantics that we'd otherwise hand-roll on UDP; cross-language bindings (`pyzmq`, `zmq.rs`); battle-tested in production middleware. *Reasons for rejection in v0 (real, not strawman)*:
    1. **Another runtime dependency on both sides** that doesn't pay back the latency budget. ZMQ adds ~0.3–0.5 ms of framing/dispatch overhead vs raw UDP loopback per message — small in isolation, but it's overhead with no compensating benefit since UDP loopback is already sub-ms.
    2. **Opaque to engineers without ZMQ experience.** The user's collaborator pool has Python+Qt fluency, not ZMQ fluency (per spec §Constraints "Collaboration mode"); debugging ZMQ requires `tcpdump` PLUS understanding the framing PLUS the socket-type semantics. OSC is a packet-on-the-wire that any junior engineer can `tcpdump` and read.
    3. **Schema choice still pending.** ZMQ is a transport; we'd still need msgpack/protobuf/JSON on top, which is another decision and another schema. OSC bundles transport AND schema in a battle-tested, externally-recognized format.
    4. **External-controller story still broken** for the same reason as B2 — TouchOSC etc. speak OSC, not ZMQ-on-msgpack.
    *When B3 wins*: a multi-process / multi-machine architecture where backpressure semantics across many subscribers matter. v0 has exactly two processes on one machine; B3 is gold-plating.

  - **B4: Cap'n Proto over Unix Domain Socket**. *Steelman*: schema-first messaging with cross-language code generation; near-zero serialization cost (zero-copy on read); UDS is local-only and reliable; the strongest "schema is the v0→v1 invariant" expression — Cap'n Proto's `.capnp` file IS the schema. *Reasons for rejection in v0 (real, not strawman)*:
    1. **Schema compiler in the build pipeline.** Cap'n Proto requires `capnpc` to generate Rust + Python bindings; that's a `bootstrap.sh` step that complicates onboarding (Critic R2 P12 60-min target) and adds a tool the collaborator pool doesn't already have.
    2. **Less L-ISA-idiomatic than OSC.** Spec §Technical Context names OSC explicitly; choosing Cap'n Proto would be a deliberate departure from the cited reference architecture for no measurable gain inside the v0 latency budget.
    3. **No external-controller story** — see B2/B3.
    4. **Team unfamiliar with Cap'n Proto** raises the learning-curve tax on every contributor without a paying-back benefit; the schema-first ethos is achievable in OSC via `proto/ipc_schema.md` + JSON schema for blobs.
    *When B4 wins*: when the schema is so complex that hand-written OSC blob parsing is the larger maintenance cost. v0's schema (~10 commands, ~7 server-to-client messages) does not approach that complexity.

- **Why chosen**: only OSC/UDP simultaneously satisfies D1 (sub-ms loopback fits the budget), D2 (the schema is the v0→v1 invariant; OSC is plain text + standard tooling), D3 (one-line client deps, off-the-shelf debug tools), and D4 (free TouchOSC / external-controller compatibility per spec §Technical Context). The shm+UDS antithesis (B2) is the *strongest* alternative, and is preserved as the v1 migration target — but only conditional on a measured falsifier, not as a default future direction. This makes v0 honest: we're betting that UDP loopback's measured budget contribution stays below threshold, and we've defined the threshold up front.

- **Migration falsifier (Critic R2 B1)**: **`p99 drag-to-render latency > 3.0 ms for ≥1% of windows under sustained 8-object 120 Hz drag in a 1 h soak`**. Operationalization:
  - Measurement: P8 latency harness extended into a 1 h drag soak with 8 active objects each receiving `/obj/{id}/pos` at 120 Hz; measure end-to-end input-OSC → speaker-onset latency in 1 s windows (3600 windows total); compute p99 of "drag-to-render" = the IPC + decode + audio-callback-wait portion of the latency table (stages 2–4 in Risk #2 decomposition).
  - Trigger: if at any P8 / P10 baseline run, ≥36 of 3600 windows show p99 > 3.0 ms in the IPC-dominant stages, file an issue tagged `migrate-to-shm-uds` and v1 starts with the B2 migration as a first-class workstream.
  - Non-trigger: if the windows pass, v1 keeps OSC and the falsifier is re-measured on each major v0 release.
  - Why this is a *real* falsifier and not theater: 3.0 ms is well-defined (it is 60% of the 5 ms acceptance budget — leaving 2 ms for the non-IPC stages), 1% over 1 h is a tractable failure rate (not a tail-of-tail), and the windowing protocol is reproducible. If the user / Architect / Critic disagree with the 3.0 ms / 1% / 1 h numbers, they can be re-litigated; the structure of the falsifier is the load-bearing claim.

- **Consequences**:
  - **+** Latency budget IPC term is <0.2 ms — fits comfortably inside the 5 ms target.
  - **+** Free external-controller support (TouchOSC, OSCulator, future text2traj OSC adapter).
  - **+** Plain-text schema; `tcpdump` and `osc_debug_console.py` debuggable; collaborators don't need new tools.
  - **+** Schema versioning in-band (`schema_version` field + `/sys/protocol_version` handshake) makes v0→v1 IPC migration explicit, not implicit.
  - **−** UDP is best-effort. We mitigate with: 120 Hz coalescing at UI source, last-write-wins per-object SPSC drop on overflow at core, 30 Hz full-state heartbeat for resync, sequence numbers + acks for command-confirmation paths.
  - **−** No native back-pressure. The lossy `/sys/state` heartbeat (Critic R2 Tier 3) is the explicit non-blocking design; the audio path is *isolated by construction* from any UI slowness.
  - **−** Two processes, one IPC schema to maintain — but the schema IS what survives v0→v1, so this cost is paying directly into Driver D2.
  - **−** Default ports 9000/9001 collide with TouchOSC defaults (Risk #9); mitigated by `--osc-cmd-port` / `--osc-state-port` overrides + ephemeral-port discovery doc.

- **Follow-ups**:
  - P5 ships the `--osc-cmd-port` / `--osc-state-port` CLI overrides (Risk #9 mitigation).
  - P5 ships the `/sys/protocol_version` handshake (Critic R2 Item 11) and `schema_version` on every command.
  - P8 latency harness produces the 1 h 8-object 120 Hz drag soak that exercises the falsifier; a passing soak is a sign-off prerequisite.
  - On falsifier trigger: open `migrate-to-shm-uds` issue, start a v1 ADR 0003-revision documenting which stage(s) of the latency table exceeded their p99 estimates, and design the shm region layout (shared `ObjectFrame[MAX_OBJECTS]` + a UDS for commands/acks) before any code is written.
  - Schema versioning policy: `schema_version` is bumped on **any** breaking change to a command's argument list or semantics; non-breaking changes (new optional fields, new top-level commands) keep the version. The `proto/ipc_schema.md` document carries a per-version changelog; v0 ships `schema_version = 1`.
  - `assert_no_alloc` crate version pin lives in `core/Cargo.toml`; quarterly review checks upstream maintenance status (Critic R2 Tier 4 follow-up).

---

## Notes for Architect / Critic review (round 2 output → round 2 input)

This round 2 draft addresses Architect Round 1 verdict REVISE-FIRST in priority order (Items 1–8) and the two bonus items (Risk #9 IPC port collision, `proto/trajectory_schema.json` placeholder), and escalates to DELIBERATE mode (pre-mortem + expanded test plan now first-class top-level sections).

### Items still open for round 2 review
- **ADR 0003** is still a stub; primary reviewer ask remains: confirm OSC/UDP for v0 or argue for shm+UDS based on a concrete observable (e.g., p99 drag-to-render latency under N-object load). Round 1 did not flag this item, so it carries forward unchanged.
- **Open Item (f) listener count for P13**: spec says "method TBD"; user memory shows N=3 is "informal validation" only and N≥12 is the perceptual-significance bar from the vid2spatial v3 listening test. Round 2 leaves this at "N≥6 lab pilot, escalate to N≥12 if effect size is small" — Architect/Critic invited to fix the number now if there's a defensible reason to.
- **Geometry-edit Non-Goal** is now explicit (Architect R1 Item 5, option (b)). If Architect/Critic prefers option (a) — i.e., add `/sys/edit_geometry` to OSC v0 so geometry editing *is* the demonstrable end-to-end UI modification — flag and we revert to (a) in round 3.

### What changed since round 1 (cross-reference for reviewer)
- Header → round 2, status `DRAFT-PENDING-ARCHITECT-R2`, mode `DELIBERATE`, with a per-item changelog at the top.
- Architecture diagram annotated with decode-thread location + SPSC `ObjectUpdate` boundary.
- Repo layout: `bootstrap.sh`, `proto/trajectory_schema.json`, `docs/onboarding.md`, `docs/onboarding_timing.md`, `docs/p3_5_smoke.md`, `docs/ui_modification_examples.md`, `docs/lab_setup.md` added.
- Phases: P1 expanded with `bootstrap.sh`; P3.5 inserted; P5 contract pinned + decode-thread invariant test added; P8 specifies kernel + audio device class + 64 frame @ 48 kHz buffer target; P10 adds UI-process RSS sub-test; P12 sets ≤60 min hard target on Ubuntu 22.04.
- Risks: #2 specificity, #6 UI-RSS, #9 port collision (new).
- ADR 0001: single-process Rust + `egui` antithesis added with concrete reasons-for-rejection and counter-acknowledgment.
- ADR 0002: `nih-plug` 1.0 falsifier with trigger condition.
- New top-level sections: Pre-mortem (3 scenarios), Expanded Test Plan (unit / integration / e2e / observability).
