# Deep Interview Spec: spatial_engine

## Metadata
- Interview ID: spatial-engine-2026-04-27
- Rounds: 6
- Final Ambiguity Score: 13%
- Type: greenfield (in research-rich ecosystem: vid2spatial, text2hoa, text2traj_v3, MMAudio, demo, thesis)
- Generated: 2026-04-27
- Threshold: 20%
- Initial Context Summarized: yes
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.90 | 0.40 | 0.360 |
| Constraint Clarity | 0.85 | 0.30 | 0.255 |
| Success Criteria | 0.85 | 0.30 | 0.255 |
| **Total Clarity** | | | **0.870** |
| **Ambiguity** | | | **0.130** |

## Goal
**Real-time, object-based spatial audio engine** in the spirit of d&b Soundscape / L-Acoustics L-ISA, with a development roadmap of `v0 prototype → v1 production native`. The engine routes multiple sound objects to a multi-speaker array in real time and is designed from day one to be extensible to binaural (HRTF) output, DAW plugin form factors (VST/AU/AAX), and to receive object trajectories from upstream modules (`text2traj`, `vid2spat`) in later phases.

The product itself — not a paper artifact, not a one-shot demo — is the unit of value. The engine becomes the user's main project ("여기서 메인으로") and the integration point that future research modules feed into.

## Constraints
- **Output backend (v0 primary)**: multi-speaker array, lab-scale 4–8 channels (research/studio room), object panning to speakers (likely VBAP or equivalent).
- **Output backend (v1+ secondary)**: binaural (HRTF) and DAW plugin (VST/AU/AAX) — must be reachable without backend rewrites; output backend abstracted from v0.
- **Stack (v0)**: Hybrid — native audio core (Rust `cpal` or C++ JUCE) + Python/Qt (PySide6) UI, with an IPC layer between. Audio real-time path is production-ready from day one; UI is the throw-away/iteration surface.
- **Stack (v1+)**: Production native end-to-end. The native audio core is preserved into v1; UI is rewritten in the production stack.
- **Acceptance numerics (pre-production-grade, applied to v0 already)**:
  - 8+ simultaneous sound objects
  - Apparent positional accuracy ±1°
  - End-to-end latency <5 ms
  - Multi-hour uninterrupted operation
- **Collaboration mode**: engineers iterate on features and UI/UX with the user. Therefore UI must be inspectable, runnable, and modifiable by collaborators (concrete cadence/team size: open item, see §Open Items).
- **Reuse**: existing assets in the user's MMHOA ecosystem (KEMAR SOFA, FOA encoders, vid2spatial trajectory format, etc.) are available and may be reused for monitoring / future input modules.

## Non-Goals
- Full live-concert / production-scale showcase (16ch+, multi-act show running) — deferred to v1+.
- Binaural-first delivery — binaural is a v1+ secondary output, not v0's primary path.
- DAW plugin packaging (VST/AU/AAX) — architecturally reachable but not built in v0.
- Trajectory generation modules (`text2traj`, `vid2spat`) — kept as future input plugins; engine in v0 accepts trajectories/positions from a simpler source (manual GUI drag, file, OSC).
- A new GUI built from scratch when the user's *only* need is engine iteration via OSC/MIDI from existing controllers (this option was explicitly considered in Round 3 and rejected: GUI is part of v0).
- A paper-only/throw-away academic demo. The engine is intended as the user's main durable project.

## Acceptance Criteria
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

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "spatial engine" was an obvious name with one obvious meaning | Round 1 ontology question forced choice between rendering-core / e2e-system / real-time-engine / sandbox | Real-time interactive engine, L-ISA/Soundscape family. text2traj/vid2spat are *future inputs*, not the core. |
| "Real-time engine" implies multi-speaker arrays only | Round 2 explicitly listed binaural / DAW-plugin / hybrid | Multi-speaker is v0 primary, but `OutputBackend` is pluggable from day one for future binaural / DAW plugin. |
| v0 demo could be production-scale or HW-less simulator | Round 3 forced choice of demo modality | Lab 4–8ch + GUI is v0; production-scale showcase deferred. |
| Production-grade native (JUCE/C++) was the only "serious" stack | Round 4 (Contrarian) challenged "production native is always optimal" | Two-phase strategy: v0 prototype (fast) → v1 production native. v0 stack still implicit at this point. |
| v0 = throw-away ⇒ relaxed acceptance numbers | Round 5 forced explicit pick of acceptance bar | User chose pre-production-grade (<5 ms / 8+ obj / multi-hour) for v0 already. |
| Pure Python+Qt v0 satisfies pre-prod numbers | Round 6 (Simplifier) surfaced the tension between the throw-away v0 and tight numerics | Hybrid: native audio core + Python/Qt UI. Audio real-time path is production-ready from v0; UI iterates fast in Python. v1 keeps the core, rewrites the UI. |

