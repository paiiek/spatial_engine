# Spatial Engine v0.9 — Lane F5: WFS / DelayLine memory remediation — REV 3 (Critic: accuracy corrections; Architect SOUND, design unchanged)

> **CONSENSUS REACHED (ralplan) — 2026-06-01.** ✅ Planner REV1 → ✅ Architect SOUND-w/-amendments → ✅ Planner REV2 → ✅ Critic ITERATE(footprint accuracy) → ✅ Planner REV3 → ✅ **Critic APPROVE**. Critic re-did §0.7 arithmetic from first principles (matches to the digit: @128 ≈ 108.2 MB, @64 ≈ 58.1 MB), verified all 4 citation fixes against source, confirmed devil's-advocate math (11.7 MB) and F5-M6 no-blanket-flip. Zero scored findings survived. **Ready for autopilot execution (F5-M1 → F5-M6).**

> Consensus (ralplan) draft. Lane C (MAX_OBJECTS 64→128) is implemented and its
> RT-budget/memory evidence is committed at `docs/RT_BUDGET_MAX_OBJECTS.md`
> (measurement commit `32f5b55`). Lane C's C-M4 memory gate **FAILED at BOTH caps**
> (64 = ~129 MB, 128 = ~250 MB, ceiling 100 MB) and explicitly deferred the DSP
> remediation to a re-planned follow-up — *this* lane (F5). Outline source: Lane C
> `docs/RT_BUDGET_MAX_OBJECTS.md` §"The dominant term the §0.4 plan model MISSED"
> and §"F3 (lazy-prime) promotion decision".
>
> This plan grounds F5-M1..F5-M6 in the actual code at HEAD `4539873`.
>
> **Mode: DELIBERATE.** This touches a hot DSP primitive (`dsp::DelayLine`,
> instantiated on the audio render path) and changes the per-cell delay buffer
> capacity that the WFS/propagation/user-delay/speaker-align audio paths read
> through every block. A too-small capacity silently produces WRONG audio (read
> wraps modulo, no crash) at large venue × high sample rate; a templating mistake
> can break the propagation/user-delay paths. Two of three drivers (D1 footprint,
> D3 correctness) are gates that only fail at scale or at a venue extreme. That
> clears the high-risk bar → §"Pre-Mortem" (3 scenarios) and §"Expanded Test Plan"
> are included per the DELIBERATE protocol. Justification at end of RALPLAN-DR.

---

## 0. Grounding — what actually exists today (file:line evidence)

This is the load-bearing context. Read it before disputing any milestone scope.

### 0.1 The dominant footprint term — WFS `delays_` (the headline)

- `WFSRenderer::delays_` is `std::vector<spe::dsp::DelayLine>` (`core/src/render/WFSRenderer.h:37`), with a sibling `ramps_` `std::vector<spe::dsp::GainRamp>` (`:38`).
- It is resized to **`spe::MAX_OBJECTS × num_speakers`** in `prepareToPlay` (`core/src/render/WFSRenderer.cpp:24-26`: `const int total = spe::MAX_OBJECTS * num_speakers_; delays_.resize(total); ramps_.resize(total);`).
- Each `spe::dsp::DelayLine` carries `std::array<float, DELAY_LINE_MAX_SAMPLES>` with `DELAY_LINE_MAX_SAMPLES = 48000` (`core/src/dsp/DelayLine.h:13,43`) = **187.5 KB**.
- `wfs_` is an **UNCONDITIONAL** `SpatialEngine` member (`core/src/core/SpatialEngine.h:369` `render::WFSRenderer wfs_;`), primed in `prepareToPlay` (`core/src/core/SpatialEngine.cpp:343` `wfs_.prepareToPlay(layout_, sample_rate);`) **regardless of whether WFS is ever the active algorithm**.
- Therefore @8 speakers: **64 × 8 × 187.5 KB ≈ 96 MB; 128 × 8 × 187.5 KB ≈ 192 MB** — allocated up-front, always. A 24-speaker layout makes this ~288 MB @64 / ~576 MB @128 (linear in speaker count).

**This is empirically the dominant term.** The committed Lane C doc model (`docs/RT_BUDGET_MAX_OBJECTS.md:148` table) measured `WFSRenderer::delays_` at **~96 MB @64 / ~192 MB @128**, vs `chains_` at ~23.5 MB / ~46.9 MB and binaural OlaConvolvers at only ~3.5 MB / ~7 MB; model and measured RSS agree within ~1% (124.6 MB vs modelled ~124 MB @64; 250.4 MB vs ~248 MB @128). The Lane C doc's own remediation note (its §"The real remediation targets") names exactly this term first.

### 0.2 The WFS delay magnitude is geometry-bounded — and the bound is SMALL

- WFS delay per (object,speaker) cell: `delay_samples = r / spe::SOUND_C * sr_` (`core/src/render/WFSRenderer.cpp:80`), where `r` = source-to-speaker Euclidean distance (`:67-77`), clamped `r = std::max(r, 0.01f)` (`:78`). `spe::SOUND_C = 343.0f` (`core/src/core/Constants.h:32`).
- The 48000-sample buffer corresponds to `48000 / 48000 × 343 = 343 m` of delay @48 kHz — absurd for any real venue.
- **Source distance is HARD-BOUNDED at 20 m** over the wire: `ADM_OSC_MAX_DIST = 20.0f` (`core/src/ipc/AdmOscConstants.h:12`), enforced by a `static_assert` identity check (`core/src/ipc/CommandDecoder.cpp:11-13`), and the ADM/OSC decode multiplies normalised distance by this cap (`CommandDecoder.cpp:188,223,228,693,714`). The header marks it a **v0 contract — "do NOT change without ADR amendment"** (`AdmOscConstants.h:3`). So `objects[obj].dist_m ≤ 20 m` on every supported input path.
- Source-to-speaker `r` = source distance + speaker-array radius. Even with a very large array (say speakers up to ~30 m out) and a 20 m source on the far side, `r` realistically stays well under 50 m.
- **Worst-case delay-sample budget** = `r_max / SOUND_C × sr_max`. There is **NO sample-rate upper validation anywhere** (grep of `SpatialEngine.cpp` finds no `sample_rate >`/cap; the daemon just forwards `--rate`, default 48000, `core/src/bin/spatial_engine_core.cpp:354,390`). So the capacity derivation must explicitly assume a max supported SR. Taking `r_max = 50 m` and `sr_max = 96 kHz`: `50/343 × 96000 ≈ 13,994 samples`. At the realistic 48 kHz / 25 m: `25/343 × 48000 ≈ 3,499 samples`. **The current 48000 is ~14–100× oversized for WFS.**

### 0.3 THE LOAD-BEARING SAFETY POINT — `processSample` does NOT clamp `delay_samples` to capacity

- `DelayLine::processSample` (`core/src/dsp/DelayLine.h:23-38`): `rd = write_ - delay_samples; while (rd<0) rd += MAX; ri0 = (int)rd % MAX; ri1 = (ri0+1) % MAX; …; write_ = (write_+1) % MAX;`.
- If `delay_samples > capacity`, the read index wraps modulo the buffer and yields **garbage audio** — NO out-of-bounds (the modulo keeps it in-bounds, no crash, no UB) but the wrong sample. Today a too-large `delay_samples` is "safe" only because the buffer is 343 m deep so no real input reaches it.
- **The only existing clamp is in `PropagationDelay::setDistance`** (`core/src/dsp/PropagationDelay.h:28-29`: `if (target > DELAY_LINE_MAX_SAMPLES-1) target = DELAY_LINE_MAX_SAMPLES-1;`). **WFS has NO such clamp** — `WFSRenderer.cpp:80` feeds `delay_samples` straight into `delay.processSample(src[n], delay_samples)` (`:100`).
- **CONSEQUENCE (load-bearing):** any capacity reduction MUST add a clamp at the WFS call site (and/or at `DelayLine::processSample`). Recommended: clamp `delay_samples = min(delay_samples, capacity - 2.f)` (−2 to keep room for the `ri0+1` interpolation tap and the frac). For in-range delays the audio is bit-identical; only delays that exceed capacity (which today already alias at 343 m, just at a much larger threshold) are affected. Document the max supported venue × SR alongside the chosen `WFS_MAX_DELAY_SAMPLES`.

### 0.4 Blast radius — `DELAY_LINE_MAX_SAMPLES` is GLOBAL; enumerate EVERY `dsp::DelayLine` user

Every instantiation of the global `spe::dsp::DelayLine` (`std::array<float,48000>`), from `grep -rn 'DelayLine' core/src`, with its REAL delay need:

| # | site | file:line | real delay need | footprint role |
|---|---|---|---|---|
| 1 | **WFS** `delays_` (`MAX_OBJECTS × num_speakers`) | `WFSRenderer.h:37`, `.cpp:24-26,80,100` | geometry r/c — **≤ ~14 k samples** (§0.2), tiny | **DOMINANT** (~96/192 MB) — must shrink |
| 2 | `PerObjectChain::user_delay_` (1 line/object, in `chains_`) | `PerObjectChain.h:84`; `:50-51`; `SpatialEngine.cpp:405` | **user-settable** `user_delay_ms` (`PerObjectChain.h:19`, set at `SpatialEngine.cpp:562`) → `ud_samples = user_delay_ms × sr × 0.001` (`PerObjectChain.h:50`). May legitimately want up to ~1 s. **No clamp today.** | secondary (~12/24 MB of the chains_ ~23.5/46.9 MB) |
| 3 | `PerObjectChain` `prop_.delay_` (the `PropagationDelay`, 1 line/object) | `PropagationDelay.h:50`; `PerObjectChain` holds a `PropagationDelay` | geometry — already CLAMPED at `PropagationDelay.h:28-29`; r ≤ 20 m → ≤ ~2800 samples @48k | secondary (other ~12/24 MB of chains_) — also oversized |
| 4 | `SpatialEngine::spk_delays_` (`std::vector<DelayLine>`, 1/speaker) | `SpatialEngine.h:434`; `.cpp:359-363`, `:369-370`, `:746` | **user-settable (layout `delay_ms`), keep large — SAME CLASS as #2.** `spk_delay_samples_[i] = delay_ms × 0.001 × sr` (`SpatialEngine.cpp:369-370`) where `delay_ms` is loaded RAW from layout YAML (`core/src/geometry/LayoutLoader.cpp:95`, no upper bound) and fed UNCLAMPED into `spk_delays_[spk].processSample(s, d)` (`:746`). 16384@48k = only ~341 ms — real venue speaker-alignment can exceed that. **Must keep `DelayLine<48000>`.** | minor (per-SPEAKER not per-object → ~8 lines × 187.5 KB ≈ 1.5 MB; keeping it large barely moves the total) |

