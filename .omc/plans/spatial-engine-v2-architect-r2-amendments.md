# Spatial Engine v2 Plan — Architect r2 Amendments (Delta)

## Context

This document is the **delta amendments package** for ralplan iteration #3,
folded inline into the three canonical base v2 plans persisted to disk:

- `.omc/plans/spatial-engine-v0.3.1-channel-mapping-fix.md`
- `.omc/plans/spatial-engine-v0.4-vst3-layout-and-bus.md`
- `.omc/plans/spatial-engine-v0.5-binaural-commercial.md`

Each base plan is self-consistent with these seven amendments integrated
into the right phase sections. This delta is retained as an audit-trail
record of the Architect r2 -> Critic r3 evolution. Critic reviews the
base plans for correctness; this delta is supplementary.

Architect r2 (`architect-r2-c4v02-review.md` — verdict: accepted & locked
on TLV state, second `AmbiDecoder` bound to 24-point t-design, double-
buffered slots, B1-default / B2-optional, JUCE deferred to v0.6) required
**seven minor amendments** (A1..A7).

All file/line citations were re-verified against `HEAD` (commit a1ae720)
before persistence. Three citation errors flagged by Critic r3 are
corrected below: `GainRamp.h` lives at `core/src/dsp/GainRamp.h` (NOT
`core/src/core/GainRamp.h`), `ALGO_SWAP_K` is `Constants.h:20` (NOT `:21`),
and `SpatialEngineProcessData.hpp` is at `vst3/SpatialEngineProcessData.hpp`
(NOT `vst3/Source/SpatialEngineProcessData.hpp`).

---

## A1 — MAX_IR_LEN = 1024 (lower than the previously assumed 4096)

**Change.** v0.5 P0 mini-ADR replaces the previous tentative `MAX_IR_LEN =
4096` with `MAX_IR_LEN = 1024` samples (~21.3 ms @ 48 kHz).

**Rationale (verified citations).**
- `core/src/hrtf/SofaBinReader.cpp:12` declares `static constexpr uint32_t
  kMaxIRLength = 1024u`. This is the hard cap enforced by the only loader
  path on disk; loads with `hdr.ir_length > 1024` return
  `SpehResult::IRLengthUnsupported` (`:42-46`).
- The supported IR-length set is `{64, 128, 256, 384, 512, 1024}`
  (`SofaBinReader.cpp:42-43`). No production HRTF on the road map exceeds
  1024 samples; ARI/SCUT/CIPIC databases are 128-512.

**Memory budget recomputation (v0.5 P0).**
- Per-object dual-slot HRIR storage: `MAX_OBJECTS x ears x slots x
  MAX_IR_LEN x sizeof(float) = 64 x 2 x 2 x 1024 x 4 = 1 MiB` (down from
  previously sized 4 MiB at 4096). Fits comfortably in L2-friendly
  working set per audio callback.
- Per-object overlap buffers: `64 x 2 x 2 x (1024 - 1) x 4 ~= 1 MiB`.
- Per-object `work_` buffers (`block_size + ir_len - 1`, sized for worst
  case `MAX_BLOCK=512 + 1023 = 1535`): `64 x 2 x 2 x 1535 x 4 ~= 1.5 MiB`.
- Total binaural-path resident memory (v0.5 P0/P1 locked): **~3.5 MiB**.

**Deferred work (explicit).** Support for IR lengths > 1024 (e.g.,
2048/4096 direct HRIR, or 16384-sample BRIR for room-aware binaural) is
deferred to **v0.6** alongside the **partitioned-FFT convolver** and a
**SOFA loader format-version bump** (`SpehHeader` major-version field,
currently implicit `v1`, becomes `v2` with explicit IR-length field and
chunk descriptor). The ADR records this so Critic and downstream readers
do not interpret the 1024 cap as a long-term ceiling.

**Risk.** Lower than expected: 1024 matches deployed asset
characteristics and removes the 3 MiB of dead-allocated buffer space
that v2 would otherwise reserve for an unsupported asset class.

---

## A2 — `OlaConvolver::loadInto(const float* ir, int ir_len)` API contract

**Change.** v0.5 P0 adds a new RT-safe IR-reload entry point on
`spe::hrtf::OlaConvolver` (file: `core/src/hrtf/OlaConvolver.h`) with
the exact signature:

