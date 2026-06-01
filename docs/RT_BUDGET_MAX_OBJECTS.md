# RT-Budget & Memory Footprint — MAX_OBJECTS 64 vs 128 (v0.9 Lane C, C-M3/C-M4)

Measured deliverable for **C-M3** (D1 — RT budget headroom) and **C-M4** (D3 —
memory ceiling) of `.omc/plans/spatial-engine-v0.9-laneC-max-objects.md`.

**Headline verdict:**

| Gate | Threshold (authoritative) | 64 build | 128 build | Result |
|---|---|---|---|---|
| **C-M3 RT** | peak per-block ≤ 50% budget (≤ 666.7 µs) **AND** xruns == 0 at the compiled cap | peak ~252 µs (18.9%), 0 xruns | peak ~558–629 µs (41.8–47.2%), 0 xruns | **PASS** (both) |
| **C-M4 RSS** | max-RSS at the cap < 100 MB | **~129 MB** | **~250 MB** | **FAIL** (both) |

The RT budget has comfortable headroom at 128. **The memory gate FAILS hard** —
and the failure is NOT the binaural OlaConvolvers the §0.4 plan model predicted;
it is the **WFS renderer's per-(object×speaker) delay-line allocation**, a term
the §0.4 model omitted entirely. See [Memory](#c-m4--memory-footprint) below.

---

## Machine spec (results are Linux-N-core-specific)

| Field | Value |
|---|---|
| CPU | Intel(R) Core(TM) i7-14700K |
| `nproc` | 28 |
| OS | Linux 6.14 |
| Build | `-DCMAKE_BUILD_TYPE=Release` (optimized, required for accurate timing) |
| Config flag | `-DSPATIAL_ENGINE_MAX_OBJECTS={64,128}` |
| Build dirs | `core/build_rel` (64), `core/build_obj128_rel` (128) |

These numbers are specific to this box. A weaker/loaded target will show higher
peak %; the gate must be re-measured per deployment class before any default
flip (C-M7).

---

## Methodology

Harness: `core/tests/perf/perf_obj_block_time.cpp` (ctest `perf_obj_block_time`,
gated on `Release` OR `SPATIAL_ENGINE_RUN_SOAK`, registered in
`core/tests/perf/CMakeLists.txt`).

- **Heaviest configuration**, per scenario: an 8-channel circular speaker layout
  (built in-process, CWD-independent) + VBAP algorithm on every object
  (`ObjCache` default) + **B2 binaural side-output ENABLED** with a real SOFA
  (`synthetic_min.speh`). With a SOFA loaded, `BinauralMonitor::primeAllSlots()`
  primes all `MAX_OBJECTS` OlaConvolver quads, and the per-block Direct (B1) path
  runs `setDirection()` + `processBlockForObject()` — a dual-slot OlaConvolver
  convolution — for **every active object** (linear in object count, the
  dominant per-object binaural cost).
- **Object-count sweep:** {8, 16, 32, 64, 96, 128}. Counts above the compiled cap
  are SKIPPED (the 64 build runs ≤ 64 only; the 96/128 rows require the 128
  build).
- **Per-block timing:** raw `std::chrono::steady_clock` timestamps are captured
  around each `audioBlock()` into a per-scenario vector (2000 warmup blocks,
  then 8000 timed blocks at 48 kHz / 64-sample). **median / p99 / peak (true
  max)** are computed EXACTLY from the sorted vector — NOT from CpuMeter's P²
  estimate. `CpuMeter::cpuPct()/peakPct()/p99Us()` are ALSO read for cross-check
  (the P² p99 is informational and can under-report a rare tail, per amendment
  5; the authoritative backstops are the exact peak + xruns).
- **xruns:** `SpatialEngine::engineOverrunCount()`.
- **RT budget:** `block / sample_rate = 64 / 48000 ≈ 1333.3 µs`. The hard gate is
  **peak ≤ 50% budget (≤ 666.7 µs) AND xruns == 0** at the compiled cap.
- **Memory:** `getrusage(RUSAGE_SELF).ru_maxrss` (KB on Linux), read after the
  heaviest config is fully primed + warmed. `ru_maxrss` is a process-wide
  high-water mark (includes the harness baseline ≈ 5–6 MB), so it is
  cross-checked against a `/proc/self/statm` lifecycle probe and the structural
  size model below.

Timing was run on an otherwise-idle machine; each config was run multiple times.
Median/p99 are stable run-to-run; the true peak shows the expected OS-scheduling
jitter (±~70 µs at 128). Representative numbers below.

---

## C-M3 — RT budget table

RT budget = **1333.3 µs/block**. 50% gate = **666.7 µs**.

### 64 build (`core/build_obj128_rel` ⇒ no; `core/build_rel`, cap = 64)

| obj | median µs | p99 µs | peak µs | med %bud | p99 %bud | peak %bud | xruns |
|----:|----------:|-------:|--------:|---------:|---------:|----------:|------:|
| 8   |  29.7 |  33.7 |  58.7 |  2.2% |  2.5% |  4.4% | 0 |
| 16  |  53.4 |  64.7 | 201.0 |  4.0% |  4.9% | 15.1% | 0 |
| 32  | 100.5 | 118.4 | 250.8 |  7.5% |  8.9% | 18.8% | 0 |
| 64  | 197.0 | 223.2 | 251.7 | 14.8% | 16.7% | 18.9% | 0 |
| 96  | — | — | — | — | — | — | SKIP (> cap) |
| 128 | — | — | — | — | — | — | SKIP (> cap) |

**64-cap gate: PASS** — peak 251.7 µs (18.9% budget) ≤ 666.7 µs, xruns 0.

### 128 build (`core/build_obj128_rel`, cap = 128)

| obj | median µs | p99 µs | peak µs | med %bud | p99 %bud | peak %bud | xruns |
|----:|----------:|-------:|--------:|---------:|---------:|----------:|------:|
| 8   |  30.6 |  33.2 |  48.5 |  2.3% |  2.5% |  3.6% | 0 |
| 16  |  54.0 |  58.1 | 148.4 |  4.1% |  4.4% | 11.1% | 0 |
| 32  | 101.7 | 112.6 | 249.5 |  7.6% |  8.4% | 18.7% | 0 |
| 64  | 196.8 | 217.6 | 330.7 | 14.8% | 16.3% | 24.8% | 0 |
| 96  | 296.4 | 320.9 | 392.5 | 22.2% | 24.0% | 29.4% | 0 |
| 128 | 400.8 | 430.8 | 593.4 | 30.0% | 32.4% | 44.5% | 0 |

(median/p99 stable; peak is the worst single block observed across runs — ranged
557.9–628.9 µs at 128, i.e. 41.8%–47.2% budget.)

**128-cap gate: PASS** — peak ≤ ~629 µs (≤ 47.2% budget) < 666.7 µs across all
runs, xruns 0. p99 (informational) ≈ 32% budget; median ≈ 30% budget.

**RT observation:** per-block cost scales close to linearly with object count
(~3.1 µs/object median), dominated by the per-object Direct binaural convolution.
The 128 peak sits just under the 50% line with this jitter — a busier or weaker
box could cross it, so the RT gate is "PASS but tight" at 128 on this hardware.

---

## C-M4 — Memory footprint

`ru_maxrss` at the compiled cap, heaviest primed config:

| build | RSS at cap | ceiling | verdict |
|---|---:|---:|---|
| 64  | **129.2–129.4 MB** | 100 MB | **FAIL** |
| 128 | **250.1–250.4 MB** | 100 MB | **FAIL** |

Both configs exceed the 100 MB ceiling — the **64 build already fails** (this is
not a regression introduced by the bump; it is a pre-existing footprint the gate
surfaces for the first time).

### Lifecycle probe (`/proc/self/statm`, fresh process, no sweep accumulation)

| stage | 64, no binaural | 64, binaural | 128, binaural |
|---|---:|---:|---:|
| after construct | 5.6 MB | 5.3 MB | 6.4 MB |
| **after prepareToPlay** | **124.6 MB** | **129.0 MB** | **250.4 MB** |
| after warmup (cap active) | 124.6 MB | 129.0 MB | 250.4 MB |
| after engine destroyed | 6.1 MB | 10.4 MB | 14.5 MB |

The jump happens entirely in `prepareToPlay`, and **~95% of it is present even
with binaural OFF** (124.6 MB without binaural; binaural adds only ~4.4 MB). So
the binaural OlaConvolvers are NOT the dominant term.

### Where the memory actually goes (structural size model vs measured)

`sizeof` measured on this build:

| structure | per-unit | count @64 / @128 (8 spk) | @64 | @128 |
|---|---:|---|---:|---:|
| `dsp::DelayLine` (`std::array<float,48000>`) | 187.5 KB | — | — | — |
| **`WFSRenderer::delays_`** `std::vector<DelayLine>` sized **MAX_OBJECTS × num_speakers** (`WFSRenderer.cpp:24-26`) | 187.5 KB ea | 512 / 1024 lines | **~96 MB** | **~192 MB** |
| `SpatialEngine::chains_` `std::vector<PerObjectChain>` — each chain holds **2** DelayLines (`user_delay_` + `prop_.delay_`), 375.3 KB ea (`PerObjectChain.h:84`, `PropagationDelay.h:50`) | 375.3 KB ea | 64 / 128 chains | **~23.5 MB** | **~46.9 MB** |
| BinauralMonitor `obj_slots_` OlaConvolver primes (`primeAllSlots`, 4 conv/obj × ir 1024+overlap 1023+work ~1087) | ~50 KB ea | 64 / 128 | ~3.5 MB | ~7 MB |
| VBAP/DBAP `ramps_`, `dry_scratch_`, mix/scratch bufs, reverb, misc | small | — | ~1–2 MB | ~2–3 MB |
| **modelled total** | | | **~124 MB** | **~248 MB** |
| **measured `prepareToPlay` RSS** | | | **124.6 MB** | **250.4 MB** |

Model and measurement agree within ~1% — the footprint is fully explained.

### The dominant term the §0.4 plan model MISSED

§0.4 of the Lane C plan modelled `chains_` (DelayLine) as the dominant ~24→48 MB
term and the OlaConvolver pairs as the second concern. **Both are real but
secondary.** The actual dominant term is **`WFSRenderer::delays_`**: a
`std::vector<spe::dsp::DelayLine>` sized `MAX_OBJECTS * num_speakers`
(`core/src/render/WFSRenderer.cpp:24-26`, `WFSRenderer.h:37`). At 8 speakers that
is **8 × the per-object delay-line cost**, and it scales with
`MAX_OBJECTS × num_speakers`:

- 64 obj × 8 spk × 187.5 KB ≈ **96 MB**
- 128 obj × 8 spk × 187.5 KB ≈ **192 MB**
- (a larger speaker layout makes this worse linearly — e.g. 24 spk × 128 obj
  ≈ 576 MB.)

This is allocated **unconditionally at every `prepareToPlay`**, regardless of
whether WFS is the active algorithm or whether any object actually uses WFS.

### F3 (lazy-prime) promotion decision

The C-M4 amendment-7 trigger (RSS ≥ ~80 MB) fires. **However, F3 as scoped
(lazy-prime the *binaural* OlaConvolvers) does NOT solve this gate** — the
OlaConvolvers are only ~4–7 MB. Promoting F3 alone would leave RSS at ~120 MB
(64) / ~243 MB (128), still over the ceiling.

The real remediation targets are the per-(object×speaker) and per-object
**delay-line** allocations:

1. **WFS `delays_` (dominant):** allocate lazily / only when WFS is the active
   algorithm, or down-size the per-cell DelayLine (a full 1 s / 48000-sample
   line per object×speaker is almost certainly oversized for WFS path-length
   delays), or share/pool delay lines.
2. **`chains_` DelayLines (secondary):** the `user_delay_` + `prop_.delay_` 1 s
   lines per object are likewise oversized for typical use.
3. **F3 binaural lazy-prime (tertiary):** the originally-scoped item; worthwhile
   but small.

All three are **engine-DSP changes** — explicitly OUT of scope for C-M3/C-M4
(test/perf + docs only). They are reported here as the evidence that the C-M4
gate fails and what must change before it can pass. This is a follow-up
milestone (call it C-M4b / a new Lane C memory-remediation milestone), to be
re-planned via `ralplan` before any DSP edit.

---

## C-M7 (default-flip) threshold — NOT met

C-M7 flips the shipped default to 128 only if (heaviest path):
**peak ≤ ~35% budget AND max-RSS < ~70 MB AND xruns == 0.**

Measured at 128: peak ≈ **42–47% budget** (> 35%), RSS ≈ **250 MB** (≫ 70 MB),
xruns 0.

→ **DO NOT flip the default. Keep `SPATIAL_ENGINE_MAX_OBJECTS` default = 64.**

The RT criterion is close (just over 35%) but the memory criterion fails by a
wide margin. Even the *opt-in* 128 config is not deployable as-is at 250 MB; and
the 64 config's own 129 MB is over the C-M4 100 MB ceiling. **The C-M4 memory
gate must be remediated (WFS/chains delay-line sizing) before either (a) the
default can flip to 128, or (b) the 100 MB gate can be claimed PASS at any cap.**

---

## How to reproduce

```bash
# 64 (default)
cmake -S core -B core/build_rel \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DCMAKE_BUILD_TYPE=Release -DSPATIAL_ENGINE_MAX_OBJECTS=64
cmake --build core/build_rel --target perf_obj_block_time -j$(nproc)
./core/build_rel/tests/perf/perf_obj_block_time          # exit 1 (RSS gate fails)

# 128
cmake -S core -B core/build_obj128_rel \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DCMAKE_BUILD_TYPE=Release -DSPATIAL_ENGINE_MAX_OBJECTS=128
cmake --build core/build_obj128_rel --target perf_obj_block_time -j$(nproc)
./core/build_obj128_rel/tests/perf/perf_obj_block_time   # exit 1 (RSS gate fails)
```

The harness prints the full table and exits non-zero if any gate fails (it
currently fails on the RSS gate at both caps — by design, this is the real
evidence, not a faked pass).
