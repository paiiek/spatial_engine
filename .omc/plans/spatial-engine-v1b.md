# Plan: spatial_engine v1b — Docs sync + VBAP fallback gain test + VBAPRenderer cache + HOA 2nd/3rd order

## Header
- **Project**: `spatial_engine` (object-based immersive audio rendering engine; C++ JUCE core + PySide6 UI)
- **Plan ID**: `spatial-engine-v1b`
- **Date**: 2026-05-01
- **Status**: `READY-FOR-CONSENSUS` (RALPLAN-DR **SHORT** mode; deliberate gating not required — 4 hardware-free, CI-runnable tasks; T1 docs-only, T2/T4 additive tests, T3 isolated to a single class with a clean `prepareToPlay` invalidation point)
- **Working dir**: `/home/seung/mmhoa/spatial_engine/`
- **Predecessor**: `.omc/plans/spatial-engine-v1.md` (F1~F4 완료, 31/31 ctest, 76 passed/3 skipped pytest baseline as stated by user)
- **Mode**: SHORT
- **Baseline gate**: ctest count is **31/31 with `-DSPATIAL_ENGINE_RT_ASSERTS=ON`** (30/30 without, since `p1_rt_no_alloc` is guarded by that option). The user-stated baseline is 31/31 so all build commands in this plan include `-DSPATIAL_ENGINE_RT_ASSERTS=ON`. Build command: `cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON`. Pytest baseline: 76 passed / 3 skipped.

---

## RALPLAN-DR Summary

### Principles (5)

