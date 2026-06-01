# Spatial Engine v0.9 — Lane C: MAX_OBJECTS 64 → 128 — REV 2 (consensus: Architect SOUND WITH AMENDMENTS + Critic APPROVE)

> Consensus (ralplan) draft. Lanes A & B & E complete. Outline source:
> `.omc/plans/spatial-engine-v0.9-feature-extension.md` §레인 C (MAX_OBJECTS 확장).
> This plan grounds C-M1..C-M7 in the actual code at HEAD `4539873`.
>
> **REV 2** applies the Architect "SOUND WITH AMENDMENTS" review. Option A and the
> four-cap finding were CONFIRMED. REV 2: (B1) fixes a NEW BLOCKER — existing tests
> hard-pin 64 (`static_assert`, `obj_id==64` rejection) so the 128 build does not
> compile/pass until guarded; (B2) corrects the four-cap correctness framing — the
> audio path drains the OSC FIFO DIRECTLY into `obj_cache_` (`SpatialEngine.cpp:473-478`)
> and does NOT read StateModel, so the bug is a **control/echo/scene-plane consistency
> defect**, not an audio-read; (3) adds the audio-plane D2 test (obj 100 renders
> non-silent at 128); (4) HOISTS the four audio-callback stack arrays to engine
> members; (5) HARDENS the RT gate with `peakPct()` + xrun backstops alongside the
> P² p99 estimate; (6) makes C-M7 an **evidence-conditional default-flip gate**;
> (7) lets C-M4 promote F3 (lazy-prime) into scope if RSS nears the ceiling.
> No fifth runtime cap exists (Architect grep confirmed — see §0.5). See Changelog.
>
> **STALE-PATH CORRECTION (load-bearing):** the feature-extension outline cites
> `core/src/util/Constants.h` — that file **does not exist**. The canonical
> constant is `MAX_OBJECTS = 64` at **`core/src/core/Constants.h:12`** (verified).
> Every reference below uses the correct path.
>
> **Mode: DELIBERATE.** Bumping a core audio constant ~doubles per-block RT load
> AND ~doubles several large memory footprints, with FOUR independent silent
> object-cap constants that can desync. This is meaningful RT + correctness risk
> → §"Pre-Mortem" (3 scenarios) and §"Expanded Test Plan" are included per the
> DELIBERATE protocol. Justification at end of RALPLAN-DR Summary.

---

## 0. Grounding — what actually exists today (file:line evidence)

This is the load-bearing context. Read it before disputing any milestone scope.

### 0.1 The canonical constant and the symbolic usages that scale for free

- `MAX_OBJECTS = 64` at **`core/src/core/Constants.h:12`** (`inline constexpr int`, namespace `spe`). Header comment (`:9-11`) already anticipates growth: *"Spec mandates 8 simultaneous; expanded to 64 for larger venue deployments (US-002)."*
- All **symbolic** `MAX_OBJECTS` uses scale automatically when the constant changes. Verified usages (object-dim, **leave the symbol, it bumps for free**):
  - `core/src/core/SpatialEngine.h:392` `std::array<ObjCache, MAX_OBJECTS> obj_cache_{}`
  - `core/src/core/SpatialEngine.h:395` `std::array<float, MAX_OBJECTS> osc_phases_{}`
  - `core/src/core/SpatialEngine.h:408` `std::array<std::array<float, MAX_BLOCK>, MAX_OBJECTS> dry_scratch_{}`
  - `core/src/core/SpatialEngine.h:409` `std::array<const float*, MAX_OBJECTS> dry_ptrs_{}`
  - `core/src/core/SpatialEngine.cpp:401,403,425,477,607,632,676-680,693-706,828,897` (resize/loop/clamp/render spans — all symbolic).
  - `core/src/output_backend/BinauralMonitor.h:554` `std::array<ObjectSlots, MAX_OBJECTS> obj_slots_{}` — **resolves to `spe::MAX_OBJECTS`** because the file `#include "core/Constants.h"` (`:55`) and lives in `namespace spe::output` (`:67`) with NO `using namespace scene` → no shadowing. CONFIRMED canonical.
  - VBAP/DBAP renderer ramps: `core/src/render/VBAPRenderer.h:35` and `core/src/render/DBAPRenderer.h:33` are `std::array<std::array<spe::dsp::GainRamp, 64>, spe::MAX_OBJECTS>` — **outer dim = `spe::MAX_OBJECTS` (object, bumps for free); inner `64` = SPEAKER (leave).**
  - `core/src/render/RenderingAlgorithm.h:25` `std::array<std::array<float, spe::MAX_OBJECTS>, 64> gains{}` — **inner `spe::MAX_OBJECTS` = object (bumps); outer `64` = SPEAKER (leave).** Note the dimension order is INVERTED vs the ramps — both are correct, the `64` is the speaker axis in all three.

### 0.2 THE LOAD-BEARING CLASSIFICATION TABLE — every hardcoded `64` / `[64]` in `core/src`

Produced from `grep -rn '\b64\b\|\[64\]' core/src`. Each occurrence classified **OBJECT-dim (must follow MAX_OBJECTS)** vs **SPEAKER-dim (leave)** vs **UNRELATED (leave)**. This table is the spec for C-M2.

| file:line | literal | classification | action |
|---|---|---|---|
| `core/src/core/Constants.h:12` | `MAX_OBJECTS = 64` | **OBJECT (canonical)** | **bump to 128 (C-M1)** |
| `core/src/scene/SceneCrossfade.h:33` | `inline constexpr int MAX_OBJECTS = 64` (namespace `spe::scene`, a **DUPLICATE** of the constant) | **OBJECT (silent shadow cap)** | **must track core; redefine in terms of `spe::MAX_OBJECTS` or bump in lockstep (C-M2 BLOCKER)** |
| `core/src/ipc/StateModel.h:15` | `STATE_MAX_OBJECTS = 64` | **OBJECT (silent cap)** — used at `StateModel.h:71,91`, `StateModel.cpp:34,52,67,82` to clamp `obj_id` and size `objects_[]` | **must track core (C-M2 BLOCKER)** |
| `core/src/ipc/EchoSubscriber.h:51` | `kEchoMaxObjects = 64` | **OBJECT (silent cap)** — used at `:71,85,92,143,151,157,162,167,172,177,212`, `EchoSubscriber.cpp:196` to size echo cache + dirty-bit map | **must track core (C-M2 BLOCKER)** |
| `core/src/render/RenderingAlgorithm.h:25` | `…, 64> gains{}` (outer) | **SPEAKER** (max speakers; inner dim is `spe::MAX_OBJECTS`) | LEAVE |
| `core/src/render/VBAPRenderer.h:35` | `<GainRamp, 64>, spe::MAX_OBJECTS>` (inner) | **SPEAKER** | LEAVE |
| `core/src/render/VBAPRenderer.h:33,64`; `VBAPRenderer.cpp:19,23,24,98` | `num_speakers_ <= 64` assert | **SPEAKER cap** | LEAVE |
| `core/src/render/DBAPRenderer.h:33` | `<GainRamp, 64>, spe::MAX_OBJECTS>` (inner) | **SPEAKER** | LEAVE |
| `core/src/render/DBAPRenderer.cpp:36,44,45` | `final_gains[64]`, `gain_acc[64]`, `g_v[64]` | **SPEAKER** (per-speaker scratch) | LEAVE |
| `core/src/render/AlgorithmAnalyticReference.cpp:13,15,17,361` | `kMaxVbapSpeakers = 64` | **SPEAKER cap** | LEAVE |
| `core/src/geometry/SpeakerLayout.h:30` | `kMaxYamlChannel = 64` | **SPEAKER/channel cap** | LEAVE |
| `core/src/output_backend/BinauralMonitor.cpp:302` | `kMaxYamlChannel = 64 ≥ 24` | **SPEAKER** (comment) | LEAVE |
| `core/src/audio_io/SharedRingBackend.h:83,85` | `1..64` per-ring channel count (ADR §2.3) | **CHANNEL cap** | LEAVE |
| `core/src/ipc/Command.h:129,130,196,197,203-206`; `CommandDecoder.cpp:155,156,162,164`; `SceneSnapshot.cpp:14`; `CueList.cpp:15` | `char name[64]` / `char buf[64]` | **UNRELATED** (64-byte string buffers) | LEAVE |
| `core/src/ipc/EchoSubscriber.h:102` | `char tag[64]` | **UNRELATED** (string) | LEAVE |
| `core/src/bin/MetricsEmit.h:33` | `char kv[64]` | **UNRELATED** (string) | LEAVE |
| `core/src/audio_io/AudioBackend.h:56,62`; `BinauralMonitor.h:87,514`; `DanteBackend.h:87`; `spatial_engine_core.cpp:352,414` | `block_size = 64` | **UNRELATED** (default audio block size, not objects) | LEAVE |
| `core/src/util/SpscRing.h:95-97`; `TraceRing.h:61-63` | `alignas(64)` | **UNRELATED** (cache-line) | LEAVE |
| `core/src/hrtf/KdTree3D.cpp:94,96` | `kStackDepth = 64` | **UNRELATED** (DFS stack) | LEAVE |
| `core/src/hrtf/SofaBinReader.cpp:36,40` | `{64,128,…}` IR length set | **UNRELATED** (HRIR taps) | LEAVE |
| `core/src/sync/Ltc*.{h,cpp}`; `FdnReverb.cpp:8`; `RingHeader.h:9`; `ObservabilityCounters.h:22,57` | various `64` (bit-widths, `x86-64`, 64-bit counters) | **UNRELATED** | LEAVE |