```cpp
void OlaConvolver::loadInto(const float* ir, int ir_len);
```

**Why a new method.** `OlaConvolver::prepare(...)` at
`core/src/hrtf/OlaConvolver.cpp:10-24` unconditionally calls
`ir_.assign(...)`, `overlap_.assign(...)`, and `work_.assign(...)` — all
of which can reallocate. v0.5's RT-side binaural-update path must reload
an HRIR into a pre-prepared convolver **without allocating**, so
`loadInto` is the explicit non-allocating cousin of `prepare`.

**Preconditions (debug-asserted via `assert`; release behavior — see
below).**
- `ir != nullptr && ir_len > 0 && ir_len <= MAX_IR_LEN` (= 1024).
- `ir_.capacity() >= MAX_IR_LEN`.
- `overlap_.capacity() >= MAX_IR_LEN - 1`.
- `work_.capacity() >= block_size_ + MAX_IR_LEN - 1`.

**Release-build behavior on capacity violation (CLARIFIED for Critic r3).**
In release builds, if any precondition fails (notably `ir_.capacity() <
ir_len`), `loadInto` **returns immediately without modifying any
internal state** (no-op). It does **NOT** silently allocate. Debug
builds `assert`. A counter `load_into_failures_` (atomic `uint64_t`)
increments on each no-op for telemetry. This is the RT-safety guarantee:
it is better to lose a single direction update than to allocate on the
audio thread. Failure surfaces to the user via the same
`/sys/binaural_warning` channel used by A6 (telemetry, not user-
catastrophic).

**Behavior (RT-safe, no allocations).**

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

