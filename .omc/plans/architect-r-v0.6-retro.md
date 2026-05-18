# v0.6 RT-safety Hardening — Architect Retroactive Review

| | |
|---|---|
| **Date** | 2026-05-18 |
| **Reviewer** | Architect (omc-architect, post-hoc retroactive lane) |
| **Commits under review** | `ece6cba` (v0.6 4-item bundle), `bcb2fed` (v0.6 docs / ADR 0016 / CH7 manual / macOS verify) |
| **In-flight commit** | P1-3 engine integration ctest + P1-4 kill-switch (modified `core/src/core/SpatialEngine.cpp`, `core/tests/core_unit/CMakeLists.txt`, new `core/tests/core_unit/test_b2_runtime_underrun_engine_integration.cpp`) — landed as `bd56a74` |
| **Predecessor plan** | `.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md` §Q5 (explicitly defers 4 items to v0.6) |
| **Post-hoc plan** | `.omc/plans/spatial-engine-v0.6-stability.md` |
| **Critic flags addressed** | (1) post-hoc plan pattern, (2) `steady_clock` overhead, (3) JUCE GPL-3 trigger, (4) integration ctest gap, (5) ARM regression gate |
| **Verdict** | **APPROVE WITH RECOMMENDATIONS** — ship as-is, but treat the 5 recommendations in §D as binding for v0.6.x / v0.7. |

---

## §0. Scope and method

This review is post-hoc. The four items are already merged and verified (ctest 85/85 + pytest 47/47 at session resume per `spatial-engine-v0.6-stability.md:174-180`). The review's purpose is:

1. **Forensic technical adequacy** — does each item actually do what its commit message claims?
2. **Adjacent-impact bookkeeping** — what did the bundle silently change beyond its stated scope?
3. **Process introspection** — the post-hoc plan pattern was flagged by Critic; what guardrails prevent its recurrence?
4. **Forward planning** — what must / should land in v0.6.x and v0.7?

Every claim cites a `file:line` reference against the commits as merged on `main`. I read the full diff for `ece6cba`, the full v0.6-stability plan, the v0.5.1 Q5 deferral context, ADR 0016, the new ctest, CH7 manual, and the relevant production source.

---

## §A. Technical adequacy

### A.1 Item #4 — Audio-thread `sendReply` hard-wall

#### What changed

Pre-`ece6cba` `vst3/SpatialEngineProcessor.cpp::process()` had two RT-thread drains around lines 786–815 (per diff) that called `engine_->oscBackend().sendReply(...)`. Those drains are deleted; only an architectural comment remains at `vst3/SpatialEngineProcessor.cpp:786-797`. The drain code moved into `heartbeatLoop()` at `vst3/SpatialEngineProcessor.cpp:1254-1332`. The "drain-first-then-wait" reorganisation is at `vst3/SpatialEngineProcessor.cpp:1268-1330`.

#### Adequacy of the "not RT-safe" claim

`OSCBackend::sendReplyImpl()` calls `out_cv_.notify_one()` at `core/src/ipc/OSCBackend.cpp:462` (encode-failure path) and `:473` (success path). The Critic's premise is correct on **POSIX/glibc** (libstdc++ `notify_one` ultimately calls `futex(FUTEX_WAKE, …)`; futex syscall is unbounded under contention and acquires the kernel futex hash bucket spinlock), and stricter still on macOS (`__ulock_wake`). On any RT-safe taxonomy that bans syscalls in the audio callback (e.g., Ross Bencina's classic list), `notify_one` is therefore not strictly RT-safe even in the "uncontended" case because the kernel still touches the wait queue.

The fix is correct as a category move (audio thread → IO thread). There is **one residual subtlety** the commit message does not mention: `sendReply` may still acquire CAS contention on `out_head_` with concurrent producers (other callers of `sendReply` on the IO thread itself, or future control-thread emitters). But CAS on a `std::atomic<std::size_t>` is lock-free at the standard level on every target platform, and this is well-trodden territory for the project. No issue.

#### 200 ms latency budget — edge cases

The plan target is 200 ms (`spatial-engine-v0.6-stability.md:44`). The drain-first-then-wait pattern at `vst3/SpatialEngineProcessor.cpp:1268-1330` does the drain immediately upon `setActive(true)`, and the worst case is therefore "thread spawn + first sendReply" (~few hundred µs ≪ 200 ms). However, three edge cases need explicit handling:

**Edge case 1 — host handshake delay.** `sendReplyImpl()` returns false early when `last_peer_len_ == 0` (`core/src/ipc/OSCBackend.cpp:440-443`). The peer is captured only when the host sends its first inbound packet. If the host hasn't sent anything by `setActive(true)`, the first heartbeat tick drops on the floor; the retry happens 1 s later. In a typical DAW that opens the plugin's GUI and immediately sends `/sys/binaural_status` queries, this is sub-100 ms in practice — but **a host that lazy-binds the OSC channel (e.g., only after the first user gesture) sees ~1 s latency, not 200 ms**. The current implementation's retry preserves correctness but silently violates the latency budget. Recommendation: lower the heartbeat period to 100 ms (or use an explicit "peer just captured" signal) — see §D.

**Edge case 2 — 1 Hz tick alignment.** Even with peer captured at `setActive(true)`, the drain runs once before the first `wait_for(1000ms, …)`. If the host's first inbound packet arrives *between* the drain and the wait, the latch arms but is not emitted until the next tick (≈1 s later). The "drain-first-then-wait" order minimises this but does not eliminate it. The same fix (100 Hz heartbeat or peer-captured signal) addresses it.

**Edge case 3 — `state_snapshot_pending_` semantics across `prepareToPlay` re-entry.** The latch is armed only once per `setupProcessing` (control thread). If a `prepareToPlay` happens mid-heartbeat and re-arms the latch *after* the drain block ran in the current tick, the wait-then-loop semantics will pick it up on the next iteration. The retry-on-no-peer pattern at `vst3/SpatialEngineProcessor.cpp:1314-1322` is correct: it does not clear `state_snapshot_pending_` on send failure. Good.

#### Alternatives evaluation

**(a) Current — heartbeat drain at 1 Hz, retry on no-peer.** Implemented. Cost: 1-s upper bound on emission latency under host-side handshake delay.

**(b) RT-safe SPSC ring for OSC messages, IO thread drains.** Architecturally cleaner: an explicit `audio_thread_msg_ring_<MsgT, 32>` where the audio callback appends a tagged message struct (`{addr_token, payload_variant}`) and the IO thread translates to `sendReply`. Cost: ~150-300 LOC of new ring + message-type scaffolding, plus a new pre-allocation site. Benefit: removes the heartbeat tick alignment edge case (latency upper bound becomes "drain loop iteration interval", which can be O(µs)). Drawback: another concurrent ring to keep ASan-clean. **Worth doing only if v0.7 introduces a higher-frequency outbound channel** (e.g., head-tracking telemetry).

**(c) 100 Hz heartbeat.** Trivial one-line change (`1000ms` → `10ms`). Cost: 100× wakeups on the IO thread per second — negligible (idle thread wake is a few µs). Benefit: caps worst-case emission latency at 10 ms instead of 1000 ms. Drawback: 100 wake/sec is still 99% wasted because the steady-state cadence of `binaural_status` is 1 Hz; if we want to keep `binaural_status` at 1 Hz while emitting other latches sooner, we need a sub-tick counter inside `heartbeatLoop()`. Cleaner: split into two thread cadences or use one cadence with edge-trigger filters.

**Recommendation:** Option (c) with cadence split. The change is small enough to land in v0.6.1 as a quality-of-life patch — see §D Must-fix.

### A.2 Item #5 — Runtime sticky-underrun auto-demote

#### What changed

New API on `BinauralMonitor`:
- `recordB2BlockTiming(int block_size, float sample_rate, long long elapsed_ns)` at `core/src/output_backend/BinauralMonitor.cpp:425-466`.
- `isRuntimeDemoted()`, `drainRuntimeDemotePending()`, plus test hooks `injectRuntimeUnderrunStrikesForTest()` / `clearRuntimeDemoteForTest()` at `core/src/output_backend/BinauralMonitor.h:241-272`.
- Three atomics: `runtime_demote_strikes_`, `runtime_demoted_`, `runtime_demote_warning_pending_` at `core/src/output_backend/BinauralMonitor.h:442-445`.
- Audio-thread wall-clock bracketing in `core/src/core/SpatialEngine.cpp:709-735` (and post-fix at `:709-742` after the in-flight P1-4 kill-switch landed at `:714-723`).
- Heartbeat drain emits `ambivs_demoted_runtime` at `vst3/SpatialEngineProcessor.cpp:1286-1294`.

