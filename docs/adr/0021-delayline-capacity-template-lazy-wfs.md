# ADR 0021 — `DelayLine` Compile-Time Capacity + Lazy WFS Allocation

| | |
|---|---|
| **Status** | Accepted (shipped v0.9 Lane F5 — `5979d1a` plan; `2e07a67` M1 / `e02361f` M2 / `1bfe5f3` M3 / `765afb9` M4 / `d87f2a9` M3b) |
| **Date** | 2026-06-01 |
| **Authors** | Planner / Architect / Critic (v0.9 Lane F5 ralplan consensus) |
| **Related** | ADR 0004 (FDN topology — own DelayLine, NOT affected), ADR 0005 (algorithm dispatch), ADR 0006a (algorithm runtime swap), ADR 0020 (`/sys/metrics`); Lane C `docs/RT_BUDGET_MAX_OBJECTS.md` (C-M4 memory gate, C-M7 default-flip) |
| **Plan** | `.omc/plans/spatial-engine-v0.9-laneF5-wfs-memory.md` (REV 3; F5-M1..M6) |

This ADR records a memory-footprint remediation of the audio-render delay path plus
the related correctness clamp and a concurrency handshake for lazy allocation. It is
an **RT-path tunable change** (`core/src/core/Constants.h:3` mandates an ADR for these).

---

## Context

Lane C (MAX_OBJECTS 64→128, ADR-adjacent `docs/RT_BUDGET_MAX_OBJECTS.md`) measured the
C-M4 memory gate (max-RSS < 100 MB, heaviest config) **FAILING at both caps**: ~129 MB
@64, ~250 MB @128. The dominant term was `WFSRenderer::delays_`, a
`std::vector<dsp::DelayLine>` sized **MAX_OBJECTS × num_speakers**, where each
`dsp::DelayLine` embedded a `std::array<float, 48000>` (187.5 KB). At 128 × 8 speakers
that is ~192 MB — **allocated unconditionally at `prepareToPlay` regardless of whether
WFS is ever the active algorithm.** Lane C deferred the DSP remediation to this lane.