**Capacity-priming step** (control thread, allocations OK — invoked
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

**Compatibility note.** `process()` at `core/src/hrtf/OlaConvolver.cpp:33`
reads `ir_.size()` as the active IR length and `overlap_.size() /
work_.size()` to size its inner loops, so `loadInto` swapping in a
shorter IR (e.g., 256-tap near-grid vs. 512-tap fallback) is honored on
the very next block with no state inconsistency. The `isReady()` guard
at `OlaConvolver.h` continues to work (`!ir_.empty()`).

**Risk.** Low — the contract is mechanical and asserted at debug-build
entry; mis-sized capacities surface immediately in CI, never silently
in production.

---

## A3 — v0.3.1 scope EXTENSION: per-channel OSC commands

**Change.** The v0.3.1 channel-map fix (originally scoped to algorithm
back-ends only) is extended to cover four per-channel OSC endpoints
that share the same architectural defect.

**Bug surface (verified citations).**
- `core/src/core/SpatialEngine.cpp:285-298` (drain block for OSC cmd
  FIFO — verified `:291` shows `qc.obj_id >= MAX_OBJECTS` gate, the
  same pattern reused for channel handlers; the channel-handler
  callsites span `:285-298` through the wider `:310-360` region):
  `NoiseType` / `NoiseGain` / `OutputGain` / `OutputLimit` handlers all
  use `qc.output_ch` (or `qc.noise_ch`) as a **vector index** into
  `spk_gain_lin_[]`, `spk_limiters_[]`, and `noise_chans_[]` — which
  are position-indexed (the natural order in the YAML file), not YAML-
  channel indexed.
- `core/src/ipc/CommandDecoder.cpp:702-750` (the matching encoder side)
  emits `"/output/" + std::to_string(p.channel) + "/gain"`, `"/noise/"
  + std::to_string(p.channel) + "/type"`, etc. The wire address
  therefore reflects "channel number" semantics, while the receive-side
  handler treats it as a position. On a non-sequential YAML (e.g.,
  reordered channels), an OSC `/output/5/gain` lands on whichever
  speaker happens to occupy vector index 5, not the speaker whose YAML
  `channel: 5`.

**Decision (must appear in v0.3.1 ADR).** Per-channel OSC endpoints
address by **YAML channel number** (1-based on the wire, mapped to
vector position via the `channel_to_idx_` lookup added in v0.3.1 P0).
When a client sends `/output/5/gain ,f -3.0`, the engine attenuates
whichever speaker in the loaded layout declares `channel: 5` in YAML.

**Rejected alternatives (steelman).** Keep position-indexed semantics
and add `/output/by_channel/N/gain` namespace as a separate API.
**Why rejected:** doubles the OSC wire surface, fragments user
automation between two conventions, and the breaking change is small
enough — no real-world users on reordered layouts have been reported
per existing fixture coverage (the four bundled fixtures
`lab_4ch.yaml`, `lab_8ch.yaml`, `lab_8ch_aligned.yaml`,
`lab_8ch_irregular.yaml` are all sequential).

**New phase: v0.3.1 P1b — extend channel-map fix to OSC handlers.**
- Files touched:
  - `core/src/core/SpatialEngine.cpp:285-298 (+ :310-360)` — replace the
    four `qc.output_ch < spk_gain_lin_.size()` (and friends) guards with
    a `channel_to_idx_.at(qc.output_ch)` lookup, falling back to a
    debug-only log + drop when the YAML channel is not present in the
    active layout.
  - No change required in `core/src/ipc/CommandDecoder.cpp:702-750`
    (the encoder simply forwards `p.channel` from the wire —
    semantically correct under the new rule).
- Channel-map table: `std::array<int16_t, kMaxYamlChannel + 1>
  channel_to_idx_` (sized to the largest declared YAML channel + 1;
  default `-1` = not present in layout). Built at
  `SpeakerLayout::load()` from the same pass that fills
  `position_to_channel_`.

**New v0.3.1 test.**
`core/tests/test_osc_output_gain_reordered_channels.cpp`
- Loads `core/tests/fixtures/lab_irregular_reordered.yaml` (new fixture:
  8 speakers, YAML channel numbers `{1, 3, 5, 7, 2, 4, 6, 8}` —
  interleaved).
- Sends `/output/5/gain ,f -6.0` via the OSC RT FIFO.
- Renders a single-block impulse on the now-attenuated channel.
- Asserts: the **vector position** whose `channel: 5` field is set (=
  vector index 2 in the reordered fixture) drops by 6 dB; all other
  positions are unchanged.
- Companion negative test sends `/output/9/gain` (unmapped channel) and
  asserts no state mutation + debug log line emitted.

**CHANGELOG note (v0.3.1).** "Per-channel OSC endpoints
(`/output/N/gain`, `/output/N/limit`, `/noise/N/type`,
`/noise/N/gain`) now address speakers by YAML channel number, not by
position in the YAML file. Users with reordered or sparse channel maps
may need to update their automation scripts."

**Risk.** Medium — semantically a breaking change for any external
automation that historically relied on position-indexed addressing.
Mitigation: explicit CHANGELOG entry + the existing four canonical
fixtures are all sequential, so default user workflows see zero
behavior change.

---

## A4 — B2 path ownership: `BinauralMonitor` owns the virtual-speaker chain

**Change.** v0.5 P4 specifies that the B2 (AmbiVS) virtual-speaker
chain — second `AmbiDecoder`, `SpeakerLayout` built from `kTDesign24`,
and 24x2 `OlaConvolver` instances — is **owned by `BinauralMonitor`**,
not by `SpatialEngine` or any shared service.

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
   `core/src/ambi/AllRADTDesigns.hpp:18`, sized 24 — verified; sibling
   tables `kTDesign100`/`kTDesign5200` at `:19-20`). Each t-design
   point `(x, y, z)` is converted to spherical `(az, el)` and inserted
   as a `SpeakerInfo` with synthetic YAML channels 1..24.
2. Call `vs_decoder_.prepare(vs_layout_)` — `AmbiDecoder.h` confirmed
   to accept arbitrary layouts.
3. For each of the 24 virtual speakers, KD-tree-lookup the nearest
   HRIR pair in the loaded `HrtfTable`, copy into `vs_hrir_L_[i] /
   vs_hrir_R_[i]`, record `vs_hrir_len_[i]`.
4. Prime each convolver: `vs_conv_L_[i].prepareForReload(block_size_)`
   then `vs_conv_L_[i].loadInto(vs_hrir_L_[i].data(),
   vs_hrir_len_[i])`; same for `vs_conv_R_[i]`.

**Layout-change behavior (CLARIFIED for Critic r3).** When the
**physical** speaker `SpeakerLayout` changes (user loads new YAML at
runtime — v0.4 P2 path), `vs_layout_` (B2 t-design layout) does
**NOT** re-initialize — it stays pinned to `kTDesign24` because B2 is
virtual-speaker independent of physical layout. The 24 HRIR pairs also
do **NOT** re-load — they are a function only of the loaded `.speh`
table, not of the physical speaker config. Re-initialization happens
only when the `.speh` table itself changes (rare; on user load of a
new HRTF set). Test:
`core/tests/test_b2_layout_change_does_not_reinit_vs.cpp`.