#### Magic number justification — 8 strikes × 90%

The plan defends these as: "8 blocks ≈ 21 ms at 48 kHz / 128 — long enough to ignore single-block hiccups, short enough to recover before the user hears multiple dropouts" (`spatial-engine-v0.6-stability.md:84-88`).

**Critique of "8 strikes":** This is a sliding-window heuristic with two assumptions. (1) Block size is ~128 — at 32-sample blocks the same 8-strike count = 5.3 ms (too short, false positives from cache-cold pages), at 1024-sample blocks it = 170 ms (too long, audible glitches before demote). (2) Failure mode is monotonic — but real DSP load oscillates with project complexity. 8 strikes of "just barely over 90%" can fire on a host that would never glitch in practice.

**Critique of "90%":** 90% gives 10% headroom for *everything else* (object render, decoder, limiter, OSC drain). The B2 path is `core/src/core/SpatialEngine.cpp:709-735` and the wall-clock excludes the encode loop at `:692-708` and the convolution decode at `:725-728`. So "90%" is actually "90% of the deadline allocated to processBlockB2 specifically." At 128/48 kHz the deadline is 2667 µs; 90% = 2400 µs; budget left for encode+limiter = 267 µs (≈10% of total). This is **dangerously thin in practice** — `core/src/core/SpatialEngine.cpp:692-708` is an `MAX_OBJECTS * 16` quadruple loop, which on slow ARM cores can easily exceed 100 µs alone for 32 active objects.

**Alternative tunings:**

| Tuning | Pros | Cons |
|---|---|---|
| **(current)** 8 × 90% | Implemented, ctest-covered. | False-positive risk at small block sizes; thin headroom. |
| **5 × 95%** | Fewer false positives (only "really over"). | 5 blocks ≈ 13 ms — fires on transient page-fault bursts. |
| **16 × 85%** | More headroom, longer hysteresis. | 42 ms before demote — user hears 3–4 audible underruns first. |
| **EWMA(α=0.1)** of `elapsed_ns / deadline_ns`, demote at smoothed >0.92 sustained for ≥10 windows | Time-constant decoupled from block size; smoother, no spurious sticky | More LOC (~30), needs unit test for EWMA convergence; magic numbers shifted not eliminated |
| **Block-size-aware**: strikes = `max(8, ceil(0.02 / block_seconds))` | Scales correctly to 32-sample DAWs and 1024-sample render hosts. | Two-line change but Python-style derived constants are harder to inspect at a glance. |

**Recommendation:** Move to **block-size-aware strike count** (one-line change in `recordB2BlockTiming` to derive `effective_strikes` from `block_seconds`). Keep 90% deadline fraction. This is a v0.6.1 candidate — see §D Should-have.

#### `std::chrono::steady_clock::now()` RT cost — cross-platform

| Platform | Underlying syscall / mechanism | RT-safety verdict | Source |
|---|---|---|---|
| Linux x86_64 (modern, ≥3.16) | vDSO `__vdso_clock_gettime(CLOCK_MONOTONIC)` — TSC read, no syscall | RT-safe (~25-50 ns) | confirmed by glibc source and kernel `arch/x86/entry/vdso/vclock_gettime.c` |
| Linux ARM64 (modern, ≥4.1) | vDSO `__kernel_clock_gettime` IF kernel built with `CONFIG_GENERIC_GETTIMEOFDAY=y` AND `CONFIG_ARM_ARCH_TIMER=y` | RT-safe iff vDSO present; **falls back to syscall (~500 ns + uncertain RT)** on older kernels or non-arch-timer SoCs | kernel/Documentation/arm64/elf_hwcaps.rst |
| macOS x86_64 / arm64 | `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` → mach_continuous_time → traps to `__commpage` (no syscall on M1) | RT-safe on M1+ (commpage read), but **older Intel macOS uses a function-call wrapper that can be ~100 ns** | Apple Foundation source (open-source XNU) |
| Windows | `QueryPerformanceCounter` → user-mode HPET/TSC read | RT-safe (~20-100 ns) | MSDN |

