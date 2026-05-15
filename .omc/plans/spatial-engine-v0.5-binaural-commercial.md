# Spatial Engine v0.5 — Track B: Commercial Binaural (B1 default + B2 AmbiVS optional)

## Context

v0.5 lands commercial-grade binaural monitoring. Two co-resident render
paths:

- **B1 (Direct, default).** Per-object HRTF rendering via OLA convolution.
  Direction updates are RT-safe via double-buffered slot swap with a
  2-block crossfade.
- **B2 (AmbiVS, optional).** Ambisonic decode to a 24-point t-design
  virtual-speaker layout, each VS feeding a fixed HRIR pair. Selected by
  user (OSC + state). Subject to a CPU-throughput probe at `setActive(true)`
  that falls back to B1 on insufficient headroom.

Mode (B1 vs. B2) is **DELIBERATE consensus mode** — high-risk DSP, RT-safety
critical, ships to commercial users. Pre-mortem (3 scenarios) and expanded
test plan included below.

All file/line citations were re-verified against `HEAD` (commit a1ae720).
Constants referenced: `MAX_OBJECTS = 64` (`Constants.h:12`),
`ALGO_SWAP_K = 256` (`Constants.h:20`), `kMaxIRLength = 1024u`
(`SofaBinReader.cpp:12`), `kTDesign24[24]` (`AllRADTDesigns.hpp:18`).
`GainRamp` is `core/src/dsp/GainRamp.h:11-58` (NOT `core/src/core/`).

---

## Principles

1. **No allocations on the audio thread.** Every direction update is
   non-allocating. The `OlaConvolver::loadInto` contract (A2) is the
   foundation; all paths must be debug-asserted alloc-free, with a CI
   `malloc`-interceptor test guarding regression.
2. **Direction updates are gradual.** Slot-swap crossfade is 2 blocks (A5;
   10.67 ms @ 256/48 kHz) — matches industry practice (Resonance Audio,
   Steam Audio, IEM) and `ALGO_SWAP_K` order of magnitude. No clicks.
3. **B2 is best-effort.** If CPU headroom is insufficient, B2 falls back
   to B1 with a user-visible OSC warning. Never silently degrade audio
   quality.
4. **Encapsulation.** B2 path lives entirely inside `BinauralMonitor`; no
   global decoder, no shared state with the speaker render path.
5. **Backwards compat.** v0.4 sessions load unchanged. The v0.4 A7
   placeholder branch is removed in the same patch that wires real
   binaural — no dead-code debt.

## Decision Drivers (top 3)

1. **Commercial release.** v0.5 is the first version users buy. Binaural
   correctness, click-freeness, and CPU predictability are non-negotiable.
2. **RT-safety.** The audio thread cannot allocate, lock, or stall. Every
   convolver buffer, every HRIR slot, every crossfade ramp must be
   pre-allocated.
3. **B1 vs B2 trade-off.** B1 is per-object and scales with object count;
   B2 is fixed-cost (24 convolvers) but adds the virtual-speaker decode
   stage. B2 wins above ~12-15 simultaneous objects on most CPUs but
   requires Ambisonic-domain content to shine.

## Viable options considered

**Option A (chosen): ship B1 + B2 co-resident with mode switch.**
**Pros:** users with high object counts (orchestral, sound-design rigs)
get B2 efficiency; users with sparse scenes get B1 fidelity; mode switch
is per-session. **Cons:** doubles the test matrix; B2 adds 24 convolver
slots + 24 HRIR pairs of resident memory (~600 KiB).

**Option B (rejected): ship B1 only in v0.5, defer B2 to v0.6.**
**Why rejected:** B1 alone caps usable object count at ~16 on midrange
CPUs (per A6 probe budgeting); orchestral users — a primary commercial
segment — would be CPU-bound on day one. Shipping B2 alongside doubles
the headroom for high-object-count workflows.

**Option C (rejected): ship B2 only.** **Why rejected:** B2 sacrifices
per-object HRTF directional precision for fan-out efficiency; low-object
sessions lose fidelity for no gain. Single-mode shipping forces users to
accept the wrong trade-off.

---

## Mini-ADR (v0.5 P1): RT-safe direction-update via double-buffered slot swap

