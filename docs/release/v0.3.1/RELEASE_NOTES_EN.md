# spatial_engine v0.3.1 — channel-mapping correctness pre-release

**Tag commit**: `cfdd987` (2026-05-15)
**Predecessor**: v0.3.0
**Changelog**: see [CHANGELOG.md §0.3.1](../../../CHANGELOG.md#031--2026-05-15-channel-mapping-correctness-pre-release).

## Summary

Single-issue patch release: per-channel OSC endpoints
(`/output/N/gain`, `/output/N/limit`, `/noise/N/type`, `/noise/N/gain`)
now address speakers by their declared YAML `channel:` number rather
than by their position in the layout file. Duplicate or out-of-range
channel numbers are now rejected at layout-load time.

## Highlights

- `SpeakerLayout::channelToIndex()` lookup built by `LayoutLoader`.
- Four OSC drain handlers rewritten in `SpatialEngine` to use the
  lookup.
- Loud-failure semantics: unmapped channel commands are dropped
  silently at the audio thread (RT-safe, no allocation); duplicate or
  > `kMaxYamlChannel` (64) channel declarations are rejected at load
  time with explicit error strings.

## Breaking semantic note

External OSC automation that historically targeted *position* (e.g.
`/output/0/gain` to mean "first speaker in the YAML file") will now
target *YAML channel*. All four canonical fixtures
(`lab_4ch.yaml`, `lab_8ch.yaml`, `lab_8ch_aligned.yaml`,
`lab_8ch_irregular.yaml`) declare sequential 1-based channel numbers,
so default workflows see zero behavior change. Users running reordered
or sparse channel maps must update automation to address speakers by
their declared YAML channel number.

## Upgrade notes

- No state-format change. v0.3.0 sessions load unchanged.
- No public OSC schema change beyond the routing semantics described
  above.
- Renderers verified to have no per-channel state requiring updates
  (`cb2862a` audit commit).

## Release validation

- `ctest`: PASS (53 tests at v0.3.1 baseline).
- `pytest`: PASS.

## Lineage commits

- `c53667a` fix(core/geometry): SpeakerLayout channel→index lookup.
- `cb2862a` chore(core/render): audit confirms renderers OK.
- `2e51661` fix(core/osc): route per-channel commands via channel_to_idx_.
- `cfdd987` chore(release): v0.3.1 tag.
