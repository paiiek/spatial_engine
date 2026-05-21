# spatial_engine v0.7.0 — RT-safety follow-through + telemetry + cross-platform CI promotion

**Tag commit**: _(pending — tag applied on explicit request)_
**Predecessor**: v0.6.0 (`ece6cba`)
**Changelog**: see [CHANGELOG.md §0.7.0](../../../CHANGELOG.md#070--2026-05-21).
**Plan**: `.omc/plans/spatial-engine-v0.7.md` (iter-3; ralplan Planner→Architect→Critic consensus).

## Summary

Eight-item cycle that closes the v0.6 retro's binding recommendations:
an in-host recovery hatch for runtime demote, block-size-aware demote
hysteresis, an event-driven diagnostic telemetry channel, a synthetic
race verifier for the v0.6 #9 weak-memory-order fix, promotion of the
Linux ARM64 CI leg to a required gate, and the GPL-3 §6 legal-surface
documentation. All OSC changes are additive — existing clients that
filter on address ignore the new verbs silently.

## Highlights

### #1 (D-S1) — `/sys/binaural_reset_demote` user recovery hatch

A new inbound verb `/sys/binaural_reset_demote ,i 1` lets a connected
client re-arm B2 after a runtime sticky-demote without a host restart or
`prepareToPlay` cycle. The reset runs on the OSC IO thread and clears the
full 8-atomic demote state in a race-safe order (`runtime_demoted_`
cleared **last** so the audio thread cannot observe
`demoted_=false ∧ strikes_≥threshold` simultaneously). A **60-second
cooldown** prevents glitch-loop ping-pong; rejections inside the window
emit `reset_demote_cooldown_active` **at most once per window**
(rate-limited to prevent outbound-ring DOS, Critic §D.7). Accept emits
`reset_demote_accepted`.

**AS-5 process-lifetime cooldown**: the cooldown counter is *not* reset
by `initialize()`/`prepareToPlay` — it persists for the process lifetime,
so a rapid close/reopen cannot bypass the cooldown. New ctests:
`b2_runtime_underrun_user_reset` (incl. the AM-1 zero-hysteresis
regression gate) and `b2_runtime_underrun_user_reset_concurrent`
(audio-thread strike-bump vs IO-thread reset; `initialize()` vs reset).

### #2 (D-S2) — Block-size-aware demote hysteresis

`kRuntimeDemoteStrikes = 8` changes from "the demote threshold" to "the
demote-strikes **floor**." At runtime,
`effective_strikes = max(8, ceil(0.020s / block_seconds))`, pinning the
demote window at ~20 ms regardless of block size. At 32 samples / 48 kHz
this yields 30 strikes (longer hysteresis, no false-positive on a
cache-cold page fault); at 1024 samples it stays at the floor of 8. The
existing `block_size <= 0 || sample_rate <= 0.f` guard already protects
the new derivation from div-by-zero. Three new scenarios added to
`b2_runtime_underrun_auto_demote`.

### #3 (D-S3) — `/sys/binaural_diag` event-driven telemetry

A new outbound verb `/sys/binaural_diag ,iif <block_size>
<sample_rate_int> <observed_max_ratio>` is emitted **once per demote
event**, immediately after `ambivs_demoted_runtime` on the same IO-thread
drain pass (source-deterministic wire order). The three fields are
snapshotted at the audio-thread CAS-success demote latch (not read live
at drain), so they reflect the demote-moment context even if
`prepareToPlay` re-initialized the engine in between. The running max
ratio uses the AM-2 relaxed-load-then-store pattern (single producer; no
CAS). Encoding uses a dedicated `sendReplyImplIIF` rather than extending
the v0.6 #8 unified `sendReplyImpl` (rationale in ADR 0017 §B). New
pytest: `test_binaural_diag_emitted_on_demote`. See ADR 0017.

### #4 (D-S4) — Relacy synthetic race verifier

The v0.6 #9 `slot.ready.store` release-store upgrade now has a synthetic
verification: `test_osc_outbound_relacy` exercises the SPSC outbound ring
under the relacy C++11 memory-model checker. Relacy is vendored as a
dev-dep behind `SPATIAL_ENGINE_BUILD_RELACY_TESTS=OFF` (default off; zero
network at clone). License audit recorded in
`third_party/relacy/LICENSE` (BSD-2-Clause, verbatim) +
`third_party/relacy/UPSTREAM_PIN.txt` (URL + commit SHA + date); a
transitive-dependency grep confirms no boost/tbb/absl includes. Relacy CI
promotion is gated on a separate 5-green soak (P1, after ARM64).

### #5 (D-S5) — Linux ARM64 CI promoted to required

`cross-platform.yml` `core-linux-arm64` (`ubuntu-24.04-arm`) exits the
`continue-on-error` regime and becomes a **required** merge gate after a
5-consecutive-green soak on `main` (soak happens post-merge; branch-
protection click-path + AS-4 rollback documented in
`docs/release/v0.7.0/cross-platform-gating.md`). The `core-macos-arm64`
(`macos-14`) leg stays **signal-only** with a named owner (`paiiek`) per
AS-6 until a hardware-handoff slot is claimed.

### #6 — ADR 0016 GPL-3 §6 legal-surface documentation

ADR 0016 gains a **Legal-review trigger events** section naming three
independent triggers (Band-1 authorization request; recipient disputes
ADR authority; jurisdiction-specific legal change), each with a default
owner (`paiiek`, reassignable with an audit-log entry) and an explicit
action. Paired with a new `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` written-
acknowledgement template. **This documentation has NOT been reviewed by
legal counsel** — it is an operational starting point, not legal advice.

### #7 — P-tag cross-reference audit

New `scripts/audit_release_p_tags.sh` audits P-tag chain integrity across
weekly progress reports / release notes / ADRs (first run is audit-only,
non-gating — see V07-Q7 deferral). Output in `audit_reports/`.

### #8 — Demote-strikes saturation guard + test-hook gating

`runtime_demote_strikes_` gains a saturation cap so a long over-budget run
cannot overflow the counter (the demote latch fires well before the cap).
The test-only `clearRuntimeDemoteForTest()` hook is gated behind the test
build flag so it cannot be linked into production builds. `open-questions.md`
gains the v0.7 question block (V07-Q1..Q8) and closes V051-OQ1 / V051-OQ2.

## Breaking changes

- None. All OSC changes are additive (one new inbound verb, one new
  outbound verb, two new `/sys/binaural_warning` string codes). State
  format unchanged.

## Upgrade notes

- `/sys/binaural_reset_demote ,i 1` is the new recovery path after an
  `ambivs_demoted_runtime` demote. The 60 s cooldown is process-lifetime;
  closing and reopening the project starts a fresh cooldown.
- `/sys/binaural_diag` fires exactly once per demote event (not periodic).
  Treat **absence** of the packet as "no demote fired", not "ratio is
  healthy" — slow degradation below the threshold is not reported (see
  Known limitations).
