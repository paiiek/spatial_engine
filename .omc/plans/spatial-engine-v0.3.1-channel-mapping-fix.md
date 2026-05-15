# Spatial Engine v0.3.1 — Channel Mapping Correctness Pre-Release

## Context

Pre-v0.4 patch release. v0.3 shipped a defect in the OSC/algorithm channel
addressing pipeline: handlers indexed speaker-state vectors by YAML position
rather than by YAML channel number, so reordered or sparse channel maps
silently routed automation to the wrong speaker. v0.3.1 is a focused
correctness release — no new features, no API additions beyond the explicit
extension folded in from Architect r2 amendment A3 (per-channel OSC endpoints
were not in the original v0.3.1 scope but share the same defect surface).

All file/line citations were re-verified against `HEAD` (commit a1ae720)
prior to writing. References use the in-tree paths
`core/src/dsp/GainRamp.h`, `vst3/SpatialEngineProcessData.hpp`, and
`core/src/core/Constants.h` (no `Source/` subdir; ALGO_SWAP_K is on line 20).

---

## Principles

1. **Correctness over performance.** No optimization in this release; the
   channel-map lookup is `O(1)` array indexing — no measurable cost.
2. **Backwards-compat for sequential layouts.** All four canonical fixtures
   (`lab_4ch.yaml`, `lab_8ch.yaml`, `lab_8ch_aligned.yaml`,
   `lab_8ch_irregular.yaml`) declare sequential channel numbers, so default
   workflows see zero behavior change.
3. **Loud failure for unmapped channels.** Receiving `/output/N/gain` on a
   YAML channel `N` not present in the layout drops the command and emits a
   debug log line; the engine state is never silently mutated.
4. **Single source of truth.** `SpeakerLayout::load()` builds both
   `position_to_channel_` and the new inverse `channel_to_idx_` in the same
   pass; there is no possibility of the two getting out of sync.
5. **No state-format change.** v0.3.1 preserves the v3 state schema
   byte-for-byte; only handler routing changes.

## Decision Drivers (top 3)

1. **External automation contracts.** OSC consumers (livestream rig
   scripts, theatre cue automation) expect channel numbers to refer to the
   YAML channel field, not the array position.
2. **Algorithm fan-out parity.** Speaker-gain, limiter, and noise-injector
   chains were already routed by position; aligning them with the YAML
   channel contract avoids per-feature inconsistency.
3. **Pre-v0.4 ship gate.** v0.4 introduces multi-bus I/O (binaural bus),
   which is dependent on the speaker-bus channel semantics being settled.
   Shipping v0.4 over a broken v0.3 channel-map would compound the breakage.

## Viable options considered

**Option A (chosen): rewrite handler-side indexing via `channel_to_idx_`
lookup.** Handlers translate the wire channel number into a vector position
inside the same drain pass. Minimal surface change. **Pros:** keeps OSC
wire surface unchanged, no breaking change to consumers using sequential
layouts, single new lookup table, easy to unit-test. **Cons:** semantically
breaking for any external automation that historically depended on
position-indexed addressing (mitigated by the sequential-default observation
above + an explicit CHANGELOG entry).

**Option B (rejected): keep position-indexed semantics, add
`/output/by_channel/N/gain` namespace as a separate API.** **Why rejected:**
doubles the OSC wire surface, fragments user automation between two
conventions, and the breaking change is small enough — the four bundled
fixtures are all sequential and no real-world users on reordered layouts
have been reported per existing fixture coverage.

---

## Phase P0 — Wire-up the channel-to-index table

**Files touched.**
- `core/src/geometry/SpeakerLayout.h` — add private member
  `std::array<int16_t, kMaxYamlChannel + 1> channel_to_idx_` (sized to the
  largest declared YAML channel + 1, default `-1` = not present in layout).
  Add public accessor `int channelToIndex(int yaml_channel) const`.
- `core/src/geometry/SpeakerLayout.cpp` — extend `load()` to fill
  `channel_to_idx_` in the same pass that fills `position_to_channel_`.
  Validate: no duplicate YAML channels (return `LoadResult::DuplicateChannel`
  on collision; existing canonical layouts pass).

**Acceptance criteria.**
- `SpeakerLayout::load()` returns the new error on a synthetic duplicate-
  channel YAML.
- `channelToIndex(N)` returns the correct vector position for all four
  canonical fixtures and `-1` for any YAML channel not in the layout.
- Unit test `core/tests/test_speaker_layout_channel_lookup.cpp` covers
  sequential, gapped, and reordered layouts.

## Phase P1 — Algorithm-backend handlers (original v0.3.1 scope)

**Files touched.**
- `core/src/render/VbapRenderer.cpp` — replace position-indexed gain lookup
  with `layout.channelToIndex(...)` for any per-channel gain trims.
- `core/src/render/DbapRenderer.cpp` — same shape.
- `core/src/render/WfsRenderer.cpp` — same shape.
- `core/src/render/AmbisonicRenderer.cpp` — same shape (decoder gain trim).

**Acceptance criteria.**
- All four algorithm backends route per-channel state by YAML channel on a
  reordered fixture (`lab_irregular_reordered.yaml` — new fixture, see P1b).