## Technical Context (greenfield, ecosystem-aware)
- **Working directory**: `/home/seung/mmhoa/spatial_engine/` — empty except `.omc/state/` at interview start.
- **Sibling repos (potential reuse)** under `/home/seung/mmhoa/`:
  - `vid2spatial`, `vid2spatial_v2` — video → binaural pipeline; KEMAR SOFA HRTF, FOA encoders, trajectory JSON format (radians az/el, meters dist).
  - `text2hoa` — text → HOA (FOA/HOA renderer).
  - `text2traj_v3` — text → trajectory keyframe list `[{t, az_deg, el_deg, dist_m}, ...]`.
  - `MMAudio`, `synth3d`, `demo` (ICASSP demo skeleton), `thesis_doc`.
- **Reference systems**: d&b Soundscape (En-Scene/En-Space, OSC control, theatrical/installation focus), L-Acoustics L-ISA (object-based, large arrays, live concerts).
- **Coordinate convention to inherit from MMHOA ecosystem** (per user memory):
  - Trajectory: `(az, el)` radians, `dist_m` meters; `az = atan2(x, z)` so RIGHT of image ⇒ az > 0.
  - AmbiX/SOFA standard: LEFT = az > 0 (counterclockwise from front). Negate az before FOA encoding (already a known fix in vid2spatial).
- **Likely audio backend candidates**: Rust `cpal` (cross-platform, low-latency) or C++ JUCE (industry reference, plugin-ready). JACK on Linux for low-latency multi-channel routing during lab bringup.
- **Likely IPC candidates** (open item): OSC over UDP (matches Soundscape/L-ISA control idioms, fits future external-controller scenario), or shared memory + control socket for tighter coupling.
- **Likely GUI**: PySide6 (Qt6 Python bindings) — cross-platform, mature, can host OpenGL/Vulkan view for 3D speaker layout.

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| SpatialEngine | core domain | runState, objectList, outputBackend | owns Objects, drives OutputBackend |
| Object | core domain | id, position(az,el,dist), audioSource, gain | rendered by SpatialEngine, optionally driven by Trajectory |
| OutputBackend | core domain (abstract) | type, channelCount, deviceConfig | implemented by SpeakerArray (v0), BinauralRenderer (v1+), DAWPlugin (v1+) |
| SpeakerArray | core domain (v0 impl) | channelCount(4–8), geometry, panLaw | concrete OutputBackend |
| Trajectory | supporting | keyframes:[{t,az,el,dist}] | drives Object position over time; future inputs from text2traj / vid2spat |
| GUI | core (v0 deliverable) | viewType, dragController | reads SpatialEngine state, writes Object positions via IPCLayer |
| DragController | supporting | inputDevice, mappingFn | UI input → Object.position |
| AudioInput | supporting | source(file/live/synth), format | feeds Object.audioSource |
| IPCLayer | core (v0 deliverable) | transport, schema, ownership | bridges native audio core ↔ Python UI |
| Engineer | external | role | iterates on GUI/UX, collaborator (cadence: open item) |
| ReferenceSystem | external | name(d&b Soundscape, L-ISA) | design inspiration |
| DevelopmentRoadmap | meta | phases:[v0 hybrid prototype, v1 production native] | governs stack transitions |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|--------------|-----|---------|--------|-----------------|
| 1 | 6 | 6 | — | — | N/A |
| 2 | 8 | 2 (OutputBackend, SpeakerArray) | 0 | 6 | 75% |
| 3 | 10 | 2 (DragController, AudioInput) | 1 (UI/UX → GUI) | 7 | 80% |
| 4 | 11 | 1 (DevelopmentRoadmap) | 0 | 10 | 91% |
| 5 | 11 | 0 | 0 | 11 | 100% |
| 6 | 12 | 1 (IPCLayer) | 0 | 11 | 92% |