- **Decision.** Each per-object binaural channel holds **two**
  `OlaConvolver` slots. Direction update writes the new HRIR into the
  idle slot via `OlaConvolver::loadInto(ir, ir_len)` (the new
  non-allocating reload entry point) and triggers a 2-block crossfade.
- **Why two slots, not three.** Two suffices because crossfade duration
  (2 blocks) equals the maximum lifetime of an "old" slot post-swap;
  a third slot only helps if updates arrive faster than 1 per crossfade
  window, which the chained-crossfade preempt-with-current-gain handoff
  (below) handles in-place.
- **Chained-crossfade behavior** (specified). When a new `setDirection`
  arrives **mid-crossfade**:
  - The currently-fading-in slot becomes "old" immediately.
  - The idle slot (formerly "old") receives the new IR and becomes
    "new".
  - A fresh 2-block crossfade begins; the previously-fading-in slot is
    ramped down from its current gain (not from 1.0), and the new slot
    ramps up from 0.
  - **Preempt with current-gain handoff** semantics — guarantees C0
    continuity even under rapid head-tracking updates.
- **Why 2 blocks (10.67 ms @ 256/48 kHz), not 1 (5.33 ms) or 4 (21 ms)**:
  - Rejected: 1-block (5.3 ms) — too short for impulsive content
    (transients, percussive sources) — perceptible discontinuity.
  - Rejected: 4-block (21 ms) — perceived as lag during fast head
    tracking — feels like the binaural image is "dragging behind" head
    motion.
  - Chosen: 2-block — sits at the short end of the
    Resonance-Audio/IEM-convention 10-20 ms window; click-free + low
    tracker lag.

---

## Phase P0 — `OlaConvolver::loadInto` API + capacity priming (A1, A2 folded)

**Decision: MAX_IR_LEN = 1024 (A1).** `SofaBinReader.cpp:12` already
declares `static constexpr uint32_t kMaxIRLength = 1024u`; loads with
`hdr.ir_length > 1024` return `SpehResult::IRLengthUnsupported`
(`SofaBinReader.cpp:42-46`). Supported IR-length set:
`{64, 128, 256, 384, 512, 1024}` (verified at `:42-43`). No production
HRTF on the road map exceeds 1024 samples; ARI/SCUT/CIPIC databases are
128-512. IR lengths > 1024 (e.g., 2048/4096 direct HRIR, or 16384-sample
BRIR for room-aware binaural) are deferred to **v0.6** alongside the
partitioned-FFT convolver and a SOFA loader format-version bump
(`SpehHeader` major-version field becomes `v2` with explicit IR-length
field and chunk descriptor).

**Memory budget (v0.5 P0/P1 locked).**
- Per-object dual-slot HRIR storage: `MAX_OBJECTS x ears x slots x
  MAX_IR_LEN x sizeof(float) = 64 x 2 x 2 x 1024 x 4 = 1 MiB`.
- Per-object overlap buffers: `64 x 2 x 2 x (1024 - 1) x 4 ~= 1 MiB`.
- Per-object `work_` buffers (sized for worst case `MAX_BLOCK=512 + 1023
  = 1535`): `64 x 2 x 2 x 1535 x 4 ~= 1.5 MiB`.
- **Total binaural-path resident memory: ~3.5 MiB.**

**Files touched.**
- `core/src/hrtf/OlaConvolver.h` — add `void loadInto(const float* ir, int
  ir_len)` and `void prepareForReload(int block_size)`.
- `core/src/hrtf/OlaConvolver.cpp` — implementations per A2 spec.

**`loadInto` contract (A2, verified citations).**
- **Why a new method.** `OlaConvolver::prepare(...)` at
  `OlaConvolver.cpp:10` unconditionally calls `ir_.assign(...)`,
  `overlap_.assign(...)`, and `work_.assign(...)` — all of which can
  reallocate. The RT-side binaural-update path must reload an HRIR into
  a pre-prepared convolver **without allocating**, so `loadInto` is the
  explicit non-allocating cousin of `prepare`.
- **Signature.** `void OlaConvolver::loadInto(const float* ir, int ir_len);`
- **Preconditions (debug-asserted via `assert`; release behavior — see
  below).**
  - `ir != nullptr && ir_len > 0 && ir_len <= MAX_IR_LEN` (= 1024).
  - `ir_.capacity() >= MAX_IR_LEN`.
  - `overlap_.capacity() >= MAX_IR_LEN - 1`.
  - `work_.capacity() >= block_size_ + MAX_IR_LEN - 1`.