**Gate.** B2 path runs only when `mode_ == BinauralMode::AmbiVS`. Mode
is set by:
- OSC `/sys/binaural_mode ,i {0|1}` (0 = B1 direct, 1 = B2 AmbiVS).
- State section `0x0004` byte 1 (TLV state file — restored at scene
  load).

Mode is consumed inside `BinauralMonitor::process()` via a single
`std::atomic<int> mode_` load at block top; switching modes mid-stream
is allowed and triggers the same 2-block crossfade described in A5.

**Rejected alternatives (steelman).** Hide bus 1 until v0.5 (related
to A7 but applicable here: B2 chain hidden from `BinauralMonitor`
until B2 ships in v0.5). **Why rejected:** owning the B2 chain inside
`BinauralMonitor` from v0.5 P4 onward (vs. a separate "AmbiVSChain"
service class) keeps cohesion tight; a hypothetical separate service
would require a stable inter-module API for `vs_decoder_` and the 48
convolvers, none of which are reused elsewhere in the engine.

**New v0.5 test.** **`core/tests/test_b2_ambivs_equivalent_to_b1_at_order3.cpp`
(perceptual merge gate).** For a single source at known `(az, el)`,
B2 ambi-VS path output should have RMS error vs. direct B1 HRTF
panning < `-20 dB FS` threshold. This is the psychoacoustic merge
gate for B2: if B2 diverges from B1 by more than 20 dB FS on a sparse
source, the AmbiVS chain is broken.

**Risk.** Low — keeps the B2 chain encapsulated and prevents
`SpatialEngine` from growing a second decoder graph; failure isolation
is clean (a B2 init fault degrades to B1 without touching the speaker
path).

---

## A5 — Crossfade duration: 2 blocks (not 1)

**Change.** v0.5 P1 mini-ADR locks the binaural slot-swap crossfade at
**2 blocks** (10.67 ms @ 256/48 kHz), up from a previously tentative
1 block.

**Rationale (with corrected citations).** A single-block (5.33 ms) ramp
is below the threshold where discontinuity becomes inaudible on
impulsive content (transients, percussive sources). Industry practice
(Steam Audio, Resonance Audio, Spatial Workstation, IEM) uses 8-16 ms
HRTF crossfades; 10.67 ms sits at the short, CPU-friendly end of that
range and matches `ALGO_SWAP_K = 256` samples (`core/src/core/Constants.h:20`
— CORRECTED from previously cited `:21`) within the same order of
magnitude for consistency across our crossfade mechanisms.

**Implementation note.**
- `GainRamp::setTarget(target, ramp_samples)` is called with
  `ramp_samples = 2 * block_size_`. The existing linear ramp at
  `core/src/dsp/GainRamp.h:11-58` (CORRECTED from previously cited
  `core/src/core/GainRamp.h:11-58` — the file lives in `core/src/dsp/`,
  not `core/src/core/`) accepts arbitrary positive sample counts — no
  API change.
- Crossfade lifetime: when a slot swap fires (new HRIR direction
  landed), both `slot_old` and `slot_new` run their convolutions for 2
  blocks. The `slot_old.gain` ramps `1 -> 0`, `slot_new.gain` ramps
  `0 -> 1`. After 2 blocks, `slot_old` is marked free and its
  convolver state can be reused by the next direction update without
  flushing.
- Lifetime correctness: a `prev_slot` reference must outlive the
  consuming blocks; the double-buffered slot-pair design guarantees
  this — the promotion of the new slot to "active" persists for the
  entire 2-block window, while the old slot, still scheduled,
  continues to produce ramp-down output without being reclaimed.

**Chained-crossfade behavior (CLARIFIED for Critic r3).** When a new
`setDirection` arrives **mid-crossfade**:
- The currently-fading-in slot becomes "old" immediately.
- The idle slot (formerly "old") receives the new IR and becomes
  "new".
- A fresh 2-block crossfade begins; the previously-fading-in slot is
  ramped down **from its current gain (not from 1.0)**, and the new
  slot ramps up from 0.
