# Changelog

All notable changes to the Spatial Engine project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] — 2026-05-15

### Added
- **Commercial-grade binaural decoder on VST3 bus 1.** Replaces the v0.4
  -6 dB downmix placeholder with per-object HRTF summation (B1 path):
  for each active source (up to `MAX_OBJECTS = 64`), the engine convolves
  its dry signal with the nearest HRTF pair and sums into the stereo
  binaural bus. A `-1 dBFS` channel limiter caps the bus output. This is
  the same family of binaural rendering shipped by Dolby Atmos Renderer,
  Nuendo, and Apple Spatial Audio.
- **RT-safe direction updates.** `BinauralMonitor` now owns two
  pre-allocated `OlaConvolver` slots per (object, ear) and swaps via an
  atomic `front_idx_` per object. Control-thread `setDirection()` calls
  `OlaConvolver::loadInto(ir, ir_len)` — a no-allocation reload contract
  that returns silently on capacity violation (no audio-thread allocs,
  ever; tracked via `load_into_failures_` counter). A 2-block linear
  crossfade (`core/src/dsp/GainRamp.h`) bridges old → new IR with
  preempt-with-current-gain handoff for rapid direction changes.
- **O(log N) HRTF lookup.** Replaces the O(N) brute-force `sin`/`cos`
  search at `core/src/hrtf/HrtfLookup.cpp` with a 3D KD-tree on unit-
  Cartesian SOFA positions (`core/src/hrtf/KdTree3D.{h,cpp}`). Built
  once at `.speh` load (control thread, allocations OK); queries are
  iterative (no recursion) and allocation-free.
- **Per-object binaural test fixture.** `core/tests/fixtures/synthetic_min.speh`
  (committed) drives the v0.5 unit tests; the existing SOFA fixture is
  preserved for the SOFA-format-level tests.

### Deferred to v0.5.1 / v0.6
- B2 ambi → 24-point virtual-speaker mode (optional low-CPU path).
- Head-tracker hook (`/sys/headtrack ,fff yaw pitch roll`).
- JUCE partitioned-convolution binaural path (v0.5 ships OlaConvolver
  only).

### Notes for users
- Bus 0 speaker audio is bit-identical to v0.4 for all canonical
  fixtures.
- Memory footprint grows by ~1 MiB (64 × 2 × 2 × 1024 floats of slot
  buffers). MAX_IR_LEN = 1024 matches the SOFA loader's existing cap.
- If `.speh` is not loaded or binaural is disabled, bus 1 keeps the
  v0.4 -6 dB downmix placeholder for diagnostic intelligibility.

## [0.4.0] — 2026-05-15

### Added
- VST3 plugin now exposes two output buses: bus 0 "Speakers" (variable
  channel count negotiated via setBusArrangements; 2/4/6/8/12/16/24
  supported) and bus 1 "Binaural" (fixed stereo). Bus 1 currently emits
  a -6 dB speaker downmix placeholder; real binaural rendering arrives
  in v0.5.
- Plugin state schema migrated to v4 (sectioned TLV). v3 sessions load
  unchanged (back-compat merge-gate test).
- Layout YAML path and SOFA (.speh) path are persisted in plugin state
  and runtime-injectable via OSC (`/sys/load_layout ,s <path>`,
  `/sys/binaural_sofa ,s <path>`, `/sys/binaural_enable ,i {0,1}`).

## [0.3.1] — 2026-05-15 (channel mapping correctness pre-release)

### Fixed

- **Per-channel OSC endpoints now address speakers by YAML channel number,
  not by position in the YAML file.** Prior to v0.3.1, handlers for
  `/output/N/gain`, `/output/N/limit`, `/noise/N/type`, and `/noise/N/gain`
  treated the wire channel `N` as a 0-based index into the speaker vector,
  ignoring the `channel:` field declared in each layout entry. Reordered
  or sparse channel maps silently routed automation to the wrong speaker.
  v0.3.1 adds `SpeakerLayout::channelToIndex()` (backed by a fixed-size
  YAML-channel → vector-index lookup built by `LayoutLoader`) and rewrites
  the four drain handlers in `SpatialEngine` to use it.
- **Loud failure for unmapped channels.** Commands targeting a YAML
  channel not declared in the active layout are now dropped silently
  (RT-safe; no allocation). Duplicate `channel:` declarations and
  channels above `SpeakerLayout::kMaxYamlChannel` (64) are rejected at
  load time by `LayoutLoader` with explicit error strings.

### Breaking semantic note

External OSC automation that historically targeted *position* (e.g.
`/output/0/gain` to mean "first speaker in the YAML file") will now
target *YAML channel* (i.e. the speaker whose `channel: 0` is declared
— which is invalid under the 1-based contract, so the command is
dropped). All four canonical fixtures (`lab_4ch.yaml`, `lab_8ch.yaml`,
`lab_8ch_aligned.yaml`, `lab_8ch_irregular.yaml`) declare sequential
1-based channel numbers, so default workflows see zero behavior change.
Users running reordered or sparse channel maps may need to update their
automation scripts to address speakers by their declared YAML channel
number rather than their position in the file.

