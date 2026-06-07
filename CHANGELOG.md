# Changelog

All notable changes to the Spatial Engine project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Post-v0.9.1 follow-ups, not yet released.

- (none yet)

## [0.9.1] — 2026-06-08

ADR 0019 PR6 (shm IPC soak) close-out plus a JUCE-ON build fix found
during the soak lane. No engine behaviour change in the JUCE-free
(NO_JUCE) path; the fix unblocks the JUCE-ON / VST3-adjacent build.
Plan: `.omc/plans/spatial-engine-v0.9-laneG-adr0019-pr6-soak.md`.

### Fixed
- **JUCE-ON link failure** — `OSCBackend` (`SPE_HAVE_JUCE` branch) was
  missing the `sendReply(const char*, const char*, int32_t, int32_t,
  const char*, const char*)` (`,iis`) overload that the JUCE-free path
  already provided, so any `SPE_HAVE_JUCE=1` build failed to link
  (`SpatialEngine.cpp` `/sys/warning ,iis` emit references it). Added the
  symbol-compatible deferred-wire stub. Verified at the object level:
  the JUCE-ON `OSCBackend.o` now defines the symbol (`T`), resolving the
  undefined reference (`U`) emitted by `SpatialEngine.o`. (`b14ed4a`)

### Added
- **ADR 0019 PR6 — 60 s cross-process C++↔Python shm soak.** Hermetic
  two-process real-shm drop-free proof (`read_idx→write_idx` primary +
  `xrun==0` both counters + `seq` no-gap + ramp write-integrity + RSS/fd
  leak gate), opt-in 60 s (`SHM_SOAK_FULL=1`) with a fast default smoke.
  Zero engine source edits. Proves drop-free streaming + clean producer
  lifecycle + no leak on x86-64; memory-ordering pairing (ARM64) and
  consumer read-integrity are explicitly deferred to PR7.
  (`30f5ce8`→`799a485`→`0a6da21`→`7ab732d`)

## [0.9.0] — 2026-06-02

Feature-extension cycle: five autonomous lanes (A/B/C/E/F5) mapping the
§4.4 limitation gaps from `docs/ENGINE_OVERVIEW_AND_COMPARISON.md`. Each
lane went through `ralplan` (Planner→Architect→Critic consensus) before
autopilot execution. NO_JUCE ctest 114/114, RT-asserts ×2, pytest green.
Plan: `.omc/plans/spatial-engine-v0.9-feature-extension.md`.

### Added
- **Lane A — real-time metrics dashboard.** `CpuMeter` + `/sys/metrics`
  1 Hz OSC emit (cpu_pct/cpu_peak_pct/p99_us scalar P² estimator/xrun/
  engine_overrun/binaural_demote), `osc_bridge` classification, WebGUI
  `MetricsHub` + `/ws/metrics` + `/dashboard` route with self-hosted
  canvas minicharts (zero external CDN) and a binaural reset-demote
  button. **ADR 0020.** (`81271dc`→`d24b530`)
- **Lane B — HRTF dataset diversification.** 4 SOFA datasets (KEMAR /
  CIPIC-003 / SADIE-II-H08 / HUTUBS-pp1) with a JSON catalog + loader,
  runtime hot-swap (`/sys/binaural_sofa_select ,s`, 2-slot table/tree +
  B2 virtual-source double-buffer), fetch script + license docs
  (`docs/HRTF_DATASETS.md`), dashboard selector, per-dataset ITD oracle
  regression guard. (`192b8d6`→`4539873`)
- **Lane E — scene/cue workflow.** `/scene/{save,load,list,rename,
  duplicate,delete,meta}` + `index.json`, `CueList` model +
  `cuelist.json`, `CueEngine` (UDP-funnelled cue automation + dwell
  auto-advance), MIDI Program Change → cue index, WebGUI scene/cue
  panels, e2e regression + `docs/SCENE_AND_CUE_WORKFLOW.md`.
  (`e9b78aa`→`a3fcec6`)

### Changed
- **Lane C — `MAX_OBJECTS` compile-time option (64/128).**
  `SPATIAL_ENGINE_MAX_OBJECTS` four-cap unification + RT-budget & memory
  measurement harness. Evidence-gated decision: **default stays 64** —
  128 is a validated opt-in for WFS-inactive deployments only (RT-peak
  headroom 46.9% > 35% + WFS-active footprint block the default flip).
  (`94c5d52`→`a686f86`)
- **Lane F5 — WFS/DelayLine memory remediation.** `DelayLine`
  templatized on compile-time capacity (WFS→16384, user/spk→48k) with a
  capacity clamp in `processSample` (over-cap clamps, no longer wraps);
  right-sized propagation DelayLine; **Option C lazy WFS allocation**
  (allocate-then-publish atomic handshake) drops @128 WFS-inactive RSS
  250 MB → 46.7 MB, clearing the 100 MB gate at both caps. TSan 0 races
  / 150 rounds. **ADR 0021.** (`2e07a67`→`2ed9c71`)
- **`docs/ENGINE_OVERVIEW_AND_COMPARISON.md` §4.1/§4.4/§5 refreshed** to
  reflect Lanes A/B/C/E (HRTF 4-dataset, 128 opt-in, scene/cue, IR
  convolution reverb now real via `IRConvReverb`).