**CONSEQUENCE (the headline correctness risk) — a control/echo/scene-plane consistency defect (REV 2 / B2 reframed):** there are **FOUR independent OBJECT-dimension caps** — `spe::MAX_OBJECTS` (canonical), `spe::scene::MAX_OBJECTS` (`SceneCrossfade.h:33`), `STATE_MAX_OBJECTS` (`StateModel.h:15`), `kEchoMaxObjects` (`EchoSubscriber.h:51`).

**The audio plane is NOT the problem — it scales for free.** The audio render path drains the OSC command FIFO **DIRECTLY into `obj_cache_`** at `SpatialEngine.cpp:473-478` (`while (cmd_fifo_.pop(qc)) { if (qc.obj_id >= MAX_OBJECTS) continue; auto& c = obj_cache_[qc.obj_id]; … }`), gated ONLY by the symbolic `MAX_OBJECTS` (`:477`). At 128, `obj_cache_[100]` is reached and rendered. **The audio path never reads StateModel** — `StateModel` is a PARALLEL CONTROL-SIDE store (sequence/reorder diagnostics + echo source), not on the render path.

**The defect is therefore a control/echo/scene-PLANE consistency desync**, not an audio drop: if only the canonical cap bumps, the audio plane renders objects 64–127 fine, but the **CONTROL plane** (`StateModel::apply` rejects them at `StateModel.cpp:34` `if (p.obj_id >= STATE_MAX_OBJECTS) return false`), the **ECHO plane** (`EchoSubscriber` mark/test short-circuit at `EchoSubscriber.h:85` so no `/echo/*` for objects ≥64), and the **SCENE plane** (`SceneCrossfade::Snapshot.objects[64]` is out of bounds for the cue/crossfade path from Lane E) all silently cap at 64. Result: objects 64–127 would make sound but be invisible to state-diag, un-echoed to subscribers, and lost across scene save/load/crossfade — a planes-out-of-sync corruption. C-M2's job is to unify all four. (REV 1 critical finding, REV 2 reframed — this is the load-bearing defect, the analogue of Lane E's second-producer race.)

### 0.3 The RT invariant — per-object alloc happens ONCE in prepareToPlay; audioBlock stays alloc-free

- `core/src/core/SpatialEngine.cpp:401-403` `chains_.resize(MAX_OBJECTS)` and `:425` per-object prepare loop run in `prepareToPlay` (control thread, may allocate). Comment `SpatialEngine.h:376`: *"each DelayLine is ~192 KB; 64 chains × 2 lines ≈ 24 MB → far too large for stack"* → `chains_` is `std::vector<PerObjectChain>` (heap). **Doubling to 128 → ~48 MB of DelayLine heap** (the dominant memory term; see §0.4).
- The audio callback `audioBlock` is alloc-free TODAY and must STAY alloc-free. **Today it places fixed-size object arrays ON THE AUDIO-CALLBACK STACK**: `SpatialEngine.cpp:676-679` declares FOUR `std::array<render::ObjectState, MAX_OBJECTS>` (vbap/dbap/wfs/ambisonic) inside `audioBlock`. `ObjectState` ≈ 20 bytes (`RenderingAlgorithm.h:12-19`: 4 floats + bool). At 64: 4 × 64 × 20 ≈ **5 KB** of audio-callback stack; at **128: ≈ 10 KB**. Plus `dry_scratch_` member `[MAX_OBJECTS][MAX_BLOCK]` = 128 × 512 × 4 ≈ **256 KB** (was 128 KB) — a member of the heap-allocated engine, NOT stack, so OK.
- **REV 2 / amendment 4 (HOIST):** C-M2 **hoists those four `std::array<render::ObjectState, MAX_OBJECTS>` from the `audioBlock` stack to engine members** (next to `dry_scratch_`, `SpatialEngine.h:408`). They are already conceptually per-block scratch; hoisting is zero runtime cost, keeps them off the audio-thread stack at any cap, and **eliminates pre-mortem scenario 2** (the ~10 KB stack-doubling concern). C-M6 verifies the stack no longer carries them. RT-safety unchanged (still fixed-size, alloc-once, no audioBlock alloc).
- `b2_sh_scratch_` (`SpatialEngine.h:440`) is `[16][MAX_BLOCK]` (SH-order, NOT object-dim) → unaffected.

### 0.4 Memory footprint estimate — 64 → 128 (the C-M4 hard deliverable, to be MEASURED not just modelled)

Per-object structures that double (dominant terms, heap unless noted):

| structure | per-object cost | ×64 | ×128 | Δ |
|---|---|---|---|---|
| `chains_` PerObjectChain — `DelayLine user_delay_` (`PerObjectChain.h:84`) `std::array<float,48000>` = 192 KB + propagation/other lines (comment "2 lines") | ~192–384 KB | ~12–24 MB | ~24–48 MB | +12–24 MB |
| BinauralMonitor `obj_slots_` (`BinauralMonitor.h:554`) — each `ObjectSlots` = 4 `OlaConvolver` (conv_L[2]+conv_R[2], `:529-530`); each OlaConvolver holds `ir_`/`overlap_`/`work_` vectors sized to MAX_IR_LEN at prime time (heap) | depends on MAX_IR_LEN | (B1 path) | doubles | measure |
| VBAP `ramps_` (`VBAPRenderer.h:35`) `[MAX_OBJECTS][64] GainRamp` (~24 B each) | 64×24=1.5 KB | ~98 KB | ~196 KB | +98 KB |
| DBAP `ramps_` (`DBAPRenderer.h:33`) same shape | 1.5 KB | ~98 KB | ~196 KB | +98 KB |
| `dry_scratch_` (`SpatialEngine.h:408`) `[MAX_OBJECTS][512] float` (engine member, heap) | 2 KB | 128 KB | 256 KB | +128 KB |
| `obj_cache_`/`osc_phases_`/`dry_ptrs_` | small | — | — | negligible |

**The DelayLine `chains_` term dominates** (tens of MB). C-M4 must MEASURE actual RSS (e.g. `/proc/self/statm` or `getrusage` max-RSS) at startup for 64 vs 128 with the heaviest config (B2 binaural + 8ch + VBAP) and assert **< 100 MB** (hard gate). The OlaConvolver pairs × 128 are the second concern and only materialize when the binaural side-output is enabled.

### 0.5 The obj_id clamp + protocol caps — is there any OTHER silent limiter? (Architect-confirmed: NO fifth cap)