- `core/tests/test_render_channel_routing_reordered.cpp` exercises one
  impulse per algorithm on the reordered fixture and asserts the output
  energy lands on the vector position whose `channel` field matches the
  input channel target.

## Phase P1b — OSC handler extension (Architect r2 A3, folded in)

**Files touched.**
- `core/src/core/SpatialEngine.cpp:285-298` (and the surrounding drain block
  through `:310-360`) — replace the four
  `qc.output_ch < spk_gain_lin_.size()` (and friends) guards with a
  `channel_to_idx_.at(qc.output_ch)` lookup, falling back to a debug-only
  log + drop when the YAML channel is not present in the active layout.
  Verified: `:291` already gates `qc.obj_id >= MAX_OBJECTS` — the same
  pattern is reused for the new gate.
- `core/src/ipc/CommandDecoder.cpp:702-750` (matching encoder side) — no
  change required; the encoder simply forwards `p.channel` from the wire,
  which is semantically correct under the new rule.

**Decision (recorded in v0.3.1 ADR).** Per-channel OSC endpoints address by
**YAML channel number** (1-based on the wire, mapped to vector position via
the `channel_to_idx_` lookup added in P0). When a client sends
`/output/5/gain ,f -3.0`, the engine attenuates whichever speaker in the
loaded layout declares `channel: 5` in YAML.

**Test fixture (new).** `core/tests/fixtures/lab_irregular_reordered.yaml` —
8 speakers, YAML channel numbers `{1, 3, 5, 7, 2, 4, 6, 8}` (interleaved).

**Acceptance criteria (per-endpoint tests).**
- `core/tests/test_osc_output_gain_reordered_channels.cpp` — sends
  `/output/5/gain ,f -6.0`; asserts the vector position whose `channel: 5`
  field is set (= vector index 2 in the reordered fixture) drops by 6 dB;
  all other positions are unchanged. Companion negative test sends
  `/output/9/gain` (unmapped channel) and asserts no state mutation +
  debug log line emitted.
- `core/tests/test_osc_output_limit_reordered_channels.cpp` — same shape
  for `/output/N/limit`.
- `core/tests/test_osc_noise_type_reordered_channels.cpp` — same shape for
  `/noise/N/type`.
- `core/tests/test_osc_noise_gain_reordered_channels.cpp` — same shape for
  `/noise/N/gain`.

## Phase P2 — Release validation

**Files touched.** No production-code changes. CI configuration only.

**Acceptance criteria.**
- All four canonical fixtures still pass existing v0.3 regression tests
  (no behavioral drift on sequential layouts).
- Full `ctest --output-on-failure` + `python3 -m pytest` green.
- Manual smoke: load `lab_irregular_reordered.yaml`, run a 60-second OSC
  automation script, confirm the right speakers respond.
- CHANGELOG entry: "Per-channel OSC endpoints (`/output/N/gain`,
  `/output/N/limit`, `/noise/N/type`, `/noise/N/gain`) now address speakers
  by YAML channel number, not by position in the YAML file. Users with
  reordered or sparse channel maps may need to update their automation
  scripts."

---

## Tests added (summary)

| Test file | Phase |
| --- | --- |
| `core/tests/test_speaker_layout_channel_lookup.cpp` | P0 |
| `core/tests/test_render_channel_routing_reordered.cpp` | P1 |
| `core/tests/test_osc_output_gain_reordered_channels.cpp` | P1b |
| `core/tests/test_osc_output_limit_reordered_channels.cpp` | P1b |
| `core/tests/test_osc_noise_type_reordered_channels.cpp` | P1b |
| `core/tests/test_osc_noise_gain_reordered_channels.cpp` | P1b |
| `core/tests/fixtures/lab_irregular_reordered.yaml` | P1b fixture |

## Cross-cutting risks

- **R1 (medium).** Semantic break for external automation on reordered YAML
  layouts. Mitigation: explicit CHANGELOG entry + sequential-fixture
  coverage shows zero default-workflow regression.
- **R2 (low).** Off-by-one in `channel_to_idx_` sizing if a fixture declares
  `channel: 0`. Mitigation: `SpeakerLayout::load()` rejects channel `0`
  explicitly (1-based contract — covered by P0 acceptance criteria).
- **R3 (low).** `channel_to_idx_.at()` throws on out-of-range — must be
  guarded by a size check before `.at()` in the audio-thread drain (use
  unchecked indexing with explicit bounds check). Verified: P1b
  implementation note pins this to a guarded `if (qc.output_ch <
  channel_to_idx_.size()) idx = channel_to_idx_[qc.output_ch];` pattern.

## ADR (v0.3.1)

- **Decision.** Per-channel OSC and algorithm-backend handlers address by
  YAML channel number; `SpeakerLayout::channel_to_idx_` is the single
  translation table.
- **Drivers.** External automation contracts; algorithm fan-out parity;
  pre-v0.4 ship gate.
- **Alternatives considered.** Position-indexed addressing kept with a
  separate `/by_channel/N/...` namespace — rejected (doubles wire surface,
  fragments automation, breaking change is small in practice).
- **Why chosen.** Lowest wire-surface footprint; single translation table;
  sequential-default workflows unaffected.
- **Consequences.** Reordered-layout users must update scripts; CHANGELOG
  flag added.
- **Follow-ups.** v0.4 inherits the same lookup; no further channel-map
  work needed downstream.