**Critique:** the commit message's claim "vDSO call on modern Linux, ~30 ns, no syscall, no alloc — RT-safe by inspection" (`SpatialEngine.cpp:711-712`, `BinauralMonitor.h:248-249`) is **correct for Linux x86_64 only**. It is conditionally true for ARM64 (depends on kernel + SoC) and silently false for old Linux on non-arch-timer ARM SoCs (Raspberry Pi 2 era). The implementation also doesn't measure the now() overhead itself — at ~30 ns × 2 calls = 60 ns per block, which is 0.002% of a 2667 µs deadline. Fine on x86_64; **on a pathological ARM SoC where each `now()` is a kernel trap (~5 µs each), the measurement infrastructure becomes 10 µs / block = 0.4% steady-state overhead, plus the syscall itself is no longer RT-safe**.

The in-flight P1-4 kill-switch at `core/src/core/SpatialEngine.cpp:714-742` correctly addresses *one* of the two concerns: once demoted, the wall-clock bracketing is bypassed, so demoted-host steady-state cost returns to zero. **The second concern — undemoted hosts on bad ARM SoCs — is not addressed.** A user with such a SoC pays the syscall cost on every B2 block forever (or until the threshold trips, which it will because the syscall is part of the wall-clock measurement). This is a self-fulfilling demote prophecy.

**Recommendation:** Detect vDSO availability at startup (`getauxval(AT_SYSINFO_EHDR)` on Linux + symbol probe) and gate `recordB2BlockTiming` behind that probe; fall back to a no-op (and an OSC `/sys/binaural_warning ,s "rt_timing_unavailable"` notice) on hosts where `steady_clock` would syscall. v0.7 territory — see §D Should-have.

#### Sticky decision recovery — UX gap

Per `core/src/output_backend/BinauralMonitor.cpp:425-466` and the plan at `spatial-engine-v0.6-stability.md:79-81`, the only recovery path is `initialize()` (i.e., next `prepareToPlay`). Plan justification: "Sticky until next prepareToPlay — transient spikes can't flap mode."

**Critique:** "transient" and "I closed the project, fixed my heavy plug-in load, reopened" are both legitimate user scenarios. The current design conflates them. A user who experienced a one-time CPU spike (e.g., another app started compiling) now lives with B1 forever for that session even though their actual sustained budget is fine. They have no in-host hatch — only "reopen the project" (which on Logic/Bitwig/Reaper is a heavy operation).

**Three-option remedy hierarchy:**

| Option | UX impact | Eng cost | Risk |
|---|---|---|---|
| **(α)** OSC `/sys/binaural_reset_demote ,i 1` — explicit user hatch | "Click button to retry B2" works | S (~30 LOC) | User can ping-pong indefinitely → audible glitch loop. Mitigate with a min-cooldown counter (e.g., reject reset if `< 60 s` since previous reset). |
| **(β)** Automatic re-arm after N seconds of good blocks | Self-healing | M (counter + timer + edge-trigger to undo demote, but the crossfade unwind must handle going back to B2) | Requires testing the B1→B2 transition direction, which is currently exercised only at prepareToPlay. |
| **(γ)** No change — document the limitation in CH7 manual | Zero | S | Worst UX, but acceptable for v0 (the manual already says "다음 prepareToPlay 까지 sticky" at CH7 line 196). |