- **AUDIO plane (the render gate):** `SpatialEngine.cpp:473-478` drains the OSC FIFO directly into `obj_cache_`, gated by `if (qc.obj_id >= static_cast<uint32_t>(MAX_OBJECTS)) continue;` (`:477`) — symbolic, scales for free. CONFIRMED. (This is THE render path; StateModel is not consulted here — see §0.2 B2 reframe.)
- `CommandDecoder.cpp:192` `/adm/obj/%d/%31s … || obj_id < 0` — only a LOWER-bound guard; no upper cap in the decoder (the cap is downstream at `:477`). CONFIRMED no hidden decoder cap.
- §0.2 found three OTHER (non-audio) caps on the OBJECT path that are NOT `MAX_OBJECTS`: `STATE_MAX_OBJECTS` (StateModel control-side store, clamps at `StateModel.cpp:34/52/67/82`), `kEchoMaxObjects` (echo cache + dirty-bit map), `spe::scene::MAX_OBJECTS` (crossfade snapshot, the cue path from Lane E). These ARE the "other protocol/array caps that silently limit objects" the survey demanded — all three must be unified in C-M2.
- **NO FIFTH RUNTIME CAP (Architect grep-confirmed, REV 2):** the Architect independently grepped the ADM/OSC decode path, the shm ring, `CueEngine`, `SceneSnapshot`, and the `Command` payloads — **none introduces a fifth object cap.** Specifics confirmed: (a) `SceneSnapshot` uses an **unbounded `std::vector<ObjectSnapshot>`** (`SceneSnapshot.h:10-18`) → no persisted-format break and no 64-cap (it stores whatever objects exist; the only fixed-64 in the scene path is `SceneCrossfade::Snapshot`, already cap #4); (b) `CommandDecoder.cpp:192` is lower-bound only (above); (c) `SharedRingBackend` `1..64` is a CHANNEL-dim cap (audio channels, not objects) → unrelated. **Conclusion: exactly four object-dim caps; C-M2's unification is complete.**

### 0.6 Build / test substrate

- cmake options live in `CMakeLists.txt` (top-level) + `core/CMakeLists.txt`. Existing: `SPATIAL_ENGINE_NO_JUCE` (`core/CMakeLists.txt:32`), `SPATIAL_ENGINE_VST3` (`:33`), `SPATIAL_ENGINE_RT_ASSERTS` (`CMakeLists.txt:43`, gates `SPE_RT_ASSERTS=1` at `core/CMakeLists.txt:67-68`). **There is NO `SPATIAL_ENGINE_MAX_OBJECTS` option today** — C-M1 adds it.
- Build dirs already present: `core/build` (NO_JUCE default), `core/build_rton` (RT-asserts), `core/build_rel` (Release/soak), `core/build_tsan`. RT-asserts test `p1_rt_no_alloc` at `core/tests/core_unit/CMakeLists.txt:106`.
- Perf harness: `core/tests/perf/soak_adm_osc_flood.cpp` hardcodes `constexpr int N_OBJ = 64` (`:74`) and `pkt_buf[64]` (`:109`) — the object count is a literal, not `MAX_OBJECTS`. C-M3 must parameterize it (or add a sibling) to flood at 128. Built only under `Release` or `-DSPATIAL_ENGINE_RUN_SOAK` (`core/tests/perf/CMakeLists.txt:5`).
- RT-budget instrument EXISTS: `core/src/util/CpuMeter.h` — `recordBlockStart()`/`recordBlockEnd(num_frames, sample_rate)` (`:46-52`), publishes scalar atomics `cpuPct()` / `peakPct()` / `p99Us()` (`:91-93`); p99 is a P² running estimate (`:28`, Jain & Chlamtac). This is the Lane A instrument; C-M3 uses it to fill the RT-budget table (median via cpuPct EWMA, p99 via p99Us). Plus `XrunCounter` (`SpatialEngine.h:490` `internal_xruns_`) for the xrun gate.
- Daemon arg parsing: `spatial_engine_core.cpp:388-389` parses `--block`/`--channels` but NOT an object count (objects come over OSC). The soak harness is the object-load driver.

---

## RALPLAN-DR Summary (for Architect / Critic)

### Principles
1. **RT path stays alloc-free AND the four object-caps stay unified.** No new allocation/lock/branch in `audioBlock`; per-object alloc stays once-in-`prepareToPlay` (`SpatialEngine.cpp:401-425`). AND the canonical `spe::MAX_OBJECTS` (`Constants.h:12`), `spe::scene::MAX_OBJECTS` (`SceneCrossfade.h:33`), `STATE_MAX_OBJECTS` (`StateModel.h:15`), `kEchoMaxObjects` (`EchoSubscriber.h:51`) must share ONE source of truth — never desync.
2. **Symbol over literal.** Every object-dimension cap derives from `spe::MAX_OBJECTS` (ideally the three duplicates become `= spe::MAX_OBJECTS`); SPEAKER-dim `64`s are deliberately left untouched (§0.2 table is the contract).
3. **64 must remain a first-class buildable config.** The bump is via a compile-time option so existing VST3/wire/tests can build at 64 unchanged; 128 is the new default OR an opt-in (Option choice below). No runtime-variable cap (RT/alloc reasons).
4. **Measure, don't assert by inspection.** The RT-budget table (median + p99 block time at 8/16/32/64/96/128 objects, heaviest path) and the RSS table are MEASURED via CpuMeter + getrusage, with hard pass/fail thresholds — not eyeballed.
5. **Additive, regression-clean.** Full NO_JUCE ctest + RT-asserts must be green at BOTH 64 and 128; existing wire bytes / scene round-trips unchanged.

### Decision Drivers (top 3)
1. **D1 — RT budget headroom at 128.** The heaviest path (128 obj + 8ch out + VBAP + B2 binaural side-output, each object a dual-slot OlaConvolver L/R) must hold **≤ 50% of the RT budget** (block time, Linux 8-core, 48 kHz / 64-block) with **zero xruns** and **zero new audioBlock allocation**. **REV 2 / amendment 5 — gate hardening:** `CpuMeter::p99Us()` (`CpuMeter.h:120-128`) is a P² **estimate** that returns the last sample until warm (`count_ >= 5`) and can under-report a rare tail. So the gate uses THREE signals as HARD backstops: **`peakPct()` (true running max, `CpuMeter.h:79`) ≤ 50% budget AND `XrunCounter == 0`** are the authoritative pass/fail, alongside the p99 estimate (informational/tracking). CpuMeter (`CpuMeter.h:79,91-93`) + XrunCounter are the instruments. This is the make-or-break gate.
2. **D2 — Four-cap unification (control/echo/scene-plane consistency at 128).** This is a PLANE-CONSISTENCY defect, not an audio drop (§0.2 B2). Verified by TWO regression tests exercising objects 64–127: **(audio plane)** an OSC `/adm/obj/100/aed` datagram drains via `SpatialEngine.cpp:473-478` into `obj_cache_[100]` and renders **NON-SILENT** output at 128 (the real "objects 64–127 produce sound" guarantee); **(control/echo/scene plane)** the same id survives `StateModel::apply` (not dropped at `StateModel.cpp:34`), is markable in the echo dirty-map (`EchoSubscriber.h:85`), and is addressable in `SceneCrossfade::Snapshot.objects[100]`. RT-asserts is NECESSARY-BUT-INSUFFICIENT here — this is a logic-cap defect, caught by functional tests, not by alloc-asserts.
3. **D3 — Memory ceiling.** Max-RSS at 128 with the heaviest config stays **< 100 MB**; the `chains_` DelayLine term (~24→48 MB) and the OlaConvolver-pair ×128 term are the dominant contributors and must be measured, not modelled.

### Viable Options

**How is the cap raised, and what is the default?** (the load-bearing architectural choice)

- **Option A — Compile-time `option(SPATIAL_ENGINE_MAX_OBJECTS 128)`, default STAYS 64 (CHOSEN).** Add a CMake cache var `SPATIAL_ENGINE_MAX_OBJECTS` (default 64) that defines `SPE_MAX_OBJECTS=<n>`; `Constants.h:12` becomes `inline constexpr int MAX_OBJECTS = SPE_MAX_OBJECTS;` (with `#ifndef SPE_MAX_OBJECTS #define SPE_MAX_OBJECTS 64 #endif` fallback so a bare compile still works). The three duplicate caps are redefined `= spe::MAX_OBJECTS`. 128 is built/tested as a first-class config (`core/build_obj128`) and gated, but the shipped default and existing build dirs stay 64 until the RT-budget table proves 128 safe.
  - Pros: Lowest blast radius — existing 64 builds/tests/VST3/wire are byte-identical (the macro defaults to 64). 128 is provably buildable and fully gated. Reversible. Matches Principle 3. Lets the RT-budget evidence (D1) drive whether/when the default flips, instead of betting on it up front.
  - Cons: Two configs to keep green in CI (64 and 128) — but that IS the deliverable (both-config regression is a hard gate anyway). A second build dir.

- **Option B — Flip the default to 128 unconditionally (constant edit, no option).** Just change `Constants.h:12` to 128 and unify the three duplicates.
  - Pros: Simplest diff; one config.
  - Cons: **Directly threatens D1 and D3 before they are proven** — every deployment pays the ~2× RT + memory cost whether or not it needs 128 objects; if the RT-budget table later shows 128+B2 exceeds 50% budget on a target box, there is no compile-time escape hatch. Removes the 64 regression baseline (Principle 3 violated — outline explicitly wants BOTH 64 and 128 buildable). **Invalidated:** the outline's "both 64 and 128 buildable" requirement + the unproven-RT-cost risk make an unconditional flip premature. (128 can still become the *default* once proven — but via the evidence-gated C-M7 step, not an unconditional constant edit, and always keeping 64 as a tested escape hatch.)

- **Option C — Runtime-configurable cap (e.g. `--max-objects` arg sizing `std::vector`s).** Make `MAX_OBJECTS` a runtime value; size all per-object arrays dynamically.
  - Pros: One binary serves any object count.
  - Cons: **Directly threatens Principle 1 / D1.** Today `obj_cache_`, `osc_phases_`, `dry_scratch_`, the renderer `ramps_`, and the FOUR `std::array<ObjectState, MAX_OBJECTS>` in `audioBlock` (`SpatialEngine.cpp:676-679`) are **fixed-size `std::array`** — RT-safe by construction (no heap, sizes known at compile time). Making the cap runtime forces these to `std::vector`/dynamic, which (a) moves the audioBlock stack arrays to heap or VLA (alloc/UB risk), (b) defeats the compile-time `std::array` RT guarantee, (c) complicates the RT-asserts story. Large blast radius for a benefit nobody asked for (the venue cap is a build-time decision). **Invalidated under Principle 1 (RT-safe fixed arrays) and the lane's RT-no-alloc constraint.**

**Conclusion:** Option A is chosen. It uniquely satisfies "BOTH 64 and 128 buildable" (Principle 3 / the outline), keeps the 64 regression baseline, defers the default-flip decision to the RT-budget evidence (D1), and preserves the compile-time `std::array` RT guarantee (vs Option C). Option B is premature (flip before proof); Option C breaks the fixed-size RT invariant. The four-cap unification (D2) is orthogonal and applies to whichever option — it is the decisive correctness defect (§0.2).

**Mode: DELIBERATE.** Justification: this bump ~doubles per-block RT load on the heaviest binaural path AND ~doubles tens-of-MB memory terms, touches the audio-callback stack-array sizing (`:676-679`), and the FOUR-cap desync is a silent-corruption class of bug. Two of the three drivers (D1 RT budget, D3 memory) are quantitative gates that can only fail at scale, and D2 is a corruption risk. That clears the "meaningful RT risk / high-risk signal" bar → pre-mortem (3 scenarios) + expanded test plan (unit/integration/e2e/observability) are included below. (If the Architect judges the RT cost trivially within budget after C-M3 measurement, the Critic may down-mode to SHORT for the final revision — but the measurement must come FIRST.)

---

## Milestones

> RT-safety invariant for **every** milestone: per-object allocation stays confined to `prepareToPlay` (`SpatialEngine.cpp:401-425`); the `audioBlock` callback adds NO new allocation/lock/branch. All object-dim caps derive from the single `spe::MAX_OBJECTS`. The SPEAKER-dim `64`s in §0.2 are deliberately untouched. D1/D3 are MEASURED at C-M3/C-M4 and re-verified at C-M6.

### C-M1 — Compile-time `SPATIAL_ENGINE_MAX_OBJECTS` option + canonical constant

**Files**
- Modify `CMakeLists.txt` (top-level) — add `set(SPATIAL_ENGINE_MAX_OBJECTS 64 CACHE STRING "Max simultaneous spatial objects (64 or 128)")` next to the existing options (`:43`); propagate `target_compile_definitions(... PUBLIC SPE_MAX_OBJECTS=${SPATIAL_ENGINE_MAX_OBJECTS})` to `spe_util`/`spe_core` (mirror the `SPE_RT_ASSERTS` plumbing at `core/CMakeLists.txt:67-68`). Add a value guard: error if not in `{64,128}`.
- Modify `core/src/core/Constants.h:12` — `#ifndef SPE_MAX_OBJECTS` / `#define SPE_MAX_OBJECTS 64` / `#endif`, then `inline constexpr int MAX_OBJECTS = SPE_MAX_OBJECTS;`. Keep the existing comment; add an ADR-amendment note (per `Constants.h:3`).

**Precise change**: a bare `cmake ..` (no flag) yields `MAX_OBJECTS == 64` (byte-identical to today, via the `#define` fallback). `cmake .. -DSPATIAL_ENGINE_MAX_OBJECTS=128` yields 128. No source outside Constants.h changes in this milestone.

**RT-safety invariant**: compile-time constant only; arrays stay `std::array` fixed-size; no audio-path change.

**Gate / test**: `cmake -DSPATIAL_ENGINE_NO_JUCE=ON ..` (default) → `static_assert`/runtime check `MAX_OBJECTS==64`; `cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128 ..` → `MAX_OBJECTS==128`. New ctest `test_p_max_objects_constant` asserts the value matches the configured macro; invalid value (e.g. 100) → cmake configure error.

**Acceptance**
- Default build unchanged (64); 128 build configures and compiles.
- Invalid `-DSPATIAL_ENGINE_MAX_OBJECTS` rejected at configure time.
- Existing build dirs (`core/build`, `core/build_rton`) still produce 64 with no flag.

---

### C-M2 — Unify the FOUR object caps (the silent-cap BLOCKER)

**Files**
- Modify `core/src/scene/SceneCrossfade.h:33` — replace `inline constexpr int MAX_OBJECTS = 64;` with a derivation from the canonical constant: `#include "core/Constants.h"` then `inline constexpr int MAX_OBJECTS = spe::MAX_OBJECTS;` (keeps the `spe::scene::MAX_OBJECTS` symbol but bound to the single source of truth). Verify `Snapshot::objects` (`:36`) now sizes to 128.
- Modify `core/src/ipc/StateModel.h:15` — `static constexpr int STATE_MAX_OBJECTS = spe::MAX_OBJECTS;` (include `core/Constants.h`). Re-verify the clamps at `StateModel.cpp:34,52,67,82` and `objects_[STATE_MAX_OBJECTS]` (`StateModel.h:91`) now admit obj_id 64–127.
- Modify `core/src/ipc/EchoSubscriber.h:51` — `static constexpr std::size_t kEchoMaxObjects = spe::MAX_OBJECTS;`. Re-verify the dirty-bit map sizing (`:71` `kTotalObjBits = kEchoMaxObjects * kBitsPerObj`, `:72` `kObjBytes`) and the cache array (`:212`).
- **REV 2 / amendment B1 (NEW BLOCKER — the 128 build does not compile/pass until this is fixed) — guard the existing hard-pinned-64 tests:**
  - `core/tests/core_unit/test_p_max_objects.cpp:14-15` — `static_assert(spe::MAX_OBJECTS == 64, …)` is a **HARD COMPILE FAILURE** under `-DSPATIAL_ENGINE_MAX_OBJECTS=128`. Replace with `static_assert(spe::MAX_OBJECTS == SPE_MAX_OBJECTS …)` (always true, value-agnostic) OR `#if SPE_MAX_OBJECTS == 64 static_assert(…==64) #elif …==128 static_assert(…==128) #endif`.
  - `core/tests/core_unit/test_p_max_objects.cpp:71-87` — Test 3 asserts `obj_id == MAX_OBJECTS (64)` is **rejected** by StateModel. At 128 the literal `64` is now a VALID id (must be ACCEPTED). Rewrite to **boundary semantics**: `obj_id == spe::MAX_OBJECTS` rejected, `obj_id == spe::MAX_OBJECTS - 1` accepted — passes at BOTH caps. (The `printf` "obj_id=64 rejected" string must use the symbol too.)
  - `core/tests/core_unit/test_p_adm_osc_v1_compat.cpp:167-190` (`test_obj_id_out_of_range`) — asserts `/adm/obj/64/azim` and `/adm/obj/65/…` decode-then-drop. The hardcoded `64`/`65` INVERT at 128 (64 is now in-range). Rewrite using `spe::MAX_OBJECTS` and `MAX_OBJECTS+1` for the out-of-range addresses so the "decoded but drained-drop" semantic holds at both caps. (Decoder still accepts any valid-address id — only the drain at `SpatialEngine.cpp:477` drops ≥ cap; this test asserts decoder behavior, which is cap-relative.)
- **REV 2 / amendment 4 (HOIST the audio-callback stack arrays):** in `core/src/core/SpatialEngine.h` add four engine members `std::array<render::ObjectState, MAX_OBJECTS> vbap_objs_/dbap_objs_/wfs_objs_/ambisonic_objs_` (next to `dry_scratch_`, `:408`); in `core/src/core/SpatialEngine.cpp:676-679` delete the four local `std::array` declarations and use the members (clear/reuse per block). Zero runtime cost; removes ~10 KB from the audio-thread stack at 128; eliminates pre-mortem scenario 2. RT-safety unchanged (fixed-size, alloc-once).

**Precise change**: all four object-dim caps become one value at compile time. After this milestone, `grep -rn 'STATE_MAX_OBJECTS\s*=\|kEchoMaxObjects\s*=\|MAX_OBJECTS\s*=' core/src` must show every object-dim definition deriving from `spe::MAX_OBJECTS` (the only literal `= 128`/`= 64` is `SPE_MAX_OBJECTS` in Constants.h / cmake). The four scratch arrays move from the `audioBlock` stack to engine members.

**RT-safety invariant**: StateModel + EchoSubscriber are control/IO-thread structures (not the audio callback); SceneCrossfade snapshot grows but is control-loop-owned (Lane E §0.4). The hoisted scratch members are fixed-size, alloc-once, written per block (no alloc). No audioBlock allocation; audio-thread stack footprint REDUCED. Memory: StateModel `objects_[128]` and echo cache `[128]` grow (small POD arrays, control-side).

**Gate / test**: NO_JUCE ctest `test_p_object_cap_unify` (new) + the guarded existing tests, at BOTH 64 and 128:
- **(audio plane — amendment 3, the real "produces sound" guarantee):** at 128, fire an OSC `/adm/obj/100/aed` **datagram** through the real UDP path; after the FIFO drain (`SpatialEngine.cpp:473-478`) and a render block, assert `obj_cache_[100]` populated AND the rendered output is **NON-SILENT** (RMS > 0 on the relevant speaker/binaural bus). At 64 the same id 100 yields silence (correctly dropped at `:477`).
- **(control/echo/scene plane):** at 128, obj_id 100 accepted by `StateModel::apply` (NOT dropped at `StateModel.cpp:34`); echo dirty-bit `mark(100,…)`/`test` works (not short-circuited at `EchoSubscriber.h:85`); SceneCrossfade `Snapshot.objects[100]` addressable.
- **(B1 regression):** at 64, `test_p_max_objects` + `test_p_adm_osc_v1_compat` pass byte-identically (boundary semantics reduce to the old `64` behavior); at 128 they compile and pass with the boundary at 128.
- Invariant grep check (CI step): no object-dim cap literal other than in Constants.h/cmake; no `std::array<render::ObjectState…>` remains a local in `audioBlock`.

**Acceptance**
- All four caps move in lockstep with `SPATIAL_ENGINE_MAX_OBJECTS`.
- **128 build COMPILES and PASSES** (B1 guarded: no `static_assert(==64)` failure, boundary-semantic obj-id tests green at both caps).
- At 128, objects 64–127 **render non-silent** (audio plane) AND survive StateModel + echo + crossfade (control/echo/scene plane).
- At 64, behavior byte-identical to today (regression baseline intact).
- Four scratch arrays hoisted off the audio-callback stack.

---

### C-M3 — RT-budget measurement harness + `docs/RT_BUDGET_MAX_OBJECTS.md` (D1 hard deliverable)

**Files**
- New: `core/tests/perf/perf_obj_block_time.cpp` — drives `SpatialEngine` through `NullBackend` at the **heaviest config** (8ch out + VBAP + B2 binaural side-output enabled, 48 kHz / 64-block) and floods objects via the real UDP path (reuse the `soak_adm_osc_flood.cpp` builder), sweeping active-object counts **{8,16,32,64,96,128}**. Reads `CpuMeter` (`cpuPct()` median-EWMA, `p99Us()`) + `XrunCounter` after a warm window; prints a table. Built under `Release`/`SPATIAL_ENGINE_RUN_SOAK` (mirror `core/tests/perf/CMakeLists.txt:5-13`).
- Modify `core/tests/perf/soak_adm_osc_flood.cpp` — replace hardcoded `N_OBJ = 64` (`:74`) and `pkt_buf[64]` (`:109`, leave — that's a packet-byte buffer, not object count) with `N_OBJ = spe::MAX_OBJECTS` so the existing soak floods at the configured cap.
- New: `docs/RT_BUDGET_MAX_OBJECTS.md` — records the measured table (median + p99 block time, % of RT budget, xruns) at 64 and 128 for each object count, the pass/fail threshold, the measurement method (CpuMeter P², getrusage), and the box spec (Linux 8-core).

**Precise change**: the harness computes RT budget = `block_size / sample_rate` (= 64/48000 ≈ 1333 µs at 48 kHz/64-block; matches `ObservabilityCounters.h:57` gate idiom) and reports block time as % of budget. **Hardened threshold (REV 2 / amendment 5):** because `p99Us()` (`CpuMeter.h:120-128`) is a P² estimate that under-reports until warm and can miss a rare tail, the AUTHORITATIVE pass/fail backstops are **`peakPct()` (true running max, `CpuMeter.h:79`) ≤ 50% budget AND `XrunCounter == 0`** at 128 obj on the heaviest path; the median (`cpuPct()` EWMA) and p99 estimate are recorded for tracking but the gate cannot pass on a low p99 estimate if `peakPct` or xruns say otherwise.

**RT-safety invariant**: the harness exercises (does not modify) the audio path; CpuMeter is the existing Lane A audio-thread instrument (already alloc-free, `CpuMeter.h:52`).

**Gate / test**: ctest `perf_obj_block_time` (Release) PASSES iff at 128/heaviest: **`peakPct()` ≤ 50% budget AND `XrunCounter == 0`** (hard backstops) — with median + p99-estimate also recorded. The table is committed to `docs/RT_BUDGET_MAX_OBJECTS.md`. If 128 exceeds 50% (peak) or shows any xrun, this gate FAILS the lane → escalate (keep default 64 per Option A / C-M7 evidence-gate, or scope the binaural path — feeds the Architect re-review).

**Acceptance**
- RT-budget table filled for {8,16,32,64,96,128} × {64-build, 128-build}, heaviest path.
- 128 **peakPct ≤ 50% RT budget AND xruns == 0** (hard); median + p99-estimate recorded.
- Doc committed with method + box spec; threshold pass/fail explicit.

---

### C-M4 — Memory-footprint verification (D3 hard deliverable)

**Files**
- Extend `core/tests/perf/perf_obj_block_time.cpp` (or a sibling `perf_obj_memory.cpp`) — after `prepareToPlay` with the heaviest config (B2 binaural ON → OlaConvolver pairs ×128 primed, 8ch, VBAP/DBAP ramps allocated), read max-RSS via `getrusage(RUSAGE_SELF).ru_maxrss` (and/or `/proc/self/statm`). Record at 64 and 128.
- Modify `docs/RT_BUDGET_MAX_OBJECTS.md` — add the memory table (§0.4 structures, modelled vs measured Δ).

**Precise change**: assert **measured max-RSS at 128/heaviest < 100 MB**. Cross-check the dominant `chains_` DelayLine term (§0.4) and the OlaConvolver-pair ×128 term against the model; flag any >20% model/measurement divergence for the Architect.

**REV 2 / amendment 7 — C-M4 can promote F3 (lazy-prime) from follow-up INTO scope:** `BinauralMonitor::primeAllSlots()` (declared `BinauralMonitor.h:558`, "Prime all OlaConvolvers in obj_slots_ to MAX_IR_LEN capacity" `:556`) primes **ALL** `obj_slots_[MAX_OBJECTS]` (= 128) OlaConvolver quads unconditionally when B2 is enabled — a **FIXED D3 cost** the moment binaural is on, NOT load-dependent (it does not matter how many objects are actually active). Therefore: **if measured RSS at 128/heaviest lands within ~20% of the 100 MB ceiling (i.e. ≥ ~80 MB), C-M4 PROMOTES F3 into Lane C scope** — implement lazy-prime (prime a slot only when its object first goes active) so the OlaConvolver memory tracks active objects, not the cap. (Lazy-prime touches the binaural path → its own RT-asserts gate; only triggered if the RSS evidence demands it.) If RSS is comfortably < 80 MB, F3 stays a follow-up.

**RT-safety invariant**: measurement only; all alloc is in `prepareToPlay`. (If F3 is promoted, lazy-prime moves some priming to a control-thread first-activation hook — still off the audio thread, with its own RT-asserts check.)

**Gate / test**: ctest assertion (in the perf target) max-RSS(128) < 100 MB; the model vs measured table in the doc; explicit F3-promotion decision recorded (in-scope vs deferred) based on the ~80 MB trigger.

**Acceptance**
- Measured 128/heaviest RSS < 100 MB.
- Memory table (modelled + measured) committed; divergences explained.
- F3-promotion decision explicit: lazy-prime brought in-scope iff RSS ≥ ~80 MB; else deferred with the measured headroom recorded.

---

### C-M5 — Full regression at BOTH 64 and 128

**Files**
- **REV 2 / amendment B1 — the two hard-pinned-64 tests guarded in C-M2 must pass in BOTH configs here:** `core/tests/core_unit/test_p_max_objects.cpp` (static_assert + boundary obj-id) and `core/tests/core_unit/test_p_adm_osc_v1_compat.cpp:167-190` (`test_obj_id_out_of_range`). C-M5 runs them at both caps to prove the boundary rewrite holds.
- Modify `core/tests/core_unit/CMakeLists.txt` if any OTHER test hardcodes 64-object expectations (audit: grep the unit tests for literal object counts that should be `MAX_OBJECTS`). Most tests use the symbol; fix any literal.
- CI/build-dir convention: keep `core/build` (64) and add `core/build_obj128` (128) as the two regression configs.

**Precise change**: run the entire NO_JUCE ctest suite in BOTH configs:
- `cmake -DSPATIAL_ENGINE_NO_JUCE=ON ..` (64) → full ctest green (the existing baseline — Lane E ended at 112/112).
- `cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128 ..` → full ctest green (incl. `test_p_object_cap_unify`, `test_p_max_objects_constant`, and the B1-guarded `test_p_max_objects` / `test_p_adm_osc_v1_compat`). **This is where B1 is proven: the 128 build now COMPILES (no `static_assert(==64)` failure) and PASSES (boundary semantics).**
- pytest (webgui/UI) — verify no Python-side hardcoded 64 object cap (the OSC obj_id range is open; UI panels enumerate objects — check `ui/` for a `range(64)` that should be configurable; document if found, fix or defer to F2).

**RT-safety invariant**: regression only.

**Gate / test**: BOTH ctest runs green; pytest green; any cross-language 64-literal cataloged.

**Acceptance**
- 64-config ctest byte-identical-green to the pre-Lane-C baseline.
- 128-config full ctest green.
- No hidden 64-object regression in C++ or Python.

---

### C-M6 — RT-asserts (build_rton) green at 128 + final D1/D3 re-verify

**Files**
- No new source; build/gate milestone. Add `core/build_rton_obj128` convention (RT-asserts × 128).

**Precise change**: re-run the two hard RT gates at 128:
1. **Alloc gate (D1):** `cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128 ..` (`core/build_rton_obj128`) builds; full RT-asserts ctest green (incl. `p1_rt_no_alloc`, `core/tests/core_unit/CMakeLists.txt:106`); **zero new `audioBlock` allocation** at 128 — the larger `dry_scratch_`/`ramps_` are still prepared once in `prepareToPlay`. **REV 2 / amendment 4 verification:** confirm the four `ObjectState` scratch arrays are now **engine members** (hoisted in C-M2), NOT locals at the old `SpatialEngine.cpp:676-679` — `grep` shows no `std::array<render::ObjectState…>` local in `audioBlock`; the audio-thread stack no longer carries the ~10 KB (pre-mortem scenario 2 retired).
2. **Budget+memory re-verify (D1/D3):** re-run `perf_obj_block_time` (Release, 128) — **peakPct ≤ 50% budget AND xruns 0** (hard backstops, amendment 5) + median/p99-estimate recorded, RSS < 100 MB, confirming the table from C-M3/C-M4 still holds after C-M2/C-M5 changes.

**RT-safety invariant**: this IS the RT verification milestone — RT-asserts is necessary (alloc) but D1's budget part is proven by C-M3's measurement, not by RT-asserts alone.

**Gate / test**: `build_rton_obj128` full ctest green + alloc-clean; `perf_obj_block_time` (128) within thresholds; audio-path source diff vs 64-build = ∅ (only the constant differs).

**Acceptance**
- RT-asserts build green at 128, zero new audioBlock alloc.
- RT-budget + memory thresholds re-confirmed post-integration.
- Audio callback logic byte-identical between 64 and 128 builds (only `MAX_OBJECTS` value differs).

---

### C-M7 — Evidence-gated default-flip decision + Documentation + ADR amendment

**REV 2 / amendment 6 — the default-flip is now an IN-LANE, evidence-gated step (no longer a someday-F1):** after C-M3/C-M4 produce the measured RT-budget + RSS tables, apply this explicit gate:

> **FLIP the shipped default to 128 in THIS lane IFF** (from C-M3/C-M4, heaviest path):
> `peakPct ≤ ~35% of RT budget` **AND** `max-RSS < ~70 MB` **AND** `xruns == 0`.
> Then `set(SPATIAL_ENGINE_MAX_OBJECTS 128 …)` becomes the cache default, and **64 is demoted to a tested-but-non-default escape hatch** (`-DSPATIAL_ENGINE_MAX_OBJECTS=64`).
> **ELSE keep default 64** (128 stays the proven opt-in).

Either way, Option A's both-config machinery + the 64 regression baseline are retained regardless of which value is the default — both configs stay green in CI.

**Files**
- Modify `CMakeLists.txt` — set the cache default to 128 **only if** the C-M3/C-M4 evidence clears the gate above; otherwise leave it at 64. Record the chosen default + the measured numbers that drove it.
- Modify `docs/RT_BUDGET_MAX_OBJECTS.md` — record the flip decision with the exact measured `peakPct`/`RSS`/`xruns` vs the ~35% / ~70 MB / 0 thresholds.
- New/modify ADR — amend the `MAX_OBJECTS` ADR note (`Constants.h:3` requires it) documenting the four-cap unification, the compile-time option, the audio-callback hoist, and the evidence-gated default decision.
- Modify `README` — pointer to `RT_BUDGET_MAX_OBJECTS.md` and the `-DSPATIAL_ENGINE_MAX_OBJECTS=<64|128>` build flag (with whichever is now default noted).

**Gate / test**: docs present; ADR amended; README links. Default decision matches the measured evidence vs the explicit thresholds. Final: BOTH-config ctest + RT-asserts(128) + perf(128) all green (re-state from C-M5/C-M6) regardless of default.

**Acceptance**
- Default value chosen by the explicit evidence gate (≤~35% budget + <~70 MB + 0 xruns → 128; else 64), with the driving measurements recorded.
- 64 remains a green, tested config whichever is default (regression baseline + escape hatch).
- ADR records decision (compile-time option, four-cap unification, hoist, evidence-gated default) + drivers + consequences.

---

## ADR — Lane C MAX_OBJECTS 64 → 128

**Decision**: Raise the per-object cap via a **compile-time CMake option `SPATIAL_ENGINE_MAX_OBJECTS`** (`{64,128}`), defining `SPE_MAX_OBJECTS` consumed by `core/src/core/Constants.h:12`. **Unify the four independent object-dimension caps** — `spe::MAX_OBJECTS` (canonical, `Constants.h:12`), `spe::scene::MAX_OBJECTS` (`SceneCrossfade.h:33`), `STATE_MAX_OBJECTS` (`StateModel.h:15`), `kEchoMaxObjects` (`EchoSubscriber.h:51`) — so all derive from the single canonical constant. Object-dim arrays stay fixed-size `std::array` (RT-safe by construction); per-object allocation stays once-in-`prepareToPlay`; the four `ObjectState` per-block scratch arrays are **hoisted off the audio-callback stack into engine members** (REV 2). SPEAKER-dimension `64`s (VBAP/DBAP ramps inner dim, `kMaxVbapSpeakers`, `kMaxYamlChannel`, channel caps) are deliberately left unchanged. 128 is a fully gated, first-class buildable config. **The shipped default is decided IN-LANE by an evidence gate (C-M7):** flip to 128 iff C-M3/C-M4 show `peakPct ≤ ~35%` budget AND RSS `< ~70 MB` AND zero xruns; else default stays 64 with 128 as the proven opt-in. The bug being fixed is a **control/echo/scene-plane consistency desync**, not an audio drop (the audio render path drains the OSC FIFO directly into `obj_cache_` at `SpatialEngine.cpp:473-478` and never reads StateModel — REV 2 / B2).

**Drivers**: (D1) the heaviest path (128 obj + 8ch + VBAP + B2 binaural) holds ≤ 50% RT budget (median + p99, 48 kHz/64-block, 8-core) with zero xruns and zero new audioBlock alloc; (D2) no silent 64-cap remains on the object path — objects 64–127 survive StateModel + echo + scene crossfade; (D3) max-RSS at 128 < 100 MB.

**Alternatives considered**:
- *Option B — unconditional flip of `Constants.h:12` to 128* — removes the 64 regression baseline the outline mandates and forces the ~2× RT/memory cost on every deployment before D1/D3 are proven. Rejected (premature; can flip later as a one-line cache-var change once evidence lands — F1).
- *Option C — runtime-configurable cap (`--max-objects`)* — would force the audio-path fixed-size `std::array`s (`obj_cache_`, `dry_scratch_`, renderer `ramps_`, and the four `std::array<ObjectState,MAX_OBJECTS>` in `audioBlock` at `SpatialEngine.cpp:676-679`) to dynamic allocation, breaking the compile-time RT-safe guarantee (Principle 1) for a benefit nobody requested (venue cap is a build-time decision). Rejected.
- *Bump only the canonical `MAX_OBJECTS`, leave the three duplicates* — the decisive defect: a **control/echo/scene-plane consistency desync** (REV 2 / B2). The AUDIO plane would render objects 64–127 fine (drain at `SpatialEngine.cpp:473-478`, gated only by the symbolic `MAX_OBJECTS`), but the CONTROL plane (`StateModel::apply` drops at `StateModel.cpp:34`), the ECHO plane (`EchoSubscriber` short-circuits at `:85`), and the SCENE plane (`SceneCrossfade::Snapshot.objects[64]` out of bounds) silently stay at 64 → those objects make sound but are invisible to diag, un-echoed, and lost across scene save/load. Rejected — four-cap unification is mandatory (D2).

**Why chosen**: Option A uniquely keeps BOTH 64 and 128 buildable (the outline's explicit requirement), preserves the 64 regression baseline, defers the default-flip to measured RT evidence (D1) instead of betting on it, and keeps the fixed-size `std::array` RT-safe guarantee (vs Option C's dynamic sizing). The four-cap unification is the load-bearing correctness fix and is orthogonal to the option choice.

**Consequences**:
- (+) 64 builds byte-identical (macro at 64); existing VST3/wire/tests untouched whichever value is default.
- (+) 128 fully gated by an RT-budget table (peakPct + xrun hard backstops) + RSS ceiling + RT-asserts, all measured.
- (+) Four object caps unified → no plane-desync class of bug; one source of truth.
- (+) Audio-callback `ObjectState` scratch hoisted to members → ~10 KB off the audio-thread stack at 128; pre-mortem scenario 2 retired.
- (+) The default-flip is evidence-gated IN-LANE (C-M7), not a deferred guess.
- (−) Two CI configs (64 + 128) and two RT-asserts/perf build dirs to keep green (that IS the both-config deliverable).
- (−) Existing hard-pinned-64 tests (`test_p_max_objects`, `test_p_adm_osc_v1_compat`) required boundary-semantic rewrites to compile/pass at both caps (B1 — done in C-M2).
- (−) At 128 the heaviest binaural path roughly doubles per-block work and the `chains_` DelayLine heap (~24→48 MB) + OlaConvolver pairs ×128 (a FIXED cost from `primeAllSlots`, `BinauralMonitor.h:558`) — bounded by the ≤50%/<100MB gates; if RSS nears the ceiling, F3 lazy-prime is promoted in-scope (C-M4).
- (−) Scene snapshots/StateModel POD arrays grow (small, control-side); `SceneSnapshot` itself is unbounded `std::vector` (no format break).

**Follow-ups**:
- **F1 (now an IN-LANE C-M7 step, not a follow-up):** the default-flip to 128 is decided in C-M7 by the evidence gate (≤~35% budget + <~70 MB + 0 xruns). Retained here only as a pointer; the someday-deferral is removed.
- **F2**: Audit/parameterize any Python/UI hardcoded `range(64)` object enumeration so the UI surfaces 65–128 (catalog in C-M5; fix here if found).
- **F3 (measured — does NOT resolve C-M4 alone, so NOT sufficient for in-scope promotion):** lazy-prime of `BinauralMonitor` OlaConvolver slots. C-M4 measurement showed the OlaConvolver term is only ~4–7 MB — F3 would save that, but the dominant memory term is elsewhere (F5). F3 remains a worthwhile but minor follow-up; it does not get C-M4 under 100 MB.
- **F5 (NEW — surfaced by C-M4 measurement; the real D3 blocker):** **WFS (and `chains_`) per-object delay-line footprint.** `WFSRenderer::delays_`/`ramps_` resize to `MAX_OBJECTS × num_speakers` (`WFSRenderer.cpp:24-26`), each `DelayLine` an inline `std::array<float, 48000>` = **187.5 KB** (`DelayLine.h:13,43`). `wfs_` is an UNCONDITIONAL `SpatialEngine` member primed in `prepareToPlay` (`SpatialEngine.cpp:343`) regardless of the active algorithm → **96 MB @64 / 192 MB @128 allocated even when WFS is unused.** This is the dominant RSS term (the §0.4 model wrongly blamed binaural). It makes the **64 baseline already 129 MB** (the <100 MB gate fails pre-Lane-C). **Fix is engine-DSP (lazy/conditional WFS allocation, or size to active objects / only when WFS selected) → needs its own ralplan consensus lane.** Benefits even the current 64 default (would drop 64 to ~33 MB). Until F5 lands, the C-M4 <100 MB gate cannot pass at any cap and the default stays 64.
- **F4**: VST3 host parameter count — confirm the VST3 scaffold (`SPATIAL_ENGINE_VST3`) does not hardcode 64 object parameters (out of NO_JUCE scope; verify when VST3 path is exercised).

---

## C-M7 OUTCOME (measured — evidence-driven decision)

**Decision: DEFAULT STAYS 64.** The C-M7 flip gate (`peakPct ≤ ~35%` AND `RSS < ~70 MB` AND `0 xruns`) is NOT met at 128: measured peak **42–47%** (> 35%) and RSS **~250 MB** (≫ 70 MB). 128 remains a fully-built, RT-verified opt-in (`-DSPATIAL_ENGINE_MAX_OBJECTS=128`).

**What Lane C delivered (C-M1/2/3/5/6 green):** compile-time `{64,128}` option; four object-cap unification (no plane-desync); both configs compile + pass (113/113 NO_JUCE, 117/117 RT-asserts at 128); audioBlock stack hoist (zero new audio-thread stack/alloc at 128); **D1 RT feasibility PROVEN** — 128 objects on the heaviest path hold ~47% peak budget with 0 xruns.

**What is NOT met:** **D3 (C-M4) memory <100 MB FAILS at both caps** — and the 64 baseline already fails (129 MB) due to the pre-existing WFS eager-allocation (F5), which the plan's §0.4 model did not anticipate. This is NOT a 64→128 regression; it is a pre-existing footprint surfaced by the measurement harness. Resolving it (and thus enabling a 128 default or a <100 MB pass) requires the **F5 WFS-memory-remediation lane** (engine-DSP, separate ralplan).

---

## Pre-Mortem (DELIBERATE mode — 3 failure scenarios)

1. **"128 objects blow the RT budget on the binaural path."** At 128 obj + B2 binaural side-output, each object drives a dual-slot OlaConvolver L/R (`BinauralMonitor.h:529-530,554`); doubling object count ~doubles the per-block convolution work. If median or p99 block time exceeds 50% of the 1333 µs budget (48 kHz/64-block) → xruns under load. **Mitigation:** C-M3 MEASURES this with CpuMeter P² p99 + XrunCounter across {8…128} BEFORE the default flips; the gate fails the lane if exceeded; Option A keeps 64 as the safe default and F3 (lazy-prime) as the escape. **Detection:** `perf_obj_block_time` ctest, hard threshold.

2. **"audioBlock stack-array doubling overflows a buffer." — RETIRED in REV 2 by the C-M2 hoist.** Originally the FOUR `std::array<render::ObjectState, MAX_OBJECTS>` inside `audioBlock` (`SpatialEngine.cpp:676-679`) would double from ~5 KB to ~10 KB of audio-callback stack. **Amendment 4 hoists them to engine members**, so at any cap they consume zero audio-thread stack. Residual: C-M6 verifies the hoist (no such local remains) and that `dry_scratch_` (256 KB) stays a heap member. **Detection:** the grep-invariant (no `ObjectState` local in `audioBlock`) + RT-asserts/perf run. This scenario is now a verification step, not an open risk.

3. **"A SPEAKER-dim 64 gets wrongly bumped and corrupts gains."** If C-M2 over-eagerly replaces a `64` that is actually the speaker axis — e.g. the inner `64` in `VBAPRenderer.h:35` / `DBAPRenderer.h:33` ramps, or the outer `64` in `RenderingAlgorithm.h:25` gains, or `kMaxVbapSpeakers` — the renderer's per-speaker gain arrays mis-size, producing out-of-bounds writes / wrong panning / silent gain corruption. **Mitigation:** the §0.2 classification table is the binding contract — C-M2 touches ONLY the four OBJECT-dim caps; a CI grep-invariant asserts no speaker-dim `64` changed; the both-config regression (C-M5) would catch panning corruption. **Detection:** §0.2 table review by Architect/Critic + the renderer unit tests in the both-config ctest.

## Expanded Test Plan (DELIBERATE — unit / integration / e2e / observability)

- **Unit:** `test_p_max_objects_constant` (C-M1, value matches macro); `test_p_object_cap_unify` (C-M2, four caps lockstep, obj 64–127 survive StateModel/echo/crossfade at 128, byte-identical at 64); **B1-guarded** `test_p_max_objects` + `test_p_adm_osc_v1_compat` (boundary semantics, compile+pass at both caps); renderer ramp/gain dimension sanity (speaker-dim `64` unchanged); hoist grep-invariant (no `ObjectState` local in `audioBlock`).
- **Integration:** both-config full NO_JUCE ctest (C-M5) — 64 baseline byte-identical-green, 128 green (this proves B1: 128 compiles); RT-asserts build green at 128 (C-M6).
- **E2E:** **audio-plane** — OSC `/adm/obj/100/aed` datagram → `obj_cache_[100]` → NON-SILENT render at 128 (amendment 3, the "objects 64–127 produce sound" guarantee); `perf_obj_block_time` real-UDP flood {8…128} on the heaviest path (8ch+VBAP+B2) → RT-budget table; `soak_adm_osc_flood` re-parameterized to flood at the configured cap (60 s, xruns 0).
- **Observability:** CpuMeter `peakPct()` (HARD gate) + `cpuPct()`/`p99Us()` (tracking) (`CpuMeter.h:79,91-93`) + XrunCounter (HARD gate) sampled into the RT-budget table; `getrusage` max-RSS into the memory table (drives the F3-promotion + default-flip evidence gates); the dashboard (Lane A) already surfaces these scalars — confirm it reads sane values at 128 (no UI 64-cap, F2).

---

## Progress Tracker

- [x] **C-M1** — `SPATIAL_ENGINE_MAX_OBJECTS` cmake option + `Constants.h` macro; `test_p_max_objects_constant`; default build unchanged, 128 configures.
- [x] **C-M2** — Unify the four object caps (`Constants.h` / `SceneCrossfade.h:33` / `StateModel.h:15` / `EchoSubscriber.h:51`) to one source; **B1: guard `test_p_max_objects.cpp:14-15,71-87` + `test_p_adm_osc_v1_compat.cpp:167-190` to boundary semantics (128 compiles/passes)**; **amendment 4: hoist the four `ObjectState` scratch arrays from `SpatialEngine.cpp:676-679` to engine members**; `test_p_object_cap_unify` (audio-plane non-silent + control/echo/scene-plane, obj 64–127 at 128, byte-identical at 64); grep-invariants: no stray object-dim `64`, no `ObjectState` local in `audioBlock`.
- [x] **C-M3** — `perf_obj_block_time` harness + `docs/RT_BUDGET_MAX_OBJECTS.md`; RT-budget table {8…128}; 128 heaviest **peakPct ≤50% budget AND xruns 0** (hard backstops) + median/p99-estimate; soak re-parameterized.
- [~] **C-M4** — Memory measured (getrusage max-RSS): 129 MB@64 / 250 MB@128 — **<100 MB gate FAILS at BOTH caps** (pre-existing, NOT a 64→128 regression). Root cause = WFS eager per-object delay-lines (**F5**), not binaural (§0.4 model was wrong). F3 insufficient. Memory table + root-cause in docs/RT_BUDGET_MAX_OBJECTS.md. **Resolution deferred to F5 ralplan lane.**
- [x] **C-M5** — Full regression at BOTH 64 and 128 (ctest both configs green incl. B1-guarded tests + pytest); cross-language 64-literal audit.
- [x] **C-M6** — RT-asserts `build_rton_obj128` green; zero new audioBlock alloc at 128; hoist verified (no stack ObjectState); D1(peakPct+xrun)/D3 re-verify; audio-path diff = ∅ (only constant differs).
- [x] **C-M7** — **Evidence-gated default flip (≤~35% budget + <~70 MB + 0 xruns → 128, else 64)** + Docs + ADR amendment; README links build flag + RT budget.

---

## Changelog — REV 1 (initial consensus draft)

- Corrected the STALE outline path `core/src/util/Constants.h` → **`core/src/core/Constants.h:12`** (verified; the util path does not exist).
- Produced the §0.2 classification table for EVERY `64`/`[64]` in `core/src` (object vs speaker vs unrelated).
- **Critical finding (load-bearing):** FOUR independent OBJECT-dim caps — canonical `MAX_OBJECTS`, `spe::scene::MAX_OBJECTS` (`SceneCrossfade.h:33`), `STATE_MAX_OBJECTS` (`StateModel.h:15`), `kEchoMaxObjects` (`EchoSubscriber.h:51`). Bumping only the canonical silently caps StateModel/echo/crossfade at 64 → C-M2 BLOCKER.
- Confirmed VBAP/DBAP `ramps_` inner `64` + `RenderingAlgorithm.h:25` outer `64` are SPEAKER-dim (leave); object dim is the `spe::MAX_OBJECTS` axis (bumps for free).
- Confirmed `BinauralMonitor.h:554 obj_slots_[MAX_OBJECTS]` resolves to canonical `spe::MAX_OBJECTS` (includes `core/Constants.h`, no shadowing).
- Memory math (§0.4): `chains_` DelayLine term dominates (~24→48 MB); OlaConvolver pairs ×128 second; RSS gate < 100 MB.
- RT instrument confirmed present: CpuMeter P² p99 (`CpuMeter.h:91-93`) + XrunCounter — RT-budget table is measurable today.
- Soak harness `N_OBJ=64` literal (`soak_adm_osc_flood.cpp:74`) flagged for parameterization.
- **Mode: DELIBERATE** (RT + memory ~doubling on the binaural path + four-cap silent-corruption risk) → pre-mortem (3) + expanded test plan included.
- Recommended **Option A** (compile-time option, default 64, evidence-driven flip).

**Status: REV 1 ready for Architect (SOUND? scrutinize D1 RT-budget feasibility, the audioBlock stack-array doubling at `SpatialEngine.cpp:676-679`, and the four-cap unification completeness) + Critic review.**

## Changelog — REV 1 → REV 2 (Architect amendments — SOUND WITH AMENDMENTS)

Architect verdict: **SOUND WITH AMENDMENTS**. Option A and the four-cap finding CONFIRMED. No fifth runtime cap exists (Architect independently grepped ADM/OSC decode, shm ring, `CueEngine`, `SceneSnapshot`, `Command` payloads — none found; `SceneSnapshot` uses unbounded `std::vector` so no persisted-format break; `CommandDecoder.cpp:192` is lower-bound only; `SharedRingBackend` 1..64 is channel-dim). Confirmation baked into §0.5.

- **B1 (NEW BLOCKER — the 128 build did not compile/pass):** existing tests hard-pin 64. Added explicit C-M2 tasks (by file:line) to guard them: `test_p_max_objects.cpp:14-15` `static_assert(MAX_OBJECTS==64)` → value-agnostic / `#if SPE_MAX_OBJECTS`; `test_p_max_objects.cpp:71-87` (obj_id==64 rejected) and `test_p_adm_osc_v1_compat.cpp:167-190` (obj 64/65 out-of-range) → **boundary semantics** (`obj_id==MAX_OBJECTS` rejected, `MAX_OBJECTS-1` accepted) so BOTH configs compile and pass. C-M5 runs them at both caps. Tracker + Acceptance updated.
- **B2 (framing fix — correctness rationale was mis-stated):** the audio path does NOT read StateModel — it drains the OSC FIFO DIRECTLY into `obj_cache_` at `SpatialEngine.cpp:473-478`, gated only by `MAX_OBJECTS` (`:477`). StateModel is a parallel CONTROL-side store. Reframed the four-cap bug as a **control/echo/scene-plane consistency desync** (not "audio reads StateModel") in §0.2 CONSEQUENCE, §0.5, D2, and ADR Alternatives. C-M2 scope unchanged (still unify all four).
- **Amendment 3 (audio-plane D2 test):** added an audio-plane test alongside the control-plane one — OSC `/adm/obj/100/aed` datagram must reach `obj_cache_[100]` and render **NON-SILENT** at 128 (proves the `:473-478` drain). Added to D2, C-M2 Gate, Expanded Test Plan.
- **Amendment 4 (HOIST the audio-callback stack arrays):** the four `std::array<render::ObjectState, MAX_OBJECTS>` at `SpatialEngine.cpp:676-679` (~10 KB stack at 128) are hoisted to engine members in C-M2 (zero runtime cost), verified in C-M6. **Pre-mortem scenario 2 retired** (now a verification step). Updated §0.3, C-M2, C-M6, pre-mortem.
- **Amendment 5 (harden the RT gate):** `p99Us()` is a P² estimate (returns last sample until warm, can under-report a tail). Gate now uses **`peakPct()` (true max, `CpuMeter.h:79`) ≤ 50% budget AND `XrunCounter == 0`** as HARD backstops alongside the p99 estimate. Updated D1, C-M3, C-M6, Expanded Test Plan.
- **Amendment 6 (evidence-conditional default-flip IN-LANE):** moved the default-flip out of someday-F1 into an explicit C-M7 gate — flip to 128 iff `peakPct ≤ ~35%` budget AND RSS `< ~70 MB` AND zero xruns; else keep 64. Both-config machinery + 64 baseline retained regardless. C-M7 rewritten; ADR Decision/Consequences/F1 updated.
- **Amendment 7 (C-M4 can promote F3 into scope):** `BinauralMonitor::primeAllSlots()` (`BinauralMonitor.h:558`) primes ALL 128 OlaConvolver quads unconditionally when B2 is on — a FIXED D3 cost. If measured RSS ≥ ~80 MB (within ~20% of the 100 MB ceiling), C-M4 promotes F3 (lazy-prime) from follow-up into Lane C scope. Updated C-M4, ADR Consequences, F3.

**Mode: remains DELIBERATE.** The amendments are localized, test-catchable additions (B1 boundary rewrites, a hoist, gate-hardening, an evidence gate) — not a scope/risk explosion. Pre-mortem retains scenarios 1 (RT budget) and 3 (speaker-dim mis-bump); scenario 2 retired by the hoist.

**Status: REV 2 — all Architect amendments applied (B1, B2, 3, 4, 5, 6, 7 + no-fifth-cap confirmation). 128 build now compiles (B1 boundary-guarded) and the default-flip is evidence-gated in C-M7. Ready for Critic final pass.**

## Critic final pass — APPROVE (consensus reached)

Critic verdict: **APPROVE** (no REV 3). All 8 checks CONFIRMED against source at HEAD; all seven Architect amendments verified landed and runnable. Notably the Critic independently confirmed `CueEngine.cpp:36` (`o.id >= MAX_OBJECTS`) + loops `:162,203,220` resolve to `scene::MAX_OBJECTS` (cap #4) and so bump for free under C-M2 — **no fifth literal**. Speaker-dim `64`s correctly excluded. B1 boundary rewrites confirmed to compile+pass at both caps (incl. the OSC-address literals `test_p_adm_osc_v1_compat.cpp:179,196`). RT gate honestly demotes the P² p99 estimate to informational with `peakPct`+xrun authoritative.

**Execution conditions (must hold during autopilot):**
1. **C-M2 is a hard blocker gate** — the 128 build must COMPILE (B1 `static_assert` + OSC-address literals rewritten) before any later milestone is claimed green. Never advance on a non-compiling 128 config.
2. **Touch ONLY the four object-dim caps** — run the speaker-dim grep-invariant after C-M2; any change to `RenderingAlgorithm.h:25` outer `64`, VBAP/DBAP ramp inner `64`, or speaker-count constants is a hard stop (pre-mortem scenario 3).
3. **RT gate authority = `peakPct ≤ 50%` AND `xruns == 0`** — never pass C-M3/C-M6 on a low p99 estimate alone (P² warmup blind spot, `CpuMeter.h:120-128`).
4. **Both-config regression non-negotiable** — full NO_JUCE ctest green at 64 (byte-identical baseline) AND 128 before C-M7; a 64-baseline regression is a stop-and-replan signal.
5. **D2 audio-plane test must use NON-SILENT input** for obj 100, else the non-silent assertion is meaningless.
6. **C-M7 default-flip is conditional** — flip to 128 only if measured `peakPct ≤ ~35%` + RSS `< ~70 MB` + `0 xruns`; otherwise leave default 64. Do not flip on optimism.
7. **Tag only on explicit user request**; commit per milestone after both-config CI is green. Record `nproc`/CPU model + note `ru_maxrss` is process-wide high-water in `RT_BUDGET_MAX_OBJECTS.md`.

Minor (non-blocking): C-M3 thresholds are Linux-8-core-specific (disclosed in doc); `ru_maxrss` includes harness baseline (gate has margin); UI `range(64)` audit may defer to F2 (not on RT/correctness path). Grep-invariant runs config-agnostic on source at both caps.

**CONSENSUS REACHED — cleared for autopilot. C-M1..C-M7 unchecked.**
