# Spatial Engine v0.4 — Track A: VST3 Bus Arrangement, State v4, Layout YAML, `.speh` Plumbing

## Context

v0.4 is the structural-plumbing release: VST3 multi-bus arrangement (speaker
+ binaural buses), sectioned/TLV state v4 schema, layout YAML injection from
the DAW host, and `.speh` HRTF file path plumbing through the processor.
**No binaural rendering yet** — bus 1 emits a placeholder (Architect r2
amendment A7: -6 dB speaker downmix, NOT silence). Real binaural arrives in
v0.5.

This release runs in parallel with v0.5 (Track B) on a different branch line;
the two converge at the v0.5 P1 wiring step, which replaces v0.4 P1's
placeholder with the real `BinauralMonitor::process()` output.

All file/line citations were re-verified against `HEAD` (commit a1ae720).

---

## Principles

1. **No DSP regressions.** v0.4 must not change a single audio sample on
   bus 0 (speaker bus) vs. v0.3.1.
2. **State v4 is a superset of v3.** Loaders read both; writers emit v4
   only; the v3-to-v4 transition is verifiable via byte-equal merge gate
   (see P0 acceptance criteria).
3. **Bus 1 is honest.** When binaural is disabled, bus 1 emits an audible,
   recognizable signal (downmix) instead of silence. No DAW user files a
   "plug-in broken" ticket.
4. **Path plumbing is opt-in.** `.speh` path injection from the host is
   optional; absent path → engine runs in speaker-only mode with bus 1
   downmix placeholder.
5. **No JUCE.** v0.4 sticks with the `SPATIAL_ENGINE_NO_JUCE=ON` build
   profile; JUCE re-introduction is deferred to v0.6.

## Decision Drivers (top 3)

1. **Multi-bus is a v0.5 prerequisite.** Bus 1 has to exist before B1/B2
   binaural rendering can target it; v0.4 lands the bus and the
   placeholder, v0.5 fills the audio.
2. **State-schema migration debt.** v3 is flat-key/positional; v4 is
   sectioned/TLV. Cleaning this up before binaural state lands prevents a
   compound migration (v3 -> v4 + binaural) in v0.5.
3. **DAW user trust.** Silent buses get reported as broken. -6 dB downmix
   preserves diagnostic intelligibility through the v0.4 -> v0.5 window.

## Viable options considered

**Option A (chosen): single-PR ship of all four substrates with a
placeholder bus 1.** **Pros:** convergence point for v0.5 is a one-line
swap; state and bus arrangement migrate together; merge-gate test catches
schema regression early. **Cons:** PR size is larger than typical (~6
phases).

**Option B (rejected): split state-v4 into its own pre-release before
bus-arrangement work.** **Why rejected:** doubles the migration surface
(two CHANGELOG entries, two regression windows); the sectioned schema and
bus exposure share the same `vst3/SpatialEngineProcessor.cpp` editing
window and benefit from co-located review.

---

## Phase P0 — State v4 sectioned/TLV schema

**Files touched.**
- `vst3/SpatialEngineState.h` — new header defining TLV section IDs:
  `0x0001 = engine_core`, `0x0002 = layout`, `0x0003 = osc_routes`,
  `0x0004 = binaural` (reserved/empty in v0.4; populated in v0.5).
- `vst3/SpatialEngineState.cpp` — reader auto-detects v3 vs. v4 from the
  4-byte magic; v3 path delegates to existing `v3_reader.cpp` parser; v4
  path walks TLV sections. Writer always emits v4.
- `vst3/SpatialEngineProcessor.cpp:265-365` (the `getState` / `setState`
  region) — replaces flat-key serialization with TLV emission.

