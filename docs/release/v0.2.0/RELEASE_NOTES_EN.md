# Spatial Engine v0.2.0 Release Notes (English)

**Release date**: 2026-05-10 (tag pending DAW hands-on verification)
**Target**: Linux (Ubuntu 24.04 / GLIBC 2.39 prebuilt; other distributions build from source)
**Previous release**: [v0.1.0](https://github.com/paiiek/spatial_engine/releases/tag/v0.1.0) (2026-05-01)

---

## Highlights

Five things to know about this release.

1. **ADM-OSC v1.0 ecosystem parity** — Spatial Engine now consumes the
   full `/adm/obj/N/{aed, gain, mute, xyz, active, width, name}` packet
   set produced by L-ISA Controller, Spat Revolution, d&b Soundscape,
   and major mixing consoles. Drop-in receive coverage for the ADM-OSC
   v1.0 spec; legacy wire format is preserved behind
   `--osc-dialect legacy` (the default).
2. **HOA decoder selection** — Four new decoders (MaxRE, AllRAD, EPAD,
   InPhase) joinable at runtime via OSC
   `/sys/ambi_decoder_type i {0..4}`. Pick the right room / layout
   trade-off without recompiling.
3. **Production-grade VST3** — Seven automation parameters (Pan Az,
   Pan El, Source Width, Master Gain, Ambi Order, Room Preset, Bypass),
   binary state v2 with multi-version reader for v1 preset
   compatibility, dry-pass bypass, 1000-iteration RT-safety probe.
   JUCE-free toolchain retained (no X11 / freetype / ALSA transitive
   dependencies).
4. **OFF byte-baseline gate** — Every CI run on `ubuntu-24.04` pins
   both the byte hash and the symbol hash of `libspe_core.a` and
   `spatial_engine_core`. Re-pin path is public (GHA Step Summary
   echoes candidate hashes; no token required).
5. **Korean language manuals** — 12-chapter install guide and
   15-chapter operation guide, covering live workflows, the OSC
   protocol reference, and troubleshooting.

---

## Added

### ADM-OSC v1.0 receive coverage (Phase C3)
- Full `/adm/obj/N/{azim, elev, dist, aed, gain, mute, xyz, active, width, name}` decoding
- 4 new `CommandTag`s: `ObjXYZ` (0x06), `ObjActiveAdm` (0x07),
  `ObjWidth` (0x08), `ObjName` (0x09)
- 88-row compliance CSV fixture
  (`core/tests/core_unit/adm_osc_v1_compliance.csv`)
- 3 synthetic vendor fixtures (L-ISA, Spat Revolution, d&b Soundscape)
  — to be replaced by real captures within 60 days of first customer
  contract
- Soak harness `core/tests/perf/soak_adm_osc_flood.cpp` —
  64 obj × 1 kHz × 60 s
- ADR 0006 spec freeze + `MAX_DIST=20.0f` single source of truth
- Bridge helper `bridge/_adm_osc_common.py` extracted
- `--osc-dialect adm` CLI flag (default `legacy` preserves v0.1.0
  behaviour)

### HOA decoder diversification (M2-HOA Extended)
- **MaxRE**: Legendre-root based (M2HOA-Q7 resolved: g_1(N=1)=0.5774)
- **AllRAD**: t-design quadrature projection (O(n×K))
- **EPAD**: two-sided Jacobi SVD (cond>1e10 fallback)
- **InPhase**: Daniel 2000 §3.30, golden vectors verified
- 5-value `DecoderType` enum + RT-safe runtime dispatch
- OSC `/sys/ambi_decoder_type i {0..4}` control surface
- ctest 51/51 PASS

### VST3 plugin integration (Phase C C2 Option B + C2B postmortem)
- JUCE-free vst3sdk hand-roll build
- 7 automation parameters (id 0..6): Pan Az, Pan El, Source Width,
  Master Gain, Ambi Order, Room Preset, Bypass
- Binary state v2 format (36 bytes: 8-byte header + 7×float) with
  multi-version v1 reader (`vst3/SpatialEngineProcessor.cpp:267-289`)
- Bypass dry pass-through (channel-wise input→output memcpy with
  null-buffer guards)
- `kIsBypass` flag on parameter id=6 (VST3 spec compliance)
- `restartComponent(kParamValuesChanged)` on `setComponentState`
- 21-assertion in-process host fixture + 1000-iter RT-safety probe
- 53 ctest tests (7 vst3_*; all PASS)

### OFF byte-baseline gate
- Dual-gate: `core/libspe_core.a` + `core/spatial_engine_core`
  byte+symbol hash pinning
- GHA `ubuntu-24.04` runner reproducible build verified
- `LD_DEBUG=libs` runtime sysdep audit gate
- Public re-pin path via `GITHUB_STEP_SUMMARY` echo (no token required)

### LTC sync (Phase C1)
- SMPTE LTC biphase decoder (25fps synthesis verified)
- `LtcChase` audio→ring→control-thread consumer
- `SpscRing<T,N>` reusable template + `QueuedCmd` POD
- `NullBackend` audio-input path with NDEBUG strip lint hook
- `/sys/ltc_chase` opcode `0x14` + `SpatialEngine` integration

### Phase B feature parity
- Per-object Source WIDTH (0..π rad) across VBAP / DBAP / WFS / HOA
  fan-out
- HOA `AmbiDecoder` + `AmbisonicRenderer` 1st-order + algorithm dispatch
- `IRConvReverb` (OLA) + `/reverb/select` OSC + runtime FDN/IR switch
- Snapshot Crossfade — time-based scene transition interpolation
- `SPATIAL_ENGINE_VST3` build option (default OFF)
- Per-speaker time-alignment (delay_ms / gain_db) at output stage
- Object Trajectory Animation (circle / line / lissajous) + WebGUI API
- Per-channel `ChannelLimiter` + `/output/{ch}/{gain,limit}` OSC
- `IRConvReverb` WAV loading + `scripts/fetch_ir.py`
- DBAP width precision — 3-virtual-source power-sum + energy-preserving
  normalization
- HOA 2nd/3rd-order decoder — Tikhonov pseudo-inverse +
  `/sys/ambi_order`

### vid2spatial integration
- Phase 2 production bridge + dual-mode switch
- `bridge/spike_vid2spatial_osc.py` IIR + 60 Hz rate-limit
- WebGUI vid2spatial integration: bridge aed fix + start/stop API +
  UI buttons

### Korean documentation
- `docs/manual_kr/install/README.md` — 12-chapter installation manual
- `docs/manual_kr/operation/README.md` — 15-chapter operation manual

### Phase C4 design contract drafts (v0.3.0 implementation target)
- ADR 0010 — VST3 plugin OSC binding model (per-instance recv-only UDP,
  A1-ε)
- ADR 0011 — VST3 plugin multi-instance discovery (file-based JSON
  registry)
- ADR 0012 — ADM-OSC vendor quirks overlay (reserved slot)

### Test infrastructure
- VBAP gain cache (0.5° bins, open-addressing FIFO,
  prepareToPlay invalidation)
- AmbisonicEncoder 2nd/3rd-order (ACN/SN3D, 9ch / 16ch closed-form)
- VBAP 3D fallback gain pattern + numerical tests

---

## Changed

- **GHA OFF baseline**: re-pinned to `ubuntu-24.04` runner image
  (three re-pin cycles after M2 / C3 / HOA-extended landings)
- **`--osc-dialect` flag**: new, defaulting to `legacy` for v0.1.0
  wire compatibility
- **CI workflow**: `vst3.yml` split into two jobs —
  `vst3-build-and-host-fixture` and `off-byte-identical`
- **WebGUI**: FastAPI lifespan migration + `asyncio.run` switch
- **Limiter**: peak-attack envelope + gain-ramp warmup; assertion
  enforced

---

## Deprecated

(none in v0.2.0)

---

## Removed

(none in v0.2.0 — internal deslop pass dead-code removals tracked
under §Fixed)

---

## Fixed

- **AmbisonicEncoder ACN4 coefficient**: `kSqrt3 → kSqrt3_2` (2× error
  corrected); test13 regression added
- **OlaConvolver alloc-free `process()`** + `SofaBinReader`
  defensive validation
- **WebGUI + bridge 5-bug cluster** discovered in pre-handoff
- **NaN Z assert** + `blockSignals` on `set_object` + `sys` import
  hoisted
- **SceneController** handler + `fromJson` safety + path-traversal
  guard
- **MIDI OSC send** + VBAP 2D-constraint documentation
- **pytest collection**: `importlib` mode + `norecursedirs` +
  pythonpath/testpaths expansion
- **sofa_inspector IR_len 384** + flaky latency test + `ADDR_METRICS`
  + `utcnow` deprecation

---

## Security

- **Path-traversal guard** (`d8056db`): `SceneController` `fromJson`
  rejects malicious filename payloads.
- **NaN Z assert** (`a2b10f9`): defensive validation strengthened on
  `SceneSnapshot` / Z-coordinate path — fail-fast on corrupted wire
  input.

---

## Compatibility

### Build environment (CI baseline)
- **Ubuntu**: 24.04 (Noble Numbat)
- **GLIBC**: 2.39
- **GCC**: 13.3.0
- **CMake**: 3.20+
- **C++ standard**: C++20

### Runtime ABI
- `--osc-port` / `--osc-dialect` default values preserve v0.1.0
  behaviour
- Component / Controller IIDs unchanged
- **VST3 state format**: v0.1.0 = state v1 (28 bytes / 6 floats).
  v0.2.0 = state v2 (36 bytes / 7 floats, added in C2B postmortem
  `acb8c27`). Multi-version reader at `Processor.cpp:267-289` ensures
  v0.1.0 `.vstpreset` files load automatically via the v1 path. No
  further state bump in v0.2.0.
- **IPC `schema_version`**: 1 (unchanged)

### Older distributions
- **Ubuntu 22.04 / Debian 11 / RHEL 9**: GLIBC mismatch
  (2.35 / 2.31 / 2.34 < 2.39). Prebuilt `.so` will not load — **build
  from source**.
- Build guide: `docs/manual_kr/install/README.md` Chapter 3
  (English translation tracked for v0.3.0).

### Prebuilt asset naming convention
- `spatial_engine_v0.2.0_linux_glibc239.tar.gz` — GLIBC version
  explicit in filename so first customers see ABI before download.

---

## Known limitations

- **VST3 plugin ADM-OSC routing**: deferred to Phase C4 / v0.3.0
  (see Plan §1.4 deliverable matrix). v0.2.0 ships design-contract
  drafts only (ADR 0010 / 0011 / 0012).
- **macOS / Windows builds**: not yet covered by CI; no prebuilt
  binaries. Candidates for v0.3.0+.
- **DAW hands-on verification**: Reaper 7.x + Bitwig Studio 5.x
  on Linux only.
- **ADM-OSC vendor captures**: currently synthetic fixtures only;
  first real captures scheduled within 60 days of first customer
  contract.

---

## Install

Full install guide:
[`docs/manual_kr/install/README.md`](../../manual_kr/install/README.md)
(English translation planned for v0.3.0).

Quick start:
```bash
# 1. Source build (or download prebuilt asset)
git clone https://github.com/paiiek/spatial_engine.git
cd spatial_engine && git checkout v0.2.0
cmake -B core/build -DSPATIAL_ENGINE_NO_JUCE=ON -S core
cmake --build core/build -j$(nproc)

# 2. Verify
cd core/build && ctest --output-on-failure

# 3. Run
./core/spatial_engine_core --version
# expected: 0.2.0
```

---

## What's next

- [`docs/release/v0.2.0/CHANGES.md`](CHANGES.md) — complete 88-commit
  inventory
- [`.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md`](../../../.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md)
  — Phase C4 v0.3.0 sidecar plan
- v0.3.0 roadmap: VST3 plugin native ADM-OSC receive
  (ADR 0010 / 0011 implementation), state v3 + `kMute` 8th parameter,
  60-day vendor fixture refresh

---

## Acknowledgements

This release was planned and verified through the RALPLAN autonomous
workflow (Planner / Architect / Critic consensus). The frozen consensus
artefacts live in commit `0282f6b`.