## [0.2.0] — 2026-05-10 (DAW hands-on pending — see release notes)

### Added

- **ADM-OSC v1.0 receive coverage** (`feat(C3-adm-osc)` 166f0c9)
  - Full `/adm/obj/N/{azim,elev,dist,aed,gain,mute,xyz,active,width,name}` decode
  - 4 new `CommandTag`s `0x06..0x09` (`ObjXYZ`, `ObjActiveAdm`, `ObjWidth`, `ObjName`)
  - 3 synthetic vendor compatibility fixtures (L-ISA, Spat Revolution, d&b Soundscape)
  - Soak harness `core/tests/perf/soak_adm_osc_flood.cpp` — 64 obj × 1 kHz × 60 s
  - ADR 0006 — ADM-OSC v1.0 spec freeze + `MAX_DIST=20.0f` constant
  - 88 compliance fixture rows in `core/tests/core_unit/adm_osc_v1_compliance.csv`
  - Bridge layer `bridge/_adm_osc_common.py` extracted for shared helpers
  - `--osc-dialect adm` CLI flag (default `legacy` preserves v0.1.0 behaviour)
- **HOA decoder diversification — 4 algorithms** (`feat(M2-hoa-extended)` e5924da)
  - MaxRE decoder (Legendre-root, M2HOA-Q7 resolved: g_1(N=1)=0.5774)
  - AllRAD decoder (t-design quadrature projection, O(n×K))
  - EPAD decoder (two-sided Jacobi SVD, cond>1e10 fallback)
  - InPhase decoder (Daniel 2000 §3.30, golden vectors verified)
  - 5-value `DecoderType` enum, RT-safe runtime dispatch
  - OSC `/sys/ambi_decoder_type i {0..4}` with `AmbisonicRenderer` plumbing
  - 4 new ctest fixtures (51/51 PASS)
- **VST3 plugin: production-grade 7-parameter integration** (`feat(C2B.*)` cb4737d, 744c0e6, 20a4da6, d1d42018, acb8c27)
  - JUCE-free `vst3sdk` hand-roll (Phase C C2 Option B)
  - 7 parameters: `kPanAz`, `kPanEl`, `kSourceWidth`, `kMasterGain`, `kAmbiOrder`, `kRoomPreset`, `kBypass`
  - Plugin entry + `IPluginFactory` vtable + 21-assertion host fixture
  - `IEditController` dispatch wiring with 1000-iter RT-safety probe
  - State v2 binary format (36-byte: 8-byte header + 7×float) with v1 multi-version reader at `vst3/SpatialEngineProcessor.cpp:267-289`
  - Bypass dry pass-through (channel-wise input→output memcpy with null-buffer guards)
  - `kIsBypass` flag on parameter id=6 per VST3 spec
  - `restartComponent(kParamValuesChanged)` on `setComponentState`
  - 53 ctest tests including `vst3_*` × 7 (all PASS)
- **OFF byte-baseline gate** (`ci(off-baseline)` 587815c)
  - Dual-gate: `core/libspe_core.a` + `core/spatial_engine_core` byte+symbol pinning
  - GHA `ubuntu-24.04` runner reproducible build verified
  - `LD_DEBUG=libs` runtime sysdep audit gate
  - Public re-pin path via `GITHUB_STEP_SUMMARY` echo (no token required)
- **LTC sync (Phase C1)** (`feat(C1.*)` c3edb6a, 6145a53, 59df7b7, 6716cf9, 6cf851c)
  - SMPTE LTC biphase decoder (25fps synthesis verified)
  - `LtcChase` audio→ring→control-thread consumer
  - `SpscRing<T,N>` reusable template + `QueuedCmd` POD
  - `NullBackend` audio-input path with NDEBUG strip lint hook
  - `/sys/ltc_chase` opcode `0x14` + `SpatialEngine` integration
- **Phase B feature parity**
  - M1: Per-object Source WIDTH (0..π rad) across VBAP / DBAP / WFS / HOA fan-out (50864bd)
  - M2: HOA `AmbiDecoder` + `AmbisonicRenderer` 1st-order + algorithm dispatch (e558149)
  - M3: `IRConvReverb` (OLA) + `/reverb/select` OSC + runtime FDN/IR switch (3d10dc3)
  - M4: Snapshot Crossfade — time-based scene transition interpolation (57254d5)
  - M5: `SPATIAL_ENGINE_VST3` build option (default OFF) (677dcbc)
  - M6: Per-speaker time-alignment (delay_ms / gain_db) at output stage (134062e)
  - M8: Object Trajectory Animation — circle / line / lissajous + WebGUI API (7662b11)
  - M9: Per-channel `ChannelLimiter` + `/output/{ch}/{gain,limit}` OSC (680c47b)
  - B3: `IRConvReverb` WAV loading + `scripts/fetch_ir.py` (b5b4ec1)
  - B4: DBAP width precision — 3-virtual-source power-sum + energy-preserving normalization (fc95a59)
  - B5: HOA 2nd/3rd-order decoder — Tikhonov pseudo-inverse + `/sys/ambi_order` (f73caff)
