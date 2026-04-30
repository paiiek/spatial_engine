# Deep Interview Spec v2.1: spatial_engine (Vendor-Augmented + Foundational Tech Locked)

## Metadata
- Interview ID: spatial-engine-2026-04-27 (resumed 2026-04-28 with vendor delta + foundational tech decisions)
- Total Rounds: 13 (R1-R6 initial + R7-R11 vendor-delta resume + R12-R13 foundational tech)
- Final Ambiguity Score: 6.0%
- Type: greenfield (in research-rich ecosystem: vid2spatial, text2hoa, text2traj_v3, MMAudio)
- Generated: 2026-04-28 (v2.1; supersedes v2 same-day, supersedes v1 dated 2026-04-27)
- Threshold: 20%
- Initial Context Summarized: yes
- Vendor Delta Source: `require.md` (vendor conversation, 2026-04-28)
- Status: PASSED
- Predecessor: `.omc/specs/deep-interview-spatial-engine.md` (v1, kept for history)

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.94 | 0.40 | 0.376 |
| Constraint Clarity | 0.96 | 0.30 | 0.288 |
| Success Criteria | 0.92 | 0.30 | 0.276 |
| **Total Clarity** | | | **0.940** |
| **Ambiguity** | | | **0.060** |

## Goal
**Real-time, object-based immersive audio rendering engine** for **performances and exhibitions** (공연/전시), in the spirit of **d&b Soundscape / L-Acoustics L-ISA**, with a development roadmap of `v0 lab prototype → v1 production native (3U rack server)`.

The engine routes multiple sound objects to a custom multi-speaker array in real time using **per-object selectable spatial rendering algorithms (VBAP, WFS, DBAP)**, supports **distance-dependent acoustics modeling** (gain, propagation delay, HF rolloff, reverb send) and **per-object insert DSP (4-Band EQ + Delay)**, and ships with **Dante PCIe-card audio I/O** (Digigram ALP-Dante on Linux, Focusrite Rednet PCIeNX on Windows).

The engine is the user's **main durable project** — not a paper artifact, not a one-shot demo — and the integration point that future research modules (`text2traj`, `vid2spat`) feed into.

## Constraints

### Output / Rendering
- **Primary v0 output**: custom Cartesian-coordinate speaker array, **lab-scale 4-8 channels**.
- **Speaker layout**: defined by **config file (Cartesian XYZ)**; v0 has **read-only matrix view** in GUI; full graphical layout editor deferred to v1+.
- **Rendering algorithms in v0**: **VBAP, WFS, and DBAP all implemented**, **per-object selectable** (each `Object` carries an `algorithm` field). Layout-compatibility checker validates that the chosen layout supports the chosen algorithm (e.g., WFS requires regular planar arrays).
- **Reverb in v0**: **single FDN-based algorithmic reverb** as the v0 concrete implementation, behind a `ReverbEngine` abstract interface that reserves a slot for v1+ IR convolution. Distance-dependent reverb amount via per-object send gain curve.
- **Future outputs (v1+)**:
  - **Binaural renderer (HRTF)** — promoted from "future output" to **v0 simultaneous side-output** for engineer monitoring (see Convenience Features).
  - **IR convolution reverb** — drops in as second `ReverbEngine` implementation behind same interface.
  - **VST3 control plugin** — DAW host parameter automation → engine command channel; not an audio plugin.

### Per-Object DSP Chain (v0 full chain)
```
Source → 4-Band EQ → Delay → Pan(VBAP|WFS|DBAP) → distance gain → distance HF rolloff (1-pole LPF) → distance propagation delay (fractional + smoothing) → reverb send → output bus
```
All elements are **v0 mandatory** per `require.md` item 8.

### External Control / Integration
- **OSC over UDP**: v0 primary external control channel; sole transport in v0.
- **VST3 Control Plugin**: **v1+ slot only**, behind `ExternalControl` abstraction. v0 OSC backend uses an abstract `Command` schema so v1+ VST3 reuses the schema.
- **Audio VST3/AU/AAX (carrying audio)**: **permanently non-goal** — engine is and remains a standalone server.

