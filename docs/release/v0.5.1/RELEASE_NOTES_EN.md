# spatial_engine v0.5.1 — binaural release hotfix

**Tag commit**: `aeb011c` (2026-05-17)
**Predecessor**: v0.5.0
**Changelog**: see [CHANGELOG.md §0.5.1](../../../CHANGELOG.md#051--2026-05-17-binaural-release-hotfix).
**Plan**: `.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md`.

## Summary

Four-item release-blocker hotfix discovered during v0.5.0 release
validation but before DAW-handoff distribution. All four items
preserve v0.5.0 byte-equality on the speaker bus; only the binaural
bus and the outbound OSC surface are affected.

## Highlights

### Q1 — Outbound OSC notification channel

Three new outbound OSC addresses, all RT-safe (audio thread sets
atomic flags; IO drain thread sends):

- `/sys/binaural_status ,i <failures_count>` — 1 Hz heartbeat
  carrying cumulative `loadInto` failure count. Expected steady-state
  is 0; any monotonic increase signals a control-thread reload that
  violated the no-allocation contract.
- `/sys/binaural_warning ,s <code>` — event-triggered. Codes shipped
  in v0.5.1:
  - `xfade_truncated_cpu` — probe-clamped 1-block ramp armed.
  - `no_sofa_loaded` — binaural enabled but no SOFA available.
- `/sys/state ,s "fallback_mode=muted" | "fallback_mode=normal"` —
  one snapshot per `prepareToPlay()` lifetime.

### Q2 — B1 ↔ B2 mode-transition crossfade

Two-block linear ramp bridges effective-mode changes (user toggle,
probe clamp, disable→re-enable). Reuses the existing xfade helper.
New ctests: `b1_b2_mode_transition_{smooth,probe_clamped,disable_reenable}`.

### Q3 — SOFA-absent forced mute

When binaural is enabled but no SOFA is loaded, `BinauralMonitor`
returns digital silence on the binaural bus rather than letting the
upstream placeholder leak through. Observable via
`/sys/binaural_warning ,s "no_sofa_loaded"` (single fire per
session) and `/sys/state ,s "fallback_mode=muted"`. Honesty-first
policy: never let a fake "similar-sounding" signal mislead a user
into thinking SOFA is loaded.

### Q4 — Test infrastructure

- Steinberg SDK static-destructor ASan crash mitigated via `_exit(0)`
  after `soak_vst3_console_flood` assertions succeed. Documented in
  `docs/CI_QUARANTINE.md`.
- Soak-harness port-reuse flake (`EADDRINUSE` on rapid re-runs)
  tightened.

## Deferred to v0.6 (later landed in v0.6.0)

- **#4** Audio-thread `sendReply` hard-wall (latch drain moved to IO
  thread).
- **#5** Runtime sticky-underrun auto-demote (B2 wall-clock detector).
- **#8** `sendReply` 3-overload consolidation.
- **#9** Outbound ring slot ready-clear release-store hardening.

All four deferred items landed in v0.6.0; see
`docs/release/v0.6.0/RELEASE_NOTES_EN.md`.

## Breaking changes

- None to the speaker bus or to state file format.
- New OSC outbound addresses; existing inbound addresses unchanged.

## Upgrade notes

- DAW automation watching `/sys/binaural_status` should treat any
  monotonic increase as a soft alarm.
- `no_sofa_loaded` is *deliberate* silence, not a bug; load a valid
  SOFA via `/sys/binaural_sofa ,s <path>` (or VST3 parameter) to
  restore binaural audio.

## Release validation

- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **81/81 PASS**
  (v0.5.0 75 → +6 NEW: 3 mode-transition variants +
  `test_writebinaural_no_sofa_muted` + 2 outbound-OSC tests).
- `pytest tests/`: **47 passed, 0 failed** (incl. new
  `test_osc_warning_channel`).

## Lineage commits

- `aeb011c` chore(release): v0.5.1 — binaural hotfix tag (single-commit release).