- **vid2spatial integration**
  - Phase 2 production bridge + dual-mode switch (4240b10)
  - `bridge/spike_vid2spatial_osc.py` IIR + 60 Hz rate-limit
  - WebGUI vid2spatial integration: bridge aed fix + start/stop API + UI buttons (3c8f3f8)
- **Korean documentation**
  - `docs/manual_kr/install/README.md` — 12-chapter installation manual (40fcc9b)
  - `docs/manual_kr/operation/README.md` — 15-chapter operation manual (40fcc9b)
- **Phase C4 design contract drafts (v0.2.0 ships drafts; v0.3.0 implements)**
  - ADR 0010 — VST3 plugin OSC binding model (per-instance recv-only UDP, A1-ε)
  - ADR 0011 — VST3 plugin multi-instance discovery (file-based JSON registry)
  - ADR 0012 — ADM-OSC vendor quirks overlay (reserved slot)
- **Test infrastructure**
  - VBAP gain cache (0.5° bins, open-addressing FIFO, prepareToPlay invalidation) (fc00a30)
  - AmbisonicEncoder 2nd/3rd-order (ACN/SN3D, 9ch / 16ch closed-form) (a0853e4)
  - VBAP 3D fallback gain pattern + numerical tests (89db643, f69e34a)

### Changed

- **GHA OFF baseline re-pinned** to `ubuntu-24.04` runner image (587815c, 8c0ca2d, ec2510d)
- **Limiter implementation**: peak-attack envelope + gain-ramp warmup, asserts enforced (5a60720)
- **WebGUI**: FastAPI lifespan migration + asyncio.run transition (c0aef3a)

### Deprecated

- (none in v0.2.0)

### Removed

- (none in v0.2.0)

### Fixed

- VST3 OFF baseline: `vst3.yml` Option B alignment + `p2_layout` configs bidirectional build (15fdb52)
- AmbisonicEncoder ACN4 coefficient: `kSqrt3 → kSqrt3_2` (was 2x error); regression test added (08e5e91)
- `OlaConvolver` alloc-free `process()` + `SofaBinReader` defensive validation (2c8086c)
- 5 critical WebGUI + bridge bugs before user handoff (d1b82f3)
- NaN Z assert + `blockSignals` on `set_object` + `sys` import hoisted (a2b10f9)
- `SceneController` handler + `fromJson` safety + path traversal guard + ctest 29/29 (d8056db)
- MIDI OSC send + VBAP 2D limitation documentation (0bee66c)
- pytest collection: importlib mode + `norecursedirs` (6af3778); pythonpath/testpaths extended for `ui/`, `ui/webgui/` (3b934e5)
- `sofa_inspector` IR_len 384 + flaky latency test + `ADDR_METRICS` + `utcnow` deprecation (97056c6)

### Security

- Path traversal guard added to `SceneController` (`d8056db`); `fromJson` rejects malicious filename payloads
- NaN Z assert + defensive validation in `SceneSnapshot` / Z-coordinate paths (a2b10f9)

### Compatibility

- **Built on**: Ubuntu 24.04 (GLIBC 2.39, GCC 13.3.0)
- **Older distros** (Ubuntu 22.04 / Debian 11) require building from source — see `docs/manual_kr/install/README.md` Chapter 3
- **Wire ABI** preserved from v0.1.0: `--osc-port` / `--osc-dialect` defaults unchanged; Component / Controller IIDs unchanged
- **VST3 state format**: v0.1.0 shipped state v1 (28 bytes, 6 floats); v2 added in C2B postmortem (acb8c27) with multi-version reader at `Processor.cpp:267-289`. v0.1.0 `.vstpreset` files load cleanly via the v1 reader path. No further state bump in v0.2.0.

### Known limitations

- VST3 plugin ADM-OSC routing deferred to Phase C4 / v0.3.0 (per Plan §1.4 deliverable matrix). v0.2.0 ships only the design contracts (ADRs 0010 / 0011 / 0012).
- macOS / Windows builds not yet covered by CI — Linux-only B3-β release artifact.
- DAW hands-on validation (R3) is gated to Reaper 7.x + Bitwig Studio 5.x on Linux only.

[0.2.0]: https://github.com/paiiek/spatial_engine/releases/tag/v0.2.0

---

## [0.1.0] — 2026-05-01

Initial public release. v0.1.0 commit `24c62c7a` — `P12 docs, perceptual pre-registration, stage-1 latency harness`.

Highlights:
- Real-time object-based immersive audio rendering engine (C++ JUCE-free core)
- VBAP 2D + 3D, DBAP, WFS, HOA 1st-order, FDN reverb, Binaural HRTF
- ADM-OSC receive subset (azim/elev/dist/gain/mute/aed)
- WebGUI (FastAPI + JS) for trajectory editing
- vid2spatial bridge for video→audio spatialization

[0.1.0]: https://github.com/paiiek/spatial_engine/releases/tag/v0.1.0