Domain converged at Round 5; Round 6 introduced `IPCLayer` as a deliberate architectural artifact of the hybrid stack decision, not as a discovered domain entity.

## Architecture Sketch (v0 hybrid)

```
┌──────────────────────────────────────────────────────┐
│ Python / PySide6 UI                  (throw-away v0) │
│  - top-down / 3D speaker view                        │
│  - drag-to-move objects (DragController)             │
│  - parameter panels, transport, scene mgmt           │
└──────────────────────────┬───────────────────────────┘
                           │
                  ┌────────▼────────┐
                  │   IPC Layer     │  (OSC / shared mem / ZMQ — TBD)
                  │  state + cmds   │
                  └────────┬────────┘
                           │
┌──────────────────────────▼───────────────────────────┐
│ Native Audio Core              (production-ready v0) │
│  - real-time DSP graph                               │
│  - Object mixer + per-object pan/gain                │
│  - OutputBackend (abstract)                          │
│      ├─ SpeakerArray (4–8ch) ← v0                    │
│      ├─ BinauralRenderer (HRTF) ← v1+                │
│      └─ DAWPlugin (VST/AU) ← v1+                     │
└──────────────────────────────────────────────────────┘
```

## Open Items (sub-threshold; resolve in next phase)
- **Native audio core language**: Rust (`cpal`) vs C++ (JUCE). JUCE wins on plugin maturity; Rust wins on memory safety and matches modern audio dev trends. Decide in planning.
- **IPC transport**: OSC (loose, tooling-rich, matches L-ISA idiom) vs shared memory + control channel (tighter, lower-latency for state). OSC likely sufficient for v0; revisit at v1.
- **Engineer collaboration cadence**: 1 vs N collaborators, sync vs async, code review vs co-coding — affects repo policy and UI mock workflow.
- **AudioInput v0 source**: file playback (simplest), live mic (validates real-time path), synth (decoupled from I/O hardware). Recommend file + synth in v0.
- **Speaker geometry input**: hardcoded layout vs config file vs GUI editor. Recommend config file in v0.
- **Positional accuracy verification method**: numerical (compare intended vs panned vector) vs perceptual (listening test). Likely numerical for v0 acceptance.

## Interview Transcript
<details>
<summary>Full Q&A (6 rounds)</summary>

### Round 1 — Goal Clarity (ontology)
**Q:** "spatial engine"은 본질적으로 무엇인가? (4 options: rendering core / e2e system / real-time engine / sandbox)
**A:** 3번에 가깝고 d&b Soundscape / L-ISA 비슷. 추후 text2traj, vid2spat 추가. 엔지니어와 UI/UX iterate.
**Ambiguity:** 70.5%

### Round 2 — Constraint Clarity (output target)
**Q:** v0 1차 출력/배포 형태? (multi-speaker / binaural / both / DAW plugin)
**A:** 1번(멀티스피커), 추후 2(바이노럴) 또는 4(DAW plugin) 가능하게.
**Ambiguity:** 56%

### Round 3 — Success Criteria (v0 demo scenario)
**Q:** v0 'works'의 1차 시연 시나리오? (lab 4-8ch+GUI / production 16ch+ / headless engine / GUI sim)
**A:** 연구실 소규모 어레이 (4-8ch) + GUI.
**Ambiguity:** 35%

### Round 4 — Constraint Clarity (Contrarian: tech stack)
**Q:** v0 = lab + iterate라면 production native가 정말 최적인가? (Python+Qt / JUCE / Web / Hybrid)
**A:** 우선 전체적으로 되는지 보고 그다음에 production native.
**Ambiguity:** 30%

### Round 5 — Success Criteria (numeric bar)
**Q:** v0 '전체적으로 된다'의 정량 기준? (minimum-viable / lab-demo / pre-prod / soft only)
**A:** Pre-production-grade (v1 기준 그대로 적용).
**Ambiguity:** 22.5%

### Round 6 — Constraint Clarity (Simplifier: tension resolution)
**Q:** Pre-prod 수치 + throw-away v0를 동시에 만족시키는 가장 단순한 v0 구조는? (Hybrid / Pure Python+measure-only / JUCE day1 / relax numbers)
**A:** Hybrid: native 오디오 코어 + Python/Qt UI.
**Ambiguity:** 13% ✅
</details>