- **Release-build behavior on capacity violation (A2 clarified).** In
  release builds, if any precondition fails (notably `ir_.capacity() <
  ir_len`), `loadInto` **returns immediately without modifying any
  internal state** (no-op). It does **NOT** silently allocate. Debug
  builds `assert`. **Counter: `load_into_failures_` (atomic `uint64_t`)
  increments on each no-op for telemetry.** This is the RT-safety
  guarantee: it is better to lose a single direction update than to
  allocate on the audio thread. Failure surfaces to the user via the
  same `/sys/binaural_warning` channel used by A6 (telemetry, not user-
  catastrophic).
- **Behavior (RT-safe, no allocations).**

```cpp
void OlaConvolver::loadInto(const float* ir, int ir_len) {
    // Release-build: silent no-op on capacity violation; counter increments.
    if (!ir || ir_len <= 0 || ir_len > /*MAX_IR_LEN=*/1024 ||
        ir_.capacity() < static_cast<size_t>(/*MAX_IR_LEN*/1024) ||
        overlap_.capacity() < static_cast<size_t>(/*MAX_IR_LEN - 1*/1023) ||
        work_.capacity() < static_cast<size_t>(block_size_ + /*MAX_IR_LEN - 1*/1023)) {
        load_into_failures_.fetch_add(1, std::memory_order_relaxed);
        assert(false && "loadInto preconditions violated");
        return;  // no-op in release
    }

    ir_.resize(ir_len);                                   // no realloc (capacity preserved)
    std::copy(ir, ir + ir_len, ir_.begin());

    overlap_.resize(ir_len - 1);                          // no realloc
    std::fill(overlap_.begin(), overlap_.end(), 0.f);     // flush tail on reload

    work_.resize(block_size_ + ir_len - 1);               // no realloc
    // work_ is fully overwritten at the start of each process() at OlaConvolver.cpp:37,
    // so no fill needed here.
}
```

- **Capacity-priming step** (control thread, allocations OK — invoked
  once at `prepare()` or via a new helper `prepareForReload(int
  block_size)`):

```cpp
void OlaConvolver::prepareForReload(int block_size) {
    block_size_ = block_size;
    ir_.reserve(/*MAX_IR_LEN*/ 1024);
    ir_.resize(/*MAX_IR_LEN*/ 1024);                       // forces backing alloc to MAX
    ir_.clear();                                           // size->0, capacity preserved
    overlap_.reserve(/*MAX_IR_LEN - 1*/ 1023);
    overlap_.resize(/*MAX_IR_LEN - 1*/ 1023);
    overlap_.clear();
    work_.reserve(block_size + /*MAX_IR_LEN - 1*/ 1023);
    work_.resize(block_size + /*MAX_IR_LEN - 1*/ 1023);
    work_.clear();
}
```

- **Compatibility note.** `process()` at `OlaConvolver.cpp:33` reads
  `ir_.size()` as the active IR length and `overlap_.size() /
  work_.size()` to size its inner loops, so `loadInto` swapping in a
  shorter IR (e.g., 256-tap near-grid vs. 512-tap fallback) is honored
  on the very next block with no state inconsistency. The `isReady()`
  guard at `OlaConvolver.h` continues to work (`!ir_.empty()`).

**Tests added.**
- `core/tests/test_ola_convolver_loadinto_rt_safe.cpp` — `malloc`-
  interceptor wraps the call; asserts zero allocations across 1000
  reloads after a single `prepareForReload`.
- `core/tests/test_ola_convolver_loadinto_clamp.cpp` — release build
  rejects `ir_len > MAX_IR_LEN` (silent no-op + counter increments);
  debug build asserts.
- `core/tests/test_ola_convolver_loadinto_capacity_violation_release.cpp`
  — explicitly forces `ir_.capacity() < ir_len` and asserts release-build
  returns without mutating state (`memcmp` of internal buffers before/after
  is unchanged) and `load_into_failures_` increments.

## Phase P1 — B1 Direct path: per-object slot pair + crossfade (A5 folded)

**Files touched.**
- `core/src/binaural/BinauralMonitor.h/.cpp` — new module.
- Per-object state inside `BinauralMonitor`:

```cpp
struct ObjectBinauralState {
    spe::hrtf::OlaConvolver conv_L[2];  // dual-slot (A2 + slot swap)
    spe::hrtf::OlaConvolver conv_R[2];
    int active_slot;                    // 0 or 1
    spe::dsp::GainRamp gain_old;        // ramps 1 -> 0 over 2 blocks
    spe::dsp::GainRamp gain_new;        // ramps 0 -> 1 over 2 blocks
    int crossfade_blocks_remaining;
};
```

**`setDirection(obj_id, az, el)` (RT-safe, called from message-thread
drain at block top — `SpatialEngine.cpp` mirror of `:285-298` drain
pattern):**
1. Resolve nearest HRIR pair via KD-tree on the loaded `HrtfTable`.
2. Compute idle slot: `idle = 1 - active_slot`.
3. `conv_L[idle].loadInto(hrir_L, hrir_len)` and same for `R`.
4. Promote: `active_slot = idle`. Reset crossfade ramps per **chained-
   crossfade behavior** specified in the mini-ADR above (preempt with
   current-gain handoff).

**Crossfade duration: 2 blocks (A5).** `GainRamp::setTarget(target,
ramp_samples)` called with `ramp_samples = 2 * block_size_`. The existing
linear ramp at `core/src/dsp/GainRamp.h:11-58` accepts arbitrary positive
sample counts — no API change.

**CPU cost during crossfade.** Per-object convolution doubles (two OLA
passes per ear). Worst case all `MAX_OBJECTS = 64` swap simultaneously:
peak 2x per-object cost for 2 blocks — bounded by A6's probe.

**Tests added.**
- `core/tests/test_p_binaural_crossfade_smooth.cpp` (amended per A5) —
  issue direction change at block boundary B0; assert C0 continuity
  (`|sample[n] - sample[n-1]| < 0.05` on a 1.0-amplitude reference
  impulse train) across blocks B0, B1, B2 (full 2-block ramp + steady-
  state landing block); assert sample at `B2 + 1 block` is bit-identical
  to a non-crossfaded reference render of the new direction (confirms
  ramp fully retired).
- `core/tests/test_p_binaural_chained_crossfade.cpp` — fires two
  direction updates within the same crossfade window; asserts C0
  continuity through both transitions and asserts preempt-with-current-
  gain handoff semantics (the previously-fading-in slot ramps down from
  its mid-ramp gain, not from 1.0).
- `core/tests/test_p_binaural_alloc_free_rt.cpp` — `malloc`-interceptor
  test on a 60-second synthetic session with rapid direction updates;
  asserts zero allocations on the audio thread.

## Phase P2 — `.speh` loading + HRIR lookup wired to the audio path

**Files touched.**
- `core/src/binaural/BinauralMonitor.cpp` — on `setSpehTable(const
  HrtfTable*)`, build KD-tree over `(az, el)` positions for nearest-
  neighbor direction lookup.
- `vst3/SpatialEngineProcessor.cpp` — wire `.speh` path injected in
  v0.4 P3 to the engine via `BinauralMonitor::setSpehTable()`.

**Acceptance criteria.**
- A loaded `.speh` ARI/SCUT/CIPIC fixture surfaces 1000 direction lookups
  in < 1 ms (control-thread budget).
- KD-tree build happens on the message thread; the audio thread only
  reads via `const HrtfTable*`.

## Phase P3 — v0.4 A7 placeholder removed; bus 1 wired to real B1 output

**Files touched.**
- `vst3/SpatialEngineProcessData.hpp:18-43` — the A7 placeholder branch
  is **deleted** in this patch (no dead-code debt per A7 spec). The
  `binaural_enable_` branch now reads from `BinauralMonitor::process()`
  output.

**Acceptance criteria.**
- `test_v04_binaural_bus_placeholder.cpp` companion run (with
  `binaural_enable == 1`) — bus 1 != bus 0 L/R sum (confirms placeholder
  is gone, real binaural is active).
- Audio bus 0 (speaker) remains bit-identical to v0.4 (regression gate).

## Phase P4 — B2 AmbiVS path: 24-VS virtual-speaker chain (A4 folded)

