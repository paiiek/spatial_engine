# spatial_engine v0.6.0 — RT-safety hardening bundle

**Tag commit**: `ece6cba` (2026-05-18)
**Predecessor**: v0.5.1
**Changelog**: see [CHANGELOG.md §0.6.0](../../../CHANGELOG.md#060--2026-05-18).
**Plan**: `.omc/plans/spatial-engine-v0.6-stability.md` (post-hoc; see
process note below).

## Summary

Four-item RT-safety hardening bundle on already-shipped surfaces. No
user-visible behavior change other than the new
`/sys/binaural_warning ,s "ambivs_demoted_runtime"` notification.
Tracks v0.5.1 plan §Q5 deferred items (#4, #5, #8, #9).

## Highlights

### #4 — Audio thread `sendReply` hard-wall

`SpatialEngineProcessor::process()` no longer drains the
`no_sofa_loaded` or `/sys/state fallback_mode` latches via
`engine_->oscBackend().sendReply(...)`. Both drains moved into
`heartbeatLoop()` 1 Hz IO thread.

Rationale: `sendReply` internally calls
`std::condition_variable::notify_one()` which is not strictly RT-safe
under all libc implementations. The heartbeat now uses a
**drain-first-then-wait** pattern so the very first tick after
`setActive(true)` emits any pending latches immediately (well within
the 200 ms latency budget). Retry-on-no-peer semantics: if the host
hasn't completed peer handshake yet, `sendReply` returns false and
the latch stays armed for the next 1 Hz tick.

### #5 — Runtime sticky-underrun auto-demote

`BinauralMonitor` now measures wall-clock B2 block timing via
`std::chrono::steady_clock::now()` (vDSO on modern Linux, ~30 ns, no
syscall, no allocation — RT-safe). When B2 exceeds 90% of the block
deadline for **8 consecutive blocks** (≈21 ms at 48 kHz / 128), the
effective mode is **sticky-demoted to B1 (Direct)** and a one-shot
warning latch is armed. The heartbeat IO thread drains the latch and
emits `/sys/binaural_warning ,s "ambivs_demoted_runtime"` exactly
once per demote event.

Sticky decision persists until the next `prepareToPlay()` so
transient spikes cannot flap the mode. New ctest:
`b2_runtime_underrun_auto_demote` (deterministic injection via
test-only `injectRuntimeUnderrunStrikesForTest()` hook).

**Tuning rationale**:
- 8 strikes ≈ 21 ms at 48 kHz / 128 — long enough to ignore single-
  block hiccups (page fault, thread preemption), short enough to
  recover before the user hears multiple dropouts.
- 90% deadline fraction leaves 10% headroom for the rest of the audio
  block (objects, decoder, limiter).

### #8 — `sendReply` 3-overload consolidation

Three near-identical ~70 LOC `OSCBackend::sendReply` overload bodies
collapsed into a single private
`sendReplyImpl(addr, types, s, have_f, f, have_i, i)`. Public
overloads are 3-line forwarders. Future outbound channels touch one
place; drift risk removed.

### #9 — Outbound ring `slot.ready.store` upgrade

Promoted from `memory_order_relaxed` to `memory_order_release` to
close a weak-memory-order corner case (ARM, Apple Silicon, ppc) where
a wrap-producer's release-true could be reordered behind a consumer's
stale relaxed-false and silently dropped. x86_64 unaffected; ARM-side
regression gate is deferred to a future CI matrix expansion (P2 in
`docs/weekly_progress_report_2026-05-18.md` §5).

## Breaking changes

- None.

## Upgrade notes

- The new `ambivs_demoted_runtime` warning is a one-shot signal that
  surfaces a real performance ceiling for the current host machine;
  once it fires, treat it as authoritative ("your CPU + plug-in chain
  cannot sustain B2 right now") rather than a transient alert. The
  decision will reset on the next `prepareToPlay()` (e.g., sample-
  rate change, project reload).
- No state-format change: state v4 schema and v3 byte-equal merge
  gate preserved.
- Public OSC schema unchanged. `/sys/binaural_warning ,s` adds one
  new string code (`ambivs_demoted_runtime`); existing codes
  (`xfade_truncated_cpu`, `no_sofa_loaded`) unchanged.

## Release validation (re-confirmed 2026-05-18 at session resume)

- `ctest --output-on-failure -j$(nproc)` (NO_JUCE build): **86/86 PASS**
  (v0.5.1 81 → +5 NEW: `b2_runtime_underrun_auto_demote`,
  `b2_runtime_underrun_engine_integration` (added in follow-up commit
  `bd56a74`), `b1_b2_mode_transition_smooth`,
  `b1_b2_mode_transition_probe_clamped`,
  `b1_b2_mode_transition_disable_reenable`).
- `pytest tests/`: **47 passed, 0 failed**.
- Total ctest time: 3.55 s (no regression vs v0.5.1).

### Coverage gaps (intentional, surfaced for honesty)

- **ARM / Apple Silicon CI matrix** is deferred to **P2-1** (see
  `docs/weekly_progress_report_2026-05-18.md` §5.3). Structural workflow
  is filed at `.github/workflows/cross-platform.yml` and runs as
  `continue-on-error: true` during the v0.6 cycle (signal-only, not
  blocking merge). Promotion to a required gate is a v0.6.x admin task.
- **macOS arm64 manual verify** is **PENDING** — see
  `docs/release/v0.6.0/macos-arm64-verify.md` checklist sections A–E,
  user-queued. The v0.5 SSE-guard and v0.6 #9 release-store have **zero
  hardware verification** until the checklist is run.
- **DAW host hands-on validation** (Reaper / Bitwig / Logic / Cubase) is
  deferred to the ADR 0016 §Band-1 workflow (≤5 named beta testers,
  written acknowledgement, audit-log discipline). Hands-on log template
  at `docs/release/v0.3.0/daw-handson-log.md`.

These three deferrals do not invalidate the green ctest+pytest signals
but they DO limit what those signals prove: ctest+pytest validate
correctness on the x86_64 Linux dev host only. Treat any v0.6 claim of
"DAW-ready" as conditional on these gates being closed.

## Process note — post-hoc plan cadence

The v0.6 plan doc (`.omc/plans/spatial-engine-v0.6-stability.md`) was
authored *after* the implementation landed in the same working
session as the v0.5.1 release validation. The standard project
ralplan-Architect-Critic loop (`.claude/CLAUDE.md` policy) was *not*
spun up for v0.6; the work was self-approved against ctest + pytest
green gates only.

This is itemised as a P1 process gap in
`docs/weekly_progress_report_2026-05-18.md` §5 and tracked for
retroactive ralplan in the next cycle. Future v0.6.x or v0.7 work
re-enters the standard ralplan loop.

## Lineage commits

- `ece6cba` feat(rt-safety): v0.6 — audio-thread OSC hard-wall + runtime auto-demote + sendReply unify + ring release-store (single-commit release).