- This is **"preempt with current-gain handoff"** semantics —
  guarantees C0 continuity even under rapid head-tracking updates.
- Documented in the v0.5 P1 mini-ADR.

**Rejected alternatives (steelman).**
- **1-block (5.3 ms):** too short for impulsive content (transients,
  percussive sources) — perceptible discontinuity even on conservative
  test signals.
- **4-block (21 ms):** perceived as lag during fast head tracking —
  binaural image "drags behind" head motion, breaking the
  externalization illusion.
- **Chosen: 2-block (10.67 ms)** — short end of Resonance Audio / IEM
  convention 10-20 ms window; click-free + low tracker lag.

**CPU cost.** During the 2 crossfade blocks, per-object convolution
doubles (two OLA passes per ear). Worst case all 64 objects swap
simultaneously: peak 2x per-object cost for 2 blocks — well inside
headroom validated by the v0.5 P4 throughput bench (A6).

**Test update.** `core/tests/test_p_binaural_crossfade_smooth.cpp` is
amended to:
- Issue a direction change at block boundary B0.
- Assert C0 continuity (`|sample[n] - sample[n-1]| < 0.05` on a 1.0-
  amplitude reference impulse train) across blocks B0, B1, B2 (the
  full 2-block ramp + the steady-state landing block).
- Assert sample at `B2 + 1 block` is bit-identical to a non-crossfaded
  reference render of the new direction (confirms ramp fully retired).

**New companion test.**
`core/tests/test_p_binaural_chained_crossfade.cpp` — fires two
direction updates within the same crossfade window; asserts C0
continuity through both transitions and asserts preempt-with-current-
gain handoff semantics.

**Risk.** Low — 10.67 ms is a standard duration with no known
psychoacoustic objection; CPU headroom is reverified in A6's bench.

---

## A6 — B2 throughput bench callsite: `setActive(true)`, not `setupProcessing`

**Change.** v0.5 P4 pre-mortem scenario 3 (CPU-throughput-not-feasible)
is amended to run the B2 throughput probe inside the VST3 plug-in's
`setActive(true)` lifecycle hook
(`vst3/SpatialEngineProcessor.cpp:199`), **not** inside
`setupProcessing()` (at `vst3/SpatialEngineProcessor.cpp:447-459`).

**Rationale.** VST3 spec leaves `setupProcessing()` ambiguous on
threading: JUCE-wrapped hosts (Bitwig, FL Studio, some Cubase project-
startup paths) invoke it from the audio render thread, which means any
synthetic convolution warm-up would either (a) glitch the first block
of audio, or (b) trip our own RT-safety asserts. `setActive(true)`, by
contrast, is contractually a control-thread call across all major
hosts and is the correct spot for one-shot CPU probes.

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

**Rejected alternatives (steelman).**
- **Lazy first-block warm-up bench:** blocks audio thread on first
  block in cold-cache hosts — produces an audible glitch on the
  initial transient and trips our own RT-safety asserts in CI runs of
  malloc-interceptor coverage.
- **Skip bench, ship at user-configurable default mode:** pushes the
  CPU-diagnostics burden to users (who would have to A/B B1 vs B2 in
  every session); breaks the "B2 is best-effort" principle by making
  fallback a user-config concern instead of a runtime guarantee.
- **Chosen: `setActive(true)`:** contractually a control-thread call
  across all major hosts; spec-correct lifecycle hook; one-shot
  measurement with cached result.

**Risk.** Low — moves the probe to a clearly-spec'd control-thread
hook; the fallback path (B2 -> B1) is already tested independently.

---

## A7 — v0.4 binaural bus placeholder: speaker downmix, not silence

**Change.** v0.4 P1 (VST3 multi-bus stub) acceptance criterion is
amended: when bus 1 (Binaural) is exposed but the runtime binaural
path is disabled (`binaural_enable == 0`, the v0.4 default), bus 1
emits the **speaker-bus L/R downmix at -6 dB**, **not** silence.