- **`FdnReverb` does NOT use the global `dsp::DelayLine`.** It defines its OWN private `struct DelayLine { std::vector<float> buf; … }` (`core/src/reverb/FdnReverb.h:50-55`), heap-sized to mutually-prime lengths ~1499–3001 samples (`:41-44`). So **reverb is NOT in the blast radius** of a `dsp::DelayLine` change — a key narrowing vs the original concern. (It is correctly listed as a non-`dsp::DelayLine` user.)
- **CONSEQUENCE:** a blanket reduction of the global `DELAY_LINE_MAX_SAMPLES = 48000` would break **user_delay (#2) AND spk-align (#4)** — the TWO users that legitimately need a long (~1 s) buffer (both are user-settable & UNCLAMPED: #2 from `user_delay_ms` over OSC, #4 from layout `delay_ms` YAML, §0.4 rows). The fix must let WFS (#1) and propagation (#3) use SMALL lines while user_delay (#2) and spk-align (#4) keep large ones. Propagation (#3) is geometry-bounded (clamped) and IS shrunk; the "keep large" constraint applies to BOTH #2 and #4. (REV 2 / amendment 1: #4 was originally mis-classified as geometry-bounded — it is NOT; corrected here.)

### 0.5 RT invariant today — alloc-once at prepareToPlay; audioBlock alloc-free

- `WFSRenderer::prepareToPlay` (`WFSRenderer.cpp:10-31`) does the only allocation (`delays_.resize`, `ramps_.resize`); the comment `WFSRenderer.h:5` states *"Delay lines allocated on heap at prepareToPlay (RT-safe thereafter)."* `processBlock` (`:33-105`) is alloc-free (writes into the pre-sized vectors, `std::memset` the output, `delay.processSample` per sample). This invariant is non-negotiable.
- `SpatialEngine::chains_` `prepareToPlay` loop (`SpatialEngine.cpp:405`) and `spk_delays_` (`:359-363`) likewise allocate once. The audio callback `audioBlock` is alloc-free TODAY (Lane C already hoisted the four `ObjectState` scratch arrays to engine members — `wfs_objs_` etc. at `SpatialEngine.h:424`).
- `dsp::DelayLine` is `std::array` (fixed-size, RT-safe **by construction** — zero heap, capacity known at compile time, no per-block alloc). Any fix MUST preserve this "RT-safe by construction" property for the audio-read path, or move heap-sizing strictly into `prepareToPlay`.

### 0.6 Build / test substrate (reused, not rebuilt)

- **Footprint harness EXISTS:** `core/tests/perf/perf_obj_block_time.cpp` — heaviest config (8ch circular layout `make8chLayout()` `:69-89`, VBAP default + binaural side-output), sweeps object counts, reads max-RSS via `getrusage(RUSAGE_SELF).ru_maxrss` (`:126-127`). The ceiling CONSTANT is `kRssCeilingKB = 100*1024` (`:65`); the gate is ENFORCED at `:302-307` (`rss_ok = rss_at_cap_kb < kRssCeilingKB; if (!rss_ok) pass = false;`) and the process returns `1` at `:326` (`return pass ? 0 : 1;`). The peak gate is `kGatePeakUs = 0.50 × kBudgetUs` (`:62`). **It already exits non-zero on the RSS gate at both caps today** (by design — the committed evidence). F5 makes it PASS. Built under `Release`/`SPATIAL_ENGINE_RUN_SOAK` (`core/tests/perf/CMakeLists.txt`).
- **WFS unit test EXISTS:** `core/tests/core_unit/test_p3_wfs.cpp` — Huygens analytic check (per-speaker delay r/c, gain 1/sqrt(r), tol 1e-3), tight linear arrays (`make_tight_linear`, 10 mm spacing). This is the WFS audio-correctness regression for in-range delays. F5 must keep it green AND add a large-venue clamp case.
- **PropagationDelay sweep test EXISTS:** `core/tests/core_unit/test_p3_propdelay_sweep.cpp` (SR 48000, `:30`) — the propagation-delay audio regression.
- **DelayLine** has no dedicated standalone unit test (it is exercised via WFS/propdelay/chain tests) — F5-M5 adds a direct capacity/clamp unit test.
- cmake options: `SPATIAL_ENGINE_NO_JUCE` (`core/CMakeLists.txt:32`), `SPATIAL_ENGINE_RT_ASSERTS` (`CMakeLists.txt:43` → `SPE_RT_ASSERTS=1`), `SPATIAL_ENGINE_MAX_OBJECTS ∈ {64,128}` (Lane C, → `SPE_MAX_OBJECTS`). RT-asserts alloc test `p1_rt_no_alloc` (`core/tests/core_unit/CMakeLists.txt:106`). Build dirs: `core/build` (64), `core/build_rton` (RT-asserts), `core/build_rel`/`core/build_obj128_rel` (Release/perf, per the doc's reproduce block).

### 0.7 The capacity arithmetic (the number F5 must commit to)

| scenario | r_max | sr_max | delay_samples | rounded capacity |
|---|---:|---:|---:|---:|
| realistic (venue ≤25 m, 48 kHz) | 25 m | 48000 | 3,499 | — |
| generous (venue ≤50 m, 96 kHz) | 50 m | 96000 | 13,994 | **16384** (2^14) |
| extreme (venue ≤50 m, 192 kHz) | 50 m | 192000 | 27,988 | 32768 (2^15) |

**Recommended `WFS_MAX_DELAY_SAMPLES = 16384`** (covers 50 m @96 kHz with margin; with the §0.3 clamp, anything beyond is gracefully clamped rather than wrong). 16384 × 4 B = **64 KB/line** vs 187.5 KB → **2.86× reduction**. Per-cell from 187.5 KB → 64 KB.

**Estimated post-fix footprint — HONEST arithmetic (REV 3 / Critic correction).** Per-line saving from 48000→16384 = `(187.5 − 64) KB = 123.5 KB`. **Only WFS (per-object×speaker) and propagation (per-object, #3) shrink; `user_delay_` (#2) and `spk_delays_` (#4) are KEPT at 48000** (so they contribute ZERO saving — the earlier ~94 MB headline wrongly implied a user_delay-side saving and is corrected here):

| term | line count | per-line Δ | @64 Δ saved | @128 Δ saved |
|---|---:|---:|---:|---:|
| WFS `delays_` (MAX_OBJECTS × 8 spk) | 512 / 1024 | 123.5 KB | ~63.2 MB | ~126.4 MB |
| propagation `prop_.delay_` in chains (1/object, #3) | 64 / 128 | 123.5 KB | ~7.7 MB | ~15.4 MB |
| `user_delay_` (#2) — KEPT large | — | 0 | 0 | 0 |
| `spk_delays_` (#4, per-speaker) — KEPT large | — | 0 | 0 | 0 |
| **total Δ saved** | | | **~70.9 MB** | **~141.8 MB** |
| total RSS today (measured) | | | 129 MB | 250 MB |
| **est. total RSS after F5-M1..M3** | | | **~58 MB** | **~108 MB** |

**The honest @128 estimate is ~108 MB — STILL OVER the 100 MB gate** (250 − 126.4 WFS − 15.4 prop ≈ 108.2 MB). The @64 estimate is ~58 MB — **comfortably under 100 MB (64 is fine)**. (A coarser rounding using the §0.4-doc figures — WFS 192→64 MB = 126 saved, chains 46.9→31.4 MB = 15.5 saved — gives 250 − 126 − 15.5 ≈ **~108.5 MB @128**; the two methods agree at ~108 MB.) **It is 128, not 64, that fails after WFS+propagation right-sizing — clearing it REQUIRES the F5-M3b Option-C work (or shrinking `user_delay_`, see the devil's-advocate below).** The user-settable large lines are deliberately KEPT at 48000: `user_delay_` (#2) and `spk_delays_` (#4) — both UNCLAMPED user delays (§0.4, REV 2 amendment 1).

**Devil's-advocate (REV 3 / Critic) — could shrinking `user_delay_` to 0.5 s (24000) clear @128 WITHOUT Option C's TSan/race work?** Shrinking `user_delay_` 48000→24000 saves `(187.5−93.75) KB × 128 ≈ 11.7 MB` → ~108 − 11.7 ≈ **~96 MB @128**, which WOULD clear the 100 MB gate with a one-line capacity change and no concurrency work. **REJECTED:** `user_delay_` is a user-settable delay param (`user_delay_ms` over OSC, `PerObjectChain.h:19`, `SpatialEngine.cpp:562`) with NO documented 0.5 s contract; halving its range to 0.5 s is a silent FUNCTIONAL REGRESSION (a user requesting 0.7 s would be clamped) — the same class of defect REV 2 amendment 1 fixed for `spk_delays_`. We do NOT trade a user-facing capability for a memory win. **Therefore F5-M3b (Option C) is the chosen path to clear @128.** (Flagged for the Architect/Critic: if a 0.5 s `user_delay_` cap is ever deemed acceptable by product, it is a cheaper alternative to Option C — but absent that explicit sign-off, it is rejected.)

**Why keeping `spk_delays_` large does NOT change the estimate (REV 3):** `spk_delays_` is **per-SPEAKER, NOT per-object** — its footprint is `num_speakers × 187.5 KB ≈ 8 × 187.5 KB ≈ 1.5 MB` at any object cap (it does NOT double 64→128). Shrinking it to 16384 would save only ~1 MB — **negligible**. So the keep-large decision (REV 2 amendment 1) is footprint-neutral; the WFS (per-object×speaker) + propagation (per-object) shrinks are the only terms that move the number. **Bottom line: @64 ≈ 58 MB CLEARS the 100 MB gate without Option C; @128 ≈ 108 MB does NOT — the 128 gate requires F5-M3b (Option C), per the honest table above.** The < 70 MB C-M7 flip target @128 is reachable only with Option C AND a WFS-inactive deployment (§F5-M6). Exact post-fix numbers are MEASURED at F5-M4, not asserted here.

---

## RALPLAN-DR Summary (for Architect / Critic)

### Principles
1. **RT-safe-by-construction stays.** The audio-read path (`DelayLine::processSample`, WFS `processBlock`) adds NO allocation/lock/new branch in the audio callback; all (re)sizing stays once-in-`prepareToPlay` (`WFSRenderer.cpp:24-26`, `SpatialEngine.cpp:359-405`). If a `std::array` becomes a `std::vector`, the vector is sized exactly once at prepare and never resized on the audio thread.
2. **Right-size per use, not blanket.** WFS (geometry, ≤~14 k) and propagation (geometry, ≤~2.8 k) get SMALL capacity; user_delay (user param, ~1 s) keeps LARGE. The global `DELAY_LINE_MAX_SAMPLES = 48000` is NOT reduced blanket (§0.4 — that breaks user_delay). Capacity is a per-use property.
3. **Correctness first — clamp before you shrink.** No capacity reduction ships without the §0.3 clamp (`delay_samples = min(delay_samples, capacity-2)`) at every shrunk site. In-range audio stays bit/numerically identical; only over-capacity delays (which already alias today, just at 343 m) change — and they change from "wrong-because-wrapped" to "clamped-to-max" (strictly safer).
4. **Measure, don't assert by inspection.** The post-fix RSS at 64 and 128 (heaviest config) is MEASURED via the existing `perf_obj_block_time` harness (`getrusage`), with the hard < 100 MB gate it already enforces — not eyeballed.
5. **Additive, regression-clean.** Full NO_JUCE ctest + RT-asserts green at BOTH 64 and 128. The WFS Huygens test, propdelay sweep, and user_delay behavior unchanged for in-range delays.

### Decision Drivers (top 3)
1. **D1 — Footprint < 100 MB at 128 (ideally < 70 MB).** Max-RSS (heaviest config: 128 obj + 8ch + VBAP + binaural side-output) must drop below the 100 MB ceiling the `perf_obj_block_time` harness already gates (enforced at `perf_obj_block_time.cpp:302-307`, returns 1 at `:326`). The WFS `delays_` term (~96/192 MB) is THE dominant contributor (§0.1) and must shrink; the propagation `chains_` line shrinks too. **Honest est. (§0.7): WFS+propagation right-sizing alone lands @64 ~58 MB (clears) but @128 ~108 MB (STILL OVER) → the F5-M3b Option-C work is LIKELY REQUIRED to clear 128.** Ideal sub-target: < 70 MB so Lane C's C-M7 default-flip becomes re-evaluable (unreachable @128 without Option C — §F5-M6).
2. **D2 — RT-safety preserved.** No allocation/lock/new branch added to `audioBlock` or `DelayLine::processSample`/`WFSRenderer::processBlock` on the audio thread; per-cell (re)sizing stays in `prepareToPlay`; `p1_rt_no_alloc` (RT-asserts) green at 128. Adding a single `min()` clamp on the audio path is a branchless `std::min` (acceptable — it is not a lock/alloc and replaces an implicit-wrap with an explicit bound).
3. **D3 — Correctness: WFS delay never exceeds capacity; audio unchanged for in-range delays.** The §0.3 clamp is added; the WFS Huygens analytic test (`test_p3_wfs.cpp`) and propdelay sweep stay green (bit/numerically identical for r within capacity); a NEW large-venue test proves a 50 m source × high-SR delay is clamped (not wrapped to garbage), with graceful degradation (max delay) rather than wrong audio.

### Viable Options

**How is the per-cell delay capacity right-sized?**

- **Option A — Template `DelayLine<int Capacity = DELAY_LINE_MAX_SAMPLES>` on compile-time capacity (CHOSEN, combined with the clamp).** WFS uses `DelayLine<WFS_MAX_DELAY_SAMPLES (16384)>`; propagation uses a small capacity too; user_delay keeps `DelayLine<48000>` (the default template arg = today's value, so existing users are source-unchanged). Still `std::array` → RT-safe **by construction**, zero runtime cost, no heap on the audio path. Add the §0.3 clamp inside `processSample` (`delay_samples = min(delay_samples, Capacity-2)`) so it is correct for ALL instantiations for free.
  - **Pros:** kills the dominant term with a fixed-size `std::array` guarantee intact (Principle 1 met by construction); per-use right-sizing (Principle 2); the clamp lives in one place and protects every user (Principle 3); the default template arg keeps user_delay/spk-align source-compatible (minimal blast radius); reverb is untouched (it has its own DelayLine, §0.4).
  - **Cons:** `DelayLine` becomes a template → its header is included by 4 users; each instantiation site must name its capacity (a typed find/replace, mechanical). The WFS capacity is a compile-time bet on `sr_max` — mitigated by the runtime clamp (§0.3): if SR×venue ever exceeds 16384, audio clamps gracefully instead of going OOB. Must pick the number (§0.7 → 16384).

- **Option B — Runtime heap-backed `DelayLine` (`std::vector`, sized at `prepareToPlay` from actual `sr_` × a max-distance constant).** One type; per-instance capacity = `ceil(r_max/SOUND_C × sr_) + margin`, sized exactly once at prepare.
  - **Pros:** capacity tracks the ACTUAL sample rate (no compile-time SR bet); one type, no template; WFS line sized precisely (e.g. only ~3.5 k @48 kHz → even smaller footprint than Option A's 16384).
  - **Cons:** changes `dsp::DelayLine` from `std::array` (RT-safe by construction) to `std::vector` for ALL users — the audio read now dereferences a heap pointer (same RT-safety *in practice* since no resize on the audio thread, but it removes the compile-time fixed-size guarantee and the `p1_rt_no_alloc` story leans on "no resize ever happens" rather than "cannot allocate"). Larger blast radius on the hottest DSP primitive; the `% MAX` becomes `% length_` (runtime modulo, marginally slower). **Invalidated as the primary mechanism:** Principle 1 prefers the fixed-size `std::array` guarantee; Option A achieves the footprint win while KEEPING that guarantee. (Option B's precise-sizing benefit is real but not worth converting every delay user to heap on the audio path; if the Architect judges 16384 too coarse, Option B can be the WFS-only fallback — see invalidation note.)

- **Option C — Lazy / conditional WFS allocation (allocate `delays_` only when WFS first becomes the active algorithm, on the control thread).** Algorithm switches arrive over OSC and are applied per-object in the audio drain (`SpatialEngine.cpp:684-690` sorts objects into `vbap_objs_`/`wfs_objs_` per block) — a control-thread first-use hook would gate `wfs_.prepareToPlay`.
  - **Pros:** zero WFS cost when WFS is never used (a common case — VBAP/binaural default).
  - **Cons:** reduces the CONDITION not the per-line SIZE — if WFS IS used at 128 it is STILL ~192 MB (the gate still fails). Needs a control-thread first-activation hook + careful sync with the audio thread (the algorithm assignment is read on the audio thread; lazy-allocating `delays_` while audio reads it is a race). Does NOT fix the per-line oversize. **Invalidated ALONE:** insufficient for the D1 gate when WFS is active; the pre-mortem scenario 3 (lazy-alloc races the audio thread) is a real hazard. It is a valid ADDITIVE optimization on top of Option A (right-size AND skip when unused) but not a substitute.

- **Option D — Combination: Option A (right-size + clamp) as the core, optionally layering C (skip when WFS unused) if A alone does not clear 128 < 100 MB.** This is the fallback path if measurement at F5-M4 shows 128 still over ceiling after right-sizing WFS + propagation.

**Decision on the user-settable large lines — `user_delay_` (#2) AND `spk_delays_` (#4):** KEEP BOTH at the large (48000 / 1 s) capacity. `user_delay_` is a user-facing delay param (`user_delay_ms`, `PerObjectChain.h:19`, set via OSC at `SpatialEngine.cpp:562`); **`spk_delays_` is ALSO user-settable & UNCLAMPED** (layout YAML `delay_ms`, `core/src/geometry/LayoutLoader.cpp:95` → `spk_delay_samples_ = delay_ms×0.001×sr` `SpatialEngine.cpp:369-370`, fed unclamped at `:746`) — both may legitimately want up to ~1 s (REV 2 amendment 1: #4 was originally mis-classed as geometry-bounded). `user_delay_`'s ~12/24 MB is the only object-scaled large term and is acceptable; `spk_delays_` is per-speaker (~1.5 MB, negligible). The propagation line (#3) inside the chain IS shrunk (geometry-bounded, already clamped). This is the §0.4 "let WFS+propagation shrink while user_delay AND spk-align stay large" requirement, made explicit.

**Conclusion:** **Option A (template + clamp)** is chosen, with the propagation line also right-sized and `user_delay_`/`spk_delays_` kept large. **Option C is LIKELY REQUIRED at 128, not a long-shot fallback** (REV 3 / Critic finding 2): the honest @128 estimate is ~108 MB after Option A alone (§0.7), so Option C (F5-M3b) is expected to be on the critical path to clear the 128 gate (it stays gated on the authoritative F5-M4 measurement). @64 (~58 MB) clears with Option A alone. Option B is the WFS-only fallback if the Architect rejects the compile-time SR assumption. The §0.3 clamp ships regardless of option (it is a correctness fix, not an optimization).

**Mode: DELIBERATE.** Justification: this edits `dsp::DelayLine` — a primitive instantiated on the audio render path and read every sample — and changes the capacity its WFS/propagation audio reads see. A too-small capacity silently yields WRONG audio at venue/SR extremes (no crash → hard to catch, §0.3); a templating mistake can break the propagation/user-delay/spk-align paths (§0.4 blast radius). D1 (footprint) and D3 (correctness) are gates that only fail at scale or at a venue extreme. That clears the high-risk bar → pre-mortem (3 scenarios) + expanded test plan below. (If the Architect judges the change mechanically contained after F5-M2, the Critic may down-mode to SHORT for the final revision — but the clamp + large-venue test must land FIRST.)

---

## Milestones

> RT-safety invariant for **every** milestone: per-cell delay (re)sizing stays confined to `prepareToPlay` (`WFSRenderer.cpp:24-26`, `SpatialEngine.cpp:359-405`); the `audioBlock` / `processBlock` / `processSample` audio path adds NO new allocation/lock; the only added audio-path op is a branchless `std::min` clamp (D2). The §0.3 clamp ships before any capacity shrink. D1/D3 are MEASURED at F5-M4 and re-verified at F5-M6.

### F5-M1 — Right-size `DelayLine` via compile-time capacity template (+ `WFS_MAX_DELAY_SAMPLES`)

**Files**
- Modify `core/src/dsp/DelayLine.h` — convert `class DelayLine` to `template <int Capacity = DELAY_LINE_MAX_SAMPLES> class DelayLine`; replace every `DELAY_LINE_MAX_SAMPLES` use inside the class (`:29,31,32,36,43`) with `Capacity`. Keep `inline constexpr int DELAY_LINE_MAX_SAMPLES = 48000;` (`:13`) as the default arg. Add `inline constexpr int WFS_MAX_DELAY_SAMPLES = 16384;` (§0.7, with a comment deriving it: 50 m × 96 kHz / 343 m·s⁻¹ ≈ 13994, rounded to 2¹⁴). Provide a convenience alias `using DelayLine48k = DelayLine<>;` so the large-buffer users spell their intent.
- Modify `core/src/render/WFSRenderer.h:37` — `std::vector<spe::dsp::DelayLine<spe::dsp::WFS_MAX_DELAY_SAMPLES>> delays_;`.
- **REV 2 / amendment 3 — templating is NOT source-unchanged; these are IN-SCOPE mechanical edits.** A bare `DelayLine x;` no longer names a type (it is now a template). Each large-buffer user must be re-spelled `DelayLine<>` (or the `DelayLine48k` alias):
  - `core/src/dsp/PerObjectChain.h:84` `DelayLine user_delay_;` → `DelayLine48k user_delay_;` (keep large — §0.4 #2).
  - `core/src/core/SpatialEngine.h:434` `std::vector<spe::dsp::DelayLine> spk_delays_;` → `std::vector<spe::dsp::DelayLine48k> spk_delays_;` (**keep large — §0.4 #4, REV 2 amendment 1**).
  - `core/src/dsp/PropagationDelay.h:50` `DelayLine delay_;` → right-sized at F5-M3 (not large) — spelled `DelayLine<WFS_MAX_DELAY_SAMPLES>` there.

**Precise change**: WFS per-cell buffer drops from `std::array<float,48000>` (187.5 KB) to `std::array<float,16384>` (64 KB) — a 2.86× reduction; `delays_` @128/8spk drops from ~192 MB to ~67 MB. The large-buffer users (user_delay #2, spk_delays #4) are re-spelled but keep 48000 capacity (no behavior change). No capacity shrinks besides WFS in THIS milestone (propagation is F5-M3). Still `std::array` (RT-safe by construction).

**RT-safety invariant**: compile-time template only; arrays stay fixed-size `std::array`; `prepareToPlay`-only allocation; no audio-path change yet (clamp is F5-M2).

**Gate / test**: both caps configure + compile (`cmake -DSPATIAL_ENGINE_NO_JUCE=ON ..` and `… -DSPATIAL_ENGINE_MAX_OBJECTS=128 ..`) — the mechanical `DelayLine<>`/`DelayLine48k` re-spellings (amendment 3) must compile clean; `test_p3_wfs` (Huygens) green — all its delays are r/c with r ≤ array bounds (tight 10 mm arrays → tiny r), so in-range, bit-identical; `test_p3_propdelay_sweep` green (untouched).

**Acceptance**
- `DelayLine` templated; large-buffer users (`user_delay_`, `spk_delays_`) re-spelled `DelayLine48k`/`DelayLine<>` → **source-COMPATIBLE via alias** (not source-identical), capacity unchanged at 48000.
- WFS uses 16384-sample lines; all four `dsp::DelayLine` users compile at both caps.
- `test_p3_wfs` + `test_p3_propdelay_sweep` byte/numerically unchanged.

---

### F5-M2 — Add the capacity clamp to `DelayLine::processSample` (the load-bearing safety fix)

**Files**
- Modify `core/src/dsp/DelayLine.h:23-37` — at the top of `processSample`, clamp: `if (delay_samples > Capacity - 2.f) delay_samples = Capacity - 2.f; if (delay_samples < 0.f) delay_samples = 0.f;` (branchless `std::min`/`std::max` acceptable). This protects EVERY instantiation (WFS 16384, propagation, user_delay 48000) for free.
- Optionally also clamp at the WFS call site (`WFSRenderer.cpp:80`) for clarity/defense-in-depth: `delay_samples = std::min(delay_samples, (float)(spe::dsp::WFS_MAX_DELAY_SAMPLES - 2));` — but the in-`processSample` clamp is the authoritative one.
- Document the WFS max supported venue×SR (50 m @96 kHz) in the `WFSRenderer.h` header comment and the ADR.

**Precise change**: a delay request exceeding `Capacity-2` is clamped to `Capacity-2` (the maximum representable delay with interpolation headroom) instead of wrapping modulo to a garbage sample (§0.3). For in-range delays (every real venue/SR within the budget) the output is **bit-identical** to today. The `PropagationDelay.h:28-29` clamp is now SUBSUMED by this primitive clamp — but note it must ALSO be capacity-corrected in F5-M3 (it currently clamps to 48000−1, wrong once `delay_` is `DelayLine<16384>`; Critic finding 4c). This `processSample` clamp is the AUTHORITATIVE one.

**REV 2 / amendment 2 — the clamp is a BEHAVIOR CHANGE on the currently-LARGE-capacity lines too, not only WFS.** Today a `spk_delay_samples_` (layout `delay_ms`, `SpatialEngine.cpp:369-370`, UNCLAMPED at `:746`) or `user_delay_` (`user_delay_ms`) value ≥ 48000 (i.e. `delay_ms ≥ 1000` @48 k, or any SR where `delay_ms×0.001×sr ≥ 48000`) WRAPS to garbage today; after F5-M2 it is PINNED to `Capacity-2 = 47998` (≈ 1.0 s @48 k). This is strictly safer (clamped > wrapped-garbage) but it IS an observable change for over-1-s layout/user delays. **Document the max-supported `delay_ms` ≈ `(48000-2)/sr` s ≈ 1.0 s @48 k (and ≈ 0.5 s @96 k, ≈ 0.25 s @192 k — it is SR-dependent) in the ADR.** A layout author requesting a >1 s speaker delay gets it clamped — if that is a real requirement, `DelayLine48k` capacity must grow (re-measure footprint).

**RT-safety invariant**: adds ONE branchless `std::min`/`std::max` per `processSample` call — no alloc, no lock, no data-dependent branch that the audio thread cannot take. This is the only audio-path change in the lane (D2 — acceptable, it replaces an implicit modulo-wrap with an explicit bound).

**Gate / test**: NEW unit `test_p_delayline_clamp` (`core/tests/core_unit/`):
- **WFS-capacity line:** instantiate `DelayLine<16384>`, push an impulse, request `delay_samples = 50000` (> capacity), assert the read is the CLAMPED tap (== `Capacity-2` delay), NOT the wrapped garbage the old code returned; request an in-range `delay_samples = 1000`, assert bit-identical to a reference.
- **(amendment 2) Large-capacity line:** instantiate `DelayLine<48000>` (the `spk_delays_`/`user_delay_` class), simulate a layout `delay_ms = 500` (→ 24000 samples @48 k, IN-range) and assert bit-identical alignment; then `delay_ms = 1100` (→ 52800 samples > 48000, OVER-capacity) and assert CLAMPED to `Capacity-2`, NOT wrapped. This proves the clamp protects the user-settable large lines, not just WFS.
- `test_p3_wfs` + `test_p3_propdelay_sweep` still green (all in-range).

**Acceptance**
- Over-capacity `delay_samples` clamped to `Capacity-2` (graceful degradation, no wrap-garbage) for ALL `DelayLine` instantiations — WFS (16384) AND the large `spk_delays_`/`user_delay_` (48000) class.
- In-range delays bit-identical (WFS Huygens + propdelay sweep + a 500 ms speaker-alignment case unchanged).
- New clamp unit test (both WFS-capacity and large-capacity cases) green at both caps.
- Max-supported `delay_ms` (≈ 1.0 s @48 k, SR-dependent) documented in the ADR.

---

### F5-M3 — Right-size the secondary `chains_` propagation line (keep user_delay large)

**Files**
- Modify `core/src/dsp/PropagationDelay.h:50` — `DelayLine<spe::dsp::WFS_MAX_DELAY_SAMPLES> delay_;` (or a dedicated `PROP_MAX_DELAY_SAMPLES = 16384`; propagation r ≤ 20 m source distance → ≤ ~2800 samples @48 k, 16384 is generous).
- **REV 3 / Critic finding 4c — FIX the `:28-29` clamp, do NOT just "leave it."** Today `PropagationDelay::setDistance` clamps `target` to `DELAY_LINE_MAX_SAMPLES-1` (= 48000−1) (`PropagationDelay.h:28-29`). Once `delay_` is `DelayLine<16384>`, that clamp is WRONG — it would admit a `target` up to 47999 into a 16384-sample buffer (a latent over-capacity request that only the new F5-M2 `processSample` clamp would catch). **Change `:28-29` to clamp against the template capacity:** `if (target > Capacity - 1) target = Capacity - 1;` where `Capacity` is the `delay_`'s template parameter (e.g. expose it via `decltype(delay_)::capacity()` or a `static constexpr int kCap = WFS_MAX_DELAY_SAMPLES;`). The F5-M2 `processSample` clamp is the AUTHORITATIVE backstop; this local clamp becomes a correct (capacity-matched) defense-in-depth rather than a now-mismatched no-op.
- This shrinks the propagation DelayLine inside every `PerObjectChain` from 187.5 KB → 64 KB.
- **DO NOT** change `PerObjectChain::user_delay_` (`PerObjectChain.h:84`) — it stays `DelayLine<>` (48000, ~1 s) per the §0.4 / RALPLAN-DR decision (user-facing delay param may want ~1 s). Document WHY in a header comment.
- **DO NOT** change `spk_delays_` (`SpatialEngine.h:434`) — **keep `DelayLine48k` (48000)**. **REV 2 / amendment 1:** `spk_delays_` is NOT geometry-bounded; it is a user-settable, UNCLAMPED delay (`spk_delay_samples_[i] = delay_ms × 0.001 × sr`, `SpatialEngine.cpp:369-370`, `delay_ms` raw from YAML `core/src/geometry/LayoutLoader.cpp:95`). 16384@48 k is only ~341 ms — real venue speaker-alignment can exceed that. Same large-buffer class as `user_delay_` (#2). It is per-SPEAKER not per-object → ~8 lines × 187.5 KB ≈ 1.5 MB, negligible; keeping it large does NOT meaningfully move the footprint. Document WHY in a header comment.

**Precise change**: `chains_` per-object cost drops from 375.3 KB (2× 187.5 KB) to ~251.5 KB (187.5 KB user_delay + 64 KB propagation) → @128: ~46.9 MB → ~31.4 MB. Combined with F5-M1's WFS win, this is the second-largest contributor addressed.

**RT-safety invariant**: `prepareToPlay`-only sizing; `PerObjectChain::process` (`PerObjectChain.h:50-51`) audio path unchanged (now protected by the F5-M2 primitive clamp).

**Gate / test**: `test_p3_propdelay_sweep` green (propagation r within 16384 capacity → in-range, bit-identical); full NO_JUCE ctest green at both caps.

**Acceptance**
- Propagation line right-sized to 16384; user_delay AND spk_delays deliberately KEPT at 48000 (documented).
- Propdelay sweep unchanged; chains_ footprint reduced.

---

### F5-M3b — skip WFS allocation when WFS is never active (Option C) — LIKELY REQUIRED at 128

**REV 3 / Critic finding 2 — re-framed from "conditional fallback / likely skipped" to LIKELY REQUIRED.** The honest @128 estimate is **~108 MB (still OVER the 100 MB gate)** after WFS+propagation right-sizing with `user_delay_`/`spk_delays_` kept large (§0.7). So Option C's allocate-then-publish handshake + TSan gate is **expected work on the critical path at 128**, NOT a long-shot fallback. (@64 lands ~58 MB and clears WITHOUT Option C — Option C is specifically the 128-clearing mechanism.) **The trigger remains the actual F5-M4 measurement (measurement is authoritative — if the measured 128 RSS somehow lands < 100 MB, F5-M3b is skipped), but the planning EXPECTATION is that F5-M3b triggers at 128.** Its handshake is fully specified here so it is not improvised under measurement pressure.

**The race hazard (why this needs a handshake):** the audio thread sorts each block's objects into `wfs_objs_` and reads the active algorithm at `SpatialEngine.cpp:684-690`, then calls `wfs_.processBlock` at `:706`. Allocating `delays_` (control thread) WHILE the audio thread reads/renders WFS is a data race / use-before-init.

**Files**
- `core/src/render/WFSRenderer.h/.cpp` — add `std::atomic<bool> ready_{false}`; move the `delays_/ramps_.resize` out of unconditional `prepareToPlay` into a control-thread `ensureAllocated(layout, sr)` that allocates THEN publishes `ready_.store(true, std::memory_order_release)`. `processBlock` early-returns silent (`std::memset(out,…)`) while `ready_.load(std::memory_order_acquire) == false` — never touches a half-built `delays_`.
- `core/src/core/SpatialEngine.cpp` — at `prepareToPlay:343`, do NOT prime WFS unconditionally; instead, on the CONTROL thread, when an object's algorithm first becomes WFS (the OSC algorithm-switch apply path), call `wfs_.ensureAllocated(...)`. The audio thread at `:684-690` continues to route objects to `wfs_objs_`; `processBlock` renders silent until `ready_` flips (at most a few blocks of WFS silence on first-ever WFS activation — acceptable, documented).

**Handshake invariant**: allocate-then-publish on the control thread (release); audio thread acquire-loads `ready_` and treats WFS as silent until true; `delays_` is NEVER resized while the audio thread may read it (allocated exactly once, behind the flag). No lock on the audio thread (single atomic acquire-load).

**RT-safety invariant**: audio thread adds ONE relaxed/acquire atomic load per block + a silent early-return branch — no alloc, no lock. Allocation stays on the control thread.

**Gate / test**: **TSan gate — `core/build_tsan`** (`cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DCMAKE_BUILD_TYPE=… ` with `-fsanitize=thread`): an algorithm-switch soak (object flips to WFS while audio renders) reports ZERO data races on `delays_`/`ready_`. Plus: WFS-never-used config measures the ~67/192 MB WFS term ABSENT (RSS drops accordingly); first-WFS-activation produces correct audio after `ready_` flips (`test_p3_wfs`-style check post-activation).

**Acceptance**
- WFS `delays_` NOT allocated until first WFS activation; WFS-unused RSS excludes the WFS term.
- TSan (`core/build_tsan`) clean on the algorithm-switch soak — zero races on `delays_`/`ready_`.
- First-WFS-activation audio correct after the flag publishes; at most a few blocks of WFS silence on first activation (documented).

---

### F5-M4 — Footprint re-measure at 64 & 128 + update `RT_BUDGET_MAX_OBJECTS.md` (D1 hard deliverable)

**Files**
- Re-run the EXISTING `core/tests/perf/perf_obj_block_time.cpp` (no harness change needed — it already reads `getrusage` max-RSS and ENFORCES the `< 100 MB` gate at `:302-307`, returning 1 at `:326`) at both caps, heaviest config.
- Modify `docs/RT_BUDGET_MAX_OBJECTS.md` — replace the FAIL rows (`:11,123-124,135,155,207-218`) with the post-fix MEASURED RSS at 64 and 128; update the structural model table (`:148` region) to reflect WFS @16384 (64 KB/line) and propagation @16384; record the WFS Δ saved; re-state the C-M7 default-flip gate verdict given the new 128 RSS.
- **Decision gate — F5-M3b is EXPECTED to trigger at 128 (REV 3 / Critic finding 2).** The honest model says @128 ≈ 108 MB after F5-M1..M3 (still over), so the measurement is expected to confirm 128 ≥ 100 MB → **TRIGGER the pre-scoped F5-M3b** (Option C, full handshake + TSan gate specified above). Measurement stays authoritative: in the unlikely event measured 128 RSS < 100 MB, F5-M3b is skipped. @64 (≈ 58 MB) clears without F5-M3b. The decision is recorded with the measured number.

**Precise change**: assert MEASURED max-RSS at 128/heaviest **< 100 MB** (the gate the harness enforces at `:302-307`, returns 1 at `:326`; F5 makes it exit 0). Cross-check against the §0.7 honest model: @64 ≈ **58 MB** (clears after WFS+propagation), @128 ≈ **108 MB** (still over after WFS+propagation alone → expect F5-M3b/Option C to be required, which removes the WFS term when WFS is inactive). Flag any >20% model/measurement divergence for the Architect.

**RT-safety invariant**: measurement only; all alloc in `prepareToPlay`.

**Gate / test**: `perf_obj_block_time` (Release) EXITS 0 (RSS < 100 MB AND peak ≤ 50% budget AND xruns 0) at BOTH 64 and 128. Doc updated with the new table + reproduce block. Option-C-promotion decision recorded explicitly (in-scope iff 128 ≥ 100 MB).

**Acceptance**
- Measured 128/heaviest RSS < 100 MB (was 250 MB); 64 RSS < 100 MB (was 129 MB).
- Doc tables (model + measured) updated; WFS Δ recorded; divergences explained.
- Option-C-promotion decision explicit (deferred iff 128 < 100 MB).

---

### F5-M5 — Regression at BOTH caps + RT-asserts + DelayLine/WFS audio invariance

**Files**
- Run full NO_JUCE ctest at both caps: `core/build` (64) and `core/build_obj128` (128) — must be green incl. `test_p3_wfs`, `test_p3_propdelay_sweep`, the new `test_p_delayline_clamp` (BOTH the WFS-capacity AND the amendment-2 large-`delay_ms` cases), and the Lane C cap-unify tests.
- **REV 2 / amendment 2 + 6 — large speaker-alignment regression:** add (or extend an engine-level test) a config with a layout `delay_ms = 500` (24000 samples @48 k, IN-range for `DelayLine48k`) and assert the per-speaker time-alignment output (`SpatialEngine.cpp:746`) is **bit-identical** to pre-F5; and a `delay_ms = 1100` case asserting CLAMPED-not-wrapped. This is the `spk_delays_`-specific detector the pre-mortem scenario 2 now requires (propdelay sweep alone does NOT exercise `spk_delays_`).
- Run RT-asserts at both caps: `core/build_rton` (64) and `core/build_rton_obj128` (128) — `p1_rt_no_alloc` (`core/tests/core_unit/CMakeLists.txt:106`) green; confirm WFS/propagation/chain/spk-align audio path adds NO new allocation (the `std::min` clamp is alloc-free).
- pytest green (no Python-side delay assumptions; the change is C++-internal).

**Precise change**: prove the WFS / propagation / user-delay / **speaker-alignment** AUDIO OUTPUT is numerically unchanged for in-range delays (the §0.3 / D3 guarantee) at both caps, and the over-capacity clamp behaves on BOTH the small (WFS 16384) and large (48000 spk/user) lines (new test). RT-asserts confirms D2 (no audioBlock alloc) at 128.

**RT-safety invariant**: regression + RT-assert verification; the lane's only audio-path op is the F5-M2 branchless clamp.

**Gate / test**: BOTH-cap NO_JUCE ctest green; BOTH-cap RT-asserts green (`p1_rt_no_alloc`); pytest green; `test_p_delayline_clamp` green.

**Acceptance**
- 64 + 128 NO_JUCE ctest green (WFS Huygens, propdelay, clamp tests incl. the large-`delay_ms` case, 500 ms speaker-alignment regression).
- 64 + 128 RT-asserts green — zero new audioBlock allocation; clamp is alloc-free.
- Audio output bit/numerically identical for in-range delays vs pre-F5 (WFS, propagation, user_delay, AND speaker-alignment).

---

### F5-M6 — Evidence-gated: can Lane C's C-M7 default now flip to 128? + ADR

**Files**
- Re-evaluate Lane C's C-M7 default-flip gate against the F5-M4 measured numbers: **flip `SPATIAL_ENGINE_MAX_OBJECTS` default to 128 IFF (heaviest path) `peak ≤ ~35% budget AND max-RSS < ~70 MB AND xruns == 0`** (the exact thresholds committed in `docs/RT_BUDGET_MAX_OBJECTS.md:207`).
- **REV 3 / Critic finding 5 — be honest about reachability:** the @128 default-flip is **UNREACHABLE by WFS+propagation right-sizing alone**. The honest post-F5-M1..M3 estimate is ~108 MB @128 (§0.7) — over even the 100 MB hard gate, let alone the 70 MB flip target. **The flip is plausible ONLY if F5-M3b (Option C) lands.** And even then, the nuance: Option C reduces the WFS term to ~zero ONLY when WFS is NOT the active algorithm (the common case — VBAP/binaural default). So with Option C, a **WFS-inactive** 128 deployment can land well under 70 MB (flip plausible), but a **WFS-active** 128 deployment still pays the ~67 MB WFS term (`delays_` @16384 ×128) on top of the ~41 MB base → ~108 MB, so the flip is NOT universally safe. **Conclusion to record: the 128 default-flip is conditional on (a) Option C landing AND (b) the target deployment profile being WFS-inactive; for WFS-active venues, 128 stays an explicit opt-in.** Do not claim a blanket flip.
- Modify `CMakeLists.txt` ONLY if the gate clears (set cache default 128); else leave 64 with the measured numbers recorded.
- New/amend ADR — document: the `DelayLine` capacity template, `WFS_MAX_DELAY_SAMPLES = 16384` + its derivation (50 m × 96 kHz), the `processSample` clamp (graceful degradation contract) + the **max-supported `delay_ms` ≈ `(48000-2)/sr` s (≈1.0 s @48 k, SR-dependent)** for the large lines, the propagation right-sizing, the deliberate **`user_delay_` AND `spk_delays_` stay-large** decision (both user-settable/unclamped), and the C-M7 re-evaluation outcome. `Constants.h:3` mandates an ADR amendment for RT-path tunable changes.
- Modify `docs/RT_BUDGET_MAX_OBJECTS.md` — final C-M7 verdict with the post-F5 measured `peak`/`RSS`/`xruns` vs the ~35% / ~70 MB / 0 thresholds.

**Gate / test**: ADR present; doc updated; default decision matches measured evidence. Final: BOTH-cap ctest + RT-asserts(128) + `perf_obj_block_time`(both, RSS < 100 MB) all green regardless of default.

**Acceptance**
- C-M7 re-evaluation outcome recorded (flip to 128 iff peak ≤35% AND RSS <70 MB AND xruns 0; else stay 64 with numbers logged).
- ADR amended (capacity template, WFS number + derivation, clamp contract + max-supported `delay_ms`, propagation right-size, `user_delay_`+`spk_delays_`-stay-large rationale).
- All hard gates green at both caps.

---

## Hard Gates (lane-level, must ALL pass for F5 done)

1. **Footprint:** measured max-RSS at 128/heaviest **< 100 MB** (was 250 MB), AND 64 < 100 MB (was 129 MB) — via the existing `perf_obj_block_time` harness (`getrusage`, gate enforced at `:302-307`, returns 1 at `:326`), which must EXIT 0. **Note: WFS+propagation right-sizing alone lands @128 at ~108 MB (still over) — F5-M3b (Option C) is expected to be required for this gate at 128 (§0.7).**
2. **Audio invariance:** WFS Huygens (`test_p3_wfs`), propdelay sweep (`test_p3_propdelay_sweep`), user_delay, AND speaker-alignment (`spk_delays_`, 500 ms case) output **bit/numerically unchanged for in-range delays**; over-capacity delays **clamped, not wrapped** on BOTH the WFS (16384) and large (48000) lines (`test_p_delayline_clamp`).
3. **RT-asserts:** `p1_rt_no_alloc` green at 128 — zero new audioBlock allocation; the clamp is alloc-free/branchless.
4. **Both caps:** full NO_JUCE ctest + RT-asserts green at 64 AND 128.

---

## ADR (final-plan stub — to be filled at F5-M6)

- **Decision:** Template `dsp::DelayLine` on compile-time capacity; WFS uses `DelayLine<16384>`, propagation uses a small capacity; **`user_delay_` AND `spk_delays_` keep `DelayLine48k` (48000)** (both user-settable, large-buffer class); add a `processSample` over-capacity clamp.
- **Drivers:** D1 footprint < 100 MB @128 (WFS ~192 MB → ~67 MB); D2 RT-safe-by-construction (`std::array`) preserved; D3 correctness (clamp replaces silent wrap-garbage).
- **Alternatives considered:** Option B (runtime `std::vector` sizing — precise but converts the hot primitive to heap, removes the fixed-size guarantee); Option C (lazy/conditional WFS alloc — reduces count not size, races the audio thread, insufficient alone — pre-scoped as conditional F5-M3b).
- **Why chosen:** Option A uniquely shrinks the dominant term while KEEPING the `std::array` RT-safe-by-construction guarantee (Principle 1), right-sizes per use (Principle 2), and the single-site clamp protects all users (Principle 3). Reverb is untouched (own DelayLine). **`user_delay_` and `spk_delays_` stay large — both are user-settable, UNCLAMPED delays (OSC `user_delay_ms`; layout YAML `delay_ms`, `core/src/geometry/LayoutLoader.cpp:95` / `SpatialEngine.cpp:369-370`) that may legitimately want ~1 s.**
- **Consequences:** `DelayLine` is now a template (header included by 4 users; default arg + `DelayLine48k` alias keep churn to mechanical re-spellings at `PerObjectChain.h:84`, `SpatialEngine.h:434`, `PropagationDelay.h:50`); WFS capacity is a compile-time SR bet, mitigated by the runtime clamp (graceful degradation past 50 m @96 kHz). One branchless `std::min` added to the audio path. **The clamp also changes behavior on the large lines: max-supported delay ≈ `(48000-2)/sr` s (≈ 1.0 s @48 k, ≈ 0.5 s @96 k, ≈ 0.25 s @192 k); a layout/user delay beyond this is clamped (was wrap-garbage). A >1 s requirement needs a larger `DelayLine48k` + footprint re-measure.**
- **Follow-ups:** Option C / F5-M3b (skip WFS alloc when unused) — TRIGGERED iff F5-M4 measures 128 ≥ 100 MB, fully pre-scoped with the allocate-then-publish handshake + TSan gate; Lane C C-M7 default-flip re-evaluation; F3 binaural lazy-prime (tertiary, ~4–7 MB).

---

## Pre-Mortem (DELIBERATE — 3 failure scenarios)

1. **WFS capacity too small → audible delay-wrap at large venue / high SR.** A deployment runs 192 kHz on a 50 m-diameter array; r reaches ~50 m → `50/343×192000 ≈ 27988 > 16384`. **Mitigation:** the F5-M2 clamp degrades gracefully (delay pinned to `Capacity-2 = 16382` samples ≈ 29 m of delay @192 kHz) instead of wrapping to garbage; the ADR documents the supported envelope (50 m @96 kHz / ~29 m @192 kHz). If a 192 kHz + >29 m venue is a real target, bump `WFS_MAX_DELAY_SAMPLES` to 32768 (§0.7) — a one-constant change, still only ~134 MB @128 (re-measure). **Detected by:** `test_p_delayline_clamp` (asserts clamp not wrap) + a large-venue WFS analytic case in F5-M5.
2. **Templating `DelayLine` breaks a non-WFS user, OR the clamp silently truncates a large user delay.** Two sub-hazards: (a) `PerObjectChain::user_delay_`, `PropagationDelay::delay_`, or `spk_delays_` fails to compile / picks the wrong capacity; (b) **REV 2 / amendment 2** — the F5-M2 clamp changes behavior on the LARGE lines: a layout `delay_ms ≥ 1000` (@48 k) or a `user_delay_ms ≥ 1000` that today WRAPS to garbage is now PINNED to `Capacity-2`, which a layout author might not expect. **Mitigation:** (a) the default template arg `= DELAY_LINE_MAX_SAMPLES (48000)` + the explicit `DelayLine48k` re-spelling keep `user_delay_`/`spk_delays_` at 48000 (amendment 3); only WFS (M1) and propagation (M3) shrink. Reverb is NOT a `dsp::DelayLine` user (`FdnReverb.h:50` own struct) → cannot be affected. (b) The clamp is strictly safer than the prior wrap-to-garbage; the max-supported `delay_ms` (≈1.0 s @48 k, SR-dependent) is DOCUMENTED in the ADR; >1 s needs a larger `DelayLine48k`. **Detected by:** both-cap compile (F5-M1) + `test_p3_propdelay_sweep` (propagation audio) + **the amendment-2 large-`delay_ms` clamp case in `test_p_delayline_clamp` (500 ms in-range bit-identical, 1100 ms clamped-not-wrapped) + the 500 ms speaker-alignment regression in F5-M5** + full ctest.
3. **Lazy/conditional WFS alloc (Option C — LIKELY REQUIRED at 128) races the audio thread on algorithm switch.** Option C is expected on the 128 critical path (honest @128 ≈ 108 MB after Option A alone — REV 3); allocating `delays_` on a control thread while the audio thread reads the per-object algorithm assignment (`SpatialEngine.cpp:684-690`) is a data race / use-before-init. **Mitigation:** F5-M3b's allocate-then-publish handshake (`std::atomic<bool> ready_`, release on the control thread after the resize; audio thread acquire-loads and renders WFS silent until the flag flips — never reads a half-built `delays_`); allocated exactly once, never resized while the audio thread may read it. Its own RT-asserts + TSan gate. **Detected by:** `core/build_tsan` run on the Option-C path + an algorithm-switch soak.

## Expanded Test Plan (DELIBERATE)

- **Unit:** `test_p_delayline_clamp` (over-capacity → clamped, in-range → bit-identical, both at `DelayLine<16384>` and `DelayLine<48000>`); `test_p3_wfs` (Huygens analytic, in-range, unchanged); `test_p3_propdelay_sweep` (propagation, unchanged).
- **Integration:** full NO_JUCE ctest at 64 AND 128 (WFS + chains + scene paths); a large-venue WFS case (50 m array, high SR) asserting graceful clamp not garbage.
- **e2e / perf:** `perf_obj_block_time` (Release, both caps) — RSS < 100 MB exit-0 gate (was exit-1), peak ≤ 50% budget, xruns 0, heaviest config.
- **RT / observability:** `p1_rt_no_alloc` (RT-asserts, 128) — zero new audioBlock alloc; if Option C promoted, `core/build_tsan` algorithm-switch race check. CpuMeter peak/p99 recorded in the doc for the C-M7 re-evaluation.

---

## Progress Tracker

| Milestone | Status | Evidence |
|---|---|---|
| F5-M1 right-size DelayLine template + WFS_MAX_DELAY_SAMPLES | ✅ done | both-cap compile exit-0; test_p3_wfs + propdelay green ×2 caps |
| F5-M2 processSample capacity clamp (safety) | ✅ done | test_p_delayline_clamp green ×2 caps (WFS-16384 + large-48000 cases); wfs+propdelay green |
| F5-M3 right-size propagation line (keep user_delay + spk_delays large) | ✅ done | propdelay green; full ctest 114/114 ×2 caps; PropagationDelay clamp now capacity-matched |
| F5-M3b skip-WFS-alloc (Option C) — TRIGGERED & DONE @128 | ✅ done | TSan `soak_wfs_algoswap_race` race-free ×150 rounds; WFS-inactive RSS **@128 111.6→46.7 MB**, **@64 59.4→27.5 MB**, perf VERDICT PASS both caps (peak 46.9% @128 under normal load) |
| F5-M4 footprint re-measure + doc update (D1) | ✅ measured / decision made | @64 **59.4 MB PASS**, @128 **~111 MB FAIL** (model ~108 confirmed) → **Option C (F5-M3b) TRIGGERED**. Final exit-0 after M3b. Doc updated. |
| F5-M5 both-cap regression + RT-asserts | ☐ pending | ctest + p1_rt_no_alloc green ×2 |
| F5-M6 C-M7 default-flip re-eval + ADR | ☐ pending | ADR + doc verdict |

---

## Changelog

- **REV 3 (Critic: ITERATE — accuracy corrections; design unchanged & Architect-approved):** one MAJOR finding (footprint) + framing/citation fixes. Applied EXACTLY:
  1. **MAJOR — corrected the @128 footprint estimate (was ~94 MB, internally inconsistent & ~14 MB too optimistic).** Honest arithmetic with `user_delay_` AND `spk_delays_` kept at 48000 (only WFS + propagation shrink, per-line Δ = 123.5 KB): **@128 = 250 − 126.4 WFS − 15.4 prop ≈ ~108 MB (STILL OVER 100 MB)**; **@64 = 129 − ~63 WFS − ~7.7 prop ≈ ~58 MB (CLEARS)**. Rebuilt the §0.7 table with explicit term-by-term arithmetic; removed the erroneous ~94 MB headline and the conflated "~12/24 MB propagation saving" wording. 64 is fine; 128 is the problem.
  2. **Re-framed F5-M3b (Option C) from "conditional / likely-skipped fallback" to LIKELY REQUIRED at 128** — at ~108 MB, WFS+propagation alone does NOT clear 128, so Option C's handshake + TSan gate is expected critical-path work. Updated F5-M3b heading/intro, the F5-M4 decision-gate, the RALPLAN-DR Conclusion, D1 driver, pre-mortem scenario 3, Hard Gate 1, and the Progress Tracker (now "☐ expected", gated on the authoritative F5-M4 measurement).
  3. **Devil's-advocate (user_delay_ → 0.5 s):** added to §0.7 — shrinking `user_delay_` 48000→24000 saves ~11.7 MB → ~96 MB @128 (would clear) WITHOUT Option C's race work; **REJECTED** because `user_delay_` is a user-settable ~1 s param with no 0.5 s contract → truncating it is a silent functional regression (same class REV 2 amendment 1 fixed for `spk_delays_`). Flagged as a cheaper alternative ONLY if product explicitly signs off on a 0.5 s cap.
  4. **Citation fixes:** (a) the RSS gate ENFORCEMENT is `perf_obj_block_time.cpp:302-307` (returns 1 at `:326`), not `:65` (constant only) — fixed all 4 enforcement-context citations (§0.6, D1, F5-M4, Hard Gate 1). (b) fully-qualified `core/src/geometry/LayoutLoader.cpp:95` (all occurrences). (c) `PropagationDelay.h:28-29` clamp: it clamps to `DELAY_LINE_MAX_SAMPLES-1` (48000) but the line becomes `DelayLine<16384>` → F5-M3 now FIXES it to clamp against the template `Capacity` (was a latent over-capacity bug masked only by the F5-M2 processSample clamp).
  5. **C-M7 default-flip honesty (F5-M6):** the @128 flip is UNREACHABLE without Option C; and even with Option C, the WFS term only vanishes when WFS is INACTIVE (the common case). So the 128 default-flip is conditional on Option C landing AND a WFS-inactive deployment profile — WFS-active venues keep 128 as an explicit opt-in. No blanket-flip claim.
  **Design unchanged:** Option A (template+clamp), the clamp design, and the `spk_delays_`/`user_delay_` keep-large decisions are Architect-approved and Critic-confirmed — untouched.

- **REV 2 (Architect: SOUND WITH AMENDMENTS):** Option A confirmed correct/RT-safe; FdnReverb confirmed out of blast radius. Applied 6 amendments:
  1. **BLOCKER — `spk_delays_` (§0.4 row #4) reclassified from "geometry-bounded, optionally shrink" to "user-settable, UNCLAMPED, KEEP LARGE."** Verified: `spk_delay_samples_ = delay_ms×0.001×sr` (`SpatialEngine.cpp:369-370`), `delay_ms` raw from YAML (`core/src/geometry/LayoutLoader.cpp:95`, no bound), unclamped at `:746`. Same class as `user_delay_`. **Removed the "optionally right-size spk_delays_ to 16384" line from F5-M3; it now KEEPS `DelayLine48k`.** Updated §0.4 table/CONSEQUENCE, RALPLAN-DR chains decision, F5-M3.
  2. **Clamp is a behavior change on the LARGE lines too** (not only WFS): a layout `delay_ms`/`user_delay_ms` ≥ capacity that WRAPS today is now PINNED. Added the amendment-2 large-`delay_ms` clamp test (500 ms in-range bit-identical + 1100 ms clamped-not-wrapped) to F5-M2 and the 500 ms speaker-alignment regression to F5-M5; documented max-supported `delay_ms` ≈ 1.0 s @48 k (SR-dependent) in the ADR.
  3. **F5-M1 wording:** templating is NOT source-unchanged — enumerated the in-scope mechanical re-spellings (`DelayLine48k`/`DelayLine<>` at `PerObjectChain.h:84`, `SpatialEngine.h:434`, `PropagationDelay.h:50`); acceptance now says "source-COMPATIBLE via alias."
  4. **Option C pre-scoped as concrete CONDITIONAL F5-M3b** (given the thin ~6 MB margin): allocate-then-publish handshake (atomic `ready_`, audio thread silent until flip, never reads half-built `delays_`), **TSan gate (`core/build_tsan`)** named; triggered only iff F5-M4 measures 128 ≥ 100 MB. Added to milestones + Progress Tracker.
  5. **Stale ref note for reviewers:** WFS delay is computed at `WFSRenderer.cpp:80` (NOT `:88` as the lane brief stated) — the plan body was already correct at `:80`; flagged so REV 2 reviewers don't chase `:88`.
  6. **Pre-mortem scenario 2 detector extended** to cover `spk_delays_` large-`delay_ms` (propdelay sweep does NOT exercise `spk_delays_`) — now cites the amendment-2 clamp test + the 500 ms speaker-alignment regression.
  **@128 estimate CONFIRMED UNCHANGED at ~94 MB (~6 MB under 100 MB) with `spk_delays_` kept large:** `spk_delays_` is per-SPEAKER not per-object (~8×187.5 KB ≈ 1.5 MB at any cap, does NOT double 64→128); shrinking it would save only ~1 MB, negligible. The WFS (per-object×speaker) + propagation (per-object) shrinks are what move the footprint; neither is `spk_delays_`. So @64 ~66 MB / @128 ~94 MB both still clear 100 MB; F5-M3b is the pre-scoped fallback for the thin margin.

- **REV 1 (initial consensus draft):** grounded F5-M1..M6 at HEAD `4539873`. Key grounding findings vs the lane brief: (a) **FdnReverb does NOT use `dsp::DelayLine`** (own `std::vector`-backed struct, `FdnReverb.h:50`) → reverb is OUT of the blast radius, narrowing the fix; (b) source distance is HARD-bounded at **20 m** (`ADM_OSC_MAX_DIST`, `AdmOscConstants.h:12`, static_assert'd) → WFS delay is small and computable; (c) **no sample-rate upper validation exists** → the capacity is a compile-time SR bet that the §0.3 clamp must backstop; (d) the WFS clamp is genuinely ABSENT (`WFSRenderer.cpp:80,100` feeds raw `delay_samples`), only `PropagationDelay` clamps — confirming §0.3 as load-bearing; (e) `WFS_MAX_DELAY_SAMPLES = 16384` derived (50 m × 96 kHz). Chose Option A (template + clamp), propagation right-sized, user_delay kept large, Option C held as Option-D fallback. Mode DELIBERATE (hot DSP primitive + silent-wrong-audio class). Est. post-fix: ~66 MB @64, ~94 MB @128 (WFS+propagation), both < 100 MB.
