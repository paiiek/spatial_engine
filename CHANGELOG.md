# Changelog

All notable changes to the Spatial Engine project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