**Decision (A4): `BinauralMonitor` owns the B2 chain.** The B2 (AmbiVS)
virtual-speaker chain — second `AmbiDecoder`, `SpeakerLayout` built from
`kTDesign24`, and 24x2 `OlaConvolver` instances — is **owned by
`BinauralMonitor`**, not by `SpatialEngine` or any shared service. This
keeps the B2 chain encapsulated, prevents `SpatialEngine` from growing a
second decoder graph, and gives clean failure isolation (a B2 init fault
degrades to B1 without touching the speaker path).

**Ownership layout (private members on `BinauralMonitor`).**

```cpp
class BinauralMonitor {
    // ... (B1 members elided)
private:
    // B2 path — constructed when mode_ == BinauralMode::AmbiVS.
    spe::ambi::AmbiDecoder         vs_decoder_;
    spe::geometry::SpeakerLayout   vs_layout_;
    std::array<spe::hrtf::OlaConvolver, 24> vs_conv_L_;
    std::array<spe::hrtf::OlaConvolver, 24> vs_conv_R_;
    // Scratch: 24 virtual-speaker output channels, one block deep.
    std::array<std::array<float, MAX_BLOCK>, 24> vs_buf_;
    // Per-VS HRIR cache keyed by t-design point — populated once at initialize().
    std::array<std::array<float, 1024>, 24> vs_hrir_L_;
    std::array<std::array<float, 1024>, 24> vs_hrir_R_;
    std::array<int, 24>            vs_hrir_len_;
};
```

**`BinauralMonitor::initialize()` (control thread, allocations OK).**
1. Construct `vs_layout_` from `spe::ambi::kTDesign24` (declared at
   `core/src/ambi/AllRADTDesigns.hpp:18`, sized 24 — verified). Each
   t-design point `(x, y, z)` is converted to spherical `(az, el)` and
   inserted as a `SpeakerInfo` with synthetic YAML channels 1..24.
2. Call `vs_decoder_.prepare(vs_layout_)` — `AmbiDecoder.h` confirmed to
   accept arbitrary layouts.
3. For each of the 24 virtual speakers, KD-tree-lookup the nearest HRIR
   pair in the loaded `HrtfTable`, copy into
   `vs_hrir_L_[i] / vs_hrir_R_[i]`, record `vs_hrir_len_[i]`.
4. Prime each convolver:
   `vs_conv_L_[i].prepareForReload(block_size_)` then
   `vs_conv_L_[i].loadInto(vs_hrir_L_[i].data(), vs_hrir_len_[i])`; same
   for `vs_conv_R_[i]`.

**Layout-change behavior (A4 spec, clarified).** When the **physical**
speaker `SpeakerLayout` changes (user loads new YAML at runtime — v0.4
P2 path), `vs_layout_` (B2 t-design layout) does **NOT** re-initialize —
it stays pinned to `kTDesign24` because B2 is virtual-speaker
independent of physical layout. The 24 HRIR pairs also do **NOT**
re-load — they are a function only of the loaded `.speh` table, not of
the physical speaker config. Re-initialization happens only when the
`.speh` table itself changes (rare; on user load of a new HRTF set).

**Gate.** B2 path runs only when `mode_ == BinauralMode::AmbiVS`. Mode
is set by:
- OSC `/sys/binaural_mode ,i {0|1}` (0 = B1 direct, 1 = B2 AmbiVS).
- State section `0x0004` byte 1 (TLV state v4 — restored at scene load
  via v0.4 P0 schema).

Mode is consumed inside `BinauralMonitor::process()` via a single
`std::atomic<int> mode_` load at block top; switching modes mid-stream
is allowed and triggers the same 2-block crossfade described in the
mini-ADR (A5).

**CPU-throughput probe at `setActive(true)` (A6 folded, NOT
`setupProcessing`).** Architect r2 amendment A6: the B2 throughput probe
runs inside the VST3 plug-in's `setActive(true)` lifecycle hook
(`vst3/SpatialEngineProcessor.cpp:199`), **not** inside `setupProcessing()`
at `:447-459`. VST3 spec leaves `setupProcessing()` ambiguous on
threading: JUCE-wrapped hosts (Bitwig, FL Studio, some Cubase project-
startup paths) invoke it from the audio render thread, which means any
synthetic convolution warm-up would either (a) glitch the first block
of audio, or (b) trip our own RT-safety asserts. `setActive(true)`, by
contrast, is contractually a control-thread call across all major hosts
and is the correct spot for one-shot CPU probes.