Two facts make the 48000-sample buffer wildly oversized for WFS:
- WFS delay is geometry-bounded: `delay = r / SOUND_C × sr`, and source distance is
  hard-capped at `ADM_OSC_MAX_DIST = 20 m` (`AdmOscConstants.h`, static_assert'd). Even
  a 50 m source-to-speaker distance at 96 kHz is ~13994 samples.
- `DelayLine::processSample` did **not** clamp the requested delay: a delay exceeding
  the buffer wrapped modulo and returned a garbage sample (silently-wrong audio, no
  crash). The 48000 buffer was "safe" only because no real input reached 343 m of delay.

`FdnReverb` uses its **own** private `std::vector`-backed delay struct, NOT
`dsp::DelayLine` — so reverb is outside the blast radius.

---

## Decision

1. **Template `dsp::DelayLine` on a compile-time capacity:**
   `template <int Capacity = DELAY_LINE_MAX_SAMPLES> class DelayLine`. Storage stays a
   fixed-size `std::array<float, Capacity>` → **RT-safe by construction** (zero heap on
   the audio path, capacity known at compile time). New constant
   `WFS_MAX_DELAY_SAMPLES = 16384` (2¹⁴; derivation: 50 m × 96 kHz / 343 m·s⁻¹ ≈ 13994,
   rounded up). Convenience alias `DelayLine48k = DelayLine<>`.

2. **Right-size per use, not blanket:**
   - WFS `delays_` → `DelayLine<16384>` (187.5 KB → 64 KB/line, 2.86×).
   - propagation `PropagationDelay::delay_` → `DelayLine<16384>` (geometry-bounded).
   - `PerObjectChain::user_delay_` and `SpatialEngine::spk_delays_` → **kept at 48000**
     (`DelayLine48k`). Both are **user-settable, UNCLAMPED** delays (OSC `user_delay_ms`;
     layout YAML `delay_ms` → `LayoutLoader.cpp:95` → `SpatialEngine.cpp:369-370`) that
     may legitimately want ~1 s. `spk_delays_` is per-speaker (~1.5 MB), so keeping it
     large is footprint-neutral.

3. **Capacity clamp (correctness, ships regardless):** `DelayLine::processSample` clamps
   `delay_samples` to `[0, Capacity-2]` (−2 for interpolation headroom). Over-capacity
   requests now degrade gracefully to the maximum representable delay instead of wrapping
   to garbage. In-range delays are **bit-identical**. `PropagationDelay::setDistance`'s
   local clamp was corrected to clamp against the template capacity (was 48000).

4. **Lazy WFS allocation (Option C):** `WFSRenderer::delays_/ramps_` are no longer
   allocated at `prepareToPlay`. A control-thread `ensureAllocated()` resizes them then
   **release-stores** `ready_`; `processBlock` **acquire-loads** `ready_` and renders
   silent until it flips, so the audio thread never reads half-built storage. The trigger
   is the OSC-thread `ObjAlgo(WFS)` handler, which calls `ensureAllocated()` before the
   switch is pushed to `cmd_fifo_`. Allocated exactly once; never resized while the audio
   thread may read it. `prepareToPlay` keeps a cheap `obj_cache_` re-scan safety net for
   the rare re-prepare-with-active-WFS case (audio stopped during prepare).

### Drivers
- **D1 — footprint < 100 MB.** WFS `delays_` is the dominant term; right-sizing +
  lazy-alloc removes it.
- **D2 — RT-safety preserved.** `std::array` stays; the only audio-path additions are one
  branchless `std::min`/`std::max` clamp and one atomic acquire-load — no alloc, no lock.
- **D3 — correctness.** The clamp replaces silent wrap-to-garbage with graceful
  degradation; in-range audio is unchanged.

### Alternatives considered
- **Option B (runtime `std::vector` sizing from actual sr):** precise but converts the
  hottest DSP primitive to heap, removing the fixed-size guarantee. Rejected as the
  primary mechanism (kept as a WFS-only fallback if the 16384 compile-time SR bet were
  ever judged too coarse).
- **Option C alone (skip-when-unused, no right-size):** reduces the *condition* not the
  *size* — a WFS-active 128 deployment still pays the full term. Insufficient alone;
  adopted as an **additive** layer on top of right-sizing.

---

## Supported envelopes (contracts)

- **WFS delay:** max representable ≈ `Capacity-2 = 16382` samples ≈ 29 m of delay @192 kHz,
  ~117 m @48 kHz; the design envelope is **50 m @96 kHz**. Beyond this the clamp degrades
  gracefully (delay pinned to max) rather than wrapping. A 192 kHz + >29 m venue would
  need `WFS_MAX_DELAY_SAMPLES = 32768` (one-constant change, re-measure footprint).
- **Large lines (`user_delay_`, `spk_delays_`):** max-supported delay ≈ `(48000-2)/sr` s
  — **≈ 1.0 s @48 kHz, ≈ 0.5 s @96 kHz, ≈ 0.25 s @192 kHz** (SR-dependent). A layout/user
  delay beyond this is clamped (was wrap-garbage). A >1 s requirement needs a larger
  `DelayLine48k` + footprint re-measure.

---

## Measured results (heaviest config, `perf_obj_block_time`, getrusage max-RSS)

| cap | pre-F5 (C-M4) | post-F5 (WFS-inactive) | ceiling | gate |
|-----|--------------:|-----------------------:|--------:|------|
| 64  | ~129 MB       | **27.5 MB**            | 100 MB  | **PASS** |
| 128 | ~250 MB       | **46.7 MB**            | 100 MB  | **PASS** |

@128 RT (normal load): median 29.9%, p99 31.6%, **peak 46.9%** budget, xruns 0.
Honest §0.7 model (WFS+propagation right-sizing, WFS allocated) predicted @64 ~58 MB /
@128 ~108 MB and was confirmed within ~3 MB by the pre-Option-C measurement (59.4 / 111.6
MB); Option C then removes the ~64 MB WFS term when WFS is inactive.

**WFS-active caveat (honest):** when WFS *is* the active algorithm, `delays_` is allocated
(128 × 8 × 64 KB = 64 MB), so a WFS-active 128 deployment is ~46.7 + 64 ≈ **~111 MB**
(over the 100 MB ceiling). The committed perf gate exercises the common VBAP/binaural
(WFS-inactive) path. Clearing WFS-active 128 < 100 MB is a follow-up (e.g. allocate WFS
lines per active-WFS-object rather than full MAX_OBJECTS × speakers).

---

## C-M7 (default-flip) re-evaluation

C-M7 flips the shipped `SPATIAL_ENGINE_MAX_OBJECTS` default to 128 only if (heaviest
path): **peak ≤ ~35% budget AND max-RSS < ~70 MB AND xruns == 0.**

Post-F5 @128 (WFS-inactive): RSS 46.7 MB (**< 70 ✓**), xruns 0 (**✓**), peak **46.9%**
(**> 35% ✗**). The memory criterion — the historical blocker — now **passes comfortably**,
but the RT-peak headroom criterion is not met, and a WFS-active 128 still exceeds the
100 MB hard ceiling.

→ **DO NOT flip the default. Keep `SPATIAL_ENGINE_MAX_OBJECTS` default = 64.** 128 remains
a **validated opt-in**, now memory-deployable (46.7 MB) for WFS-inactive venues. There is
**no blanket flip**: it is conditional on (a) RT-peak headroom reaching ≤35% and (b) the
target deployment being WFS-inactive (WFS-active 128 needs the per-active-object follow-up).

---

## Consequences

- `dsp::DelayLine` is a template (header included by 4 users; default arg + `DelayLine48k`
  alias keep churn to mechanical re-spellings at `PerObjectChain.h`, `SpatialEngine.h`,
  `PropagationDelay.h`).
- WFS capacity is a compile-time SR bet, backstopped by the runtime clamp.
- The clamp is a behaviour change on the large lines too: a layout/user `delay_ms` beyond
  the SR-dependent max is now clamped (was wrap-garbage) — strictly safer, documented above.
- Audio path adds one branchless clamp + one atomic acquire-load (Option C). RT-asserts
  (`p1_rt_no_alloc`) green at both caps.
- First-ever WFS activation may render at most a few blocks of WFS silence while
  `ensureAllocated()` publishes (documented; control-thread allocation).

## Follow-ups
- **WFS-active 128 < 100 MB:** allocate WFS lines per active-WFS-object (not full
  MAX_OBJECTS × speakers) to clear the WFS-active footprint.
- **RT-peak ≤35% for the C-M7 flip** (separate from memory; Lane C territory).
- F3 binaural lazy-prime (tertiary, ~4–7 MB) — now well below ceiling, stays a follow-up.

---

## Verification

- Unit: `test_p_delayline_clamp` (over-capacity clamp + in-range bit-identical, at
  `DelayLine<16384>` AND `DelayLine<48000>`); `test_p3_wfs` (Huygens, + `ensureAllocated`
  / `isReady`); `test_p3_propdelay_sweep`.
- Concurrency: `soak_wfs_algoswap_race` under ThreadSanitizer — **zero data races** on
  `ready_`/`delays_`/`ramps_` over 150 rounds; correct non-silent audio after each flip.
- Footprint: `perf_obj_block_time` exit-0 (RSS < 100 MB) at both caps.
- Regression: full NO_JUCE ctest **114/114 at 64 AND 128**; RT-asserts `p1_rt_no_alloc`
  green at both caps; UI pytest 61 passed / 3 skipped.
