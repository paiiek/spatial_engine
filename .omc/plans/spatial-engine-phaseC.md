# Plan: Spatial Engine Phase C — Production Integration (v3)

**Status:** APPROVED via ralplan consensus (Planner v1 → Architect ITERATE → Planner v2 → Critic ITERATE → Planner v3 → Critic APPROVE, 2026-05-04). Fixed opcode collision (`0x60` → `0x14`), reconciled pytest baseline command, added CommandFifo POD extension sub-steps, committed CI venue to `.pre-commit-config.yaml`, quantified RoomEngine memory budget, defined Option-C fallback trigger, sharpened all hand-wavy gates, specified LTC skipif probe.

**Predecessors:**
- `.omc/plans/spatial-engine-v0.md` — P0~P12 foundation (commit 19679c6, v0.1.0)
- `.omc/plans/spatial-engine-phaseA-feature-parity.md` — M1/M2/M3/M6/M8/M9 feature parity
- `.omc/plans/spatial-engine-phaseB.md` — B1~B5/M4/M5/M7 precision & rendering quality (incl. `IRConvReverb::loadIRFromWav` runtime IR loading at `spatial-engine-phaseB.md:81-96`)

**Verified Status Snapshot (2026-05-04, commit 5a60720):**
- `ctest`: 43/43 PASS (Release build now genuinely enforces assertions — `add_compile_options(-UNDEBUG)` permanent in tests dir, NDEBUG strip bug fixed)
- `pytest`: **invocation = `python3 -m pytest`** from repo root (uses `pytest.ini` testpaths = `tests bridge/tests ui/tests ui/webgui/tests`). Result: 193 passed + 3 skipped. **Skip composition (actual)**: 1 SOFA (`tests/e2e/test_ir_loader.py:23`) + 2 MIDI hardware gates (`ui/tests/test_midi_bridge.py:73, :109`). **Note**: additional `pytest.skip` call sites exist at `tests/e2e/test_ir_loader.py:13,88,119`, `ui/tests/test_protocol_version.py:83`, `bridge/tests/test_accuracy.py:104,129,178` — these are conditional skips that did NOT fire in today's run (env capabilities present). **Skip count is runtime-dependent on env capabilities; CI baseline assumes headless Linux with `libsndfile` + `h5py` present, no JACK loopback, no `rtmidi` backend.**
- Latency harness: dry-run p99 = 4.68 ms, soak p99 = 650 µs, 0 xruns, 0 heartbeat miss
- Accuracy: VBAP 8ch max_az_err = 6.62°, DBAP 8ch = 2.00°, rE ≥ 0.56
- M1 source-width π/2 → 8 speakers, dominant 0.823; M9 limiter peak-attack rewrite exact; B4 DBAP energy = 1.000000

> ⚠ Phase C must preserve the post-5a60720 NDEBUG-enforced regime: every new test directory inherits `-UNDEBUG`, enforced by `scripts/check_test_ndebug.sh` wired into `.pre-commit-config.yaml` (Principle 4).

---

## 배경

Phase A delivered API/CLI/OSC parity, Phase B delivered numerical precision (HOA decoders, energy-conserving width, biphase LTC, snapshot crossfade, **runtime IR loading via `IRConvReverb::loadIRFromWav`**). Phase C is the **production-integration** phase — wiring the engine into hosts and real-world signal sources.

Phase B exit notes deferred to Phase C: VST3 functionality, real-time MIDI/LTC sync wiring, multi-room ABC routing.