**Probe behavior.**
1. On `setActive(true)`, if `binaural_enable == 1 && requested_mode ==
   BinauralMode::AmbiVS`:
   - Build a synthetic 512-sample HRIR pair (single-impulse + decay).
   - Run 256 sample-blocks through a sacrificial `OlaConvolver` (24x to
     simulate the B2 fan-out).
   - Measure wall-clock via `std::chrono::steady_clock`.
2. Compute `throughput = (256 * 256 / sr) / elapsed_seconds`. Require
   `throughput >= 1.5` (50 % headroom over real-time).
3. If below threshold: clamp `effective_mode_ = BinauralMode::Direct`
   (B1 fallback), emit OSC `/sys/binaural_warning ,sf
   "ambivs_disabled_cpu" <measured_throughput>` so the UI can surface
   it.
4. Bench result cached in `BinauralMonitor::probe_throughput_` for the
   lifetime of the engine instance; re-probed only on sample-rate
   change.

**Tests added.**
- `core/tests/test_b2_ownership_lifecycle.cpp` (A4) —
  `BinauralMonitor` constructs/teardown of `vs_decoder_` + 48
  convolvers without leaks (ASan run).
- `core/tests/test_b2_throughput_probe_fallback.cpp` (A6) — synthetic
  slow-CPU mock confirms B2 -> B1 fallback path + OSC warning emission.
- **`core/tests/test_b2_ambivs_equivalent_to_b1_at_order3.cpp` —
  perceptual merge gate.** For a single source at known `(az, el)`,
  B2 ambi-VS path output should have RMS error vs. direct B1 HRTF
  panning < `-20 dB FS` threshold. This is the psychoacoustic merge
  gate for B2: if B2 doesn't reasonably approximate B1's direct
  rendering of a sparse source, the AmbiVS chain is broken.
- `core/tests/test_b2_layout_change_does_not_reinit_vs.cpp` (A4 clarif)
  — fires a runtime physical-layout YAML swap; asserts B2 `vs_layout_`,
  `vs_hrir_L_`, `vs_hrir_R_` are bit-identical pre- and post-swap.

## Phase P5 — State v4 binaural section populated

**Files touched.**
- `vst3/SpatialEngineState.cpp` — section `0x0004 = binaural` now writes
  and reads `binaural_enable` (1 byte), `mode` (1 byte: 0=B1, 1=B2),
  `requested_mode` (1 byte; differs from `mode` when B2 falls back to
  B1 — preserves user intent across reload), reserved padding.

**Tests added.**
- `vst3/tests/test_vst3_state_v4_binaural_section.cpp` — round-trips
  `{enable=1, mode=B2, requested_mode=B2}`; reloads after a forced B2
  fallback and asserts `requested_mode=B2` is preserved across saves.

## Phase P6 — Release validation

**Acceptance criteria.**
- Full `ctest --output-on-failure` + `python3 -m pytest` green.
- VST3 validator passes; REAPER + Bitwig manual smoke (B1 + B2 modes).
- Soak test: 30-minute session with random head-tracking updates at
  60 Hz on all 64 objects; assert zero audio glitches (no
  `load_into_failures_` increments, no underruns reported by host).
- ASan, TSan, UBSan clean.
- v0.4 -> v0.5 manual upgrade: saved v0.4 session loads, bus 1 now
  emits real binaural (A7 placeholder gone).
- CHANGELOG entry: "Adds commercial-grade binaural monitoring (B1
  direct per-object HRTF default + optional B2 AmbiVS at 24-point
  t-design). Mode switch via OSC `/sys/binaural_mode` and persisted in
  v4 state."

---

## Pre-mortem (DELIBERATE consensus mode — 3 failure scenarios)

**Scenario 1: Audio-thread allocation slips past `loadInto` contract.**
- Symptom: Sporadic glitches/underruns in long sessions with rapid
  head-tracking; CI green because the `malloc`-interceptor test only
  runs a fixed-duration burst.
- Cause: A code path in `BinauralMonitor::process()` (e.g., a
  `std::vector::push_back` in a debug-only branch, or a `std::string`
  format for an OSC error) escapes detection in the targeted CI test.
