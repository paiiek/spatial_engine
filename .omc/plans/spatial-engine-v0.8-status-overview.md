# Spatial Engine — Status Overview (2026-05-28)

> Snapshot at HEAD `98741a4` (main, v0.7.0+7).
> Companion to `.omc/plans/spatial-engine-v0.8-audit-remediation.md`.
> Generated to give future sessions a single "engine map" without re-surveying the tree.

---

## 1. Build & Run Entrypoints

### Build directories

| Dir | Flags | Purpose | Status |
|---|---|---|---|
| `core/build` | `SPATIAL_ENGINE_NO_JUCE=ON` | Default CI gate (JUCE-free core) | ✅ active, 98 ctest |
| `core/build_rton` | `+ SPATIAL_ENGINE_RT_ASSERTS=ON` | RT-no-alloc sentinel (catch audio-thread malloc) | ✅ active, 98 ctest |
| `core/build_vst3` | `SPATIAL_ENGINE_VST3=ON` | VST3 plugin + 29 state/param tests | ⚠️ stale (~22 days), not in CI |
| `core/build_rel` | Release optimization | Release builds | archived |
| `core/build_relacy*` | `+ SPATIAL_ENGINE_BUILD_RELACY_TESTS=ON` | Relacy synthetic race detection (decoder-type swap, OSC ring) | ✅ active |

### Primary artifacts

- `core/build/spatial_engine_core` — headless C++ engine binary (Null/Dante backend, OSC 9100/9101).
- `core/build_vst3/vst3/SpatialEngine.vst3/` — DAW plugin bundle (manual build).
- ctest + pytest binaries.

---

## 2. Shipped feature inventory

### DSP rendering

| Feature | Location | Tests | Status |
|---|---|---|---|
| VBAP 2D/3D | `core/src/render/VBAPRenderer.cpp`, `AlgorithmAnalyticReference.cpp` | `test_p_vbap*`, `test_p_vbap3d*` | ✅ v0.0; v0.8 P1.3 removed RT-alloc |
| DBAP | `core/src/render/DBAPRenderer.cpp` | `test_p_dbap*` | ✅ v0.0 |
| WFS | `core/src/render/WFSRenderer.cpp` | `test_p_wfs*` | ✅ v0.0 |
| Ambisonic encoder (SN3D, AmbiX) | `core/src/ambi/AmbisonicEncoder.cpp` | `test_p_ambi.cpp:214-241` + v0.8 P1.2 independent SN3D oracle | ✅ v0.1c |
| Ambisonic decoders ×5 (Sampling/MaxRE/EPAD/AllRAD/InPhase) | `core/src/ambi/*Decoder.cpp` | `test_p_ambi_decoder*` (12 ctest) | ✅ v0.3–v0.5; v0.8 P1.1 runtime swap = lock-free double-buffer (commit `64352df`) |
| EPAD rank-aware energy scale | `core/src/ambi/EPADDecoder.cpp:226` | new `test_epad_rank_aware*` (v0.8 P2.1, commit `98741a4`) | ✅ fixed |
| HRTF Binaural (KEMAR SOFA, B1/B2 modes) | `core/src/hrtf/` | `test_p_binaural*` (11 ctest) | ✅ v0.5; interpolation test missing (v0.8 P3.4) |
| FDN Reverb (16-line Hadamard) | `core/src/reverb/FdnReverb.cpp` | `test_p7_fdn_*` | ✅ v0.1; T60 accuracy missing (v0.8 P3.3) |
| EQ / Limiter / DRC | — | — | ❌ not implemented |

### Input/Output backends (`core/src/audio_io/`)

| Backend | Status |
|---|---|
| `NullBackend` | ✅ test/headless (silence input) |
| `DanteBackend` | ✅ JUCE=ON build only (real-time PCIe) |
| `SharedRingBackend` (ADR 0019 PR2–3) | ✅ v0.7; `--input-backend shm:/NAME` |
| `SharedMemoryRegion` (ADR 0019 PR1, v0.8 P6.1) | ✅ `O_NOFOLLOW` symlink guard (commit `1aebd43`) |