Architect + Critic reviews surfaced six corrections versus the initial draft and eight versus v2:
1. Only **1** pytest skip is SOFA-related (not 3); 2 are intentional MIDI hardware gates.
2. **VST3 params and host comm cannot split** — `vst3/SpatialEngineVST3.cpp:27` returns nullptr from `GetPluginFactory()`; pluginval-clean acceptance requires factory + processor + automation together.
3. **Multi-room is bigger than first sized** — `core/src/core/SpatialEngine.h:93-110` shows `MAX_OBJECTS`-sized arrays with no room dimension.
4. **LTC live wiring is the highest unknown** — no audio-input path or SPSC ring exists.
5. **Opcode `0x60` is taken** by `ObjDsp` (`core/src/ipc/Command.h:64`) — reassigned `SysLtcChase = 0x14`.
6. **`CommandFifo::QueuedCmd` is POD-only** — must extend with new fields (no `std::string`, no heap) for both LTC and Room opcodes.
7. **CI venue confirmed** — repo has no `.github/workflows/`; hook goes into `.pre-commit-config.yaml`.
8. **RoomEngine memory cost is quantifiable** — ~2 MB worst case across 4 rooms.

---

## RALPLAN-DR Summary

### Principles (5)
1. **Hardware-free first.** Items needing DAW/audio interface/network gear that CI lacks must degrade to `pytest.mark.skipif` with explicit reason strings.
2. **Test gates are non-negotiable AND honestly counted.** Every item ships at least one assertion-enforced ctest under `-UNDEBUG` and/or pytest. **Skip count policy:** skip count may grow only via reason-tagged hardware/env gates documented in this plan; counted as **"effective skips" in CI = 2 (MIDI hw) + 1 (LTC loopback, conditional) = 3 max under Phase C** (i.e., end-of-Phase-C skip count must be ≤ 3, with the 1 SOFA skip flipped to passing).
3. **Single-responsibility commits, with documented exceptions.** Each milestone is normally one commit. **Exception**: C2 (VST3 MVP) is a multi-commit milestone reaching green together because pluginval requires factory+params+automation as a prerequisite chain.
4. **No NDEBUG strip regression.** Concrete enforcement: `scripts/check_test_ndebug.sh` greps every `core/tests/CMakeLists.txt` (and any `add_subdirectory(tests*)` target) for `-UNDEBUG`; missing → exit 1. **Wired into `.pre-commit-config.yaml` as a local hook** (repo has no `.github/workflows/` today; hook venue confirmed).
5. **ADR continuity.** Phase B already shipped runtime IR loading (`IRConvReverb::loadIRFromWav`, `spatial-engine-phaseB.md:81-96`). Phase C does NOT re-implement runtime SOFA loading in C++; it ships a **Python-only offline SOFA→bin pipeline** that produces files the existing loader consumes. C++ runtime SOFA is explicitly Phase D.

### Decision Drivers (top 3)
1. **Surface unknowns early** — LTC live wiring requires a brand-new audio-input path and the first SPSC ring in this repo. Doing it last means a stall blocks the whole phase.
2. **Production usability without scope creep** — VST3 plugin + multi-room are user-facing must-haves; SOFA C++ runtime is a "nice-to-have" Phase B's existing loader already covers if fed bin files.
3. **Test depth without hardware** — every ctest assertion-enforced; pytest skips truthful and reason-tagged.

### Viable Options

#### Option A — VST3-first (initial draft, retained for record)
Sequence: SOFA → VST3-params → VST3-comm → LTC → multi-room. **Rejected**: defers highest-risk to last; over-invests in C++ runtime SOFA; C2/C3 split cannot reach green independently.

#### Option B — Sync-first (Architect-recommended) ✓ adopted
Sequence: **LTC → VST3 MVP → SOFA offline → Multi-room scaffold**. Front-loads highest-unknown integration; fuses VST3 into a single test-able MVP; right-sizes SOFA to 1d Python-only; honest multi-room sizing with RoomId refactor pre-step.

#### Option C — Drop multi-room from Phase C (held as fallback)
Sequence: LTC → VST3 MVP → SOFA offline → (C4 → Phase D as D8). **Fallback decision gate (added per Critic feedback):**
> "Fallback to Option C (drop C4 multi-room) activates IF: **(a)** C1 unresolved by D+7 (i.e., LTC sub-step C1.c still red after 7 working days from Phase C kickoff), OR **(b)** C2 pluginval failures still red at D+12. In either case, C4 rolls cleanly into Phase D as a new **D8** entry, and Phase C closes with C1 + C2 + C3 only."

