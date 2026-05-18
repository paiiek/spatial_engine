# spatial_engine v0.5.0 — commercial-grade binaural decoder

**Tag commit**: `3d266f4` (2026-05-15) + P4/P4.1/P5/P6 follow-up commits
shipped under the same v0.5 line.
**Predecessor**: v0.4.0
**Changelog**: see [CHANGELOG.md §0.5.0](../../../CHANGELOG.md#050--2026-05-15).

## Summary

Binaural rendering on VST3 Bus 1 promoted from `-6 dB` placeholder to
a commercial-grade decoder with two paths (B1 per-object HRTF + B2
24-pt AmbiVS). RT-safe by construction: HRTF lookups are O(log N)
KD-tree, IR reload is no-allocation, and direction changes use
2-block crossfaded slot swaps.

## Highlights

- **B1 — per-object HRTF summation.** Each active source (up to
  `MAX_OBJECTS = 64`) is convolved with the nearest HRTF pair and
  summed into the stereo binaural bus. A `-1 dBFS` channel limiter
  caps the bus. RT-safe: `OlaConvolver::loadInto()` is the no-
  allocation reload contract, with capacity violations counted via
  `load_into_failures_`.
- **B2 — 24-pt t-design AmbiVS path** (optional). Ambisonic decode to
  a 24-point t-design virtual-speaker layout, each VS feeding a fixed
  HRIR pair. A CPU-throughput probe (`runThroughputProbe()`) runs at
  `setActive(true)`; on insufficient headroom (< 1.5× RT) the
  effective mode auto-clamps back to B1 while `requestedMode()`
  preserves the user intent for later retry.
- **O(log N) HRTF lookup.** `KdTree3D` replaces the O(N) brute-force
  `sin`/`cos` search. Built at `.speh` load (control thread,
  allocations OK); queries are iterative (no recursion) and
  allocation-free.
- **State v4 binaural section is fully populated**:
  `[enable u8, effective_mode u8, requested_mode u8, reserved u8]`.
  The reader dispatches off `requested_mode` so user-asked B2 is
  restored across reload even if a prior session was probe-clamped to
  B1. v3 sessions and partially-populated v4 sessions both load.
- **Per-object binaural test fixture**:
  `core/tests/fixtures/synthetic_min.speh` (committed).
- **OSC mode control.** `/sys/binaural_mode ,i {0=B1, 1=B2}`.

## Deferred to v0.6 (later landed in v0.6.0)

- Head-tracker hook (`/sys/headtrack ,fff yaw pitch roll`) — still
  deferred.
- JUCE partitioned-convolution binaural path — v0.5 ships
  OlaConvolver only; deferred indefinitely.

## Breaking changes

- Bus 0 (speakers) is bit-identical to v0.4 for all canonical fixtures.
- Bus 1 changes from `-6 dB` placeholder to real binaural when
  `.speh` is loaded and binaural is enabled; otherwise the v0.4
  placeholder semantics are preserved for diagnostic intelligibility.

## Upgrade notes

- Memory footprint grows by ~1 MiB (64 × 2 × 2 × 1024 floats of slot
  buffers). `MAX_IR_LEN = 1024` matches the SOFA loader's existing
  cap.
- Users running on CPU-constrained machines should expect the B2
  probe to occasionally clamp the effective mode to B1; this is
  surfaced via `requestedMode() != effectiveMode()` and (from
  v0.5.1 forward) via `/sys/binaural_warning ,s "..."`.

## Release validation (P6)

- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **75/75 PASS**
  (includes `ola_convolver_loadinto_*`,
  `kdtree3d_matches_brute_force`, `binaural_b1_64_objects_rt_safe`,
  `b2_throughput_probe_fallback`, `vst3_state_v4_binaural_section`).
- `pytest`: 221 passed, 3 skipped, 0 failed (full suite including
  Playwright WebGUI tests).
- ASan + UBSan: v0.5 binaural surface clean. Two pre-existing failures
  (`vst3_intra_plugin_spsc_drain`, `vst3_console_flood`) on Steinberg
  static-destruction ordering are scheduled for v0.5.1 / v0.6 ASan
  cleanup (do not touch binaural code path).

## Lineage commits

- `55ba305` feat(hrtf): OlaConvolver::loadInto + prepareForReload.
- `470aecc` feat(hrtf): KdTree3D O(log N) lookup.
- `cd5059c` feat(binaural): RT-safe double-buffer slot swap + 2-block xfade.
- `3eb55de` feat(binaural): per-object HRTF summation (B1).
- `3d266f4` chore(release): v0.5 tag.
- `3e08964` feat(binaural): B2 AmbiVS path (P4).
- `7cdcee8` fix(binaural): v0.5 P4.1 hotfix (mode-race, enable-gate, probe accuracy, A6 wiring).
- `05d05ec` feat(state): v0.5 P5 state v4 binaural section.
- `f734796` chore(release): v0.5 P6 release validation + webgui playwright seed fix.
