# RT-Budget — SPEAKER sweep (v1.0 Phase 1.4)

Measured deliverable for **Phase 1.4** of the v1.0 full-coverage plan
(`.omc/plans/spatial-engine-v1-full-coverage-plan.md`). Closes the architect's
flagged measurement gap: all prior RT measurement (`RT_BUDGET_MAX_OBJECTS.md`,
`perf_obj_block_time`) swept the **object** dimension at a **fixed 8 speakers**
and never measured how per-block cost grows with the **speaker** (output-channel)
dimension — even though the spec targets **128 channels** and several
subsystems scale with speaker count.

**Headline:**

| Path | Scales to 128 spec? | Verdict |
|---|---|---|
| **Core panners** (render-only: VBAP/DBAP/VAP/WFS + per-speaker mix) | **Yes** — peak ≤ 30.6% budget at 128 spk | **PASS** |
| **Room reverb** (render + spatial room) | **No** — ~**O(spk²)**, real-time only to ~16 spk | spec-vs-runtime boundary, **optimization flagged** |

The **DoD §1 spec scenario (8 spk + room) PASSES** at 32.6% peak (< 40% target).
The room reverb is real-time at small/medium arrays but does **not** reach the
128-channel spec in real time on a single core; this is the documented
"spec envelope vs runtime envelope" the plan's risk section anticipated.

---

## Machine spec (results are box-specific)

| Field | Value |
|---|---|
| CPU | Intel(R) Core(TM) i7-14700K |
| `nproc` | 28 |
| OS | Linux 6.14 |
| Build | `-DCMAKE_BUILD_TYPE=Release` (optimized; required for accurate timing) |
| Config flag | `-DSPATIAL_ENGINE_MAX_SPEAKERS=128` |
| RT budget | block / sample_rate = 64 / 48000 ≈ **1333.3 µs** |

---

## Methodology

Harness: `core/tests/perf/perf_speaker_sweep.cpp` (ctest `perf_speaker_sweep`,
gated on `Release` OR `SPATIAL_ENGINE_RUN_SOAK`).

- **Fixed 32 active VBAP objects** (the DoD object count); **sweep the speaker
  count** {8, 16, 24, 32, 48, 64, 96, 128} (counts > compiled cap skipped).
- Speakers spread over the unit sphere via the Fibonacci/golden-spiral so the
  panners do real per-speaker triangulation at any K.
- Two configs per count: **render-only** (panning + per-speaker mix) and
  **render+room** (adds the spatial room reverb, `/reverb/select room`, with a
  0.5 reverb send per object).
- Raw `std::chrono::steady_clock` around each `audioBlock()`; exact
  median/p99/peak from the sorted vector. 1000 warmup + 4000 timed blocks.
- **Gates:** render-only peak < 50% budget at every count (core scales to spec);
  render+room peak < 100% budget (real-time) up to 16 spk (room RT ceiling).

---

## Results (peak per-block, % of 1333.3 µs budget)

| spk | render-only median / peak | %bud (peak) | render+room median / peak | %bud (peak) |
|----:|---:|---:|---:|---:|
| 8   | 41.8 / 116.1 µs  | 8.7%  | 268.0 / 434.2 µs    | **32.6%** |
| 16  | 53.6 / 149.0 µs  | 11.2% | 448.0 / 714.4 µs    | 53.6% |
| 24  | 64.0 / 196.4 µs  | 14.7% | 1099.5 / 1321.4 µs  | 99.1% |
| 32  | 82.0 / 119.3 µs  | 8.9%  | 2246.7 / 3298.9 µs  | 247.4% |
| 48  | 110.3 / 151.1 µs | 11.3% | 6629.5 / 7293.2 µs  | 547.0% |
| 64  | 130.9 / 261.7 µs | 19.6% | 14410.9 / 14845.5 µs| 1113.4% |
| 96  | 174.4 / 407.6 µs | 30.6% | 44164.0 / 46271.5 µs| 3470.4% |
| 128 | 217.4 / 347.9 µs | 26.1% | 98586.4 / 106591.6 µs | 7994.4% |

(0 engine overruns in every scenario. Block=64, 48 kHz.)

### Core panners — linear, scales to spec
render-only median rises ~5× from 8→128 spk (41.8 → 217.4 µs) — sub-linear in
practice (per-object VBAP gains are cached in `VBAPRenderer`, so the per-block
cost is dominated by the O(spk) per-speaker mix). Peak stays ≤ 30.6% of budget
even at 128 channels. **The core rendering path meets the 128-channel spec with
comfortable headroom.**

### Room reverb — O(spk²) wall
render+room median rises **~368×** from 8→128 spk (268 → 98 586 µs) for a 16×
speaker increase ⇒ **≈ O(spk²·¹)**. It crosses 50% budget at ~16 spk, 100%
(real-time) at ~24 spk, and is **8 000% of budget at 128 spk**.

**Root cause:** the room path recomputes analytic-VBAP gains **every block,
uncached**, for **dynamic** per-block directions, many times per block:
- `renderRoomEarly`: per active object × 6 image rings × 3 width-spread samples →
  `vbap_gain_into` (the analytic reference is ~O(spk²) for triangulation).
- `computeLateFdnGains`: 8 late FDN lines, opp-biased directions that change
  every block with source energy → 8 × `vbap_gain_into`.
- cluster fan-out: another opp-biased `vbap_gain_into`.

So room-early alone ≈ `obj × 18 × O(spk²)` per block. Unlike `VBAPRenderer`'s
positional cache, these directions are **dynamic per block** (object motion +
opp-bias steering), so the existing positional cache cannot help.

**Fix (flagged follow-up, not in Phase 1.4):** replace the per-block O(spk²)
analytic VBAP in the room path with a sub-O(spk²) gain lookup (precomputed
triangulation / kd-tree nearest-speaker, already present as `KdTree3d` in the
tree used elsewhere), or quantise+cache the room directions. This is an
algorithmic change with its own correctness surface — deferred to a dedicated
performance increment / v1.1. Large-array (> ~24 spk) **room reverb** is not
real-time until then; **panning** at 128 spk is fine today.

---

## DoD status (Phase 1 §"10.성능 실측 충족")

- **① 32 obj / 8 spk + room ≤ 40%:** ✅ render+room peak **32.6%** at 8 spk.
  (Binaural adds an O(obj), not O(spk), term — measured separately in
  `RT_BUDGET_MAX_OBJECTS.md`; the 64-obj/8-spk + B2 binaural peak is 25.3%.)
- **② 128 spk envelope measured + documented:** ✅ this document — core panners
  scale (≤ 30.6%); room reverb O(spk²) wall documented with root cause.
- **③ denormal stall removed:** ✅ Phase 1.1 (`test_convergence_denormal_guard`).
- **④ room+decorr practical-scene soak xrun=0:** pending (Phase 1.4 soak step) —
  must use a **practical** speaker count (≤ ~16) given the room O(spk²) wall.

---

## Reproduce

```
cmake -S . -B build-rel128 -DCMAKE_BUILD_TYPE=Release \
  -DSPATIAL_ENGINE_MAX_SPEAKERS=128 -G "Unix Makefiles"
cmake --build build-rel128 --target perf_speaker_sweep --parallel
./build-rel128/core/tests/perf/perf_speaker_sweep
```