### Audio I/O Hardware
- **Linux v0**: **Digigram ALP-Dante PCIe** (https://www.getdante.com/product/alp-dante/) via ALSA/JACK.
- **Windows v1+**: **Focusrite Rednet PCIeNX Dante** (https://focusrite.com/products/rednet-pcienx) via ASIO/WASAPI.
- Native audio core (cpal or JUCE) abstracts the device backend; both Dante PCIe families look like standard ALSA/ASIO devices to the host.

### Form Factor
- **Target deployment**: **3U rack-mount server** (production v1+ industrial deployment).
- **v0 development**: standard lab workstation; spec only encodes *3U-compatible code constraints* (low-noise, no passive/low-power-GPU dependency, ECC-RAM-friendly memory patterns).

### OS Target
- **v0**: **Linux only** (industry preference per vendor; single audio backend = ALSA/JACK + Digigram).
- **v1+**: **Windows added** with Rednet PCIeNX. Cross-platform-friendly stack picked from day one (cpal/JUCE/PySide6).

### Performance & Acceptance Numerics (v0 already pre-production-grade)
- **8+ simultaneous sound objects** with full per-object DSP chain.
- **Apparent positional accuracy ±1°** (numerical pan-law verification).
- **End-to-end input → speaker latency < 5 ms** on lab target machine.
- **Multi-hour uninterrupted operation** without dropouts/leaks.
- **Per-object DSP load verification (numerical)**: with `8 obj × VBAP × full chain`, **Xrun 0 / 30 min, mean CPU < 50%, 99-percentile per-block processing time < block_size × 0.7**.

### Stack (foundational tech locked at R12-R13)
- **v0 (hybrid)**:
  - **Native audio core**: **C++ + JUCE** (decided R12). Industry-standard audio framework; first-class VST3 SDK integration; mature `juce::dsp` modules (Biquad, IIR, Convolution, SmoothedValue, AudioBuffer) accelerate per-object DSP chain implementation; `juce::AudioIODeviceType` handles Dante PCIe via ALSA/JACK on Linux. License: GPL for non-commercial v0; commercial license required at v1+ industrial deployment (~$130/mo or perpetual).
  - **UI**: Python + PySide6 (Qt6 bindings).
  - **IPC**: **OSC over UDP, single transport** (decided R13). Same transport and same `Command` schema as external control — UI is the engine's first-class OSC client. JUCE `juce::OSCSender`/`juce::OSCReceiver` ↔ Python `python-osc`. v1+ VST3Control plugin and external OSC controllers (TouchOSC, etc.) join the same schema.
- **v1+**: production native end-to-end. JUCE core preserved (now also packaged as VST3 control plugin via `juce::AudioProcessor` wrapper). UI rewritten in production stack (likely C++/JUCE-native). WebGUI option may also be added at v1+ via OSC + WebSocket adapter.

### Convenience Features (v0 scope confirmed)
- **GUI (PySide6)**: top-down/3D speaker view, drag-to-move objects (1-4 in v0 bring-up), parameter panels, transport, scene management.
- **Binaural Monitor (KEMAR SOFA)**: simultaneous side-output alongside `SpeakerArray`. Engineer can monitor current scene over headphones without physical array.
- **Noise Generator**: per-channel white/pink noise with gain slider, routed directly to speaker output for array verification.
- **Audio Matrix**:
  - DSP routing (input channel → object slot, rendered channel → physical Dante output) is **v0 core** (sample path).
  - GUI is **v0 minimal**: YAML/JSON config + read-only matrix table view + runtime change via OSC API. Full graphical matrix editor → v1+.

### Collaboration Mode
- Engineers iterate on features and UI/UX with the user. UI must be inspectable, runnable, modifiable by collaborators. Concrete cadence/team size remains an open item (planning-phase concern, not a spec gate).

### Reuse
- KEMAR SOFA HRTF, FOA encoders from `text2hoa` ecosystem, trajectory format from `vid2spatial` (radians az/el, meters dist) — available for monitoring and future input modules.

## Non-Goals
- Full live-concert / production-scale showcase (16ch+, multi-act show running) — deferred to v1+.
- Binaural-first **delivery** (binaural monitor IS in v0 as a side-output for engineer use, but not the primary delivery target).
- **Audio-carrying VST3/AU/AAX plugin (engine-as-plugin)** — permanently non-goal.
- v0 graphical speaker-layout editor / graphical audio-matrix editor — config-file-driven in v0.
- Trajectory generation modules (`text2traj`, `vid2spat`) — kept as future input plugins.
- IR convolution reverb in v0 — interface slot only.
- Windows v0 — Linux only in v0; Windows added at v1+.
- 3U rack hardware integration in v0 — only *3U-compatible code constraints* in v0; physical rack at v1+ deployment.

## Acceptance Criteria
- [ ] Custom Cartesian-coordinate 4-8ch speaker layout config loads and routes correctly on lab Linux machine with Digigram ALP-Dante PCIe.
- [ ] All three rendering algorithms (VBAP, WFS, DBAP) implemented; each `Object` selects its algorithm at runtime; layout-compatibility checker rejects incompatible combinations with clear error.
- [ ] Per-object full DSP chain (4-Band EQ → Delay → Pan → distance gain → distance HF rolloff → distance propagation delay → reverb send) processes 8 simultaneous objects without dropouts.
- [ ] FDN-based algorithmic reverb operates with per-object send modulation; distance-dependent reverb amount audibly increases with distance.
- [ ] `ReverbEngine` interface uses block-based `process()`, `getLatencySamples()`, optional `SupportsIRLoading` capability, and extensible parameter-ID table — future IR-convolution drop-in requires no changes to bus/send/routing code.
- [ ] `OutputBackend` abstraction has **two simultaneous v0 implementations**: `SpeakerArray` (Dante PCIe, 4-8ch) + `BinauralMonitor` (KEMAR SOFA, headphones).
- [ ] `ExternalControl` abstraction with `OSCBackend` v0 implementation; abstract `Command` schema covers all object-position / object-gain / scene-control operations; VST3Control v1+ slot is callable.
- [ ] Distance-dependent propagation delay uses fractional delay + smoothing/crossfade — no zipper/click artifacts when object moves rapidly.
- [ ] GUI: top-down/3D speaker view, drag-to-move 1-4 objects in real time, read-only audio-matrix view, noise-generator panel, parameter inspectors.
- [ ] Engineer collaborator can clone, build, and run engine + GUI on their Linux machine and modify a UI element end-to-end.
- [ ] **Numerical performance gate**: `8 obj × VBAP × full chain` sustains Xrun 0 / 30 min, mean CPU < 50%, 99-percentile per-block time < block_size × 0.7.
- [ ] **Latency gate**: end-to-end input → Dante output measured < 5 ms.
- [ ] **Positional accuracy gate**: numerical pan-law verification ≤ ±1° apparent angle error.
- [ ] **Stability gate**: multi-hour uninterrupted operation without memory leaks, GC/IPC stalls, or audio underruns.
- [ ] IPC layer between native audio core and PySide6 UI documented (transport, message schema, state ownership).
- [ ] Code constraints for 3U-rack target (no passive/low-power-GPU dependency, low-noise, ECC-friendly memory patterns) documented.

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "spatial engine" was an obvious name with one obvious meaning | R1 ontology question forced category choice | Real-time interactive engine, L-ISA/Soundscape family. text2traj/vid2spat are *future inputs*, not the core. |
| "Real-time engine" implies multi-speaker arrays only | R2 listed binaural / DAW-plugin / hybrid | Multi-speaker is v0 primary; `OutputBackend` pluggable from day one. |
| v0 demo could be production-scale or HW-less simulator | R3 forced demo modality choice | Lab 4-8ch + GUI is v0; production-scale showcase deferred. |
| Production-grade native (JUCE/C++) was the only "serious" stack | R4 (Contrarian) | Two-phase: v0 prototype → v1 production native. |
| v0 = throw-away ⇒ relaxed acceptance numbers | R5 forced explicit pick | User chose pre-production-grade for v0. |
| Pure Python+Qt v0 satisfies pre-prod numbers | R6 (Simplifier) | Hybrid: native audio core + Python/Qt UI + IPC. |
| One rendering algorithm (VBAP) covers v0 | **R7** vendor-delta — `require.md` lists VBAP/WFS/DBAP, "오브젝트 별 별도 구현" | **All three v0, per-object selectable** (`algorithm` field on `Object`); layout-compatibility checker validates. |
| Reverb is "or" between 3D and IR | **R8** vendor-delta — `require.md` 3 lists "3D 리버브 또는 IR 리버브" | **FDN algorithmic v0 single concrete impl** behind `ReverbEngine` abstraction; **IR convolution v1+ slot** with 4 interface hooks (block-based `process`, `getLatencySamples`, `SupportsIRLoading` capability, ID-based extensible parameter table) so future drop-in is "구현체 하나 끼우기". |
| `DAWPlugin` v1+ slot was for an audio plugin (engine-as-VST) | **R9** vendor-delta — `require.md` 10 says VST3 = "오직 컨트롤 제어만" | **`OutputBackend.DAWPlugin` slot deleted (audio VST permanently non-goal)**. New `ExternalControl` abstraction added: `OSCBackend` v0, `VST3Control` v1+ slot. Abstract `Command` schema shared. |
| Per-object DSP (EQ/Delay) and distance-dependent acoustics could be deferred to v1+ | **R10** vendor-delta — `require.md` 8 lists them as core "파라미터" | **Full chain in v0**: 4-Band EQ + Delay + 4 distance-dependent variables (gain, HF rolloff, propagation delay, reverb send). Fractional-delay + smoothing for propagation delay (zipper-noise mitigation) baked in from day one. |
| OS target / hardware form factor / convenience features needed individual rounds | **R11** batch triage | **Linux only v0** (Win v1+); **3U rack as v1+ deployment**, v0 only encodes 3U-compatible code constraints; **PySide6 confirmed** (WebGUI v1+ option via OSC/WebSocket adapter); **Audio Matrix DSP routing v0 core, GUI v0 minimal**; **Noise Generator + Binaural Monitor v0 mandatory**; **per-object DSP load verification = numerical** (Xrun 0/30 min, CPU <50% mean, 99p block time < block_size×0.7). |

## Technical Context
- **Working directory**: `/home/seung/mmhoa/spatial_engine/`. v0 starts essentially empty.
- **Vendor source**: `/home/seung/mmhoa/spatial_engine/require.md` (industrial-vendor conversation; treated as authoritative requirements delta over v1 spec).
- **Sibling repos under** `/home/seung/mmhoa/`:
  - `vid2spatial`, `vid2spatial_v2` — KEMAR SOFA HRTF, FOA encoders, trajectory JSON format.
  - `text2hoa` — text → HOA renderer.
  - `text2traj_v3` — text → keyframe list `[{t, az_deg, el_deg, dist_m}, ...]`.
  - `MMAudio`, `synth3d`, `demo`, `thesis_doc`.
- **Reference systems**: d&b Soundscape (En-Scene/En-Space, OSC-controlled algorithmic reverb, theatrical/installation focus), L-Acoustics L-ISA (object-based, large arrays, live concerts).
- **Coordinate convention**:
  - Internal: `(az, el)` radians, `dist_m` meters. `az = atan2(x, z)` ⇒ RIGHT of image az > 0.
  - AmbiX/SOFA standard: LEFT = az > 0 ⇒ negate `az` before FOA/HRTF encoding (existing fix in vid2spatial ecosystem).
  - Speaker layout config: Cartesian XYZ (per `require.md` 4).
- **Audio backend** (decided R12): C++ JUCE. `juce::AudioIODeviceType` selects ALSA or JACK on Linux; same abstraction handles ASIO on Windows when v1+ adds Rednet PCIeNX.
- **IPC** (decided R13): OSC-over-UDP, single transport, single `Command` schema, unified with external OSC control + v1+ VST3Control. Hybrid (OSC + shm ring) deferred unless v0 profiling shows localhost UDP saturation.
- **GUI**: PySide6 confirmed.
- **Reverb implementation**: FDN with ~16 delay lines + diffusion matrix + tone control + per-object send bus. RT60 / room-size parameters real-time modulatable.
- **Distance model**:
  - Gain: 1/r or configurable rolloff.
  - HF rolloff: 1-pole LPF, cutoff ∝ 1/r.
  - Propagation delay: r/c (c = 343 m/s), fractional delay line per object with smoothing.
  - Reverb send: send-gain curve increasing with r (per `require.md` 8 last bullet).

## Ontology (Key Entities, final)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| SpatialEngine | core domain | runState, objectList, outputBackends, externalControl, reverbEngine, audioMatrix | owns Objects, drives OutputBackends, hosts ReverbEngine, ExternalControl, AudioMatrix |
| Object | core domain | id, position(az,el,dist), audioSource, gain, **algorithm: enum(VBAP\|WFS\|DBAP)**, **eq: 4BandEQ**, **delay: DelayLine**, **distanceModel: {gain, hfRolloff, propagationDelay, reverbSend}** | rendered by SpatialEngine via selected RenderingAlgorithm |
| RenderingAlgorithm | core domain (abstract) | type, paramTable | implemented by VBAPRenderer, WFSRenderer, DBAPRenderer |
| VBAPRenderer / WFSRenderer / DBAPRenderer | core domain (concrete, v0) | algorithm-specific params | concrete RenderingAlgorithm |
| LayoutCompatibilityChecker | core domain | rules | validates (SpeakerLayout, RenderingAlgorithm) pairs |
| OutputBackend | core domain (abstract) | type, channelCount, deviceConfig | implemented by SpeakerArray, BinauralMonitor (both v0), DAWPlugin (REMOVED) |
| SpeakerArray | core domain (v0 impl) | channelCount(4-8), cartesianGeometry, panLaw | concrete OutputBackend, primary v0 output |
| BinauralMonitor | core domain (v0 impl) | sofaPath(KEMAR), hrirSampleRate | concrete OutputBackend, simultaneous side-output for engineer monitoring |
| SpeakerLayout | supporting | XYZ coords, channel map | loaded from config file (v0); GUI editor v1+ |
| ReverbEngine | core domain (abstract) | block-based process(), getLatencySamples(), paramTable | implemented by FDNReverb (v0), IRConvolution (v1+ slot) |
| FDNReverb | core domain (v0 concrete) | delayLines, diffusionMatrix, RT60, roomSize, toneControl | concrete ReverbEngine |
| IRConvolution | core domain (v1+ slot) | partitionedConvolution, IR file path | concrete ReverbEngine, implements SupportsIRLoading |
| SupportsIRLoading | capability trait | loadImpulseResponse(path) | optional capability on ReverbEngine impls |
| FractionalDelayWithSmoothing | supporting | currentDelay, targetDelay, smoothingTau | per-object propagation-delay component |
| AudioMatrix | core domain | inputToObjectMap, outputToPhysicalMap | routes input channels → object slots, rendered channels → Dante outputs |
| ExternalControl | core domain (abstract) | commandSchema | implemented by OSCBackend (v0), VST3Control (v1+ slot) |
| OSCBackend | core domain (v0 concrete) | port, address-pattern bindings | concrete ExternalControl |
| VST3Control | core domain (v1+ slot) | host param bindings | concrete ExternalControl, NOT an audio plugin |
| Command | supporting | type, payload | abstract operation; serialized by ExternalControl backends |
| DanteIO | infrastructure | platformDriver(ALSA/JACK on Linux, ASIO on Win) | wraps Digigram ALP-Dante (v0) and Rednet PCIeNX (v1+ Win) |
| NoiseGenerator | supporting (v0) | type(white/pink), gain, channel | direct-to-output diagnostic tool |
| Trajectory | supporting | keyframes:[{t,az,el,dist}] | drives Object position over time; future inputs from text2traj/vid2spat |
| GUI | core (v0 deliverable) | viewType(top-down/3D), dragController, matrixView, noiseGenPanel | reads SpatialEngine state, writes via IPCLayer |
| DragController | supporting | inputDevice, mappingFn | UI input → Object.position |
| AudioInput | supporting | source(file/live/synth), format | feeds Object.audioSource |
| IPCLayer | core (v0 deliverable) | transport, schema, ownership | bridges native audio core ↔ Python UI |
| Engineer | external | role | iterates on GUI/UX, collaborator |
| ReferenceSystem | external | name(d&b Soundscape, L-ISA) | design inspiration |
| DevelopmentRoadmap | meta | phases:[v0 hybrid prototype Linux, v1 production native + Win + 3U rack] | governs stack/OS/HW transitions |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|--------------|-----|---------|--------|-----------------|
| 1 | 6 | 6 | — | — | N/A |
| 2 | 8 | 2 | 0 | 6 | 75% |
| 3 | 10 | 2 | 1 | 7 | 80% |
| 4 | 11 | 1 | 0 | 10 | 91% |
| 5 | 11 | 0 | 0 | 11 | 100% |
| 6 | 12 | 1 | 0 | 11 | 92% |
| 7 (vendor-delta) | 14 | 3 (RenderingAlgorithm + 3 concretes + LayoutCompatibilityChecker, counted as 3 net) | 1 (Object += algorithm field) | 11 | 92% |
| 8 (vendor-delta) | 17 | 4 (ReverbEngine, FDNReverb, IRConvolution, SupportsIRLoading) — 1 reused slot | 0 | 14 | 94% |
| 9 (vendor-delta) | 19 | 3 (ExternalControl, OSCBackend, VST3Control, Command) — 1 deletion (DAWPlugin) | 0 | 17 | 95% |
| 10 (vendor-delta) | 21 | 1 (FractionalDelayWithSmoothing) + Object DSP fields | 1 (Object += eq, delay, distanceModel) | 19 | 95% |
| 11 (vendor-delta, final) | 25 | 4 (BinauralMonitor, AudioMatrix, NoiseGenerator, DanteIO) | 1 (OutputBackend now has 2 simultaneous v0 impls) | 21 | 96% |

Domain converged at R11 with strong vendor-delta integration; all new entities trace cleanly to specific `require.md` items.

## Architecture Sketch (v0 hybrid, vendor-augmented)

```
┌──────────────────────────────────────────────────────────────────┐
│ Python / PySide6 UI                          (throw-away v0 UI)  │
│  - top-down / 3D speaker view (Cartesian layout from config)     │
│  - drag-to-move 1-4 objects (DragController)                     │
│  - parameter panels (per-object: pos, gain, EQ, Delay, dist mdl) │
│  - read-only AudioMatrix view (full editor → v1+)                │
│  - NoiseGenerator panel (per-channel white/pink + gain)          │
│  - transport, scene mgmt                                         │
└──────────────────────────────┬───────────────────────────────────┘
                               │
                      ┌────────▼────────┐
                      │  IPC: OSC/UDP   │  (single transport, single Command schema)
                      │  state + cmds   │  juce::OSC ↔ python-osc
                      └────────┬────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────┐
│ Native Audio Core (C++ + JUCE)          (production-ready v0)    │
│                                                                  │
│  ExternalControl (abstract)                                      │
│    ├─ OSCBackend     ← v0                                        │
│    └─ VST3Control    ← v1+ slot (control-only, NOT audio plugin) │
│         ↓ Command schema                                         │
│  AudioMatrix                                                     │
│    ├─ inputCh → ObjectSlot                                       │
│    └─ renderedCh → physical Dante output                         │
│         ↓                                                        │
│  Per-Object DSP Chain (× 8+ objects):                            │
│    Source → 4-Band EQ → Delay → Pan(VBAP|WFS|DBAP)               │
│         → distance gain → distance HF rolloff (1-pole LPF)       │
│         → distance propagation delay (fractional + smoothing)    │
│         → reverb send → object bus                               │
│         ↓                                                        │
│  ReverbEngine (abstract)                                         │
│    ├─ FDNReverb            ← v0 concrete                         │
│    ├─ IRConvolution        ← v1+ slot (SupportsIRLoading)        │
│    └─ HybridReverb         ← v2+ option                          │
│         ↓                                                        │
│  OutputBackend (abstract, simultaneous in v0):                   │
│    ├─ SpeakerArray (4-8ch, Cartesian, VBAP/WFS/DBAP)  ← v0       │
│    │     ↓ DanteIO (Digigram ALP-Dante on Linux, Rednet on Win)  │
│    └─ BinauralMonitor (KEMAR SOFA, headphones)        ← v0       │
│                                                                  │
│  NoiseGenerator → direct-to-channel (verification utility)       │
└──────────────────────────────────────────────────────────────────┘
```

## Open Items (planning-phase decisions, no spec gate)
*Native core language and IPC transport were resolved at R12-R13 and moved into Constraints/Stack above.*
- **Engineer collaboration cadence + team size**: 1 vs N collaborators, sync vs async, code review vs co-coding. Affects repo policy. Resolve at project kickoff.
- **AudioInput v0 source mix**: file playback (simplest), live mic (validates real-time path), synth (decoupled from I/O). Recommend file + synth in v0 bring-up; live mic added once Dante I/O is verified.
- **FDN topology specifics**: number of delay lines (8 vs 16), diffusion-matrix family (Hadamard vs Householder), tone-control model. Implementation detail; decide in planning.
- **VBAP / WFS / DBAP parameter tuning**: per-algorithm gain compensation, edge-fade behavior, layout-compatibility rule details. Implementation detail.
- **IPC profiling threshold**: monitor OSC throughput in v0; if 60Hz drag + meter updates push localhost UDP near saturation, revisit hybrid (OSC + shm ring) at v0 mid-cycle. Default: OSC-only.
- **JUCE commercial license trigger**: free GPL for v0 lab/research; commercial license procured at v1+ industrial deployment kickoff.

## Vendor Requirements Traceability (`require.md` → spec)
| `require.md` # | Item | Spec section |
|---|---|---|
| 1 | 공연/전시 활용 | Goal |
| 2 | 오브젝트 베이스 + WFS/VBAP/DBAP | Goal, Constraints/Output, R7 |
| 3 | 3D 또는 IR 리버브 | Constraints/Output, R8 (FDN v0 + IR v1+ slot) |
| 4 | 커스텀 스피커 레이아웃 (Cartesian) | Constraints/Output (config-file driven), Architecture |
| 5 | Linux 또는 Win, 산업=Linux | Constraints/OS Target, R11 (Linux v0, Win v1+) |
| 6 | <5ms 레이턴시 | Performance & Acceptance Numerics |
| 7 | 3U 랙마운트 서버 | Constraints/Form Factor, R11 (target deployment v1+) |
| 8 | 파라미터 (Position / Object Processing EQ+Delay / 거리 종속 4변수) | Per-Object DSP Chain, R10 (full chain v0) |
| 9 | 오브젝트 별 알고리즘 별도 구현 | Constraints/Output (per-object `algorithm` field), R7 |
| 10 | 편의기능 (GUI / OSC / VST3 control / 바이노럴 모니터링 / 노이즈 / 매트릭스) | Constraints/External Control, Convenience Features, R9, R11 |
| 11 | Dante PCIe (Digigram on Linux, Rednet on Win) | Constraints/Audio I/O Hardware |

## Interview Transcript
<details>
<summary>Full Q&A (11 rounds: R1-R6 initial, R7-R11 vendor-delta resume)</summary>

### Round 1 — Goal Clarity (ontology, initial)
**Q:** "spatial engine"은 본질적으로 무엇인가? (4 options)
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
**Q:** Pre-prod 수치 + throw-away v0를 동시에 만족시키는 가장 단순한 v0 구조는?
**A:** Hybrid: native 오디오 코어 + Python/Qt UI.
**Ambiguity:** 13% ✅ (initial threshold met)

--- vendor delta integration begins (require.md from vendor conversation) ---

### Round 7 — Constraint Clarity (rendering algorithm policy)
**Q:** require.md 8번 끝줄 "오브젝트 별 렌더링 알고리즘 별도 구현"의 의미? VBAP만 v0 / VBAP+DBAP v0, WFS v1+ / 셋 다 v0 per-object / 알고리즘은 layout 단위 / 기타.
**A:** C — 세 알고리즘 모두 v0, 오브젝트 별 user 선택.
**Ambiguity:** 17.4%

### Round 8 — Constraint Clarity (reverb)
**Q:** v0 리버브 형태? IR only / 알고리즘 only / 둘 다 / hybrid early-late / 인터페이스만 둘 다, 실제는 IR / 기타.
**A:** B+E 추천 수용 — FDN 알고리즘 단일 구현 v0 + ReverbEngine 추상화에 IR convolution v1+ 슬롯 + 4 hooks.
**Confirm:** "나중에 IR 필요하면 단순하게 컨볼루션만 끼워넣으면 될거 같은데 어때?" → assistant 4 hooks 정리 (block-based process, getLatencySamples, SupportsIRLoading capability, ID-based param table). 사용자 확인.
**Ambiguity:** 13.1%

### Round 9 — Constraint Clarity (VST3 control-only scope)
**Q:** require.md 10번 VST3 = "오직 컨트롤 제어만"의 슬롯 위치? ExternalControl v1+ / ExternalControl v0 / 두 슬롯 다 유지 / 영원히 안 함 / 기타.
**A:** A — ExternalControl 슬롯, v1+. Audio VST3는 영구 폐기. v0=OSC만.
**Confirm:** "나중에 VST 확장 작업할 수도 있나 필요시?" → assistant 2 hooks 정리 (ExternalControl abstract + Command schema transport-분리). v1+ VST3 추가 = SDK 바인딩 + 메시지 어댑터로 끝.
**Ambiguity:** 11.5%

### Round 10 — Constraint Clarity (per-object DSP chain v0)
**Q:** require.md 8번 per-object DSP 체인 v0 범위? 풀 체인 / 공간 핵심만 / Pan + gain만 / propagation delay만 v1+ / 기타.
**A:** 추천 수용 (풀 체인 A) — Source→EQ→Delay→Pan→distance gain→HF rolloff→propagation delay→reverb send. propagation delay는 fractional + smoothing 처음부터.
**Ambiguity:** 9.6%

### Round 11 — Constraint Clarity batch (final scope items)
**Q:** 7개 잔여 항목 triage 테이블 — OS / 3U HW / UI 스택 / Audio Matrix / Noise Gen / Binaural Monitor / 부하 검증 메서드.
**A:** "다 그대로" — Linux only v0 (Win v1+) / 3U는 v1+ 배포, v0는 코드 제약만 / PySide6 confirmed (WebGUI v1+ option) / Audio Matrix DSP 코어 v0 + GUI minimal v0 / Noise Gen v0 / Binaural Monitor v0 (OutputBackend 두 번째 v0 impl) / 수치 검증 (Xrun 0/30min, mean CPU<50%, 99p block time < block_size×0.7).
**Ambiguity:** 6.6% ✅

--- foundational tech decisions ---

### Round 12 — Constraint Clarity (native core language)
**Q:** native audio core 언어/프레임워크? C++ JUCE / Rust+cpal+자작DSP / C++ 자작 / Rust+nih-plug / 기타.
**A:** A — C++ + JUCE. 산업 표준, juce::dsp 모듈로 EQ/biquad/convolution/SmoothedValue 즉시 사용, AudioIODeviceType으로 Dante PCIe(ALSA/JACK) 처리, VST3 v1+ slot은 juce::AudioProcessor wrapper로 거의 공짜. 라이선스 = GPL 무료 v0, 상용 라이선스는 v1+ 산업 배포 시.
**Ambiguity:** 6.3%

### Round 13 — Constraint Clarity (IPC transport)
**Q:** PySide6 UI ↔ JUCE native core IPC? OSC/UDP 단일 / OSC + shm 하이브리드 / ZeroMQ / custom binary over UDS / 기타.
**A:** A — OSC over UDP 단일 transport, 단일 `Command` schema. UI = 엔진의 첫 번째 OSC 클라이언트, v1+ VST3Control + 외부 OSC 컨트롤러도 같은 schema 합류. JUCE juce::OSCSender/Receiver ↔ Python python-osc. 60Hz drag + meter는 localhost UDP로 충분, 프로파일링 후 hot path만 v0 mid-cycle에 shm으로 옮길 여지 남김.
**Ambiguity:** 6.0% ✅
</details>