- Unknown `/sys/binaural_warning` codes must be treated as "log + ignore"
  per the existing schema contract.

## Release validation (2026-05-21)

- `ctest` (NO_JUCE `build_off`, serial): **122/122 PASS, 0 failed**.
  v0.7 tests included and green: `b2_runtime_underrun_user_reset`,
  `b2_runtime_underrun_user_reset_concurrent`, the three new
  `b2_runtime_underrun_auto_demote` block-size scenarios,
  `osc_security_peer_validation` (reset_demote peer-reject scenario),
  `osc_outbound_multi_producer` (`,iif` overload FIFO scenario).
  - Note: under `-j` parallelism, `vst3_bind_collision` can flake
    (port-bind race); it passes in isolation and in the serial run.
- `pytest tests/`: **48 passed, 0 failed**, including the new
  `test_binaural_diag_emitted_on_demote`.
- Relacy: `test_osc_outbound_relacy` (1024 iterations) — no race
  detected (verifies the v0.6 #9 release-store fix under the C++11 MM).

The ctest count is 122 (not the plan's projected 118): the baseline grew
after the plan was authored, because the ADR 0018/0019 PCM-IPC commits
added tests in the same period. pytest is 48 (not the projected 49) for
the same estimate drift. Both suites are fully green.

## Known limitations (intentional, surfaced for honesty)

- **`bench_heartbeat_drain_latency` not implemented** — the Critic §C.1
  heartbeat-drain perf-guard ctest (a Should-have) was deferred to
  **v0.7.x**. The D-S3 drain extension is functionally tested, but there
  is no automated regression gate on its wall-clock cost yet. Tracked in
  `.omc/plans/open-questions.md`.
- **ARM64 required-gate soak is post-merge** — the `core-linux-arm64`
  promotion needs 5 consecutive green runs on `main` before branch
  protection is flipped; until the repo-admin click-path is done, the
  gate is configured but not yet enforced.
- **macOS arm64** stays signal-only (no hardware verification of the
  release-store fix on Apple Silicon in CI).
- **Slow-degradation telemetry** (ratio creeping up over hours without
  crossing the threshold) is not detected by event-driven
  `/sys/binaural_diag`; a pre-demote-window summary (B-3) is deferred to
  v0.8 pending real telemetry shape (V07-Q1).
- **Legal review** of ADR 0016 / the Band-1 template has not happened;
  the docs are operational scaffolding, not counsel-reviewed.
- **DAW host hands-on validation** remains deferred to the ADR 0016
  Band-1 workflow.

## Lineage commits

_(per-item commits applied in this cycle; footers reference
`v0.7 §item-N` + iter-2/iter-3 tags. Tag `v0.7.0` applied on explicit
request only.)_