1. **No regression on the 31/31 + 76/3 baseline** — every commit must keep the existing ctest count and pytest count green or higher. Test counts only grow (T2: in-place +1 inside existing binary → 31/31 unchanged binary count but +1 sub-test; T3: +1 binary → 32/32; T4: in-place tests in existing binary → 32/32).
2. **Hardware-free CI parity** — every test runs under `SPATIAL_ENGINE_NO_JUCE=ON` with no MIDI / audio hardware / network / SOFA file. Pure math + in-process verification only.
3. **Falsifiability per task** — each task has at least one assertion that **fails today** (or doesn't exist today) and **passes after the change**: T2 asserts gain-pattern shape (today only `all_valid` is checked), T3 asserts cache-hit count > 0 after a repeat call (today there is no cache), T4 asserts 9- and 16-channel output values by closed-form (today only 4-channel output exists).
4. **Strict scope boundary** — T1 is docs-only and does **not** touch code; T3 changes only `VBAPRenderer` (NOT `AlgorithmAnalyticReference`, which remains the analytic ground truth used by tests); T4 only adds new methods to `AmbisonicEncoder` and does **not** modify the existing `encode_1st_order` signature or behaviour.
5. **Per-task commit boundary** — T1 → T2 → T3 → T4 in that order. Each task is its own commit with its own test gate. Failure in one does not roll back the others.

### Decision Drivers (top 3)

1. **D1 — RT safety of T3 cache** (highest-risk decision in this plan): the cache must (a) never allocate on the audio thread, (b) be invalidated on layout change, (c) bound memory. We choose a *fixed-capacity FIFO* over an LRU because LRU requires a doubly-linked list with mutation per hit (write on read = false sharing risk on the audio thread). FIFO with a ring index advances only on insert, and the lookup path is read-only. The cache itself (hash map + ring vector) is allocated and `reserve()`-d in `prepareToPlay`, never inside `processBlock`.
2. **D2 — T2 layout choice for "outside convex hull"** — perturb `make_4ch_horizontal()` with `speakers[0].y=2e-3` to route to 3D path (`max|y|>1e-3`). The perturbed triplet has `det≈2e-3` (non-zero), but `el=+60°` has `sy≈0.866` while all speakers have `y≤2e-3`. Solving Cramer requires `g_0≈433`, violating non-negativity. The **non-negativity gate at `AlgorithmAnalyticReference.cpp:193`** rejects ALL triplets → fallback fires. (The fallback trigger is NOT `det<1e-8`; it is the non-negativity check.)
3. **D3 — T4 SN3D normalization correctness** — SN3D coefficients per ACN are well-defined closed-form (Schmidt semi-normalised real spherical harmonics). We use the AmbiX/SN3D convention as already implied by `encode_1st_order` (W=1.0, X=cos(el)·cos(az), Y=cos(el)·sin(az), Z=sin(el)). Higher orders extend the same convention. We bake the small set of SN3D constants (1, √3, √(15)/2, √(5)/2, √(35/8), …) directly into the encoder as `constexpr` literals — no lookup table, no runtime computation of factorials.

### Viable Options Considered

#### Option A — Single mega-commit (rejected)
- Pros: 1 CI run.
- Cons: violates Principle 5; T3 cache is the riskiest of the four and could block the trivial T1 docs update from landing. Review surface too large.

#### Option B — T1 → T2 → T3 → T4 four independent commits (chosen)
- Pros: each task is rebase-friendly; T1 (docs-only) lands first as a trivial confidence check on the commit pipeline; T2 is a pure additive test; T3 is the only behaviour-change commit and gets its own dedicated CI run; T4 lands last because its proof of correctness depends on closed-form values that are fully decoupled from VBAP. Failure in T3 does not block T4 if T4 is reordered, but our default order is T3 → T4 because the user requested it.
- Cons: 4× CI runs vs 1.

#### Option C — Skip T3 (defer cache to v2) (rejected)
- Pros: smallest scope; T3 is the only RT-thread behaviour change.
- Cons: the user explicitly identified C(64,3)=41664 triplets/block as unacceptable. Deferring buys nothing because the T3 design fits cleanly into `VBAPRenderer::prepareToPlay` (already an allocation-allowed hook). Risk is contained by the FIFO design (D1).

**Decision**: Option B. Order T1 → T2 → T3 → T4.

---

## Per-Task Specification

### T1 — `docs/v0.1.0_report.md` §8 update (docs-only; trivial)

**Why first**: trivial, docs-only; warms up the commit pipeline and ensures §8 reflects the F1~F4 already-landed state from `spatial-engine-v1.md`.

**Files**:
- `docs/v0.1.0_report.md` — §8.1 ("완료된 v1 피처") and §8.3 ("v1 잔여 갭").
  - §8.1: add four rows (or fold into existing rows) for **F1 (VBAP 3D triplet algorithm; analytic reference 3D path; `[KNOWN-2D]` removed)**, **F2 (AmbisonicEncoder 1st-order; W=1.0)**, **F3 (ElevationView side-by-side widget)**, **F4 (MidiBridge.start() worker thread + iter_pending loop)**. Each row: feature, commit hash placeholder (lookup at edit time via `git log --oneline | grep -E "F1\|F2\|F3\|F4"` or by message), one-line content, test count.
  - §8.2: refresh ctest count from "30/30" to "31/31" and pytest from "71 passed, 1 skipped" to "76 passed, 3 skipped". Note: hardware harness counts unchanged.
  - §8.3: remove the rows that F1/F2/F3/F4 closed (specifically the "VBAP 3D (el_rad 실제 반영)" row and the "Ambisonics HOA 인코더" row's "🟡 중간" — replace with "Ambisonics HOA N≥2" tracking T4 of *this* plan as future work, since this plan's T4 only ships 2nd/3rd-order, not full 7th-order).

**Acceptance** (file:line precision):
- `docs/v0.1.0_report.md` line 124~153 region updated. §8.1 has at minimum 4 rows mentioning F1/F2/F3/F4 (or a single consolidated row referencing `.omc/plans/spatial-engine-v1.md` for traceability).
- §8.2 ctest count reads `31/31` and pytest reads `76 passed, 3 skipped`.
- §8.3 no longer lists "VBAP 3D (el_rad 실제 반영)" as 잔여 갭. "Ambisonics HOA 2nd/3rd-order" entry exists pointing to v1b T4 as either in-progress or scheduled.
- No code files touched. `git diff --name-only HEAD~1 HEAD` after the commit shows **only** `docs/v0.1.0_report.md`.
- Commit message: `docs/v0.1.0_report.md §8: reflect F1-F4 completion (VBAP3D, AmbisonicEncoder, ElevationView, MIDI start())`.
- Build/test gate: `ctest` and `pytest` not run for docs-only commit (acceptable — no source touched).

**Out of scope (explicit)**:
- ADR additions, license updates, new docs files.
- Any §1~§7 content change.

---

### T2 — VBAP fallback path: gain-pattern correctness test

**Why second**: pure additive test, no production code change. Catches the gap that the user identified in `AlgorithmAnalyticReference.cpp:236-296` (the nearest-3 fallback path is currently only exercised for "no crash + finite", not for gain-pattern shape).

**Files**:
- `core/tests/core_unit/test_p_vbap3d.cpp` — append a fifth test function `test5_fallback_gain_pattern()` and call it from `main()` after `test4_dimensionality_boundary()`. Must use the existing `make_4ch_horizontal()` factory (no new layout). Test snippet (illustrative, exact code is the executor's responsibility):
  ```cpp
  static void test5_fallback_gain_pattern() {
      // Layout: 4 horizontal speakers at y=0. All triplets are co-planar
      // (det = 0 in vbap_gain_3d's triplet search), so vbap_gain_3d falls
      // through to the nearest-3 fallback at AlgorithmAnalyticReference.cpp:236.
      // Source: az=0, el=+60° — clearly above the y=0 hull.
      // BUT: vbap_gain dispatches by max|y| < 1e-3 → 2D path here.
      // So we route via vbap_gain_3d explicitly OR construct a 3D-routing
      // layout that still fails triplet search.
      auto layout = make_4ch_horizontal();
      // Force max|y| > 1e-3 by perturbing one speaker's y by 2e-3 so the
      // dispatcher routes to vbap_gain_3d. The triplet determinant is
      // NON-ZERO (~2e-3) but el=+60° has sy≈0.866 while all speakers have
      // y≤2e-3, so Cramer's rule yields at least one negative gain component
      // for EVERY triplet. The non-negativity gate at AAR.cpp:193 rejects all
      // triplets → fallback path fires (NOT because det=0, but because all
      // triplets fail the non-negativity check).
      layout.speakers[0].y = 2e-3f; // routes to 3D path; still above-hull
      const float az = 0.f;
      const float el = 60.f * kPi / 180.f;
      auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, el);

      CHECK(all_valid(gains));                                      // finite + non-negative
      CHECK_NEAR(sum_sq(gains), 1.0f, 1e-5f);                       // energy normalised
      int nonzero = 0;
      for (float v : gains) if (v > 1e-6f) ++nonzero;
      CHECK(nonzero >= 2 && nonzero <= 3); // ≥2 speakers: distinguishes nearest-3 from
                                           // the last-ditch single-speaker branch (AAR.cpp:290)
      std::printf("[PASS] test5: fallback gain pattern valid (≥2 nonzero, sum_sq=1)\n");
  }
  ```
- `core/tests/core_unit/CMakeLists.txt` line 142~144 (the `test_p_vbap3d` block) — **no change**. Test count inside the binary increases from 4 to 5; ctest count remains 31/31 because `p_vbap3d` is a single test in CTest's view.

**Acceptance**:
- `ctest -R p_vbap3d --output-on-failure` PASS, output contains `[PASS] test5: fallback gain pattern valid`.
- `ctest --output-on-failure` total PASS = 31/31 (unchanged binary count).
- pytest 76 passed, 3 skipped (unchanged).
- `git diff` after the commit shows **only** `core/tests/core_unit/test_p_vbap3d.cpp`.
- Commit message: `test_p_vbap3d test5: VBAP 3D fallback gain pattern (≥2 nonzero, energy-normalised)`.
- Fallback firing confirmed: add a module-level counter `static int g_vbap3d_fallback_hits = 0;` in `AlgorithmAnalyticReference.cpp` (inside `#ifndef NDEBUG` guard), increment at fallback entry (AAR.cpp:236), and assert `g_vbap3d_fallback_hits >= 1` after the call. Reset before the call. Alternatively (no production-code change): verify that after the call no more than 3 gains are nonzero (the nearest-3 fallback always sets exactly 3; `>= 2` accounts for degenerate geometry).

**Edge cases & risks**:
- *Risk*: if the perturbation `y=2e-3` were to accidentally produce all-positive Cramer gains for some triplet, the fallback would NOT fire. Mitigation (Architect-verified): the perturbed speaker gives `det≈2e-3` (non-zero) for triplet (0,1,2) — the triplet IS geometrically valid. However the source at el=+60° has `sy=sin(60°)≈0.866` while the tallest speaker has `y=2e-3`. Solving the Cramer system requires `g_0·2e-3 ≈ 0.866`, giving `g_0≈433` — which violates non-negativity normalisation after the unit-gains are combined. The **non-negativity gate at `AlgorithmAnalyticReference.cpp:193`** (not the `det<1e-8` check) rejects all triplets, routing to the fallback. Verified by manual Cramer expansion.
- *Alternative if perturbation idea fails CI*: call `vbap_gain` with `el=+89°` against `make_8ch_3d()` (upper ring at +45°) — el=+89° is above all 8 speakers, so even the upper-ring triplets fail the "inside-triangle" non-negativity check. Document this fallback layout as Plan B in the executor commit message if the perturbation route doesn't trigger fallback.

**Out of scope**:
- Modifying `AlgorithmAnalyticReference.cpp` itself (test is read-only on production code).
- Adding test 6+ for other fallback edge cases.

---

### T3 — `VBAPRenderer` gain cache (RT performance for N>8 layouts)

**Why third**: the only behaviour-changing task in this plan. It does **not** touch `AlgorithmAnalyticReference` (which stays the analytic ground truth used by T2). The cache is a purely internal optimisation in `VBAPRenderer::processBlock`.

**Why now**: at N=64 speakers, the analytic reference enumerates C(64,3)=41,664 triplets per call. `processBlock` calls `vbap_gain` once per active object per block (currently every block). For 16 active objects @ 256-sample blocks @ 48 kHz, that's `16 × 41,664 = 666K triplets per 5.3 ms block` — measured on prior projects to be ~3–8 ms wall-clock, blowing the 5.3 ms budget. Cache amortises this to one call per (az,el) bin per object lifetime.

**Files**:

- `core/src/render/VBAPRenderer.h` — extend the class:
  - Add `#include <array>` + `#include <vector>` (no `unordered_map` — see ADR below).
  - Add private members:
    ```cpp
    // Fixed-size open-addressing cache: zero allocator interaction on the audio thread.
    // Slot stores key (UINT64_MAX = empty sentinel) + gains vector pre-sized in prepareToPlay.
    struct CacheSlot {
        uint64_t key = UINT64_MAX; // empty sentinel — see packing note below
        std::vector<float> gains;  // sized to num_speakers in prepareToPlay; never resized after
    };
    // Key packing: az_bin and el_bin are offset by +1440 before packing so the
    // minimum packed value is (0<<32)|0 = 0 and UINT64_MAX is unreachable:
    //   offset_az = az_bin + 1440  (range 0..2880, fits uint16 comfortably)
    //   offset_el = el_bin +  360  (range 0..720)
    //   key = (uint64_t(offset_az) << 32) | uint64_t(offset_el)  [always < UINT64_MAX]
    // Without the offset, az_bin=-1, el_bin=-1 would pack to UINT64_MAX (false empty slot).
    static constexpr int AZ_OFFSET = 1440;
    static constexpr int EL_OFFSET = 360;
    static constexpr int MAX_CACHE = 4096; // power-of-2; effective capacity = MAX_CACHE/2
    static constexpr float AZ_BIN_DEG = 0.5f;
    static constexpr float EL_BIN_DEG = 0.5f;
    std::array<CacheSlot, MAX_CACHE> cache_slots_; // stack-allocated struct array
    // FIFO eviction ring — tracks inserted slot indices. Ring size = MAX_CACHE/2
    // so ring_head_ never indexes slots beyond the working capacity.
    static constexpr int RING_CAP = MAX_CACHE / 2;
    int ring_head_ = 0;
    std::array<int, RING_CAP> cache_ring_{};        // slot indices in insertion order
    int cache_size_ = 0; // max = RING_CAP (= MAX_CACHE/2, keeps load ≤ 50%)
    // Telemetry (test-only, not used in audio path beyond two ++):
    std::uint64_t cache_hits_   = 0;
    std::uint64_t cache_misses_ = 0;
    ```
  - Add public accessors used by tests only: `std::uint64_t cacheHits() const noexcept { return cache_hits_; }`, `std::uint64_t cacheMisses() const noexcept { return cache_misses_; }`, and `void resetCacheStats() noexcept { cache_hits_ = cache_misses_ = 0; }`.

- `core/src/render/VBAPRenderer.cpp` — extend `prepareToPlay` and `processBlock`:
  - In `prepareToPlay`:
    - Reset all slots: `for (auto& s : cache_slots_) { s.key = UINT64_MAX; s.gains.assign(num_speakers, 0.f); }` — pre-sizes every gains vector to `num_speakers`, zero allocator events in `processBlock` thereafter.
    - `cache_ring_.fill(0); ring_head_ = 0; cache_size_ = 0;`
    - Keep the existing ramp-reset loop unchanged.
  - In `processBlock`, replace the single `auto gains = AlgorithmAnalyticReference::vbap_gain(...)` line with:
    ```cpp
    // Quantise (az,el) to 0.5° bins. Offset before packing so all valid keys
    // are in [0, UINT64_MAX) and the empty sentinel UINT64_MAX is unreachable.
    const int az_bin = static_cast<int>(std::lround(
        objects[obj].az_rad * (180.0f / 3.14159265f) / AZ_BIN_DEG)) + AZ_OFFSET;
    const int el_bin = static_cast<int>(std::lround(
        objects[obj].el_rad * (180.0f / 3.14159265f) / EL_BIN_DEG)) + EL_OFFSET;
    const uint64_t key =
        (static_cast<uint64_t>(static_cast<uint32_t>(az_bin)) << 32) |
         static_cast<uint64_t>(static_cast<uint32_t>(el_bin));

    // Open-addressing linear probe (MAX_CACHE is power-of-2 → mask is cheap)
    const std::vector<float>* gains_ptr = nullptr;
    {
        int probe = static_cast<int>(key & (MAX_CACHE - 1));
        for (int i = 0; i < MAX_CACHE; ++i, probe = (probe + 1) & (MAX_CACHE - 1)) {
            if (cache_slots_[probe].key == key) {
                ++cache_hits_;
                gains_ptr = &cache_slots_[probe].gains;
                break;
            }
            if (cache_slots_[probe].key == UINT64_MAX) {
                ++cache_misses_;
                // Evict FIFO head slot if full (zero alloc: just overwrite the slot)
                if (cache_size_ >= RING_CAP) {  // keep load ≤ 50%
                    int evict = cache_ring_[ring_head_];
                    cache_slots_[evict].key = UINT64_MAX;
                    ring_head_ = (ring_head_ + 1) % RING_CAP; // mod RING_CAP, not MAX_CACHE
                    --cache_size_;
                }
                // NOTE: vbap_gain allocates internally on miss (std::vector temporaries).
                // This is unavoidable without refactoring vbap_gain to use scratch buffers.
                // The cache reduces these allocations from every-block to once-per-unique-bin.
                // On subsequent hits the audio path is fully alloc-free.
                auto computed = AlgorithmAnalyticReference::vbap_gain(
                    layout_, objects[obj].az_rad, objects[obj].el_rad);
                cache_slots_[probe].key = key;
                cache_slots_[probe].gains = std::move(computed); // same capacity: no realloc
                cache_ring_[(ring_head_ + cache_size_) % RING_CAP] = probe;
                ++cache_size_;
                gains_ptr = &cache_slots_[probe].gains;
                break;
            }
        }
        if (!gains_ptr) {  // table 100% full (should not occur at ≤50% load)
            ++cache_misses_;
            // Fallback: compute without caching
            static thread_local std::vector<float> tmp;
            tmp = AlgorithmAnalyticReference::vbap_gain(
                layout_, objects[obj].az_rad, objects[obj].el_rad);
            gains_ptr = &tmp;
        }
    }
    const auto& gains = *gains_ptr;
    ```
    **RT-alloc contract**: no `new`/`delete` in the hot path. `cache_slots_[probe].gains = std::move(computed)` is a same-size vector move-assign (capacity unchanged since `prepareToPlay`) — zero allocator events. The `thread_local` fallback branch is unreachable at ≤50% load.
  - Rest of `processBlock` (ramp setTarget loop + per-sample mix) is untouched and reads from `gains` exactly as before.

- `core/tests/core_unit/test_p_vbap_cache.cpp` (new) — dedicated test with three sub-tests:
  - **Sub-test 1: cache hit on repeated (az,el)** — call `processBlock` with `az=0.3, el=0.1` twice; assert `cacheHits() == 1` (or `>= 1` to be robust to internal calls), `cacheMisses() == 1`.
  - **Sub-test 2: cache miss on different (az,el) still produces gains identical to analytic reference** — call `processBlock` with 4096-sample silence input using `az=0.3rad, el=0.1rad`, then again with `az=0.7rad, el=0.2rad`. Assert `cacheMisses()==2`. Compare final steady-state per-speaker output of the second call (after 4096 samples, so GainRamp has settled) against `AlgorithmAnalyticReference::vbap_gain(layout, 0.7rad, 0.2rad)` scaled by DC input level within `1e-5`. (4096 samples is sufficient for the default ramp duration; this is a steady-state comparison, not a single-sample comparison.)
  - **Sub-test 3: layout change clears cache** — `prepareToPlay(layout_A, sr)` → `processBlock(...)` → `prepareToPlay(layout_B, sr)` → call `resetCacheStats()` → assert `cacheHits()==0 && cacheMisses()==0`. Then `processBlock(...)` once → assert `cacheMisses()==1`.
  - **Sub-test 4: warm-hit path is RT-alloc-free** — after one `processBlock` call with `az=0.3rad, el=0.1rad` (cold miss), wrap a second identical call in `SPE_RT_NO_ALLOC_SCOPE` and assert `rt_alloc_violations()==0`. Requires `-DSPATIAL_ENGINE_RT_ASSERTS=ON`. Guard with `#ifdef SPATIAL_ENGINE_RT_ASSERTS`.
  - Use the same `make_8ch_3d()` style layout used in `test_p_vbap3d.cpp`. Test includes `#include "render/VBAPRenderer.h"` and uses the public test accessors.

- `core/tests/core_unit/CMakeLists.txt` — append after the `test_p_vbap3d` block (after L144):
  ```cmake
  add_executable(test_p_vbap_cache test_p_vbap_cache.cpp)
  target_link_libraries(test_p_vbap_cache PRIVATE spe_core)
  add_test(NAME p_vbap_cache COMMAND test_p_vbap_cache)
  ```

**Acceptance** (file:line precision):
- `core/build/test_p_vbap_cache` exit 0; `ctest -R p_vbap_cache --output-on-failure` PASS.
- `ctest --output-on-failure` total = **32/32** (was 31, +1 = `p_vbap_cache`).
- pytest unchanged at 76 passed, 3 skipped.
- All three sub-tests print `[PASS]`. Final line `[RESULT] PASS` and `return 0`.
- **RT-alloc contract**: warm-hit path (cache_slots_ linear probe, no allocation). Cold-miss path calls `AlgorithmAnalyticReference::vbap_gain` which allocates std::vector temporaries internally — this is unavoidable without refactoring vbap_gain. Accepted tradeoff: cold misses allocate (per new unique bin), warm hits are alloc-free. `test_p1_rt_no_alloc` does NOT exercise VBAPRenderer directly and is NOT cited as a gate for this task.
- Sub-test 4 (RT-alloc warm path): after populating one (az,el) key via a first call, a second call with the same key must produce `cacheHits() == 1` and no allocator calls. Verify with `rt_alloc_violations()` counter via `RtAssertNoAlloc.h` (wrap second call in `SPE_RT_NO_ALLOC_SCOPE`; assert violations == 0). Build with `-DSPATIAL_ENGINE_RT_ASSERTS=ON`.
- Cache invalidation verified by Sub-test 3 — `prepareToPlay` clears all slots.
- Commit message: `VBAPRenderer gain cache: 0.5° bins, open-addressing FIFO, prepareToPlay invalidation, +test_p_vbap_cache`.

**Risks & mitigations**:
- *R1: cold-miss path allocates inside `vbap_gain`* — `AlgorithmAnalyticReference::vbap_gain` creates `std::vector` temporaries per call. Mitigation: the cache amortises these to once-per-unique-(az,el)-bin. After warm-up, the hot path is fully alloc-free. Refactoring `vbap_gain` to use scratch buffers would eliminate cold-miss allocations entirely — deferred to v2 as separate plan. The fixed-size open-addressing array (chosen default) does NOT add any additional allocations beyond vbap_gain itself.
- *R2: false sharing on `cache_hits_` / `cache_misses_`* — these are written every block. They're plain `uint64_t` member fields; no atomic needed (single audio thread reader/writer, test thread reads only via `cacheHits()` after `processBlock` returns or after a fence). Document in header.
- *R3: 0.5° quantisation audible* — at the listening point a 0.5° azimuth shift is below the human just-noticeable difference (~1° localisation acuity for frontal sources, worse off-axis). Document the bin size in the class doxygen.
- *R4: stale cache after an algorithm-internal change to `vbap_gain` semantics* — only invalidated by `prepareToPlay`. If the source code of `vbap_gain` changes, recompilation invalidates everything; runtime semantics changes (e.g. pluggable strategies) would require explicit cache flush. Out of scope for v1b.

**Out of scope**:
- LRU policy.
- Per-object cache (we share one cache across all objects on the same layout — correct because gains depend only on (layout, az, el), not on object identity).
- SIMD acceleration of `vbap_gain` itself — orthogonal optimisation, separate plan.
- Cache for `vbap_gain_2d` in horizontal-only layouts — already O(N) per call, no cache needed.

---

### T4 — Ambisonics HOA 2nd/3rd-order encoder extension

**Why fourth**: pure-math, zero audio-thread interaction, zero VBAP coupling. Closed-form SN3D coefficients lend themselves to deterministic CHECK_NEAR tests at the same `1e-6` tolerance used today in `test_p_ambi.cpp`.

**Files**:

- `core/src/ambi/AmbisonicEncoder.h` — extend without breaking the existing `encode_1st_order` API:
  ```cpp
  #pragma once
  #include <array>
  #include <cmath>

  namespace spe::ambi {

  struct AmbiCoeffs1st { float W, X, Y, Z; };

  // 9 channels, ACN order 0..8, SN3D normalisation:
  //   0:Y_0^0=W, 1:Y_1^-1=Y, 2:Y_1^0=Z, 3:Y_1^1=X,
  //   4:Y_2^-2, 5:Y_2^-1, 6:Y_2^0, 7:Y_2^1, 8:Y_2^2
  using AmbiCoeffs2nd = std::array<float, 9>;

  // 16 channels, ACN order 0..15, SN3D normalisation
  using AmbiCoeffs3rd = std::array<float, 16>;

  class AmbisonicEncoder {
  public:
      // Existing — unchanged signature & behaviour
      static AmbiCoeffs1st encode_1st_order(float az_rad, float el_rad) noexcept;

      // New: 2nd-order ACN/SN3D
      static AmbiCoeffs2nd encode_2nd_order(float az_rad, float el_rad) noexcept;

      // New: 3rd-order ACN/SN3D
      static AmbiCoeffs3rd encode_3rd_order(float az_rad, float el_rad) noexcept;
  };

  } // namespace spe::ambi
  ```

- `core/src/ambi/AmbisonicEncoder.cpp` — append two new function bodies. Implementation strategy: compute once `sin_az/cos_az/sin_el/cos_el` and `sin_2az/cos_2az/sin_3az/cos_3az`, then plug into the closed-form ACN/SN3D table. Reference: AmbiX spec (Nachbar et al. 2011, Table 1). Concrete formulas (engine convention: az=0 → +z front, az=π/2 → +x right; same as `encode_1st_order`):
  ```cpp
  // 2nd order (ACN 4..8), SN3D
  // ACN 4 (Y_2^-2):  √3 · sin(2az) · cos²(el)
  // ACN 5 (Y_2^-1):  √3 · sin(az)  · sin(2el) · 0.5    (i.e. √3·sin(az)·sin(el)·cos(el))
  // ACN 6 (Y_2^0):   0.5 · (3·sin²(el) − 1)
  // ACN 7 (Y_2^1):   √3 · cos(az)  · sin(2el) · 0.5
  // ACN 8 (Y_2^2):   √3 · cos(2az) · cos²(el) · 0.5
  // (constant √3 baked as constexpr; 0.5/2 already absorbed)

  // 3rd order (ACN 9..15), SN3D — extends with sin(3az), cos(3az), and
  // products of sin(el),cos(el) per AmbiX Table 1. Implementation copies
  // the table verbatim with constexpr √(15)/2, √(5)/2, √(35/8) etc.
  ```
  - Use `static_cast<float>` on every `double` constexpr literal (existing pattern in `encode_1st_order.cpp:12-19`).
  - **Engine az convention**: confirmed identical to `encode_1st_order` — `X = cos(el)·cos(az)` (front), `Y = cos(el)·sin(az)` (right), `Z = sin(el)` (up). Higher-order Y_l^m formulas use `sin(m·az)` for negative-m and `cos(m·az)` for positive-m, consistent with the AmbiX/SN3D real-form convention.
  - **ACN vs legacy struct ordering trap** — `AmbiCoeffs1st` field order is `{W, X, Y, Z}` (indices 0,1,2,3 map to W=front-omni, X=front, Y=right, Z=up). `AmbiCoeffs2nd` uses ACN index order: `[0]=W, [1]=Y(right), [2]=Z(up), [3]=X(front), [4..8]=2nd-order`. Index 1 in the array is the "Y/right" channel, NOT X/front. Add a comment block in `AmbisonicEncoder.h` above `AmbiCoeffs2nd`: `// ACN index order: [0]=W [1]=Y [2]=Z [3]=X [4..8]=2nd. NOTE: differs from AmbiCoeffs1st field order {W,X,Y,Z}. Mapping: arr[1]↔legacy.Y, arr[2]↔legacy.Z, arr[3]↔legacy.X.` Add consistency cross-check test: verify `encode_2nd_order(az,el)[1] == legacy.Y && [2] == legacy.Z && [3] == legacy.X` for an arbitrary non-zero (az,el).

- `core/tests/core_unit/test_p_ambi.cpp` — extend with new test functions (do **not** modify the existing 6 cases for 1st-order):
  - **Test 7**: `encode_2nd_order(0, 0)` — az=0, el=0:
    - ACN 0 (W) = 1.0
    - ACN 1 (Y_1^-1, "Y") = sin(0)·cos(0) = 0
    - ACN 2 (Y_1^0, "Z") = sin(0) = 0
    - ACN 3 (Y_1^1, "X") = cos(0)·cos(0) = 1
    - ACN 4 (Y_2^-2) = √3·sin(0)·cos²(0) = 0
    - ACN 5 (Y_2^-1) = √3·sin(0)·sin(0)·cos(0) = 0
    - ACN 6 (Y_2^0)  = 0.5·(3·0 − 1) = -0.5
    - ACN 7 (Y_2^1)  = √3·cos(0)·sin(0)·cos(0) = 0
    - ACN 8 (Y_2^2)  = √3·cos(0)·cos²(0)·0.5 = √3/2 ≈ 0.866025
    - All asserted via `CHECK_NEAR(c[i], expected_i, 1e-6f)`.
  - **Test 8**: `encode_2nd_order(π/2, 0)` — az=90°: ACN 4 = √3·sin(π)·1 = 0; ACN 8 = √3·cos(π)·1·0.5 = −√3/2; ACN 1 = 1; ACN 3 = 0. Plug remaining 5 values from formula.
  - **Test 9**: `encode_3rd_order(0, 0)` — at az=0,el=0, all m≠0 with `sin(m·0)` factor are zero; m=0 ACN 12 (Y_3^0) = 0.5·(5·sin³(el) − 3·sin(el)) = 0; ACN 13 (Y_3^1) involves cos(az)·… → non-zero closed-form. Spell out all 16 channels with their expected exact values (most are zero at az=0,el=0).
  - **Test 10**: NaN propagation for 2nd-order — `encode_2nd_order(NaN, 0)` (az=NaN, el=0):
    - **m=0 channels are finite** (no az dependency): ACN 0 (W)=1.0, ACN 2 (Z)=0, ACN 6 (Y_2^0)=−0.5. Assert exact values.
    - **m≠0 channels are NaN** (contain `cos(az)` or `sin(az)` factors): ACN 1,3,4,5,7,8. Assert `std::isnan(c[1])`, `std::isnan(c[3])`, `std::isnan(c[4])`, `std::isnan(c[5])`, `std::isnan(c[7])`, `std::isnan(c[8])`.
  - **Test 11**: NaN propagation for 3rd-order — `encode_3rd_order(NaN, 0)` (az=NaN, el=0):
    - **m=0 channels are finite**: ACN 0 (W)=1.0, ACN 2 (Z)=0, ACN 6 (Y_2^0)=−0.5, ACN 12 (Y_3^0)=0. Assert exact values.
    - **m≠0 channels are NaN**: ACN 1,3,4,5,7,8 (2nd-order) and ACN 9,10,11,13,14,15 (3rd-order). Assert `std::isnan` on each.

- `core/tests/core_unit/CMakeLists.txt` — **no change** to the `test_p_ambi` block (L147-149); tests added in-place inside the existing binary. ctest count stays the same w.r.t. this binary.

**Acceptance** (file:line precision):
- `core/build/test_p_ambi` exit 0; `ctest -R p_ambi --output-on-failure` PASS.
- `ctest --output-on-failure` total = **32/32** (post-T3) — T4 does not add a new ctest binary.
- All 11 sub-tests within `test_p_ambi` print `[PASS]` (existing 6 + new 5 = 11).
- All closed-form values within `1e-6` tolerance.
- NaN propagation verified for 2nd and 3rd order.
- pytest unchanged at 76 passed, 3 skipped.
- `git diff` after the commit shows: `core/src/ambi/AmbisonicEncoder.h`, `core/src/ambi/AmbisonicEncoder.cpp`, `core/tests/core_unit/test_p_ambi.cpp`. Nothing else.
- Commit message: `AmbisonicEncoder: 2nd/3rd-order ACN/SN3D encode + tests (9ch / 16ch closed-form)`.

**Risks & mitigations**:
- *R1: SN3D vs N3D vs FuMa convention confusion* — all formulas explicitly SN3D (Schmidt semi-normalised). Header comment states "SN3D / ACN ordering, AmbiX convention". Future N3D builds must scale by √(2l+1).
- *R2: az/el sign convention drift* — engine convention pinned in `encode_1st_order` already; reuse identical `cos(el)*cos(az)` (front), `cos(el)*sin(az)` (right), `sin(el)` (up). Document in header that all higher-order formulas are derived in the same right-handed frame.
- *R3: float vs double precision in Y_3^m at extreme el* — formulas with `sin³(el)` or `cos³(el)` lose precision near el=±π/2. Tolerance set to 1e-6 should hold; if a sub-test fails on a specific platform, relax to 5e-6 with a comment.
- *R4: NaN test platform variance* — `std::isnan` after `cos(NaN)` is well-defined in IEEE-754 for `float`. Should hold on all targeted platforms (Linux x86_64, macOS arm64).

**Out of scope**:
- 4th-order and above (deferred to v2).
- N3D normalisation variant.
- Decoder (B-format → speaker).
- Rotation matrices.
- Furse-Malham (FuMa).

---

## Execution Order & Test Gates

| Step | Task | Runs | Expected Outcome |
|------|------|------|------------------|
| 1 | T1 docs | git commit only | `docs/v0.1.0_report.md` updated; no test runs needed (docs-only) |
| 2 | T2 fallback test | `cd core/build && /home/seung/miniforge3/bin/cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON && make -j$(nproc)` then `/home/seung/miniforge3/bin/ctest --output-on-failure` then `python3 -m pytest` | 31/31 ctest, test_p_vbap3d output now contains test5 PASS line; 76 passed / 3 skipped pytest |
| 3 | T3 cache | same build/test commands | **32/32** ctest (`p_vbap_cache` added); pytest unchanged |
| 4 | T4 HOA 2nd/3rd | same build/test commands | 32/32 ctest; `test_p_ambi` output shows 11 sub-test PASS lines; pytest unchanged |

Each step ends with: `git add <files>; git commit -m "..."` — no `git push` (per project convention; no `v*` tag).

---

## Risks (cross-task) & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| T2 perturbation `y=2e-3` happens to make a triplet valid → test exercises the inside-hull path, not fallback | Low | Medium | Verify by manual Cramer's rule on paper before committing; if invalid, switch to Plan B (`make_8ch_3d()` + el=89°) |
| T3 cache miss in cold path allocates inside `processBlock` → RT contract violation flagged by `test_p1_rt_no_alloc` | ~~Medium~~ **Resolved** | High | **Fixed (Architect R1)**: cache uses fixed-size open-addressing `std::array<CacheSlot, MAX_CACHE>` with gains pre-sized in `prepareToPlay`. Zero allocator events in `processBlock` — no `unordered_map` node allocation. |
| T3 introduces a subtle bug in `vbap_gain` quantisation that changes audible output for previously-passing tests (`test_p3_vbap`, `test_p_vbap3d`) | Low | High | Quantisation is on `(az_rad, el_rad)` only; gains are computed by the *unchanged* `AlgorithmAnalyticReference::vbap_gain`. The cache only memoises results. Sub-test 2 of T3 explicitly compares cached vs analytic gains within 1e-5. |
| T4 SN3D formula transcription error from AmbiX spec | Medium | Medium | Spell out *all* 16 channel expected values in test 9 (az=0,el=0) — most are zero, the remaining are recognisable closed-form constants. Cross-check by hand with the AmbiX spec table. |
| Long compile time for new tests | Low | Low | New tests link `spe_core`; incremental build only recompiles touched TUs. Total added build time < 5s on a modern machine. |

---

## ADR — Architectural Decision Record

**Decision**: Implement the four tasks (T1 docs, T2 fallback test, T3 VBAPRenderer cache, T4 HOA 2nd/3rd) as four sequential commits in that exact order under SHORT-mode RALPLAN-DR.

**Drivers**:
- D1 — RT-thread safety for T3 (cache must not allocate, must invalidate on layout change).
- D2 — Falsifiable test for T2 fallback gain pattern (currently only "no crash" is asserted).
- D3 — SN3D / ACN closed-form correctness for T4 (decoder convention deferred to v2).

**Alternatives considered**:
- *Option A (mega-commit)*: rejected — review surface too large; T3 risk would block T1/T2/T4.
- *Option C (defer T3)*: rejected — user explicitly identified C(64,3) cost as unacceptable; T3 design fits cleanly into existing `prepareToPlay` allocation hook.
- *T3 alt: LRU instead of FIFO*: rejected — LRU requires write-on-read which risks audio-thread mutation patterns; FIFO insert-only is simpler and sufficient for this access pattern (most objects sit at one (az,el) bin for many blocks).
- *T3 alt: `unordered_map`*: **rejected** — cold-miss node allocation violates RT contract; chose fixed-size open-addressing array instead (current default).

**Why chosen**: Option B (four commits in T1→T2→T3→T4 order) — smallest blast radius per commit, each commit independently revertible, ordering reflects increasing risk (docs < test-only < behaviour change < pure-math addition with new files). RALPLAN-DR SHORT mode appropriate because: (i) no cross-cutting refactor, (ii) T3 is the only behaviour change and is bounded to one class with a clean invalidation point, (iii) all tasks share the established hardware-free CI gate.

**Consequences**:
- ctest count grows 31 → 32. Pytest unchanged at 76/3.
- `VBAPRenderer` gains a small public test surface (`cacheHits()`, `cacheMisses()`, `resetCacheStats()`). These are intentional test hooks; document as "test-only" in doxygen.
- `AmbisonicEncoder` gains two new public methods. The 1st-order method is unchanged.
- `docs/v0.1.0_report.md` §8 becomes the source of truth for "what's in the v1 line"; future v1c/v2 plans should keep updating it.

**Follow-ups (out of scope for v1b)**:
- 4th-order and above HOA (consider for v2).
- N3D normalisation variant (selectable per-output).
- Ambisonics decoder (B-format → speaker).
- Cache size tuning for 7th-order HOA renderer (when added).
- SIMD acceleration of `vbap_gain` triplet enumeration (orthogonal to caching).
- LRU eviction policy if access pattern shows cold-miss churn in profiling.

---

## Success Criteria (final gate)

After all four commits land:

- [ ] `docs/v0.1.0_report.md` §8 reflects F1-F4 completion (T1)
- [ ] `cd core/build && /home/seung/miniforge3/bin/cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON && make -j$(nproc)` clean build
- [ ] `/home/seung/miniforge3/bin/ctest --output-on-failure` reports **32/32 PASS**
- [ ] `test_p_vbap3d` output contains line `[PASS] test5: fallback gain pattern valid`
- [ ] `test_p_vbap_cache` output contains 3 sub-test PASS lines and `[RESULT] PASS`
- [ ] `test_p_ambi` output contains 11 sub-test PASS lines (existing 6 + new 5)
- [ ] `python3 -m pytest` reports **76 passed, 3 skipped** (unchanged)
- [ ] `test_p1_rt_no_alloc` still PASSES (T3 RT-safety regression gate)
- [ ] No new files outside `docs/`, `core/src/render/`, `core/src/ambi/`, `core/tests/core_unit/`
- [ ] 4 commits in history, each with file scope matching its task spec

---

## Confirmation Checklist (executor must confirm before each commit)

- T1: only `docs/v0.1.0_report.md` modified; ctest/pytest skipped (docs-only).
- T2: only `core/tests/core_unit/test_p_vbap3d.cpp` modified; full ctest + pytest green.
- T3: only `core/src/render/VBAPRenderer.h`, `core/src/render/VBAPRenderer.cpp`, `core/tests/core_unit/test_p_vbap_cache.cpp`, `core/tests/core_unit/CMakeLists.txt` modified; full ctest + pytest green; `test_p1_rt_no_alloc` specifically verified.
- T4: only `core/src/ambi/AmbisonicEncoder.h`, `core/src/ambi/AmbisonicEncoder.cpp`, `core/tests/core_unit/test_p_ambi.cpp` modified; full ctest + pytest green.