- Mitigation:
  1. `prepareForReload` is the **only** entry point to capacity priming;
     any code path that reaches `loadInto` without a prior
     `prepareForReload` trips a debug assert.
  2. CI `malloc`-interceptor coverage extended to a **20-second random
     workload** (not just a single burst) — `test_p_binaural_alloc_free_rt`.
  3. Release-build `load_into_failures_` counter surfaces a non-zero
     value in `/sys/binaural_warning` telemetry; soak test asserts it
     stays zero across a 30-minute run.

**Scenario 2: B2 path silently produces wrong binaural cues.**
- Symptom: B2 sounds "off" subjectively in user reports; no test catches
  it because B1 and B2 are tested independently against synthetic
  ground truths.
- Cause: KD-tree direction lookup for the 24 virtual speakers maps to
  the wrong HRIR pair (e.g., a Cartesian -> spherical conversion bug in
  `initialize()` step 1 mis-orders azimuth/elevation), or the
  `AmbiDecoder` matrix is computed against the wrong t-design table.
- Mitigation:
  1. **B2 perceptual merge gate**:
     `test_b2_ambivs_equivalent_to_b1_at_order3.cpp` — for a single
     source at known `(az, el)`, B2 ambi-VS path output should have
     RMS error vs. direct B1 HRTF panning < `-20 dB FS`. This is the
     psychoacoustic merge gate: if B2 diverges from B1 by more than 20
     dB FS on a sparse source, the AmbiVS chain is broken.
  2. Direction-lookup verification test: KD-tree returns hand-computed
     nearest HRIR pair for 8 canonical directions (front, back, left,
     right, up, down, FL30, FR30).
  3. T-design table sanity: `AllRADTDesigns.hpp:18-20` already has a
     `static_assert` on sizes (Appendix B2); extend with a runtime check
     that `vs_layout_` round-trips through azimuth/elevation conversion
     within float-epsilon.

**Scenario 3: CPU-throughput probe gives false-positive headroom on
ARM/Apple-silicon, then B2 underruns at runtime.**
- Symptom: B2 enabled on Mac M-series in `setActive`, then sporadic
  block underruns once real audio starts (cold cache, real HRIR vs.
  synthetic impulse).
- Cause: Probe uses synthetic impulse HRIRs that compress well in cache;
  real HRIRs have more cache footprint; probe's 50% headroom
  underestimates real cost.
- Mitigation:
  1. Probe runs **24x to simulate the full B2 fan-out** (not a single
     convolver), capturing cache pressure realistically.
  2. Headroom margin set to **50% over real-time** (A6) — generous;
     trims to 35% would also pass on most CPUs but the 50% buffer absorbs
     the synthetic-vs-real-HRIR cache delta.
  3. Runtime monitor: a sticky underrun counter on bus 1 triggers an
     auto-demote to B1 with `/sys/binaural_warning ,sf "ambivs_demoted_runtime"`
     if 3+ underruns occur in the first 30 seconds of `setActive(true)`.
     This is a belt-and-suspenders fallback to the probe.

---

## Expanded test plan (DELIBERATE consensus mode)

### Unit
- `test_ola_convolver_loadinto_rt_safe.cpp` — malloc-free under 1000
  reloads (P0).
- `test_ola_convolver_loadinto_clamp.cpp` — release-build silent no-op
  on `ir_len > MAX_IR_LEN` (P0).
- `test_ola_convolver_loadinto_capacity_violation_release.cpp` — release
  no-op on capacity violation + `load_into_failures_` increments (P0).
- `test_speaker_layout_load_from_string.cpp` — inherited from v0.4 (P2).
- `test_b2_layout_change_does_not_reinit_vs.cpp` — runtime physical-
  layout YAML swap doesn't disturb B2 chain (P4).

### Integration
- `test_p_binaural_crossfade_smooth.cpp` — C0 continuity across 2-block
  ramp; bit-equal landing block (A5; P1).
- `test_p_binaural_chained_crossfade.cpp` — preempt-with-current-gain
  handoff under back-to-back direction updates (P1).
- `test_p_binaural_alloc_free_rt.cpp` — 20-second random workload, zero
  allocations on audio thread (P1, Scenario 1 mitigation).
- `test_b2_ownership_lifecycle.cpp` — ASan-clean construct/teardown of
  the 48-convolver B2 chain (A4; P4).
- `test_b2_throughput_probe_fallback.cpp` — synthetic slow-CPU mock,
  B2 -> B1 fallback + OSC warning (A6; P4).