**Tests added.**
- `vst3/tests/test_vst3_state_v4_persist.cpp` — round-trips a v4 blob
  (write -> read -> compare every section's payload bytewise).
- **`vst3/tests/test_state_v3_loads_byte_identical_under_v4_writer.cpp`**
  (merge-gate test) — loads `vst3/tests/fixtures/state_v3_capture.bin`
  (existing v3 captured-state fixture; co-located with the existing
  `test_vst3_state_v3_persist.cpp`/`test_vst3_state_v3_reader_only.cpp`
  fixtures) with the v4-capable reader, re-emits as v4, loads the v4 blob
  into a fresh processor, asserts internal engine state floats are
  **byte-identical** (`memcmp`) to the first load. **Merge gate: must
  block PR merge if this test regresses.**
- Updated `vst3/tests/test_vst3_state_persist.cpp` — exercises both v3 and
  v4 reader paths against the same fixtures.

**Acceptance criteria.**
- `cmake --build . --target test_vst3_state_v4_persist && ctest -R
  state_v4` passes.
- `test_state_v3_loads_byte_identical_under_v4_writer` passes
  (**merge gate: v3 state byte-equal round-trip via v4 writer**).
- Any v3-only test in `vst3/tests/test_vst3_state_v3_*.cpp` still passes
  unchanged (reader compat preserved).

## Phase P1 — VST3 bus arrangement (speaker + binaural)

**Files touched.**
- `vst3/SpatialEngineProcessor.cpp:414-430` (`setBusArrangements`) — accept
  speaker bus with channel count matching loaded layout (1..32) and a fixed
  2-channel binaural bus.
- `vst3/SpatialEngineProcessor.cpp:447-459` (`setupProcessing`) — propagate
  bus topology to engine prepare path.
- `vst3/SpatialEngineProcessor.cpp:468-...` (`process`) — wire bus 1 output
  through the A7 downmix branch.
- `vst3/SpatialEngineProcessData.hpp:18-43` (verified: this is the in-tree
  path; no `Source/` subdir) — adapter accepts bus 1 output channel buffers.

**Implementation (A7 folded in).** In the v0.4 P1 adapter refactor at
`vst3/SpatialEngineProcessData.hpp:18-43`, when writing to `outputs[1]`:

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

The -6 dB sum (rather than per-channel -3 dB -> ~0 dB sum-of-correlated)
keeps headroom safe against in-phase content and matches typical
mono-downmix conventions. v0.5 replaces this branch with real
`BinauralMonitor::process()` output, so the placeholder code is removed
in the same patch that introduces real binaural rendering — no dead-code
debt.

**Tests added.**
- `core/tests/test_v04_binaural_bus_placeholder.cpp` — loads a 2-speaker
  scene with a 0 dBFS impulse on object 0 panned to speaker 0; asserts:
  `binaural_enable == 0` -> bus 1 contains a non-zero attenuated sum
  (peak ~0.5); asserts bus 1 L equals bus 1 R bit-exactly (mono downmix
  property); companion run with `binaural_enable == 1` (v0.5 wired)
  -> bus 1 != bus 0 L/R sum (confirms the placeholder is gone).
- `vst3/tests/test_vst3_bus_arrangement_speaker_binaural.cpp` — exercises
  `setBusArrangements` with various speaker bus widths.

**Acceptance criteria.**
- VST3 validator passes with the new bus topology.
- Bus 0 audio is bit-identical to v0.3.1 (regression gate).
- Bus 1 emits the A7 downmix when binaural is disabled.

## Phase P2 — Layout YAML injection from host

**Files touched.**
- `vst3/SpatialEngineProcessor.cpp` (around the parameter / message handler
  region near `:780-785`) — add a `kLayoutYaml` IMessage handler that
  receives a YAML string blob from the host (controller side via
  `IComponentHandler2::performEdit`-style message bus) and forwards to
  engine's `SpeakerLayout::loadFromString(...)`.
- `core/src/geometry/SpeakerLayout.h/.cpp` — add `loadFromString` (sibling
  of `load(path)`).

**Acceptance criteria.**
- Host can switch layouts mid-session by emitting `kLayoutYaml` IMessage.
- Layout change is RT-safe: parsed on the message-thread, swapped via
  double-buffered atomic pointer the next block.
- New unit test `core/tests/test_speaker_layout_load_from_string.cpp`
  covers all four canonical YAMLs.

## Phase P3 — `.speh` HRTF path plumbing

**Files touched.**
- `vst3/SpatialEngineProcessor.cpp` — add a `kSpehPath` IMessage handler.
- `core/src/hrtf/HrtfTable.h` — add `loadFromPath(const std::string&)`
  delegating to existing `SofaBinReader::loadSpeh()`.
- v0.4 only **stores** the loaded `HrtfTable` on the engine; bus-1
  consumption is wired in v0.5.

**Acceptance criteria.**
- A valid `.speh` blob loads without error (existing
  `SofaBinReader.cpp:12,42-43` enforces `kMaxIRLength=1024` cap and the
  supported IR-length set `{64, 128, 256, 384, 512, 1024}`).
- A malformed `.speh` returns `SpehResult::*` enum error and surfaces an
  OSC `/sys/hrtf_error ,s "..."` for UI visibility.
- v0.4 must not yet **use** the loaded table on the audio path; bus 1
  remains on the A7 placeholder until v0.5.

## Phase P4 — Release validation

**Files touched.** CI configuration only.

**Acceptance criteria.**
- Full `ctest --output-on-failure` + `python3 -m pytest` green.
- VST3 validator passes (Steinberg validator + REAPER smoke).
- v0.3 -> v0.4 manual upgrade test: load a saved v3 session in v0.4 build,
  verify zero behavioral drift on bus 0, verify bus 1 plays the downmix
  placeholder.
- CHANGELOG entry: "Adds binaural output bus (currently a -6 dB speaker
  downmix placeholder; real binaural rendering arrives in v0.5). State
  format migrates to v4 sectioned TLV schema; v3 sessions load
  unchanged."