### Control / IPC

- **OSC dual dialect**: legacy `/obj/{id}/aed` + ADM-OSC v1.0 `/adm/obj/{id}/aed` — `core/src/ipc/CommandDecoder.cpp:179+`
- **Heartbeat / `/sys/state` 1Hz + event `/sys/warning`** — `OSCBackend.cpp:780-900`, `HeartbeatPublisher.cpp` (ADR 0018)
- **shm telemetry** (`/sys/warning shm_*`) — ADR 0019 PR4 (commit `996bc50`)
- **VST3 6 params** (aed_az/el/dist, gain, mute, reserved) + 2-bus output (multi-ch + binaural stereo) — `vst3/SpatialEngineProcessor.cpp` (ADR 0010, 0011)
- **WebGUI** FastAPI + canvas drag + trajectory synth — `ui/webgui/server.py`, `osc_bridge.py`

### Sidecar / external

- **adm_player** (sibling: `/home/seung/mmhoa/adm_player/`) — ADM BWF playback → `--sink ipc://NAME` (ADR 0019 PR5, commit `868f750`)
- **vid2spatial_osc bridge** — video tracker → OSC

---

## 3. Production data flows

```
[A] Standalone headless (production-ready):
   adm_player wav.bwf --sink ipc://NAME  →  /dev/shm/NAME  →  spatial_engine_core --input-backend shm:/NAME --wav out.wav
                                                                       ↑
                                                          OSC 9100 (optional: user sends /obj/N/aed)

[B] VST3 (in-DAW processing):
   DAW (Reaper/Logic/Bitwig) → VST3 plugin (audioProcessBlock) → shared spe_core .a
                                       ↑                            ↓
                                  6 params/automation         multi-ch bus + binaural bus
                                  OSC 9100 (per-instance recv)

[C] WebGUI (browser control):
   Browser localhost:8000 ── WebSocket ──► FastAPI ── OSC 9100 ──► spatial_engine_core
                                              ▲           ◄── 9101 ── /sys/state
                                              └── trajectory runner (auto-path)
```

Production-ready combinations: A, B, C each in isolation. B+C concurrent works but watch port 9100/9101 contention.

---

## 4. Testing surface (copy-paste ready)

### Automated tests
```bash
# C++ base (98/98)
cd /home/seung/mmhoa/spatial_engine/core/build && ctest --output-on-failure

# C++ RT-asserts (98/98, audio-thread malloc sentinel armed)
cd /home/seung/mmhoa/spatial_engine/core/build_rton && ctest --output-on-failure

# Python (225 pass, 4 skip)
cd /home/seung/mmhoa/spatial_engine && python3 -m pytest

# Relacy (1024 iter, synthetic race detection)
cd /home/seung/mmhoa/spatial_engine/core/build_relacy && ctest -R relacy
```

### Manual smoke (Standalone, canonical)
```bash
# Terminal 1 (producer)
cd /home/seung/mmhoa/adm_player/dreamscape
python3 -m adm_player ./01.wav --sink ipc://spe-smoke --block-size 256 --ring-frames 8192 --no-osc

# Terminal 2 (consumer, 10s)
cd /home/seung/mmhoa/spatial_engine/core/build
./spatial_engine_core \
  --input-backend shm:/spe-smoke --block 256 \
  --channels 8 --rate 48000 \
  --layout ../../configs/lab_8ch.yaml \
  --backend null --wav /tmp/engine_out.wav --seconds 10

# Terminal 3 (telemetry monitor)
socat - UDP-RECV:9101    # /sys/state at 1 Hz
```

### VST3 (DAW)
1. Rebuild `core/build_vst3` (~22 days stale) → `SpatialEngine.vst3/` bundle
2. Add to Reaper VST3 scan paths → Insert plugin
3. Automate 6 params, save/load session to verify state persist
4. Bus 1 (binaural) → headphone output

### WebGUI
```bash
# Terminal 1: engine
./core/build/spatial_engine_core --backend null --input-backend null

# Terminal 2: WebGUI server
PYTHONPATH=.:ui python3 -m uvicorn ui.webgui.server:app --port 8000

# Browser
http://localhost:8000   # canvas drag → /adm/obj/N/aed OSC
```

