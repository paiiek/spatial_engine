# spatial_engine v0.4.0 — VST3 layout, two-bus arrangement, state v4

**Tag commit**: `3a7e9f6` (2026-05-15)
**Predecessor**: v0.3.1
**Changelog**: see [CHANGELOG.md §0.4.0](../../../CHANGELOG.md#040--2026-05-15).

## Summary

VST3 plugin is now ready for DAW workflows that need simultaneous
multichannel-speaker and stereo-binaural output from a single track.
Plugin state schema migrated to a sectioned TLV (v4) with v3 back-
compat merge gate. The binaural bus ships as a `-6 dB` speaker
downmix placeholder; real binaural rendering arrives in v0.5.

## Highlights

- **Two output buses.**
  - Bus 0 "Speakers": variable channel count negotiated via
    `setBusArrangements` (2 / 4 / 6 / 8 / 12 / 16 / 24 supported).
  - Bus 1 "Binaural": fixed stereo. v0.4 emits a `-6 dB` downmix
    placeholder; v0.5 replaces it with the B1 per-object HRTF path.
- **State schema v4 (sectioned TLV).** v3 sessions load unchanged
  (back-compat merge-gate ctest).
- **Layout YAML path and `.speh` (SOFA bundle) path** persisted in
  plugin state and runtime-injectable via OSC
  (`/sys/load_layout ,s <path>`, `/sys/binaural_sofa ,s <path>`,
  `/sys/binaural_enable ,i {0,1}`).

## Breaking changes

- None for VST3 hosts that already supported multi-bus instances.
  Single-bus hosts will see only Bus 0.

## Upgrade notes

- No CHANGELOG entry for v0.4 OSC schema change. The `/sys/load_layout`
  and `/sys/binaural_sofa` addresses are additions, not modifications.
- DAW hands-on for v0.4-specific bus arrangement remains user-action
  (see `../v0.3.0/daw-handson-log.md` template — port to v0.4 and add
  a "Bus 0 visible + Bus 1 placeholder audible at -6 dB" check).

## Release validation

- `ctest`: PASS (75 tests at v0.4 baseline).
- `pytest`: PASS.

## Lineage commits

- `1301a51` feat(vst3): state v4 sectioned TLV.
- `c7426d3` feat(vst3): two output buses + -6 dB placeholder.
- `a6f6a6f` docs(plans): persist Critic-approved v2 ralplan plans.
- `2221a7c` feat(vst3,ipc): P2 layout YAML path injection.
- `e16701a` feat(vst3,ipc): P3 .speh path + binaural-enable plumbing.
- `3a7e9f6` chore(release): v0.4 tag.