### Known limitations
- **WFS-active @128 footprint ≈ 111 MB > 100 MB** — lazy alloc clears the
  gate only when WFS is inactive; activating WFS re-allocates the ~64 MB
  `delays_` term. Follow-up: per-active-object WFS allocation (ADR 0021).
- **Default `MAX_OBJECTS` flip 64→128 blocked** on RT-peak headroom, not
  memory (ADR 0021 §C-M7).
- **Scene snapshot drops `width_rad`/`reverb_send`** (reset to 0 on cue
  load, F4) — serialization fix tracked as next lane.
- **ADR 0019 PR6 (60 s cross-process soak) + PR7 (cross-platform CI)**
  still open.

## [0.8.0] — 2026-05-29

Audit-remediation + Phase B/C IPC cycle (never separately tagged; rolled
forward into the v0.9.0 release). Tracks
`.omc/plans/spatial-engine-v0.8-audit-remediation.md` and the ADR 0018/
0019 PR plans.

### Added
- **ADR 0018 — Phase B sync handlers** (Accepted): handshake / heartbeat /
  transport-timetag type-tag contract; control-tick player-heartbeat
  staleness watchdog (`f22f9ca`→`9311902`).
- **ADR 0019 — Phase C PCM IPC** (Accepted, shipping): sample-accurate
  POSIX shared-memory ring backend, PRs 2–5 — shm region + wire header,
  `SharedRingBackend`, engine `--input-backend shm:` wiring + backend
  pairing, `/sys/warning shm_*` + `/sys/state` telemetry, and the
  adm_player `IpcRingSink` Python producer + `--sink ipc://` CLI
  (`f9c0fec`→`868f750`). `O_NOFOLLOW` hardening on shm regular-file open
  (P6.1, PR3-Q7).

### Fixed
- **v0.8 audit DSP remediation.** P1 — runtime decoder-type
  double-buffered apply (M2HOA-Q14) + VBAP RT-alloc removal +
  SN3D-constant lock test; P2 — EPAD rank-aware energy scale (DSP-4) +
  VBAP fallback Σg²≈1 guard (DSP-5); **P2.3 — FdnReverb effective-delay
  fix (DSP-6, `readPos = writePos`)** + T60 oracle reinstated.
  (`64352df`, `98741a4`, `d7f3e6c`)
- **P0 flaky-test stabilization** — OSC/binaural sleep-barriers replaced
  with event/condvar sync (`32bfd5a`).

### Added (test/docs)
- P3 (partial) — ambisonic absolute-gain golden + HRTF interp oracle +
  OSC malformed-extras coverage; ADR status/index reconcile; CHANGELOG +
  hygiene record.

### Deferred
- **P3.1** (VST3 state-contract test → NO_JUCE CI), **P3.5**
  (`vst3_bind_collision` race) — supervised VST3 sprint (stale
  `build_vst3`); **P7.1** (`SpatialEngine` god-object refactor);
  **ADR 0019 PR6/PR7** (cross-process soak + cross-platform CI).

## [0.7.0] — 2026-05-21

RT-safety follow-through + telemetry + cross-platform CI promotion.
Eight-item cycle closing the v0.6 retro's binding recommendations. All
OSC changes are additive; no state-format change. Tracks
`.omc/plans/spatial-engine-v0.7.md` (iter-3 ralplan consensus). Full
notes: `docs/release/v0.7.0/RELEASE_NOTES_EN.md`.