### One-liner health checks
```bash
ctest --test-dir core/build --output-on-failure | tail -1   # "100% passed, 98/98"
python3 -m pytest -q | tail -1                              # "225 passed, 4 skipped"
ls /dev/shm/ | grep spe                                     # shm ring created
ss -unlp | grep 9100                                        # engine OSC listening
```

---

## 5. Gap analysis (forward work)

### v0.8 audit remaining (`.omc/plans/spatial-engine-v0.8-audit-remediation.md`)

| Phase | Item | Priority |
|---|---|---|
| **P3.1** | VST3 state-contract test ported into NO_JUCE CI (29 tests live only in vst3/tests → setState regressions slip) | HIGH |
| P3.2 | Ambisonic absolute-gain golden vector | MED |
| P3.3 | FDN T60 accuracy test (impulse → −60 dB time, ±10%) | MED |
| P3.4 | HrtfLookup interpolation test (direction between measurements) | MED |
| P3.5 | `vst3_bind_collision` race fix (`getsockname` + RUN_SERIAL) | MED |
| P3.7 | OSC malformed-flood edge cases (truncated tag, unknown type, misaligned padding) | LOW-MED |
| P4.2 | `open-questions.md` reconcile (99 open; close ADM-OSC C3-Q* + M2HOA-Q14 cohort) | MED |
| P6.2 | Python dep advisories (starlette/urllib3/idna/pytest, PIN-DEFER per plan) | LOW |
| **P7.1** | `SpatialEngine` god-object refactor (442-line header, ~30 binaural forwarders) | **DEFERRED** — supervised sprint required |

### ADR/docs cleanup
- ADR 0007–0009 numbering gap (0006a→0007 rename deferred)
- ADR 0012 ADM-OSC vendor quirks — 60-day field-capture logistics pending
- `CHANGELOG [Unreleased]` accumulates 14+ commits (ADR 0018 Phase B + ADR 0019 PR2–5 + v0.8 P0–P2)

### Missing features (clearly future work)

| Feature | Note |
|---|---|
| **WAV file direct input backend** | `core/src/input/FileInput.cpp` exists but no CLI `--input-backend file:<wav>` selector. Required for single-process standalone testing without adm_player. |
| **EQ / Limiter / DRC** | DSP chain ends at Ambi/HRTF |
| **Real-time metrics dashboard** | Telemetry channels exist; UI visualization missing |
| **Per-bus / per-object decoder selection** | Currently global `/sys/ambi_decoder_type`; per-object = M2HOA-Q12 open |
| **MAX_ORDER > 3 (5th–7th order)** | Currently 1–3 order; M2HOA-Q10 open |
| **Non-KEMAR HRTF datasets** (CIPIC, RIEC, HUTUBS) | Loader is generic SOFA; only KEMAR shipped |
| **MPEG-H IO** | License pending |
| **ADR 0019 PR6 (60s cross-process soak)** + **PR7 (release)** | Close out shm IPC track |
| **Room acoustics simulation** (MultiVerse-style) | Backlog |

### Key open questions (`.omc/plans/open-questions.md`)
- V07-Q1 telemetry cadence (event vs 1Hz vs pre-demote window)
- V07-Q2 cooldown duration (60s appropriate?)
- v03-Q4 ADM-OSC vendor quirks (60-day lab session)
- v03-Q7 macOS/Windows port activation criterion (A5-α → A5-β fallback)

---

## 6. One-line summary

**At v0.7.0+7 we have three production paths working (standalone adm_player→shm→engine, VST3, WebGUI). v0.8 audit P0–P2 zeroed DSP defects (commits `32bfd5a`, `64352df`, `98741a4`). Remaining: (a) P3 test-harness hardening (especially P3.1 VST3 state in CI), (b) P4.2 open-questions reconcile, (c) P7 god-object refactor (supervised), (d) outside-of-code roadmap — WAV-direct input / EQ / real-time metrics UI / 5+ order Ambisonics / HRTF dataset diversity — for v0.9–v1.x.**