**Recommendation:** Option (α) for v0.6.1 (a parameter knob that's easy to wire and reuses existing CAS-clear infra). Option (β) deferred to v0.7 once telemetry confirms how often users hit the demote.

#### Probe accuracy calibration via telemetry — design

The plan calls this out at `spatial-engine-v0.6-stability.md:213-214` ("collect strike-counter telemetry across user machines first"). A concrete design is missing. Here's a sketch:

**Data path.** On every demote event, emit (in addition to the existing `ambivs_demoted_runtime` notification):
- `/sys/binaural_diag ,iif <block_size> <sample_rate_int> <observed_max_ratio>` where `observed_max_ratio = max_elapsed_ns / deadline_ns` over the strike window.

**Storage.** Host-side soak harness (`tests/soak_harness/test_osc_warning_channel.py:212`) already filters by `/sys/binaural_warning`. Extend filter to capture `/sys/binaural_diag`; log to `soak_reports/binaural_diag_YYYYMMDD.jsonl`. This is voluntary local-only telemetry (no upload).

**Privacy.** No PII; just block size, sample rate, ratio. Aligned with ADR 0016 Band-0 (internal lab) workflow. If/when Band-1 testers (≤5) volunteer to share, the user manually grep+attach the diag log.

**Feedback loop.** Aggregate ratios across runs → if `p95(observed_max_ratio) < 0.8`, the threshold is too generous (probe under-rejects); if `p95 > 1.1`, probe is too aggressive. Reflect the adjustment in `kRuntimeDemoteBudgetFraction` and the startup probe's `kMinB2Throughput`.

**Recommendation:** Land `/sys/binaural_diag` channel in v0.6.1 (S — 1 outbound code + soak harness extension). The threshold-adjustment automation is v0.7 territory once enough data exists.

### A.3 Item #8 — `sendReply` overload unification

Mechanically obvious — `encodeOscReply()` already had the `have_s/have_f/have_i` flag-based signature, so this is a pure refactor with no behavioural change. Tests `test_osc_outbound_multi_producer` and `test_osc_security_peer_validation` cover all 3 overloads end-to-end (plan `spatial-engine-v0.6-stability.md:122-124`). No issue.

#### Future-channel extensibility — mixed-type signatures

**Current limit.** `sendReplyImpl` takes exactly one optional string, one optional float, one optional int. `encodeOscReply` itself enforces this via its 5 boolean+value params. A `,sffif` packet is not expressible with the current signature.

**Recommendation:** For v0.6.1's `/sys/binaural_diag ,iif`, **add a single `sendReply(addr, types, int32_t i1, int32_t i2, float f1)` overload** — small, targeted, no architecture impact. Defer the general `,sffif`-class redesign to when a real consumer demands it.

### A.4 Item #9 — Outbound ring slot `ready` clear: relaxed → release

Fix is correct as a defensive belt-and-suspenders move. **But no regression test on ARM exists.**

#### Stress test pattern to detect regression

The bug class (consumer's relaxed clear racing with producer's release-true on wrap) needs:
1. Multi-producer load (existing `test_osc_outbound_multi_producer` does this on x86_64).
2. Forced wrap.
3. ARM/Apple Silicon hardware OR `relacy_race_detector` synthetic verification.

ThreadSanitizer is sequentially-consistent-by-default → will flag the `relaxed` version even when the hardware happens to be okay (false positive on x86_64), and may miss subtle reordering races (false negative). Relacy would catch the prior bug deterministically.

**Recommendation:** Add `relacy` as a dev-dep behind a CMake flag, plus a `test_osc_outbound_relacy` that models the producer/consumer state machine.

---

## §B. Adjacent-area impact

### B.1 State v4 schema byte-equal compatibility

- State-save writes `binaural_effective_mode` at `vst3/SpatialEngineProcessor.cpp:589-591` as `payload[1]`.
- State-read at `:451-462` explicitly **ignores byte[1]** and dispatches off byte[2] (`requested_mode`).

**Verdict:** byte-equal preserved. v0.6 #5's `runtime_demoted_` flips `effective_mode_` to Direct (`BinauralMonitor.cpp:464`), so a state save *during* a demoted session writes `effective_mode = 0`. On reload, that byte is ignored, and `requested_mode = 1` (B2) is honoured — the user's intent persists across project saves even after a demote, and the *new* session re-evaluates whether B2 is viable.

**Recommendation:** Add one paragraph to CH7 §7.5 explaining "sticky demote does not persist across project save/load; B2 is re-attempted on reopen." S effort.

### B.2 VST3 host OSC consumer back-compat

Verdict: back-compat preserved. The CH7 manual at `docs/manual_kr/CH7_BINAURAL.md:159` documents the new code for human readers.

### B.3 `runtime_demoted_` ↔ `BinauralMonitor` ↔ `SpatialEngine` lifetime invariant — SILENT BUG

The plan asserts 1:1 lifetime: "Sticky until next prepareToPlay — i.e., next `initialize()`." Cross-check:

- `runtime_demoted_` is a `BinauralMonitor` member at `core/src/output_backend/BinauralMonitor.h:443`.
- `BinauralMonitor::initialize()` at `core/src/output_backend/BinauralMonitor.cpp:14-67` resets `prev_effective_mode_`, `outgoing_mode_`, `incoming_mode_` (at `:24-28`) but **does NOT explicitly reset `runtime_demote_strikes_`, `runtime_demoted_`, `runtime_demote_warning_pending_`**.

This is a **silent bug**. The plan and commit message claim "Sticky until next prepareToPlay — cleared only by initialize()" but the code does not actually clear them in `initialize()`. The clear is only available via the test-only hook `clearRuntimeDemoteForTest()` at `:477-481`.

**Severity:** This contradicts the plan, the commit message, the CH7 manual (`CH7_BINAURAL.md:159` says "다음 `prepareToPlay()` 까지"), and the test `test_b2_runtime_underrun_auto_demote.cpp` (which only validates `clearRuntimeDemoteForTest()`, never `initialize()`-driven clear). The test passes precisely because it uses the test-only hook.

**The fix is a 3-line addition in `initialize()`** to reset the 3 atomics. **This is a MUST-FIX in v0.6.1.** See §D.

---

## §C. Process and policy

### C.1 Post-hoc plan pattern — risk analysis

The v0.6-stability plan acknowledges the post-hoc cadence explicitly (`spatial-engine-v0.6-stability.md:184-205`). Defence: (1) RT-safety hardening on shipped surfaces, (2) all 4 items pre-enumerated in v0.5.1 plan §Q5 with Architect+Critic approval at that time, (3) ctest 85/85 + pytest 47/47 substitutes for verifier signal.

**Critique:** Point (3) is the weakest — the §B.3 silent bug was not covered by any test that exercises `initialize()` → check `isRuntimeDemoted()` flow. Test pass count is necessary but not sufficient as a verifier signal.

**Cumulative risk:** Each post-hoc plan reduces the pre-commit Architect/Critic review surface. If 3 cycles in a row use post-hoc plans, the cumulative blind spot becomes structural.

**Guardrail recommendation (binding for the next cycle):**

> **Post-hoc plan policy.** A session that ships without a pre-commit ralplan must tag the commit with `[plan-shadow]` in the subject line. At the next ralplan cycle's start, an explicit **retro-ralplan pass** (Planner + Architect + Critic, ≥15-min minimum walkthrough of every `[plan-shadow]` commit since the previous cycle) is mandatory. A retro-ralplan that uncovers MUST-FIX issues blocks the *new* cycle's planning until those fixes land. Two consecutive `[plan-shadow]` cycles trigger a "process review" task.

### C.2 ARM/macOS verification gap on #9

**My critique:** Shipping a weak-memory-order fix on a platform where the fix has not been tested is defensible **only if** (a) the fix is monotonically safer (true — release ≥ relaxed in C++11 MM), (b) cost of waiting is meaningful (true — ARM users are significant), (c) testing plan is concrete and dated (**partially true** — checklist exists but no date target / owner-name).

**Rule for next time:**

> **Weak-memory-order fixes require either (a) verification on actual weak-memory hardware before merge, OR (b) a relacy-race-detector unit test that models the exact race scenario and proves the fix correct under the C++11 memory model.** Synthetic test environments (`relacy`, ThreadSanitizer with custom model, formal verification via `cppmem`) are acceptable substitutes for hardware in the "before merge" step; hardware verification then becomes a "before-release" gate.

---

## §D. Recommendations for v0.6.x / v0.7

### Must-fix in v0.6.x

| # | Item | Effort | Blocks |
|---|---|---|---|
| **D-M1** | **`BinauralMonitor::initialize()` must reset `runtime_demote_strikes_`, `runtime_demoted_`, `runtime_demote_warning_pending_`.** Add 3 atomic stores at `core/src/output_backend/BinauralMonitor.cpp:14` (top of `initialize()`). Add a new ctest scenario in `test_b2_runtime_underrun_auto_demote.cpp` that exercises the `initialize()`-driven clear (not just `clearRuntimeDemoteForTest()`). Without this, the documented "sticky until next prepareToPlay" contract is silently violated. | S | Honesty of v0.6 release notes and CH7 manual; correctness of documented sticky-recovery UX. |
| **D-M2** | **`steady_clock` vDSO availability probe + fallback.** At `SpatialEngine` startup, probe whether `clock_gettime(CLOCK_MONOTONIC)` is vDSO-accelerated. If unavailable, skip the wall-clock bracketing entirely and emit a one-shot `/sys/binaural_warning ,s "rt_timing_unavailable"`. Without this, users on old kernels / non-arch-timer ARM SoCs pay an unbounded RT-unsafe syscall cost per audio block. | M | RT-safety claim on all non-x86_64-Linux platforms. |
| **D-M3** | **Document the demote re-evaluation on project reload** in CH7 §7.5. One paragraph clarifying that save during demote → reload re-attempts B2 → may re-demote. | S | User-facing accuracy. |
| **D-M4** | **Drop heartbeat period from 1000 ms to 100 ms (or add a sub-tick scheduler).** Cap worst-case latch emission latency at 10 ms instead of 1000 ms; preserves the documented 200 ms target across all host handshake timings. Note: keep `/sys/binaural_status` at 1 Hz via a sub-tick counter; only the latch drain accelerates. | S | Latency budget honesty. |

### Should-have in v0.7

| # | Item | Effort | Blocks |
|---|---|---|---|
| **D-S1** | **OSC `/sys/binaural_reset_demote ,i 1`** — explicit user hatch for the sticky-demote recovery UX gap (§A.2). Include a 60-second cooldown counter to prevent ping-pong. | S | None blocks, but unlocks beta-tester feedback workflows. |
| **D-S2** | **Block-size-aware strike count.** Change `kRuntimeDemoteStrikes = 8` to a derived constant `max(8, ceil(0.02s / block_seconds))`. Add a test scenario at 32-sample and 1024-sample block sizes. | S | Quality of demote heuristic on non-default DAW configurations. |
| **D-S3** | **`/sys/binaural_diag ,iif <block_size> <sample_rate> <observed_max_ratio>`** outbound channel for telemetry capture. Soak harness extension to log to `soak_reports/binaural_diag_*.jsonl`. Add new `sendReply(addr, types, int32_t, int32_t, float)` overload. | M | Probe accuracy calibration loop. |
| **D-S4** | **Relacy race-detector dev-dep + `test_osc_outbound_relacy`** modeling the multi-producer/single-consumer ring under the C++11 weak memory model. Closes the #9 verification gap without waiting for ARM hardware. | M | The "weak-memory-order fix requires synthetic verification" rule (§C.2). |
| **D-S5** | **GHA matrix: add `macos-14` (Apple Silicon) runner + `ubuntu-22.04-arm64`.** Run ctest + pytest on both. This is the canonical "before-release gate" for the v0.6 #9 fix and any future weak-memory work. *(Already filed as `.github/workflows/cross-platform.yml` in commit `aa8ecca` — verify next push.)* | M | The "weak-memory-order fix requires hardware verification before release" rule (§C.2). |

### Long-term (v0.7+)

| # | Item | Effort | Blocks |
|---|---|---|---|
| **D-L1** | **RT-safe SPSC ring for audio-thread → IO-thread OSC messages** (alternative (b) from §A.1). Worth doing **only** if v0.7 introduces a higher-frequency outbound channel. | L | Future high-frequency telemetry surfaces. |
| **D-L2** | **EWMA-smoothed demote detector**. Replaces the strike-counter with a continuous metric. Blocked on D-S3 telemetry. | M | Quality of demote precision. |
| **D-L3** | **Automatic re-arm of demoted state after N seconds of good blocks**. Cleaner UX than D-S1 but requires confidence in the B1→B2 reverse transition direction. | M | Eliminates the sticky-demote UX gap entirely. |
| **D-L4** | **Post-hoc plan policy enforcement** — implement the `[plan-shadow]` commit-tag rule (§C.1) as a `.git/hooks/commit-msg` hook + automation. | S | Structural integrity of the ralplan-required policy. |

---

## §F. Verdict

**APPROVE WITH RECOMMENDATIONS.**

Summary: v0.6's four items are technically sound and the ship was justified, but the bundle contains one silent correctness bug (D-M1, missing `initialize()` reset that contradicts the documented sticky-recovery contract) and three honesty gaps that v0.6.1 must close (D-M2 vDSO probe, D-M3 manual clarification, D-M4 latency budget). The post-hoc plan pattern and ARM verification gap are real but addressable via the §C.1 and §C.2 policy rules; the current cycle's risk is bounded and acceptable. The Critic's five flagged risks are all either addressed by the P1-3/P1-4 work or queued in §D — none of them justify reverting the v0.6 commits.