- `test_b2_ambivs_equivalent_to_b1_at_order3.cpp` — perceptual merge
  gate, B2 vs. B1 RMS < `-20 dB FS` on sparse source (Scenario 2
  mitigation; P4).
- `test_vst3_state_v4_binaural_section.cpp` — TLV section 0x0004 round-
  trip including `requested_mode` preservation across forced fallback
  (P5).
- `test_v04_binaural_bus_placeholder.cpp` (companion run with
  `binaural_enable == 1`) — confirms A7 placeholder removed and real
  binaural is on bus 1 (P3).

### End-to-end
- VST3 validator suite (Steinberg) — pass with B1 default and B2 mode.
- REAPER smoke — 10-minute session, head-tracking via OSC at 60 Hz,
  manual A/B B1 vs. B2; no glitches, no host complaints.
- Bitwig smoke — same shape, validates non-JUCE-wrapped lifecycle.
- ProTools smoke (where AAX wrapper exists) — same shape.

### Observability
- `/sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput>` —
  emitted on probe-based fallback (A6).
- `/sys/binaural_warning ,sf "ambivs_demoted_runtime"` — emitted on
  runtime-underrun-based demotion (Scenario 3 belt-and-suspenders).
- `load_into_failures_` (atomic `uint64_t`) — surfaced via OSC
  `/sys/binaural_status ,i <count>` on a 1 Hz heartbeat; soak test
  asserts zero increments across a 30-minute random workload (Scenario
  1).
- Per-mode CPU usage on `/sys/cpu_usage` heartbeat — distinguishes B1
  vs. B2 cost in user telemetry.

### Soak
- 30-minute random head-tracking, all `MAX_OBJECTS=64` objects active,
  random direction updates at 60 Hz per object. Acceptance: zero
  underruns reported by host; `load_into_failures_` zero; no
  `/sys/binaural_warning` emissions; sample-aligned ASan/TSan clean.

---

## ADR (v0.5)

- **Decision.** Ship B1 (direct, default) + B2 (AmbiVS, optional) co-
  resident with mode switch persisted in v4 state. Direction updates
  use dual-slot `OlaConvolver` with `loadInto` (alloc-free) and a
  2-block crossfade. B2 throughput probed at `setActive(true)`, with
  fallback to B1 + user-visible warning on insufficient CPU. `MAX_IR_LEN
  = 1024` (matches `SofaBinReader` cap); IR > 1024 deferred to v0.6.
- **Drivers.** Commercial-release correctness; RT-safety;
  per-object/fixed-fan-out trade-off (B1 vs B2).
- **Alternatives considered.**
  - Ship B1 only, defer B2 to v0.6 — rejected (caps usable object
    count at ~16 on midrange CPUs; orchestral users CPU-bound).
  - Ship B2 only — rejected (loses per-object directional fidelity for
    sparse scenes).
  - Single-slot convolver with in-place reload — rejected (no way to
    avoid audible click on mid-block IR swap without 2 slots + ramp).
  - Crossfade duration 1 block (5.3 ms) — rejected (too short for
    impulsive content); 4 blocks (21 ms) — rejected (perceived as lag
    during head tracking).
  - Probe at `setupProcessing` — rejected (VST3 host-threading
    ambiguity; A6 rationale).
  - B2 chain owned by `SpatialEngine` — rejected (couples failure
    modes between speaker and binaural paths; A4 rationale).
- **Why chosen.** Covers both user segments (sparse + dense object
  counts); RT-safety guarantees layer cleanly (priming + alloc-free
  loadInto + dual-slot crossfade + runtime probe + telemetry); failure
  isolation between paths; v0.6 has a clean expansion lane (partitioned-
  FFT + > 1024-tap IR).
- **Consequences.** ~3.5 MiB resident binaural memory (within budget);
  doubled test matrix (B1 and B2 verified independently + jointly);
  state section 0x0004 owned by binaural; v0.6 inherits the
  `prepareForReload` priming contract.
- **Follow-ups.**
  - v0.6: partitioned-FFT convolver + IR > 1024 + SOFA loader format
    bump to v2.
  - v0.6: JUCE re-introduction (deferred from v0.5).
  - v0.7: BRIR / room-aware binaural (16384-sample IR support gated
    on partitioned-FFT shipping).