**Rationale.** Returning silence on bus 1 in v0.4 triggers a
predictable class of DAW user reports ("plug-in seems broken — I
routed headphones to bus 1 and hear nothing"). A -6 dB speaker downmix
produces audible output that is recognizable as the loaded scene even
if it lacks true binaural HRTF coloration, preserving "diagnostic
intelligibility" while v0.5 is still cooking.

**Implementation (with corrected citation).** In the v0.4 P1 adapter
refactor at `vst3/SpatialEngineProcessData.hpp:18-43` (CORRECTED from
previously cited `vst3/Source/SpatialEngineProcessData.hpp:18-43` —
there is no `Source/` subdirectory; the file lives directly under
`vst3/`), when writing to `outputs[1]` (the new binaural bus):

```cpp
// v0.4 placeholder: if binaural is disabled, downmix speaker channels 0+1.
if (!binaural_enable_) {
    const float* L_spk = outputs[0].channelBuffers32[0];
    const float* R_spk = outputs[0].channelBuffers32[1];
    float* L_bin = outputs[1].channelBuffers32[0];
    float* R_bin = outputs[1].channelBuffers32[1];
    for (int n = 0; n < num_samples; ++n) {
        const float mix = 0.5f * (L_spk[n] + R_spk[n]);  // -6 dB sum
        L_bin[n] = mix;
        R_bin[n] = mix;
    }
} else {
    // v0.5 replaces this branch with the real binaural path.
}
```

The -6 dB sum (rather than a per-channel -3 dB -> ~0 dB sum-of-
correlated) keeps headroom safe against in-phase content and matches
typical mono-downmix conventions. v0.5 replaces this branch with the
real `BinauralMonitor::process()` output, so the placeholder code is
removed in the same patch that introduces real binaural rendering —
no dead-code debt.

**Rejected alternatives (steelman).**
- **Hide bus 1 until v0.5:** host bus arrangement changes between
  releases break session compat — a v0.4 session opened in v0.5 would
  have its bus topology mutated, triggering DAW user re-routing work.
- **Ship full silence on bus 1:** DAW users file "plugin broken"
  tickets when routing bus 1 to headphones produces nothing audible;
  diagnostic intelligibility is zero.
- **Chosen: -6 dB downmix as placeholder:** minimum-friction option;
  audible-and-recognizable signal; -6 dB sum keeps headroom safe
  against in-phase content; placeholder branch is deleted (no dead-
  code debt) in the same v0.5 patch that wires real binaural.

**New test.** `core/tests/test_v04_binaural_bus_placeholder.cpp`
- Loads a 2-speaker scene with a 0 dBFS impulse on object 0 panned to
  speaker 0.
- Asserts: `binaural_enable == 0` -> bus 1 contains a non-zero
  attenuated sum (peak ~0.5).
- Asserts: bus 1 L equals bus 1 R bit-exactly (mono downmix property).
- Companion run with `binaural_enable == 1` (v0.5 wired) -> bus 1 !=
  bus 0 L/R sum (confirms the placeholder is gone).

**Risk.** Negligible — purely additive on the output path; no RT-
safety concern (single scalar mix, no allocation, no FFT).

---

## Updated test inventory delta

New tests added by these amendments (relative to the v0.3.1 / v0.4 /
v0.5 plan set Critic last saw):

| Test file | Phase | Purpose |
| --- | --- | --- |
| `core/tests/test_osc_output_gain_reordered_channels.cpp` | v0.3.1 P1b | A3 — OSC `/output/N/gain` on reordered YAML resolves by YAML channel, not position |
| `core/tests/test_osc_output_limit_reordered_channels.cpp` | v0.3.1 P1b | A3 — same shape, `/output/N/limit` endpoint |
| `core/tests/test_osc_noise_type_reordered_channels.cpp` | v0.3.1 P1b | A3 — same shape, `/noise/N/type` endpoint |
| `core/tests/test_osc_noise_gain_reordered_channels.cpp` | v0.3.1 P1b | A3 — same shape, `/noise/N/gain` endpoint |
| `core/tests/fixtures/lab_irregular_reordered.yaml` | v0.3.1 P1b | Test fixture: 8 speakers with interleaved YAML channels `{1,3,5,7,2,4,6,8}` |
| `vst3/tests/test_state_v3_loads_byte_identical_under_v4_writer.cpp` | v0.4 P0 (**merge gate**) | v3 state byte-equal round-trip via v4 writer (Critic r3 requirement) |
| `core/tests/test_v04_binaural_bus_placeholder.cpp` | v0.4 P1 | A7 — bus 1 emits -6 dB speaker downmix when binaural disabled |
| `core/tests/test_ola_convolver_loadinto_rt_safe.cpp` | v0.5 P0 | A2 — `OlaConvolver::loadInto` is alloc-free after `prepareForReload` (malloc-interceptor test) |
| `core/tests/test_ola_convolver_loadinto_clamp.cpp` | v0.5 P0 | A2 — `loadInto` rejects `ir_len > MAX_IR_LEN` in release builds (silent no-op + counter) |
| `core/tests/test_ola_convolver_loadinto_capacity_violation_release.cpp` | v0.5 P0 | A2 — release no-op on capacity violation + `load_into_failures_` increments |
| `core/tests/test_p_binaural_chained_crossfade.cpp` | v0.5 P1 | A5 — preempt-with-current-gain handoff on chained crossfade |
| Updated: `core/tests/test_p_binaural_crossfade_smooth.cpp` | v0.5 P1 | A5 — extends C0-continuity assertion across the 2-block ramp window |
| `core/tests/test_b2_throughput_probe_fallback.cpp` | v0.5 P4 | A6 — synthetic slow-CPU mock confirms B2 -> B1 fallback path + OSC warning emission |
| `core/tests/test_b2_ownership_lifecycle.cpp` | v0.5 P4 | A4 — `BinauralMonitor` constructs/teardown of `vs_decoder_` + 48 convolvers without leaks (ASan run) |
| `core/tests/test_b2_layout_change_does_not_reinit_vs.cpp` | v0.5 P4 | A4 — runtime physical-layout YAML swap doesn't disturb B2 chain |
| `core/tests/test_b2_ambivs_equivalent_to_b1_at_order3.cpp` | v0.5 P4 (**perceptual merge gate**) | B2 vs. B1 RMS error < `-20 dB FS` on sparse source |

Total: **13 new test files + 1 new YAML fixture + 1 amended existing
test**.

---

## Per-amendment risk note

- **A1.** Lower-than-baseline risk: matches enforced loader cap; cuts
  dead memory; defers a feature that has no current consumer.
- **A2.** Low risk: contract is mechanical and surfaced via debug
  asserts + a dedicated CI test; mis-sized capacity fails loud, never
  silent. Release behavior fully specified (no-op + counter).
- **A3.** Medium risk: semantic break for external automation on
  reordered YAML layouts; offset by explicit CHANGELOG and an
  unchanged default-fixture baseline. Rejected `/by_channel/N` namespace
  alternative captured.
- **A4.** Low risk: encapsulates B2 inside `BinauralMonitor`; failure
  modes isolated from the speaker render path. Layout-change behavior
  spec'd (no B2 re-init on physical layout change).
- **A5.** Low risk: 10.67 ms is a textbook HRTF-ramp duration; CPU cost
  bounded by A6's probe. Chained-crossfade preempt-with-current-gain
  handoff fully specified. Rejected 1-block / 4-block alternatives
  steelmanned.
- **A6.** Low risk: relocates a probe to the contractually-correct
  VST3 lifecycle hook; fallback already in-plan. Rejected lazy-warm-up
  / skip-bench alternatives steelmanned.
- **A7.** Negligible risk: additive output mix, no RT-safety
  implications, removed cleanly in v0.5. Rejected hide-bus / silence
  alternatives steelmanned.

---

## Sign-off

`spatial-engine-v2-architect-r2-amendments.md` (this delta) together
with the canonical v2 base plans persisted at
`.omc/plans/spatial-engine-v0.3.1-channel-mapping-fix.md`,
`.omc/plans/spatial-engine-v0.4-vst3-layout-and-bus.md`, and
`.omc/plans/spatial-engine-v0.5-binaural-commercial.md` forms the
canonical **Critic input** for ralplan iteration #3.

All 7 amendments are folded inline into the respective base plans.
Citation errors flagged by Critic r3 (`core/src/dsp/GainRamp.h` vs.
`core/src/core/GainRamp.h`, `Constants.h:20` vs. `:21`,
`vst3/SpatialEngineProcessData.hpp` vs. `vst3/Source/...`) are
corrected. v3 -> v4 byte-equal merge gate test specified in v0.4 P0.
A2 release behavior and A5 chained-crossfade semantics fully
specified. A4 layout-change behavior clarified. B2 perceptual merge
gate test added. Steelmans (rejected alternatives) captured for A3,
A5, A6, A7.

— Planner, ralplan iter #3 (final iteration)