---

## Per-Item Specification (Option B)

### C1 — Real-Time LTC Live Wiring (front-loaded, 4–5d)
- **Why first:** Highest unknown. `core/src/io/audio_io.cpp` has no input path; no SPSC ring exists.
- **Sub-step commits (each green individually):**
  - **C1.a** — `audio_io` input path: extend `AudioIO` to support input channels alongside output. **Test gate:** silence input → 0.0 RMS (tolerance < 1e-9), 1 kHz sine input @ 0 dBFS → expected RMS within 0.5 dB. **C1.a commit also adds `scripts/check_test_ndebug.sh` + `.pre-commit-config.yaml` hook entry** (Principle 4 enforcement).
  - **C1.b** — SPSC ring API + pre-allocation contract + **CommandFifo POD extension**:
    - New: `core/include/spatial_engine/spsc_ring.hpp` template, fixed capacity, no allocations after construction.
    - Extend: `core/src/util/CommandFifo.h::QueuedCmd` POD with LTC fields (e.g., `int32_t ltc_chase_enable`). **No `std::string`, no heap.** Verified via `RT_ASSERT_NO_ALLOC` macro in test.
    - **Test gate:** 1M push/pop roundtrip; capacity-1 boundary; capacity-N wrap; `RT_ASSERT_NO_ALLOC` reports 0 allocations during steady-state push/pop loop (override `operator new` in test, assert call count == 0).
  - **C1.c** — Control-rate LTC consumer: existing M7 biphase decoder consumes from ring, exposes `getCurrentTimecode()` to scene clock. **Test gate:** synthetic LTC fixture injected at audio rate (48 kHz, biphase pattern) → control-rate `getCurrentTimecode()` advances 1 frame per 1/25 s within ±1 frame over 1000 frames (40 s simulated).
  - **C1.d** — `/sys/ltc_chase {0|1}` OSC: enable/disable chase; transport sync follows decoder when enabled.
    - **Opcode: `SysLtcChase = 0x14`** (next free in /sys/* range after `SysAmbiOrder = 0x13`; **NOT** `0x60`, which is already `ObjDsp` per `core/src/ipc/Command.h:64`).
    - **Test gate:** pytest OSC roundtrip toggles flag; engine state reflects new value; LTC chase active flag observable via `/sys/status` query.
- **Files to touch:**
  - `core/src/io/audio_io.{hpp,cpp}` (input path)
  - `core/include/spatial_engine/spsc_ring.hpp` (new)
  - `core/src/util/CommandFifo.h` (extend `QueuedCmd` POD)
  - `core/include/spatial_engine/ltc_chase.hpp`, `core/src/ltc_chase.cpp` (new)
  - `core/src/ipc/Command.h` (declare `SysLtcChase = 0x14`)
  - `core/src/ipc/CommandDecoder.cpp` (decode 0x14, mirror `/output/{ch}/...` multiplexed pattern at `:299-311`)
  - `python/spatial_engine/osc_router.py` (`/sys/ltc_chase`)
  - `core/tests/test_audio_io_input.cpp`, `core/tests/test_spsc_ring.cpp`, `core/tests/test_ltc_chase.cpp`, `python/test_p_ltc_chase.py`
  - `scripts/check_test_ndebug.sh` (new, in C1.a), `.pre-commit-config.yaml` (hook entry, in C1.a)
- **Quantified test gates:**
  - `ctest -R "audio_io_input|spsc_ring|ltc_chase"` PASS
  - SPSC: `RT_ASSERT_NO_ALLOC` reports 0 allocations on producer/consumer hot path
  - LTC sync: ≤ 1 frame drift over 1000 LTC frames; **ring xruns == 0 over 10 s @ 48 kHz, block size 64, `NullBackend` audio thread, synthetic LTC fed from test fixture**
  - **LTC pytest skipif probe:** `os.environ.get("SPE_LTC_LOOPBACK") == "1"` enables; absent → skip with reason `"requires JACK virtual loopback (set SPE_LTC_LOOPBACK=1 to enable)"`. **Counts toward effective-skip cap (Principle 2).**
  - Latency harness p99 unchanged (≤ 5 ms)
- **Effort:** 4–5d.

### C2 — VST3 MVP (fused params + host comm + automation, 6–7d)
- **Why fused:** `vst3/SpatialEngineVST3.cpp:27` returns nullptr from `GetPluginFactory()`. pluginval-clean requires real factory + processor + parameterChanged firing — splitting "params" from "host comm" yields red intermediate states. Documented exception to Principle 3.
- **Multi-commit breakdown (all-green at milestone end, not after each commit):**
  - **C2.a** — Real `GetPluginFactory()` + processor skeleton + `AudioProcessorValueTreeState` with 6 params (pan-az, pan-el, source-width, master-gain, ambi-order, room-preset-idx)
  - **C2.b** — `processBlock` thread-safety audit + atomic param read; `setLatencySamples` reports correct value
  - **C2.c** — `parameterChanged` host notification; bypass behavior
  - **C2.d** — `getStateInformation` / `setStateInformation` save/restore
- **Files to touch:**
  - `core/vst3/SpatialEngineVST3.cpp` (replace nullptr factory)
  - `core/vst3/SpatialEngineProcessor.{hpp,cpp}` (new)
  - `core/vst3/SpatialEngineEditor.cpp` (JUCE generic UI; full custom editor → Phase D)
  - `core/CMakeLists.txt` (`SPATIAL_ENGINE_VST3=ON` actually links VST3 SDK via JUCE; OFF default unaffected)
  - `core/tests/test_vst3_param_roundtrip.cpp`, `core/tests/test_vst3_automation_offline.cpp`
- **Quantified test gates (all required for milestone-green):**
  - **Param-roundtrip:** normalized [0, 1] roundtrip with tolerance ≤ 1e-6 (single-precision float quantization through VST3); **16 sample points per param × 6 params = 96 assertions**.
  - **Automation-offline:** azimuth normalized 0 → 1 ramp over 48000 samples (1 s @ 48 kHz), sampled at frames **0 / 12000 / 24000 / 36000 / 47999**; output channel weights match VBAP analytic reference **within 1 % per channel**.
  - `pluginval --strictness 5` clean exit on Linux
  - **OFF-path:** `SPATIAL_ENGINE_VST3=OFF` build artifact byte-identical to current main
  - Latency harness p99 ≤ 4.68 ms baseline
- **Effort:** 6–7d.

### C3 — SOFA → bin Offline Pipeline (Python-only, 1d)
- **Why narrow:** Phase B already shipped `IRConvReverb::loadIRFromWav` (`spatial-engine-phaseB.md:81-96`). Repo has `tools/ir_sofa_loader.py` (h5py) and `tools/sofa_to_bin.py`. Closing the loop costs ~1d. Flips the **1** SOFA-related skip (`tests/e2e/test_ir_loader.py:23`); the 2 MIDI skips at `ui/tests/test_midi_bridge.py:73, :109` are hardware gates and stay skipped.
- **Scope:** Verify and harden the existing Python pipeline so a SOFA file → `.bin`/`.wav` is consumable by the existing C++ loader. Add a checked-in 5 KB synthetic SOFA fixture for CI. C++ runtime SOFA loader is explicitly Phase D.
- **Files to touch:**
  - `tools/ir_sofa_loader.py` (harden if needed; document CLI)
  - `tools/sofa_to_bin.py` (verify output matches `IRConvReverb::loadIRFromWav` byte layout)
  - `tests/e2e/test_ir_loader.py` (un-skip the SOFA path; add fixture)
  - `tests/fixtures/synthetic_min.sofa` (new, ~5 KB)
- **Test gates:**
  - `pytest tests/e2e/test_ir_loader.py` skip → pass (overall: 193 + 1 = **194 passed**, **2 mandatory MIDI skips** remain reason-tagged)
  - `tools/sofa_to_bin.py fixture.sofa out.bin` produces a file `IRConvReverb::loadIRFromWav` round-trips (existing ctest re-pointed at output)
- **Effort:** 1d.

### C4 — Multi-Room ABC Routing (with RoomEngine refactor pre-step, 6–8d)
- **Why bigger than first sized:** `core/src/core/SpatialEngine.h:93-110` shows `obj_cache_`, `osc_phases_`, `dry_scratch_`, `dry_ptrs_` all sized `MAX_OBJECTS` with NO room dimension. SceneManager and Command stream are also single-instance.
- **Memory budget (quantified):**
  - 4 × `sizeof(RoomEngine)` static cost.
  - `dry_scratch_` alone: `MAX_OBJECTS × MAX_BLOCK × sizeof(float) = 64 × 512 × 4 B = 128 KB` per room → **512 KB across 4 rooms**.
  - Plus `WFSRenderer::delays_`, `obj_cache_`, `osc_phases_`, per-room `chains_`. **Total worst case ~2 MB** (cold static allocation; not on audio thread stack).
  - **Constraint:** arrays remain heap-allocated via existing `chains_` pattern (heap `unique_ptr` / `vector` owned by `RoomEngine`); **NOT promoted to stack** (would blow audio-thread stack). RoomEngine constructor pre-allocates everything in `prepareToPlay`; **never allocates on audio thread** (existing invariant preserved).
- **Sub-step commits:**
  - **C4.0** — RoomEngine extraction (own ADR section): move per-room state from `SpatialEngine` into `RoomEngine`; outer `SpatialEngine` owns `std::array<std::unique_ptr<RoomEngine>, MAX_ROOMS>` (MAX_ROOMS = 4). Single-room behavior preserved (room 0 default). **Test gate:** existing 1-room ctest baseline unchanged; new 2-room round-trip ctest inserts then queries each room independently.
  - **C4.a** — Per-room speaker layout: each RoomEngine has independent `SpeakerLayout`. **Test gate:** load 2 different layouts into rooms 0 and 1; verify channel routing differs (channel count and per-speaker gain checked).
  - **C4.b** — Per-room scene/snapshot state: each RoomEngine has independent SceneManager. **Test gate (operational definition):** "an `/obj/move` command targeting room N affects only `RoomEngine[N].obj_cache_[obj_id]`; query of `RoomEngine[M]`, M ≠ N, returns the value from before that command." Implemented as ctest with explicit before/after snapshots of both rooms' obj_cache.
  - **C4.c** — `/room/*` OSC schema + **CommandFifo POD extension**:
    - **Confirmed-free opcodes** (verified against `Command.h`): `RoomSelect = 0x70`, `RoomLayoutLoad = 0x71`, `RoomSnapshotStore = 0x72`, `RoomSnapshotRecall = 0x73`. None collide.
    - **CommandFifo extension:** extend `QueuedCmd` POD with Room fields: `uint8_t room_id`, `uint16_t layout_path_handle`, `uint16_t snapshot_idx`. **Layout paths must be pre-loaded to indexed handles before queueing** (handle table populated at control-rate, lookup by index on audio thread); **NO `std::string` in QueuedCmd**.
    - **Test gate:** `RT_ASSERT_NO_ALLOC` over 1000 queued room commands; pytest roundtrip per opcode.
  - **C4.d** — Click-free room switch: switchover preserves last-sample continuity. **Test gate:** switch room mid-render at sample boundary k; assert `|sample[k] − sample[k−1]| ≤ 1e-3` per channel; **assertion-enforced 0 audio-thread allocations during room switch** (via `RT_ASSERT_NO_ALLOC`).
- **Sequential render only:** one room renders per `process()` call (selected room). Concurrent multi-room mixdown → Phase D (D5).
- **Files to touch:**
  - `core/src/core/RoomEngine.{h,cpp}` (new — extracted from SpatialEngine)
  - `core/src/core/SpatialEngine.{h,cpp}` (refactor to own array of RoomEngines)
  - `core/src/util/CommandFifo.h` (extend `QueuedCmd` with Room fields)
  - `core/src/ipc/Command.h` (add 0x70–0x73 opcodes)
  - `core/src/ipc/CommandDecoder.cpp` (room demux, mirroring output-channel demux at `:299-311`)
  - `python/spatial_engine/osc_router.py` (`/room/*` namespace)
  - `core/tests/test_room_engine_extract.cpp`, `core/tests/test_room_routing.cpp`, `core/tests/test_room_switch_clickfree.cpp`, `python/test_p_room_osc.py`
- **Test gates summary:**
  - `ctest -R "room_engine_extract|room_routing|room_switch_clickfree"` PASS
  - 2-room round-trip; click-free switch (max |Δsample| ≤ 1e-3); 0 audio-thread allocations on switch
  - `pytest -k room_osc` PASS
  - Counts: ctest 43 → ≈51, pytest 194 → ≈198 (after C3's +1)
- **Effort:** 6–8d.

### Phase D (explicitly deferred — for ADR continuity)
- **D1**: C++ runtime SOFA loader (libmysofa)
- **D2**: Real-IR measurement integration with hardware capture
- **D3**: Dante / AES67 real send/receive (requires Dante VSC or NIC)
- **D4**: WebGUI scene visualization enhancement
- **D5**: Concurrent multi-room mixdown rendering (C4 ships sequential-per-process only)
- **D6**: Full custom VST3 editor UI (C2 ships JUCE generic UI only)
- **D7**: MIDI bridge real-hardware tests (un-skip `ui/tests/test_midi_bridge.py:73, :109`)
- **D8** *(new placeholder, activated only if Option-C fallback triggers)*: Multi-room ABC routing rolled over from C4

---

## Execution Order

```
C1  LTC live wiring          (4-5d)  ← FRONT-LOADED: highest unknown
       │
       │ (audio-input path + SPSC ring + CommandFifo POD pattern → reusable infra)
       ▼
C2  VST3 MVP fused           (6-7d)  ← params + host comm + pluginval clean as one milestone
       │
       │ (independent of C3, C4)
       ▼
C3  SOFA→bin offline (Py)    (1d)    ← flips 1 SOFA skip; reuses Phase B IR loader
       │
       ▼
C4  Multi-room ABC scaffold  (6-8d)  ← RoomEngine refactor + per-room state + /room/* OSC
                                       (concurrent mixdown → D5)

  Fallback gate: if C1.c red at D+7 OR C2 pluginval red at D+12 → drop C4 → D8.
```

**Total estimated effort:** 17–21 person-days (≈ 4–5 weeks single-developer).

---

## Test Gates (after each item)

After each Cn milestone (C2/C4 multi-commit; gate runs at milestone end), the **mandatory regression gate**:
```bash
cd core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j$(nproc)
ctest --output-on-failure                    # 43+ΔN PASS, no failures
cd ../.. && python3 -m pytest                # passed grows by ΔN; effective skips ≤ 3 cap
python3 latency_harness/run_dryrun.py        # p99 ≤ 5 ms; xruns == 0
bash scripts/check_test_ndebug.sh            # Principle 4 enforcement (also fires via pre-commit)
```

**OFF-path acceptance row** (must pass at every milestone):
| Flag | OFF behavior |
|---|---|
| `SPATIAL_ENGINE_VST3=OFF` (default) | build artifact byte-identical to current main |
| `SPATIAL_ENGINE_NO_JUCE=ON` (CI default) | build green, 43+ΔN ctest PASS |

**`scripts/check_test_ndebug.sh`** — added in C1.a commit, hooked via `.pre-commit-config.yaml`:
```bash
#!/usr/bin/env bash
set -e
fail=0
for f in $(find core/tests -name CMakeLists.txt); do
  grep -q -- '-UNDEBUG' "$f" || { echo "MISSING -UNDEBUG: $f"; fail=1; }
done
exit $fail
```

`.pre-commit-config.yaml` entry (added C1.a):
```yaml
- repo: local
  hooks:
    - id: check-test-ndebug
      name: Verify -UNDEBUG in test CMakeLists
      entry: bash scripts/check_test_ndebug.sh
      language: system
      pass_filenames: false
```

**Effective-skip ceiling under Phase C (Principle 2):** ≤ 3 = 2 (MIDI hw) + 1 (LTC loopback, conditional on `SPE_LTC_LOOPBACK=1`). The 1 SOFA skip MUST flip to passing by C3 milestone end.

---

## ADR

**Decision:** Adopt Option B (Sync-first). Sequence: **C1 LTC → C2 VST3 MVP → C3 SOFA offline → C4 Multi-room**. Defer C++ runtime SOFA, real-IR measurement, Dante, WebGUI viz, concurrent multi-room mixdown, full custom VST3 editor, and MIDI hardware tests to Phase D. **Fallback** to Option C drops C4 to D8 if C1.c stalls at D+7 or C2 pluginval stalls at D+12.

**Drivers:**
1. Surface unknowns early (LTC = highest unknown).
2. Production usability without scope creep (VST3 MVP + multi-room are user-facing must-haves).
3. Honest test-gate accounting (only 1 SOFA skip exists; effective-skip cap = 3).

**Alternatives considered:**
- **Option A (VST3-first / initial draft Option C)** — rejected: defers highest-risk LTC to last; over-invests in C++ runtime SOFA that flips only 1 skip; C2/C3 split cannot reach green independently due to nullptr `GetPluginFactory()` at `vst3/SpatialEngineVST3.cpp:27`.
- **Option C (drop multi-room)** — held as fallback with explicit decision gate (D+7 / D+12).
- **Skip Phase C entirely, jump to D** — rejected: Phase B exit notes commit Phase C scope; reordering breaks ADR chain.

**Why chosen:**
Option B front-loads the highest-unknown integration (audio-input + SPSC ring + LTC chase) so any stall is found in week 1 rather than week 4 after 11+ days of sunk cost. It fuses VST3 work into a single MVP milestone that can actually pass `pluginval` (the alternative — separate "params" and "host comm" milestones — yields red intermediate states because the factory must exist before any param test can load the plugin). It right-sizes SOFA to a 1d Python-only pipeline that reuses Phase B's existing `IRConvReverb::loadIRFromWav` (`spatial-engine-phaseB.md:81-96`) rather than re-implementing runtime SOFA in C++. And it honestly sizes multi-room at 6–8d with a RoomEngine refactor pre-step, instead of papering over the `MAX_OBJECTS`-sized fixed arrays at `core/src/core/SpatialEngine.h:93-110`. Critic-flagged collisions and POD constraints are resolved: `SysLtcChase = 0x14` (not `0x60`); `QueuedCmd` extended with POD-only fields for both LTC and Room opcodes; layout paths use pre-loaded indexed handles (no `std::string` on audio thread).

**Consequences:**
- Phase C ships: VST3 plugin clean under `pluginval`; LTC chase from live audio input; 4-room sequential routing; 1 fewer SOFA skip.
- ctest count grows from 43 → ≈51; pytest passed grows from 193 → ≈198; effective-skip count moves from 3 (today: 1 SOFA + 2 MIDI) to 2 or 3 (2 MIDI + 1 conditional LTC if `SPE_LTC_LOOPBACK` unset; 1 SOFA flipped to passing).
- `scripts/check_test_ndebug.sh` + `.pre-commit-config.yaml` hook permanently prevent NDEBUG-strip regression (5a60720 bug class).
- C2 multi-commit milestone documented as exception to single-responsibility commits; future milestones revert to default.
- `/room/*` OSC opcodes (0x70–0x73) verified collision-free; `SysLtcChase = 0x14` corrects v2's `0x60` opcode collision with `ObjDsp`.
- `QueuedCmd` POD extension establishes the pattern for future audio-thread message types: pre-loaded indexed handles, never strings.
- C++ runtime SOFA, concurrent multi-room rendering, full custom VST3 editor, Dante real send, real-IR measurement, WebGUI viz, MIDI hardware tests honestly deferred to Phase D rather than half-implemented.

**Follow-ups (Phase D candidates):**
- D1: C++ runtime SOFA loader (libmysofa)
- D2: Real-IR measurement integration with hardware capture
- D3: Dante/AES67 real send/receive (requires Dante VSC or NIC)
- D4: WebGUI scene visualization enhancement
- D5: Concurrent multi-room mixdown rendering
- D6: Full custom VST3 editor UI
- D7: MIDI bridge real-hardware tests (un-skip `ui/tests/test_midi_bridge.py:73, :109`)
- D8 *(conditional)*: Multi-room ABC routing rollover, activated only if Option-C fallback triggers

---

## Success Criteria Checklist

- [ ] **C1.a** — audio_io input path: ctest PASS; silence → 0 RMS (< 1e-9), 1 kHz sine @ 0 dBFS → expected RMS within 0.5 dB. Commit also adds `scripts/check_test_ndebug.sh` + `.pre-commit-config.yaml` hook entry.
- [ ] **C1.b** — SPSC ring + `QueuedCmd` POD extension: 1M push/pop roundtrip PASS; `RT_ASSERT_NO_ALLOC` reports 0 allocations on hot path
- [ ] **C1.c** — LTC consumer: ≤ 1 frame drift over 1000 LTC frames; ring xruns == 0 over 10 s @ 48 kHz, block 64, NullBackend, synthetic LTC fixture
- [ ] **C1.d** — `/sys/ltc_chase` OSC opcode `0x14` (NOT `0x60`); pytest roundtrip PASS or skipif with reason `"requires JACK virtual loopback (set SPE_LTC_LOOPBACK=1 to enable)"`
- [ ] **C2 milestone (fused)** — `pluginval --strictness 5` clean; 96 param-roundtrip assertions PASS (≤ 1e-6 tol); automation 5-frame VBAP within 1 % per channel; `SPATIAL_ENGINE_VST3=OFF` artifact byte-identical
- [ ] **C3** — SOFA offline: `tests/e2e/test_ir_loader.py:23` flips skip → pass; total pytest 194 passed; effective skips ≤ 3 (2 MIDI + ≤ 1 conditional LTC)
- [ ] **C4.0** — RoomEngine extraction: 1-room ctest baseline unchanged; 2-room round-trip ctest PASS; ~2 MB worst-case static budget verified (heap-allocated, never on audio-thread stack)
- [ ] **C4.a/b** — per-room layout + scene-state isolation: operational gate (`/obj/move` to room N leaves RoomEngine[M], M≠N, `obj_cache_` unchanged) PASS
- [ ] **C4.c** — `/room/*` opcodes 0x70–0x73 wired (collision-free verified); `QueuedCmd` extended with POD-only Room fields + indexed layout-path handles; `RT_ASSERT_NO_ALLOC` over 1000 room commands PASS; pytest PASS
- [ ] **C4.d** — click-free room switch: max |Δsample| ≤ 1e-3 per channel; 0 audio-thread allocations on switch
- [ ] After every milestone: `ctest && pytest && latency_harness && scripts/check_test_ndebug.sh` ALL PASS
- [ ] `scripts/check_test_ndebug.sh` + `.pre-commit-config.yaml` hook live and firing on every commit (verified C1.a end)
- [ ] All commits use `feat(Cn[.x]):` prefix; C2 fusion documented in milestone-end commit message as Principle-3 exception
- [ ] Latency harness dry-run p99 ≤ 5 ms; xruns == 0 at every milestone
- [ ] Effective-skip count under Phase C ≤ 3 throughout (Principle 2)
- [ ] No `v*` tag created without explicit user request
- [ ] Fallback gate (Option C → D8) consulted at D+7 (C1.c) and D+12 (C2 pluginval); decision recorded in commit message if invoked
- [ ] Phase D follow-ups (D1–D7, plus conditional D8) documented in Phase C exit notes for next-phase ADR continuity
- [ ] ralplan→autopilot policy honored: each Cn ran through Architect/Critic consensus before implementation