### Added
- **`/sys/binaural_reset_demote ,i 1` user recovery hatch (#1, D-S1).**
  In-host re-arm of B2 after a runtime sticky-demote, on the OSC IO
  thread. Clears the full 8-atomic demote state in a race-safe order
  (`runtime_demoted_` last). 60 s cooldown prevents glitch-loop
  ping-pong; rejections emit `reset_demote_cooldown_active` at most once
  per window (DOS rate-limit, Critic §D.7); accept emits
  `reset_demote_accepted`. Cooldown is process-lifetime (AS-5) — not
  reset by `prepareToPlay`. New ctests `b2_runtime_underrun_user_reset`
  (+ AM-1 zero-hysteresis regression gate) and
  `b2_runtime_underrun_user_reset_concurrent`.
- **`/sys/binaural_diag ,iif` event-driven telemetry (#3, D-S3).**
  `<block_size> <sample_rate_int> <observed_max_ratio>` emitted once per
  demote event, immediately after `ambivs_demoted_runtime` on the same
  IO drain pass. Fields snapshotted at the audio-thread demote latch
  (demote-moment context, not live-at-drain). New `sendReplyImplIIF`
  encoder (ADR 0017 §B). New pytest `test_binaural_diag_emitted_on_demote`.
- **Relacy synthetic race verifier (#4, D-S4).**
  `test_osc_outbound_relacy` exercises the SPSC outbound ring under the
  relacy C++11 memory-model checker, verifying the v0.6 #9 release-store
  fix. Vendored dev-dep behind `SPATIAL_ENGINE_BUILD_RELACY_TESTS=OFF`
  (default off). License audit in `third_party/relacy/`.
- **`scripts/audit_release_p_tags.sh` (#7).** P-tag chain integrity
  audit across weekly reports / release notes / ADRs (audit-only,
  non-gating for v0.7 — V07-Q7).
- **ADR 0017** documenting the `/sys/binaural_diag` telemetry channel
  (event-driven, dedicated encoder, snapshot-at-latch).

### Changed
- **Block-size-aware demote hysteresis (#2, D-S2).**
  `kRuntimeDemoteStrikes = 8` is now the demote-strikes *floor*;
  `effective_strikes = max(8, ceil(0.020s / block_seconds))` pins the
  demote window at ~20 ms regardless of block size (30 strikes at 32
  samples / 48 kHz; floor of 8 at ≥128). Three new
  `b2_runtime_underrun_auto_demote` scenarios.
- **Linux ARM64 CI promoted to required (#5, D-S5).**
  `cross-platform.yml` `core-linux-arm64` (`ubuntu-24.04-arm`) exits
  `continue-on-error` and becomes a required merge gate after a 5-green
  post-merge soak. `core-macos-arm64` stays signal-only with named owner
  (AS-6). Gating + AS-4 rollback in
  `docs/release/v0.7.0/cross-platform-gating.md`.
- **Demote-strikes saturation guard (#8).** `runtime_demote_strikes_`
  caps so a long over-budget run cannot overflow; the test-only
  `clearRuntimeDemoteForTest()` hook is gated behind the test build flag.
- **ADR 0016 legal-review trigger events (#6).** Three named triggers
  (Band-1 request; ADR-authority dispute; jurisdiction change), each with
  a default owner (`paiiek`) and explicit action; paired with
  `docs/legal/BAND_1_HANDOFF_TEMPLATE.md`. Not legal-counsel reviewed.

### Release validation (2026-05-21)
- `ctest` (NO_JUCE `build_off`, serial): **122/122 PASS, 0 failed**
  (count exceeds the plan's projected 118 because the ADR 0018/0019
  PCM-IPC commits grew the baseline after the plan was authored). Under
  `-j` parallelism `vst3_bind_collision` can flake on a port-bind race;
  passes in isolation and serially.
- `pytest tests/`: **48 passed, 0 failed** (incl. the new diag test).
- Relacy `test_osc_outbound_relacy` (1024 iterations): no race detected.

### Known limitations (intentional)
- **`bench_heartbeat_drain_latency` ctest (Critic §C.1) not implemented
  — deferred to v0.7.x.** The D-S3 drain extension is functionally
  tested but has no automated wall-clock-cost regression gate yet.
- ARM64 required-gate soak is post-merge; branch protection not yet
  flipped. macOS arm64 stays signal-only (no Apple Silicon HW verify).
- Slow-degradation telemetry not detected by event-driven
  `/sys/binaural_diag` (B-3 pre-demote-window summary deferred to v0.8,
  V07-Q1). ADR 0016 / Band-1 template not legal-counsel reviewed.

### Closed open questions
- **V051-OQ1** — `/sys/binaural_status` stays 1 Hz; diagnostic telemetry
  is event-driven (not 1 Hz polling).
- **V051-OQ2** — runtime sticky auto-demote shipped as committed v0.6 #5;
  v0.7 D-S2 makes the strike count block-size-aware.

### Plan
- `.omc/plans/spatial-engine-v0.7.md` (iter-3). Full ralplan
  Planner→Architect→Critic consensus (contrast with the v0.6 post-hoc
  cadence). Architect/Critic review artifacts under `.omc/plans/`.

---

## [0.6.0] — 2026-05-18

RT-safety hardening bundle on already-shipped surfaces. No user-visible
behavior changes other than the new `ambivs_demoted_runtime` notification
described below. Tracks `.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md`
§Q5 deferred items (#4, #5, #8, #9).

### Added
- **Runtime sticky-underrun auto-demote (#5).** `BinauralMonitor` now
  measures wall-clock B2 block timing via `std::chrono::steady_clock` (vDSO
  on modern Linux, RT-safe). When B2 exceeds 90% of the block deadline for
  8 consecutive blocks (≈21 ms at 48 kHz / 128), the effective mode is
  sticky-demoted to B1 (Direct) and a one-shot warning latch is armed.
  The heartbeat IO thread drains the latch and emits
  `/sys/binaural_warning ,s "ambivs_demoted_runtime"` exactly once per
  demote event. Sticky decision persists until the next `prepareToPlay()`
  so transient spikes cannot flap the mode. New ctest:
  `b2_runtime_underrun_auto_demote` (deterministic injection via
  test-only `injectRuntimeUnderrunStrikesForTest()` hook).
- **`SpatialEngine::binauralIsRuntimeDemoted()` /
  `binauralDrainRuntimeDemotePending()`.** Engine-level forwarders so VST3
  doesn't reach into `BinauralMonitor` directly.

### Changed
- **Audio thread `sendReply` hard-wall (#4).** `SpatialEngineProcessor::
  process()` no longer drains the `no_sofa_loaded` or `/sys/state
  fallback_mode` latches via `engine_->oscBackend().sendReply(...)`.
  Both drains moved into `heartbeatLoop()` 1 Hz IO thread. Rationale:
  `sendReply` internally calls `std::condition_variable::notify_one()`
  which is not strictly RT-safe under all libc implementations. The
  heartbeat now uses a drain-first-then-wait pattern so the very first
  tick after `setActive(true)` emits any pending latches immediately
  (well within the 200 ms target). Retry-on-no-peer semantics: if the
  host hasn't completed peer handshake yet, `sendReply` returns false
  and the latch stays armed for the next 1 Hz tick.
- **`OSCBackend::sendReply` consolidation (#8).** Three near-identical
  ~70 LOC overload bodies collapsed into a single private
  `sendReplyImpl(addr, types, s, have_f, f, have_i, i)`. Public overloads
  are 3-line forwarders. Future outbound channels touch one place.
- **Outbound ring `slot.ready.store` upgrade (#9).** Promoted from
  `memory_order_relaxed` to `memory_order_release` to close a weak-memory-
  order corner case (ARM, Apple Silicon, ppc) where a wrap-producer's
  release-true could be reordered behind a consumer's stale relaxed-false
  and silently dropped. x86_64 unaffected; ARM-side regression gate is
  deferred to a future CI matrix expansion (P2 in
  `docs/weekly_progress_report_2026-05-18.md` §5).

### Release validation (P0 re-confirm at 2026-05-18 session resume)
- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **86/86 PASS**
  (v0.5.1 81 → +5 NEW: `b2_runtime_underrun_auto_demote`,
  `b2_runtime_underrun_engine_integration` (follow-up commit `bd56a74`),
  `b1_b2_mode_transition_smooth`,
  `b1_b2_mode_transition_probe_clamped`,
  `b1_b2_mode_transition_disable_reenable`).
- `python3 -m pytest tests/`: **47 passed, 0 failed**.
- Total ctest time: 3.55 s (no regression vs v0.5.1).

### Coverage gaps (intentional, surfaced for honesty)
- **ARM / Apple Silicon CI matrix** deferred to P2-1 (`docs/weekly_progress_report_2026-05-18.md` §5.3). Structural workflow filed as
  `.github/workflows/cross-platform.yml` (signal-only `continue-on-error`
  during v0.6 cycle; promotion to required gate is v0.6.x admin task).
- **macOS arm64 manual verify** is PENDING — see
  `docs/release/v0.6.0/macos-arm64-verify.md` checklist (sections A–E
  user-queued; v0.5 SSE-guard + v0.6 #9 release-store have zero hardware
  verification until the checklist is run).
- **DAW host hands-on validation** (Reaper / Bitwig / Logic / Cubase)
  deferred to the ADR 0016 §Band-1 workflow (≤5 named beta testers,
  written acknowledgement, audit-log discipline). Hands-on log template
  at `docs/release/v0.3.0/daw-handson-log.md`.

### Notes for users
- The new `ambivs_demoted_runtime` warning is a one-shot signal that
  surfaces a real performance ceiling for the current host machine; once
  it fires you should treat it as authoritative ("your CPU + plug-in
  chain cannot sustain B2 right now") rather than a transient alert.
  The decision will reset on the next `prepareToPlay()` (e.g., sample-
  rate change, project reload).
- No state-format change: state v4 schema and v3 byte-equal merge gate
  preserved.
- Public OSC schema unchanged. `/sys/binaural_warning ,s` adds one new
  string code; existing codes (`xfade_truncated_cpu`, `no_sofa_loaded`)
  unchanged.

### Plan
- `.omc/plans/spatial-engine-v0.6-stability.md` (post-hoc; documents
  trail from v0.5.1 §Q5 deferred items). The post-hoc cadence is itself
  a process gap and is itemised as a P1 in
  `docs/weekly_progress_report_2026-05-18.md` §5.

---

## [0.5.1] — 2026-05-17 (binaural release hotfix)

Four-item release-blocker hotfix on top of v0.5.0. All Q-items below
were discovered during release validation after v0.5.0 tagging but
before DAW-handoff distribution. Tracks
`.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md`.

### Added
- **Outbound OSC notification channel (Q1).** Three new outbound
  addresses, all RT-safe (audio thread sets atomic flags; IO drain
  thread sends):
  - `/sys/binaural_status ,i <failures_count>` — 1 Hz heartbeat carrying
    the cumulative `OlaConvolver::loadInto` failure count (control-
    thread reload contract violations; expected 0).
  - `/sys/binaural_warning ,s <code>` — event-triggered. Codes shipped
    in v0.5.1: `xfade_truncated_cpu` (probe-clamped 1-block ramp armed),
    `no_sofa_loaded` (binaural enabled but no SOFA available).
  - `/sys/state ,s "fallback_mode=muted"|"fallback_mode=normal"` — one
    snapshot per `prepareToPlay()` lifetime.
- **B1 ↔ B2 mode-transition crossfade (Q2).** Two-block linear ramp
  bridges effective-mode changes (e.g. user toggles requested_mode while
  audio is live). The ramp reuses the existing xfade helper and is
  edge-triggered: probe clamps, disable→re-enable cycles, and explicit
  user-driven switches all participate. New ctest:
  `b1_b2_mode_transition_smooth`.
- **SOFA-absent forced mute (Q3).** When binaural is enabled but no SOFA
  is loaded, `BinauralMonitor` returns digital silence on the binaural
  bus rather than letting the upstream placeholder leak through. The
  forced mute is observable via `/sys/binaural_warning ,s "no_sofa_loaded"`
  (single fire per session) and `/sys/state ,s "fallback_mode=muted"`.
  New ctest: `test_writebinaural_no_sofa_muted`.
- **`test_osc_warning_channel.py`** — pytest soak harness assertion
  that the warning channel survives end-to-end through the OSC IPC
  layer.

### Fixed
- **Steinberg SDK static-destructor ASan crash (Q4).** `soak_vst3_console_flood`
  ASan run was failing on `munmap_chunk(): invalid pointer` during
  process teardown (Steinberg static-destruction order racing ASan-tracked
  socket close). Mitigated via `_exit(0)` after the soak assertions
  succeed, bypassing dtor ordering. The defense is documented in
  `docs/CI_QUARANTINE.md`.
- **Soak harness port-reuse flake (Q4).** Multiple soak runs in quick
  succession could trip `EADDRINUSE`; the port-reuse pattern was
  tightened.
- **P4.1 binaural mode-race + probe accuracy + A6 wiring carry-over.**
  Already shipped in v0.5.0 line (commit `7cdcee8`); listed here for the
  v0.5 → v0.5.1 audit trail.

### Deferred to v0.6
- **#4** Audio-thread `sendReply` hard-wall (latch drain moved to IO thread).
- **#5** Runtime sticky-underrun auto-demote (B2 wall-clock detector).
- **#8** `sendReply` 3-overload consolidation.
- **#9** Outbound ring slot ready-clear release-store hardening.

All four deferred items landed in v0.6.0 (see above).

### Release validation
- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **81/81 PASS**
  (v0.5.0 75 → +6 NEW: 3 mode-transition variants +
  `test_writebinaural_no_sofa_muted` + 2 outbound-OSC tests).
- `python3 -m pytest tests/`: **47 passed** (incl. new
  `test_osc_warning_channel`).

### Notes for users
- DAW automation that reads `/sys/binaural_status` should treat any
  monotonic increase in the `,i` payload as a soft alarm — the
  underlying `loadInto` contract violation indicates a control-thread
  reload that bypassed the no-alloc guarantee. Expected steady-state
  value is 0.
- `no_sofa_loaded` is a state of *deliberate* silence, not a bug; load
  a valid SOFA via `/sys/binaural_sofa ,s <path>` (or VST3 parameter) to
  restore binaural audio.

---

## [0.5.0] — 2026-05-15

### Added
- **Commercial-grade binaural decoder on VST3 bus 1.** Replaces the v0.4
  -6 dB downmix placeholder with per-object HRTF summation (B1 path):
  for each active source (up to `MAX_OBJECTS = 64`), the engine convolves
  its dry signal with the nearest HRTF pair and sums into the stereo
  binaural bus. A `-1 dBFS` channel limiter caps the bus output. This is
  the same family of binaural rendering shipped by Dolby Atmos Renderer,
  Nuendo, and Apple Spatial Audio.
- **RT-safe direction updates.** `BinauralMonitor` now owns two
  pre-allocated `OlaConvolver` slots per (object, ear) and swaps via an
  atomic `front_idx_` per object. Control-thread `setDirection()` calls
  `OlaConvolver::loadInto(ir, ir_len)` — a no-allocation reload contract
  that returns silently on capacity violation (no audio-thread allocs,
  ever; tracked via `load_into_failures_` counter). A 2-block linear
  crossfade (`core/src/dsp/GainRamp.h`) bridges old → new IR with
  preempt-with-current-gain handoff for rapid direction changes.
- **O(log N) HRTF lookup.** Replaces the O(N) brute-force `sin`/`cos`
  search at `core/src/hrtf/HrtfLookup.cpp` with a 3D KD-tree on unit-
  Cartesian SOFA positions (`core/src/hrtf/KdTree3D.{h,cpp}`). Built
  once at `.speh` load (control thread, allocations OK); queries are
  iterative (no recursion) and allocation-free.
- **Per-object binaural test fixture.** `core/tests/fixtures/synthetic_min.speh`
  (committed) drives the v0.5 unit tests; the existing SOFA fixture is
  preserved for the SOFA-format-level tests.
- **B2 AmbiVS optional render path.** Ambisonic decode to a 24-point
  t-design virtual-speaker layout, each VS feeding a fixed HRIR pair. A
  CPU-throughput probe runs at `setActive(true)` (`runThroughputProbe()`
  in `core/src/output_backend/BinauralMonitor.cpp`); on insufficient
  headroom (< 1.5× RT) the effective mode clamps back to B1 while
  `requestedMode()` preserves the user intent for later retry. Mode
  switching is exposed via OSC `/sys/binaural_mode ,i {0=B1,1=B2}`
  (`core/src/ipc/Command.h:50`) and persisted in v4 state.
- **State v4 binaural section is fully populated.** Section `0x0004` now
  carries `[enable u8, effective_mode u8, requested_mode u8, reserved
  u8]`. The reader dispatches off `requested_mode` (`payload[2]`) so a
  user-asked B2 mode is restored across reload **even if a prior session
  had been clamped to B1 by the throughput probe**. `effective_mode`
  byte is telemetry-only and re-derived on the next probe. v0.4 sessions
  written with the previous 2-byte payload still load (enable is
  applied; mode is left at the in-memory default — guarded by
  `sec_len >= 3`). The v3 byte-equal merge gate
  (`test_state_v3_loads_byte_identical_under_v4_writer`) is preserved.

### Deferred to v0.6
- Head-tracker hook (`/sys/headtrack ,fff yaw pitch roll`).
- JUCE partitioned-convolution binaural path (v0.5 ships OlaConvolver
  only).

### Fixed
- **Playwright webgui test seed loops** (`ui/webgui/tests/playwright/`):
  `seed_64_objs` / `seed_single_obj` / `test_fps_desktop` iterated
  `for (i=1; i<=64; i++)`, but `canvas.js` declares `objects` as
  `Array.from({length: MAX_OBJECTS})` (0-indexed, 0..63). Accesses to
  `objects[64]` returned `undefined` and the JS injection threw
  `TypeError: Cannot set properties of undefined`. Iteration now uses
  `i=0..63`; production code unchanged.

### Notes for users
- Bus 0 speaker audio is bit-identical to v0.4 for all canonical
  fixtures.
- Memory footprint grows by ~1 MiB (64 × 2 × 2 × 1024 floats of slot
  buffers). MAX_IR_LEN = 1024 matches the SOFA loader's existing cap.
- If `.speh` is not loaded or binaural is disabled, bus 1 keeps the
  v0.4 -6 dB downmix placeholder for diagnostic intelligibility.

### Release validation (P6)
- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **75/75 PASS**.
  Includes all new v0.5 tests (`ola_convolver_loadinto_*`,
  `kdtree3d_matches_brute_force`, `binaural_b1_64_objects_rt_safe`,
  `b2_throughput_probe_fallback`, `vst3_state_v4_binaural_section`).
- `python3 -m pytest`: **221 passed, 3 skipped, 0 failed** (full suite
  including `ui/webgui/tests/playwright/`).
- ASan + UBSan (NO_JUCE, `-fsanitize=address,undefined -O1 -g`): the
  v0.5 binaural surface (`vst3_state_v4_binaural_section`,
  `binaural_b1_64_objects_rt_safe`, `b2_throughput_probe_fallback`,
  `ola_convolver_loadinto_*`, `vst3_state_v4_persist`,
  `vst3_state_v3_back_compat`, `vst3_dispatch_rt_safety`) is **clean**.
  Two pre-existing ASan failures (`vst3_intra_plugin_spsc_drain`,
  `vst3_console_flood`) emit `munmap_chunk(): invalid pointer` during
  subprocess shutdown — Steinberg SDK static-destruction order with
  ASan-tracked UDP socket teardown. Both tests pass under the regular
  build. The failures pre-date v0.5 (introduced by C4-S3 commit
  `12be2f0` and C4-S2.6 commit `94a6e9a`) and are scheduled for v0.6
  ASan-cleanup work; they do not touch the binaural code path.

## [0.4.0] — 2026-05-15

### Added
- VST3 plugin now exposes two output buses: bus 0 "Speakers" (variable
  channel count negotiated via setBusArrangements; 2/4/6/8/12/16/24
  supported) and bus 1 "Binaural" (fixed stereo). Bus 1 currently emits
  a -6 dB speaker downmix placeholder; real binaural rendering arrives
  in v0.5.
- Plugin state schema migrated to v4 (sectioned TLV). v3 sessions load
  unchanged (back-compat merge-gate test).
- Layout YAML path and SOFA (.speh) path are persisted in plugin state
  and runtime-injectable via OSC (`/sys/load_layout ,s <path>`,
  `/sys/binaural_sofa ,s <path>`, `/sys/binaural_enable ,i {0,1}`).

## [0.3.1] — 2026-05-15 (channel mapping correctness pre-release)

### Fixed

- **Per-channel OSC endpoints now address speakers by YAML channel number,
  not by position in the YAML file.** Prior to v0.3.1, handlers for
  `/output/N/gain`, `/output/N/limit`, `/noise/N/type`, and `/noise/N/gain`
  treated the wire channel `N` as a 0-based index into the speaker vector,
  ignoring the `channel:` field declared in each layout entry. Reordered
  or sparse channel maps silently routed automation to the wrong speaker.
  v0.3.1 adds `SpeakerLayout::channelToIndex()` (backed by a fixed-size
  YAML-channel → vector-index lookup built by `LayoutLoader`) and rewrites
  the four drain handlers in `SpatialEngine` to use it.
- **Loud failure for unmapped channels.** Commands targeting a YAML
  channel not declared in the active layout are now dropped silently
  (RT-safe; no allocation). Duplicate `channel:` declarations and
  channels above `SpeakerLayout::kMaxYamlChannel` (64) are rejected at
  load time by `LayoutLoader` with explicit error strings.

### Breaking semantic note

External OSC automation that historically targeted *position* (e.g.
`/output/0/gain` to mean "first speaker in the YAML file") will now
target *YAML channel* (i.e. the speaker whose `channel: 0` is declared
— which is invalid under the 1-based contract, so the command is
dropped). All four canonical fixtures (`lab_4ch.yaml`, `lab_8ch.yaml`,
`lab_8ch_aligned.yaml`, `lab_8ch_irregular.yaml`) declare sequential
1-based channel numbers, so default workflows see zero behavior change.
Users running reordered or sparse channel maps may need to update their
automation scripts to address speakers by their declared YAML channel
number rather than their position in the file.

## [0.2.0] — 2026-05-10 (DAW hands-on pending — see release notes)

### Added

- **ADM-OSC v1.0 receive coverage** (`feat(C3-adm-osc)` 166f0c9)
  - Full `/adm/obj/N/{azim,elev,dist,aed,gain,mute,xyz,active,width,name}` decode
  - 4 new `CommandTag`s `0x06..0x09` (`ObjXYZ`, `ObjActiveAdm`, `ObjWidth`, `ObjName`)
  - 3 synthetic vendor compatibility fixtures (L-ISA, Spat Revolution, d&b Soundscape)
  - Soak harness `core/tests/perf/soak_adm_osc_flood.cpp` — 64 obj × 1 kHz × 60 s
  - ADR 0006 — ADM-OSC v1.0 spec freeze + `MAX_DIST=20.0f` constant
  - 88 compliance fixture rows in `core/tests/core_unit/adm_osc_v1_compliance.csv`
  - Bridge layer `bridge/_adm_osc_common.py` extracted for shared helpers
  - `--osc-dialect adm` CLI flag (default `legacy` preserves v0.1.0 behaviour)
- **HOA decoder diversification — 4 algorithms** (`feat(M2-hoa-extended)` e5924da)
  - MaxRE decoder (Legendre-root, M2HOA-Q7 resolved: g_1(N=1)=0.5774)
  - AllRAD decoder (t-design quadrature projection, O(n×K))
  - EPAD decoder (two-sided Jacobi SVD, cond>1e10 fallback)
  - InPhase decoder (Daniel 2000 §3.30, golden vectors verified)
  - 5-value `DecoderType` enum, RT-safe runtime dispatch
  - OSC `/sys/ambi_decoder_type i {0..4}` with `AmbisonicRenderer` plumbing
  - 4 new ctest fixtures (51/51 PASS)
- **VST3 plugin: production-grade 7-parameter integration** (`feat(C2B.*)` cb4737d, 744c0e6, 20a4da6, d1d42018, acb8c27)
  - JUCE-free `vst3sdk` hand-roll (Phase C C2 Option B)
  - 7 parameters: `kPanAz`, `kPanEl`, `kSourceWidth`, `kMasterGain`, `kAmbiOrder`, `kRoomPreset`, `kBypass`
  - Plugin entry + `IPluginFactory` vtable + 21-assertion host fixture
  - `IEditController` dispatch wiring with 1000-iter RT-safety probe
  - State v2 binary format (36-byte: 8-byte header + 7×float) with v1 multi-version reader at `vst3/SpatialEngineProcessor.cpp:267-289`
  - Bypass dry pass-through (channel-wise input→output memcpy with null-buffer guards)
  - `kIsBypass` flag on parameter id=6 per VST3 spec
  - `restartComponent(kParamValuesChanged)` on `setComponentState`
  - 53 ctest tests including `vst3_*` × 7 (all PASS)
- **OFF byte-baseline gate** (`ci(off-baseline)` 587815c)
  - Dual-gate: `core/libspe_core.a` + `core/spatial_engine_core` byte+symbol pinning
  - GHA `ubuntu-24.04` runner reproducible build verified
  - `LD_DEBUG=libs` runtime sysdep audit gate
  - Public re-pin path via `GITHUB_STEP_SUMMARY` echo (no token required)
- **LTC sync (Phase C1)** (`feat(C1.*)` c3edb6a, 6145a53, 59df7b7, 6716cf9, 6cf851c)
  - SMPTE LTC biphase decoder (25fps synthesis verified)
  - `LtcChase` audio→ring→control-thread consumer
  - `SpscRing<T,N>` reusable template + `QueuedCmd` POD
  - `NullBackend` audio-input path with NDEBUG strip lint hook
  - `/sys/ltc_chase` opcode `0x14` + `SpatialEngine` integration
- **Phase B feature parity**
  - M1: Per-object Source WIDTH (0..π rad) across VBAP / DBAP / WFS / HOA fan-out (50864bd)
  - M2: HOA `AmbiDecoder` + `AmbisonicRenderer` 1st-order + algorithm dispatch (e558149)
  - M3: `IRConvReverb` (OLA) + `/reverb/select` OSC + runtime FDN/IR switch (3d10dc3)
  - M4: Snapshot Crossfade — time-based scene transition interpolation (57254d5)
  - M5: `SPATIAL_ENGINE_VST3` build option (default OFF) (677dcbc)
  - M6: Per-speaker time-alignment (delay_ms / gain_db) at output stage (134062e)
  - M8: Object Trajectory Animation — circle / line / lissajous + WebGUI API (7662b11)
  - M9: Per-channel `ChannelLimiter` + `/output/{ch}/{gain,limit}` OSC (680c47b)
  - B3: `IRConvReverb` WAV loading + `scripts/fetch_ir.py` (b5b4ec1)
  - B4: DBAP width precision — 3-virtual-source power-sum + energy-preserving normalization (fc95a59)
  - B5: HOA 2nd/3rd-order decoder — Tikhonov pseudo-inverse + `/sys/ambi_order` (f73caff)
- **vid2spatial integration**
  - Phase 2 production bridge + dual-mode switch (4240b10)
  - `bridge/spike_vid2spatial_osc.py` IIR + 60 Hz rate-limit
  - WebGUI vid2spatial integration: bridge aed fix + start/stop API + UI buttons (3c8f3f8)
- **Korean documentation**
  - `docs/manual_kr/install/README.md` — 12-chapter installation manual (40fcc9b)
  - `docs/manual_kr/operation/README.md` — 15-chapter operation manual (40fcc9b)
- **Phase C4 design contract drafts (v0.2.0 ships drafts; v0.3.0 implements)**
  - ADR 0010 — VST3 plugin OSC binding model (per-instance recv-only UDP, A1-ε)
  - ADR 0011 — VST3 plugin multi-instance discovery (file-based JSON registry)
  - ADR 0012 — ADM-OSC vendor quirks overlay (reserved slot)
- **Test infrastructure**
  - VBAP gain cache (0.5° bins, open-addressing FIFO, prepareToPlay invalidation) (fc00a30)
  - AmbisonicEncoder 2nd/3rd-order (ACN/SN3D, 9ch / 16ch closed-form) (a0853e4)
  - VBAP 3D fallback gain pattern + numerical tests (89db643, f69e34a)

### Changed

- **GHA OFF baseline re-pinned** to `ubuntu-24.04` runner image (587815c, 8c0ca2d, ec2510d)
- **Limiter implementation**: peak-attack envelope + gain-ramp warmup, asserts enforced (5a60720)
- **WebGUI**: FastAPI lifespan migration + asyncio.run transition (c0aef3a)

### Deprecated

- (none in v0.2.0)

### Removed

- (none in v0.2.0)

### Fixed

- VST3 OFF baseline: `vst3.yml` Option B alignment + `p2_layout` configs bidirectional build (15fdb52)
- AmbisonicEncoder ACN4 coefficient: `kSqrt3 → kSqrt3_2` (was 2x error); regression test added (08e5e91)
- `OlaConvolver` alloc-free `process()` + `SofaBinReader` defensive validation (2c8086c)
- 5 critical WebGUI + bridge bugs before user handoff (d1b82f3)
- NaN Z assert + `blockSignals` on `set_object` + `sys` import hoisted (a2b10f9)
- `SceneController` handler + `fromJson` safety + path traversal guard + ctest 29/29 (d8056db)
- MIDI OSC send + VBAP 2D limitation documentation (0bee66c)
- pytest collection: importlib mode + `norecursedirs` (6af3778); pythonpath/testpaths extended for `ui/`, `ui/webgui/` (3b934e5)
- `sofa_inspector` IR_len 384 + flaky latency test + `ADDR_METRICS` + `utcnow` deprecation (97056c6)

### Security

- Path traversal guard added to `SceneController` (`d8056db`); `fromJson` rejects malicious filename payloads
- NaN Z assert + defensive validation in `SceneSnapshot` / Z-coordinate paths (a2b10f9)

### Compatibility

- **Built on**: Ubuntu 24.04 (GLIBC 2.39, GCC 13.3.0)
- **Older distros** (Ubuntu 22.04 / Debian 11) require building from source — see `docs/manual_kr/install/README.md` Chapter 3
- **Wire ABI** preserved from v0.1.0: `--osc-port` / `--osc-dialect` defaults unchanged; Component / Controller IIDs unchanged
- **VST3 state format**: v0.1.0 shipped state v1 (28 bytes, 6 floats); v2 added in C2B postmortem (acb8c27) with multi-version reader at `Processor.cpp:267-289`. v0.1.0 `.vstpreset` files load cleanly via the v1 reader path. No further state bump in v0.2.0.

### Known limitations

- VST3 plugin ADM-OSC routing deferred to Phase C4 / v0.3.0 (per Plan §1.4 deliverable matrix). v0.2.0 ships only the design contracts (ADRs 0010 / 0011 / 0012).
- macOS / Windows builds not yet covered by CI — Linux-only B3-β release artifact.
- DAW hands-on validation (R3) is gated to Reaper 7.x + Bitwig Studio 5.x on Linux only.

[0.2.0]: https://github.com/paiiek/spatial_engine/releases/tag/v0.2.0

---

## [0.1.0] — 2026-05-01

Initial public release. v0.1.0 commit `24c62c7a` — `P12 docs, perceptual pre-registration, stage-1 latency harness`.

Highlights:
- Real-time object-based immersive audio rendering engine (C++ JUCE-free core)
- VBAP 2D + 3D, DBAP, WFS, HOA 1st-order, FDN reverb, Binaural HRTF
- ADM-OSC receive subset (azim/elev/dist/gain/mute/aed)
- WebGUI (FastAPI + JS) for trajectory editing
- vid2spatial bridge for video→audio spatialization

[0.1.0]: https://github.com/paiiek/spatial_engine/releases/tag/v0.1.0