---

## Tests added (summary)

| Test file | Phase |
| --- | --- |
| `vst3/tests/test_vst3_state_v4_persist.cpp` | P0 |
| `vst3/tests/test_state_v3_loads_byte_identical_under_v4_writer.cpp` | P0 (merge gate) |
| `vst3/tests/test_vst3_bus_arrangement_speaker_binaural.cpp` | P1 |
| `core/tests/test_v04_binaural_bus_placeholder.cpp` | P1 |
| `core/tests/test_speaker_layout_load_from_string.cpp` | P2 |

## Cross-cutting risks

- **R1 (medium).** v3 -> v4 schema migration silently corrupts state if
  TLV section IDs collide with a stray byte in the v3 blob. Mitigation:
  v4 magic is `SPE4` (distinct from v3's `SPE3`); reader dispatches on
  magic before walking TLV.
- **R2 (low).** Bus 1 downmix is louder than expected when both
  speaker channels carry correlated content. Mitigation: -6 dB sum (A7
  rationale); unit-test asserts peak <= 1.0 on a worst-case correlated
  impulse fixture.
- **R3 (low).** `.speh` path injected mid-stream while engine is rendering
  blocks. Mitigation: load happens on message thread; engine adopts the
  new `HrtfTable` via atomic pointer swap at next block boundary.

## ADR (v0.4)

- **Decision.** Ship multi-bus arrangement, state v4, layout/`.speh` path
  plumbing in one release; bus 1 emits A7 placeholder downmix until v0.5
  fills it with real binaural rendering.
- **Drivers.** v0.5 prerequisite (bus must exist); state-schema migration
  debt; DAW user trust (no silent bus).
- **Alternatives considered.** Split state-v4 into its own release —
  rejected (doubles migration window).
- **Why chosen.** Co-located review surface; one-line swap point for v0.5;
  merge-gate test catches schema regression at PR time.
- **Consequences.** Bus 1 listeners get an audible-but-not-binaural signal
  in the v0.4 -> v0.5 window; documented in CHANGELOG.
- **Follow-ups.** v0.5 P1 removes the A7 placeholder branch and replaces
  with `BinauralMonitor::process()`.
