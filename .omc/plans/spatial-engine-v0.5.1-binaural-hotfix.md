# Spatial Engine v0.5.1 — Binaural Hotfix Plan

**Status:** DRAFT — iteration 3 (Planner revision after Architect APPROVE-with-flags + Critic ITERATE on iter 2)
**Mode:** ralplan short (focused hotfix; no auth/migration/destructive scope)
**Base:** main @ f734796 (v0.5.0 released)
**Target tag:** v0.5.1 — **tag only on explicit user request after green CI** (per `.claude/CLAUDE.md`; Critic APPROVE alone is insufficient)
**Owner agent flow:** Planner → Architect → Critic → Autopilot (executor)

---

## Revision History

### Iter 3 (this revision) — addresses Critic ITERATE + Architect minor flags on iter 2

**BLOCKERs (Critic) — all three resolved:**

- **B1-iter3 (wrong ASan test target name):** Iter 2 cited `test_vst3_console_flood` everywhere. Repo evidence — file is `vst3/tests/soak_vst3_console_flood.cpp`; CMake registers `add_executable(soak_vst3_console_flood ...)` at `vst3/tests/CMakeLists.txt:248`, `add_test(NAME vst3_console_flood COMMAND soak_vst3_console_flood)` at line 261. CMake comment (line 246): *"Default run: 5s mini-soak. Full 60s: SPATIAL_ENGINE_SOAK=ON ctest -R vst3_console_flood -V"* — so the target IS in the default ctest invocation as a 5s mini-soak (no `SPATIAL_ENGINE_SOAK` gate on `add_test`). **Action chosen: (a)** Rename every occurrence of `test_vst3_console_flood` to `soak_vst3_console_flood` (binary name) / `vst3_console_flood` (ctest test name); confirm scope covers both default 5s mini-soak runs AND the SOAK_ON 60s lane. Updated in Q4 Files Touched, Q4 acceptance criteria, ADR-v051-03.
- **B2-iter3 (OlaConvolver path + production-unreachable test):** Iter 2 implied `core/src/output_backend/`. Repo evidence — header is at `core/src/hrtf/OlaConvolver.h`, `kOlaMaxIRLength = 1024` at line 27. Production clamp at `core/src/output_backend/BinauralMonitor.cpp:278` (and lines 87-88 for B1 path, 293-296 for B2 path) uses `std::min(p.ir_length, hrtf::kOlaMaxIRLength)` — so `ir_len > kOlaMaxIRLength` is unreachable through `BinauralMonitor::loadInto` chain. **Action:** Fix path in B9 to `core/src/hrtf/OlaConvolver.h`; explicitly label the new TEST_CASE as **defense-in-depth coverage of an internal `OlaConvolver::loadInto` boundary that is unreachable from production today** (test bypasses BinauralMonitor and calls `OlaConvolver::loadInto(ir, ir_len > kOlaMaxIRLength)` directly).
- **B3-iter3 (Q5 underrun-detection mechanism unimplementable):** Iter 2 ME claim — "audio thread writes `runtime_demoted_` on sticky-underrun detection". Repo evidence — `XrunCounter::record_underrun()` is only called from `core/src/audio_io/NullBackend.cpp:125` and `core/src/audio_io/DanteBackend.cpp:155`. VST3 plugin process (the v0.5.1 deployment target) has no path that feeds underrun signal into `BinauralMonitor`. **Action chosen: (b)** demote Q5 to a **"v0.6 design-spike" note** and remove it from the executable scope of this plan. Q5 section is replaced by a single short "Deferred to v0.6" paragraph documenting the gap. ME revision history entry from iter 2 is preserved here for traceability but no longer drives an executable phase.

**MAJORs (Critic) — landed in iter 3 unless noted:**

- **MAJOR 1-iter3 (Q2 truncated crossfade telemetry):** When the 1-block ramp is taken on probe-clamped hardware (R7 mitigation), emit `/sys/binaural_warning ,s "xfade_truncated_cpu"` once per truncation event. Added to Q2 wire surface, Files Touched, tests, acceptance criteria. Wire surface listing in Q1 also updated.
- **MAJOR 2-iter3 (Q2 cross-correlation gate window):** Critic notes 64 samples @ 440 Hz/48 kHz is < 1 cycle. **Action chosen: (a)** bump window to **256 samples** (≥ 2 cycles at 440 Hz @ 48 kHz; comfortably above the per-cycle floor) AND switch to **normalized** cross-correlation (Pearson correlation coefficient). Threshold remains ≥ 0.99. Documented in Q2 tests + acceptance.
- **MAJOR 3-iter3 (Q3 multi-prepare test):** `test_writebinaural_no_sofa_muted` (new) must drive **≥ 2 `prepareToPlay` cycles** (a re-prep after sample-rate change at minimum) and assert exactly **2 emissions** of `/sys/binaural_warning ,s "no_sofa_loaded"` (one per prepare lifetime). Added to Q3 tests + acceptance.
- **MAJOR 4-iter3 (D1 Option C invalidation needs sustained-burst caveat):** Added to ADR-v051-01 Consequences: *"Under sustained outbound burst (>10 warnings/sec), inbound `recvfrom` may be starved by `sendto` head-of-line blocking on the shared FD. v0.5.1 mitigation: outbound warnings are one-shot-per-event (no repeated emission per state). Reconsider Option C in v0.6 if telemetry frequency increases."*

**Architect minor flags — all three addressed:**

- **Architect-min-B5-iter3 (Q2 threading model wording):** Iter 2 said "control thread writes ONLY `requested_mode_`". Repo evidence — `BinauralMonitor::setRequestedMode` at `core/src/output_backend/BinauralMonitor.cpp:302-323` writes BOTH `requested_mode_` (line 305) AND `effective_mode_` (line 310/317/319) via atomic release stores. Probe path also writes `effective_mode_` (lines 379/386/398/405). Iter 3 wording: *"Control thread `setRequestedMode()` performs race-free atomic stores of both `requested_mode_` and `effective_mode_` (already implemented). Audio thread reads the most recent `effective_mode_` at the top of each block; the audio thread owns only the local crossfade state (counter + outgoing/incoming snapshot)."*
- **Architect-min-B9-iter3 (OlaConvolver path):** Iter 2 path string ambiguity fixed in Q4 — header is `core/src/hrtf/OlaConvolver.h`, `kOlaMaxIRLength` at line 27 (confirmed). Critic-confirmed.
- **Architect-min-B4-iter3 (socket count):** `vst3/tests/test_vst3_no_feedback_loop.cpp:117` asserts `sockets_opened >= 1 && sockets_opened <= 2`. With Q1, outbound replies are emitted by **core `OSCBackend`**, NOT by `SpatialEnginePluginUdp` (the test instantiates only `SpatialEnginePluginUdp`; the core `OSCBackend` is not instantiated inside this test process). So the structural socket count for this test is unchanged. **Added a Q1 sub-step requiring execution-time re-verification:** run `ctest -R vst3_no_feedback_loop` on the Q1 branch and inspect `sockets_opened` printf; if for any reason a third socket appears, bump the upper bound to 3 and document why.

**Critic-flagged "What's Missing" — landed where cheap:**

- **WM-1 (RT-thread alloc audit for Q3 no-SOFA emission path):** Q3 emission from `prepareToPlay` is control-thread → control-thread enqueue to Q1's SPSC ring → IO-thread `sendto` drain. Audio thread does NOT call any emission path in the no-SOFA case (mute path is a pure `std::fill(0)` on bus 1 L/R). Added one-line note to Q3 Files Touched.
- **WM-2 (legacy `reply_port = 0` round-trip):** Added to Q1 acceptance: *"legacy v0.4/v0.5 client (no `reply_port` field — i.e., `reply_port = 0` default) round-trip test — reply lands at sender's source port captured via `recvfrom()`."*
- **WM-3 (bypass-path byte-identity regression):** Q3 acceptance rephrased the iter-2 "bit-for-bit" claim to **"within ≤ 1e-6f per-sample tolerance"** matching the existing `test_v04_binaural_bus_placeholder.cpp` regression gate.
- **WM-4 (R8 schema sentinel risk row):** Added R8 to Risk Register: *"Future v0.6+ client interprets `reply_port = 0` literally as port-0 rather than as 'reply to source port' sentinel. Mitigation: documented in ADR-v051-01 Consequences; v0.6 may introduce a named constant `kReplyToSource = 0` plus assertion against literal port-0 binds in the receiver layer."*

**OQ status:** OQ1–OQ3 remain resolved (iter 2). No new open questions introduced; Q5 demotion is documented as a v0.6 deferral note, not a new open question.

### Iter 2 (preserved for traceability)

- **B1 (D1):** Deleted invented `/hb/pong` precedent claim; adopted Architect's S1 (`recv()` → `recvfrom()` migration on BOTH JUCE and stub paths) AND added Critic's Option C (separate outbound socket) as a third viable option with explicit pros/cons. Schema bump to `PayloadSysHandshake` carries optional `reply_port` (0 = same port as sender).
- **B2 (D3):** Replaced `exitcode=0` CTest property suppression with `std::quick_exit(0)` end-of-main approach (Architect S3); ADR-v051-03 rationale rewritten.
- **B3 (Q4 osc-port):** Reuse existing `--osc-sink-port` flag (integer, accepts 0 for ephemeral); add `osc_sink_port_actual` to soak report and to `REQUIRED_TOP_FIELDS`.
- **B4 (Q1 + test_vst3_no_feedback_loop):** Added the test to Files Touched with explicit allowlist carve-out for `/sys/binaural_warning` and `/sys/binaural_status`; route outbound via core `OSCBackend`, NOT via `SpatialEnginePluginUdp` (preserves recv-only structural invariant on the parameter-edit path).
- **B5 (Q2 threading model):** Rewrote — control thread only writes `requested_mode_`; audio thread owns the crossfade counter (arm on observed change, decrement per render, no control-thread writes during ramp). **[Superseded by Architect-min-B5-iter3 wording above — both atomic writes already exist in setRequestedMode.]**
- **B6 (Q3 + test_v04_binaural_bus_placeholder):** Added the placeholder test to Files Touched. Q3 only changes the active-engine non-bypass path; the bypass path retains -6 dB downmix. Test stays valid unchanged.
- **B7 (Q3 bypass split):** `writeBinauralPlaceholder()` split into `writeBinauralPlaceholderBypass()` and `writeBinauralPlaceholderNoSofa()`.
- **B8 (Q1 loadIntoFailures already exists):** Renamed proposal to "expose via `SpatialEngine` forwarding accessor".
- **B9 (Q4 OlaConvolver test):** Extend the existing `test_ola_convolver_loadinto_capacity_violation_release.cpp` with a new TEST_CASE covering `ir_len > kOlaMaxIRLength`. NDEBUG-skip logic preserved. **[Path corrected and "defense-in-depth" framing added in iter 3 — see B2-iter3.]**
- **MA (R6):** Added DAW silent-track auto-collapse risk.
- **MB (R7):** Added double-CPU-during-crossfade risk; mitigation = bound to 1 block when probe-clamped.
- **MC (D2 three-way):** Added Option C (identity stereo bypass) as a third viable option.
- **MD (Q2 acceptance):** Replaced "bit-equal reference" with RMS diff ≤ -60 dBFS. **[Iter 3: cross-correlation window bumped from 64 → 256 samples + switched to normalized correlation, per MAJOR 2-iter3.]**
- **ME (Q5 threading):** Specified `runtime_demoted_` as `std::atomic<bool>` with audio-thread writes. **[Superseded by B3-iter3 — Q5 demoted to v0.6 because VST3 process has no `record_underrun()` signal feeding `BinauralMonitor`.]**
- **MF (Q1 HeartbeatPublisher wiring):** Confirmed `HeartbeatPublisher::tick()` has no VST3 caller; added explicit IO-thread timer wiring sub-step in Q1.
- **MG (OQ1–OQ3 resolved):** Remains resolved in iter 3.
- **Mn1:** Deleted `/hb/pong` precedent claim from ADR-v051-01.

---

## RALPLAN-DR Summary

### Principles
1. **Fail loud over silent fallback** — when a configured feature degrades or is missing inputs, the DAW must hear AND be told. Silent transparent placeholders that look like the real feature are a release blocker.
2. **RT-thread alloc-free invariant is non-negotiable** — every change must respect the audio-thread contract (no allocation, no locks, no syscalls). Outbound OSC, crossfade scratch, and any new state must obey this.
3. **Test-fixture pollution is a release blocker** — flaky port binding and ASan-poisoned suites mask real regressions; v0.5.1 cannot ship while full ctest+pytest is red even if isolated runs are green.
4. **Bus 0 (speaker) is sacred** — no v0.5.1 change may regress bus-0 output bit-for-bit vs v0.4/v0.5 fixtures. All scope sits on bus 1 (binaural) and out-of-band control.
5. **Surgical patches over rearchitecture** — v0.6 is the place for B2 perceptual-merge tightening, broader scenario coverage, and Q5 runtime auto-demote (the v0.5.1 VST3 process lacks a `record_underrun()` feed into `BinauralMonitor`). v0.5.1 lands four bounded fixes.

### Decision Drivers (top 3)
1. **DAW observability of degraded state** — the user must be able to detect "AmbiVS probe demoted you to B1" and "no SOFA loaded → bus 1 is muted" without reading engine logs. Measurable: a Python test subscribes to `/sys/binaural_warning` and asserts the correct code arrives within 200 ms of the trigger.
2. **Zero-click mode transitions** — `/sys/binaural_mode` flips must not produce a sample discontinuity > 1e-3 in normalized L/R. Measurable: a unit test asserts `|y[boundary] - y[boundary-1]| < 1e-3` AND a monotonically-bounded ramp envelope across the 2-block window; RMS diff vs reference ≤ -60 dBFS; **normalized cross-correlation over 256 samples ≥ 0.99**.
3. **CI fully green, not just "mostly green"** — `ctest` full suite + `pytest` full suite in one invocation, including the soak-webgui schema test in the full pytest run. ASan target binaries must either pass cleanly or terminate before Steinberg SDK static destruction via `std::quick_exit(0)` (documented).

### Mode
**SHORT** — single focused hotfix cycle; no pre-mortem / expanded test pyramid required. Q5 is no longer a stretch phase; v0.5.1 ships Q1–Q4 only.

---

## Context

v0.5.0 shipped the "commercial-grade binaural" milestone (B1 per-object HRTF + B2 24-pt t-design VS chain with a CPU throughput probe). Three user-visible gaps and one test-infra debt prevent recommending v0.5.0 to DAW integrators:

1. **No OSC outbound channel** — the engine computes `binauralProbeWarningCode()` (e.g. `"ambivs_disabled_cpu"`) but never emits it to the network. DAWs cannot learn that B2 was clamped to B1.
2. **B1↔B2 mode-flip click** — runtime `/sys/binaural_mode` swaps the render branch at the next block boundary with no crossfade; an audible click occurs on every toggle. v0.5 plan §P4 A5 already specified a 2-block ramp.
3. **SOFA-missing fallback masquerades as binaural** — when no `.speh` is loaded and the engine is *active* (non-bypass), bus 1 emits a -6 dB speaker→stereo downmix that looks identical to a real binaural feed. The user gets a flat downmix labelled "binaural". (The bypass code path keeps the downmix intentionally — see Q3.)
4. **Test-infra debt** — two promised v0.5 unit tests were never written (`test_ola_convolver_loadinto_capacity_violation_release` covers prepared-capacity violation but NOT the `ir_len > kOlaMaxIRLength` boundary; KD-tree 8-canonical-direction never landed); `tests/soak_harness/test_soak_webgui_schema.py` flakes with `EADDRINUSE` when run after webgui playwright tests; two VST3 ASan targets (`test_vst3_intra_plugin_spsc_drain` and `soak_vst3_console_flood`) abort during Steinberg SDK static destruction with `munmap_chunk(): invalid pointer` (glibc `__libc_message → abort()` raised *before* ASan's exit handler runs — so the `exitcode=0` ASan option does NOT intercept it).

v0.5.1 ships four bounded phases (Q1–Q4). B2 perceptual merge gate tightening (-20 dBFS), hardware/DAW manual smoke, and runtime sticky-underrun auto-demote (was Q5 in iter 2) are explicitly **deferred to v0.6** — Q5 in particular because the VST3 plugin process has no `XrunCounter::record_underrun()` feed into `BinauralMonitor` (only `NullBackend.cpp:125` and `DanteBackend.cpp:155` call it; the VST3 plugin is the v0.5.1 deployment target).

---

## Viable Options (Decision Points for Architect)

### D1 — OSC outbound channel transport

**Option A: Reuse the inbound `OSCBackend` UDP socket; migrate `recv()` → `recvfrom()` on both compile paths to capture sender's endpoint; outbound replies go to `last_peer_endpoint_`. Optionally honour a `reply_port` field in the handshake payload.**

- Pros:
  - Single FD → no new firewall hole, no new bind to coordinate.
  - Outbound enqueue lives on the same IO thread that already drains the inbound queue → trivial RT-thread safety story.
  - `recvfrom()` is a 1-line drop-in replacement for `recv()` at `OSCBackend.cpp:78` (stub path) and the equivalent JUCE-path receiver callback (when JUCE wires up the real receiver in v1+; for now the JUCE branch can stay an in-process stub).
  - Schema extension is additive: `PayloadSysHandshake { uint16_t client_schema_version; uint16_t reply_port = 0; }` (0 = "reply to my source port") — old clients that never set `reply_port` get the implicit "reply to source" behaviour, no migration required.
- Cons:
  - The IO thread that calls `recvfrom()` then immediately calls `sendto()` on the same FD has a brief window where it cannot recv another inbound packet. Bounded by `sendto()` latency (microseconds on loopback / LAN). Mitigated by SPSC-queued outbound drain: recv loop reads inbound, drains outbound queue, repeat.
  - "Last peer wins" is wrong when multiple DAWs subscribe simultaneously — out of scope for v0.5.1 (single-DAW topology).

**Option B: Open a dedicated outbound socket bound to `0.0.0.0:0`, broadcast all replies to a configured `reply_port` (default 9001, configurable via `/sys/handshake` extension).**
- Pros: Clean separation of inbound/outbound; matches some DAW OSC patterns; multicast-friendly if topology grows.
- Cons: Permanent new configuration surface (`reply_port`), new failure mode (reply port collision), still needs the schema bump anyway.

**Option C (Critic-raised, separate outbound-only socket bound to `0.0.0.0:0`, no schema bump, replies sent to `last_peer_endpoint_` captured via `recvfrom()`).**
- Pros:
  - No schema bump (use ephemeral source port automatically).
  - No `recv()`/`sendto()` race on a single FD — distinct sockets eliminate the head-of-line scenario.
  - Smaller blast radius vs Option B (no user-configurable reply port).
- Cons:
  - Two FDs to manage per OSCBackend instance — must close both on `stop()`.
  - DAWs that whitelist outbound traffic by port (rare but exists) would need to allow ephemeral source ports.
  - Still requires the `recvfrom()` migration to capture the peer endpoint — so the "single FD" simplification disappears anyway; Option C is essentially Option A + one extra socket.

**Recommendation:** **Option A**. The `recv()` → `recvfrom()` migration is unavoidable in all three options. Given that, the only marginal cost of A is the bounded `sendto()` latency window on the inbound FD, which is dominated by network RTT in practice. The schema bump is additive and zero-cost for legacy clients. Option C buys negligible safety (the race window is microsecond-scale; the SPSC drain ordering eliminates contention) at the cost of a second FD lifecycle. Option B is over-engineered for v0.5.1's single-DAW topology.

**Invalidation rationale for Option B:** Requires permanent user-facing `reply_port` config; same schema bump as A; gains nothing in single-DAW topology.

**Invalidation rationale for Option C:** Architecturally clean but the engineering cost is non-trivial: a second `socket()`/`close()` pair, second IO loop or shared loop with two FDs in a `select()`, second `udp_thread_` lifecycle. Saves a microsecond-scale race window that the SPSC drain ordering already eliminates. **Iter 3 caveat (MAJOR 4):** Under sustained outbound burst (>10 warnings/sec), the shared-FD `sendto()` could starve `recvfrom()`; v0.5.1 mitigation is one-shot-per-event emission (no repeated emission per warning state). Reconsider Option C in v0.6 if telemetry frequency increases.

---

### D2 — SOFA-missing fallback policy (engine-active path only; bypass path is out of scope per B6)

**Option A: Mute bus 1 (zero L/R) + emit `/sys/binaural_warning ,s "no_sofa_loaded"` once per `prepareToPlay()`.**
- Pros: Unambiguous to the listener (silence on the binaural bus = obvious "something is wrong"); honours principle #1; cannot be confused with a legitimate flat-mix preview.
- Cons: First-time integrators who forget to load a `.speh` get silence — needs USER_GUIDE.md note (R6 mitigation). DAW track-level silence detection (Logic, Cubase) may auto-collapse the muted bus.

**Option B: Keep the -6 dB downmix as a transparent diagnostic feed, expose `fallback="downmix"` in `/sys/state`, add a one-shot `/sys/binaural_warning ,s "no_sofa_loaded"` on first render after fallback engages.**
- Pros: DAWs hear *something* on bus 1; backward-compatible.
- Cons: Same root failure mode as today: a user who ignores `/sys/state` still hears a flat mix labelled binaural. This is the bug the hotfix exists to fix.

**Option C (Architect-raised, identity stereo bypass): bus 1 L = in_L, R = in_R at 1.0 gain (NOT a -6 dB downmix), plus warning + `/sys/state.fallback_mode = "stereo_passthrough"`.**
- Pros:
  - Preserves audio continuity for users without an OSC subscriber (no silent-track auto-collapse risk — R6 mitigated).
  - Cannot be confused with binaural (no ITD/ILD, no L≠R characteristic of HRTF processing).
  - 1.0 gain means no surprise level change vs the dry input.
- Cons:
  - L=in_L, R=in_R is exactly what a user would hear in a "no plugin" reference monitoring chain — subtle, can still mislead a careless integrator who thinks "binaural is working because I hear stereo".
  - Slightly weakens principle #1 (the warning OSC is now the primary tell).
  - Adds another fallback mode to document and test.

**Recommendation:** **Option A**. Principle #1 ("fail loud") is the cardinal driver for this hotfix; Option C re-introduces the "you can't tell by listening" failure mode that Option B has today, just with stereo passthrough instead of -6 dB downmix. Option A's silent-bus risk (R6) is mitigated by:
1. Prominent USER_GUIDE.md note (one paragraph, R6 mitigation).
2. OSC warning is the primary signal channel (DAW integrators per spec subscribe to `/sys/binaural_warning`).
3. `/sys/state.fallback_mode = "muted"` field for UIs.

Architect may override to C in iter 3+ if a concrete user already depends on the bypass-style behaviour. Defer the option-C variant to a v0.6 "user-selectable fallback policy" feature if needed.

**Invalidation rationale for Option B:** Restates the bug we're fixing.

**Conditional invalidation for Option C:** Holds unless Architect provides evidence a real integrator needs continuous audio on bus 1 without subscribing to the OSC warning channel. If that evidence emerges, switch to Option C and document the strengthened spec ("integrators MUST subscribe to `/sys/binaural_warning`").

---

### D3 — ASan test isolation strategy

**Option A: Add `std::quick_exit(0)` at the end of each ASan-flaky test's `main()` (after all assertions pass) so process termination skips Steinberg SDK static destructors entirely.**

- Pros:
  - Surgical: only the two known-broken targets are touched (`test_vst3_intra_plugin_spsc_drain` and `soak_vst3_console_flood`); all other ASan-built tests continue with normal exit and full static-dtor coverage.
  - Bypasses the actual root cause (glibc `__libc_message → abort()` raised from a Steinberg static dtor BEFORE ASan's `_exit` handler runs — the `ASAN_OPTIONS=exitcode=0` flag exists but controls ASan's *own* exit path; it does NOT intercept the SIGABRT from glibc).
  - No CMake property gymnastics, no fragile shell wrappers.
  - Each affected test self-documents the workaround in its own `main()` (a 3-line comment + `std::quick_exit(0)`).
- Cons:
  - `quick_exit` skips static destructors → any leak in the test body itself is not flagged at exit. Mitigation: the test body's leaks are still flagged by ASan's per-allocation tracking during execution; only end-of-process leak reporting is suppressed.

**Option B: Per-test bash wrapper translating SIGABRT (134) to success when stderr matches `munmap_chunk\(\): invalid pointer`.**
- Pros: Doesn't require modifying test source.
- Cons: Cross-OS shell quoting hell, fragile regex, harder to audit; rejected.

**Recommendation:** **Option A** (`std::quick_exit(0)`). Document in `docs/CI_QUARANTINE.md` with the corrected root cause ("Steinberg static dtor + glibc `__libc_message` SIGABRT, raised before ASan's exit path, so `exitcode=0` does not help"). v0.6 attempts an upstream Steinberg SDK patch.

---

## Phase Q1 — OSC Outbound Channel + Telemetry

### Files touched
- `core/src/ipc/Command.h` — extend `PayloadSysHandshake` with `uint16_t reply_port = 0;` (additive; 0 = "reply to source port"). Add `ReplyTag::BinauralWarning = 0x07`, `ReplyTag::BinauralStatus = 0x08`. Extend `Reply` to optionally carry a typed payload variant (warning code string OR status int) without breaking existing `Warning`/`Error` consumers.
- `core/src/ipc/OSCBackend.h` — add `last_peer_endpoint_` (struct sockaddr_storage + socklen_t, populated by `recvfrom()`); add `void sendReply(const char* addr, const char* types, ...)` (control thread → IO thread enqueue) backed by an SPSC ring of pre-allocated `char[256]` packet buffers.
- `core/src/ipc/OSCBackend.cpp` (stub path, lines 75–83) — replace `recv()` with `recvfrom()`; capture sender into `last_peer_endpoint_`. Update `injectPacket()` overload to optionally accept a peer-address parameter (default empty → no reply target known).
- `core/src/ipc/OSCBackend.cpp` (JUCE path, lines 32–37) — same `recvfrom()` migration once the JUCE receiver is wired in v1+; for v0.5.1 the JUCE-path `dispatch()` and `injectPacket()` stubs only need the `last_peer_endpoint_` member compiled in (no behaviour change because the JUCE wire path remains deferred).
- `core/src/ipc/HeartbeatPublisher.cpp` / `.h` — keep existing `HbPing` emission unchanged. Add optional `binaural_status_cb_` callback hook that is invoked on the same 1-Hz tick. **MF wiring:** `tick()` is currently called from nobody in `vst3/`; add a dedicated IO-thread timer in `vst3/SpatialEngineProcessor.cpp` (`prepareToPlay` starts the timer, `setActive(false)` stops it). The timer runs at 1 Hz on a `std::thread` + `std::this_thread::sleep_for(1000ms)` loop, NOT on the audio thread. Audio thread only writes the snapshot atomics (`load_into_failures_count_atomic_`).
- `core/src/core/SpatialEngine.h` / `.cpp` — remove `TODO(A2)` at `SpatialEngine.h:76`; after `triggerBinauralProbe()` clamps to Direct, call `OSCBackend::sendReply("/sys/binaural_warning", ",sf", "ambivs_disabled_cpu", probe_throughput)`. Add **forwarding accessor** `loadIntoFailuresCount()` that delegates to `BinauralMonitor::loadIntoFailures()` (already exists at `core/src/output_backend/BinauralMonitor.cpp:220-230`; this PR only adds the engine-level forwarding wrapper).
- `vst3/SpatialEngineProcessor.cpp` / `.hpp` — on first render after the active-engine no-SOFA fallback engages, emit `/sys/binaural_warning ,s "no_sofa_loaded"` once per `prepareToPlay()` lifetime via the core `OSCBackend` reply path (see Q3 split). NOT via `SpatialEnginePluginUdp` (which stays recv-only per B4 / ADR 0010 A2-α). Add `bool no_sofa_warning_emitted_{false}` reset in `prepareToPlay`.
- `vst3/tests/test_vst3_no_feedback_loop.cpp` (lines 165–171) — update the documentation comment to specify the allowlist: `SpatialEnginePluginUdp` remains zero-outbound; the new `/sys/binaural_warning` and `/sys/binaural_status` channels are emitted by the core `OSCBackend` (a separate object), not by `SpatialEnginePluginUdp`. The existing assertion (`grep sendto vst3/SpatialEnginePluginUdp.cpp` returns no results) stays valid because the new outbound code lives in `core/src/ipc/OSCBackend.cpp`. Add an inline comment cross-referencing Q1. **Iter 3 sub-step (Architect-min-B4):** after the Q1 commit lands locally, run `ctest -R vst3_no_feedback_loop -V` and read the printf at line 112 (`sockets_opened`). The test instantiates ONLY `SpatialEnginePluginUdp` (line 102) — core `OSCBackend` is NOT created in this test process, so `sockets_opened` SHOULD remain in `[1, 2]`. If for any unexpected reason a third socket appears, bump the upper bound in the assertion at line 117 to 3 AND document the root cause inline. Do NOT pre-emptively bump.

### API / wire surface
```
/sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput_ratio>
/sys/binaural_warning ,s  "no_sofa_loaded"
/sys/binaural_warning ,s  "xfade_truncated_cpu"            # iter 3 MAJOR 1 — Q2 1-block ramp on probe-clamped HW
/sys/binaural_status  ,i  <load_into_failures_count>       # 1 Hz heartbeat
PayloadSysHandshake { uint16_t client_schema_version; uint16_t reply_port = 0; }
```

### Tests added / modified
- `core/tests/core_unit/test_osc_outbound_reply_smoke.cpp` (new) — bind a loopback UDP listener on an ephemeral port, send an inbound packet from that listener's socket, assert `OSCBackend` captures the sender endpoint, drive `OSCBackend::sendReply` from the control thread, assert the packet arrives back at the listener within 200 ms. **Iter 3 (WM-2):** include a legacy-client sub-case that omits `reply_port` from the handshake (i.e., `reply_port = 0`) and asserts the reply lands at the sender's source port (captured via `recvfrom()`).
- `core/tests/core_unit/test_binaural_probe_warning_emission.cpp` (new) — inject a synthetic low-throughput probe result, call `triggerBinauralProbe()`, assert `/sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput>` is observed.
- `tests/soak_harness/test_osc_warning_channel.py` (new) — pytest fixture spawns the engine subprocess, subscribes to `/sys/binaural_warning`, exercises the no-SOFA path and the probe-fail path, asserts both warning codes are received.
- `vst3/tests/test_vst3_no_feedback_loop.cpp` (modified — comment + structural assertion update + iter-3 socket-count re-verification sub-step; see Files Touched).

### Acceptance criteria
- `ctest -R osc_outbound_reply_smoke` passes (≥10 consecutive runs on ephemeral ports), AND the new legacy-client sub-case (no `reply_port`) round-trip lands at the sender's source port.
- `ctest -R binaural_probe_warning_emission` passes.
- `ctest -R vst3_no_feedback_loop` still passes (allowlist update; structural invariant preserved); `sockets_opened` printf remains in `[1, 2]` — if not, follow the iter-3 sub-step bump-and-document procedure.
- `pytest tests/soak_harness/test_osc_warning_channel.py -v` passes; both warning codes observed within 500 ms.
- Manual loopback check: `nc -ulk 9000 | hexdump -C` shows the warning packet on probe-fail and on no-SOFA load.
- RT-alloc test (`tests/perf/test_no_alloc_in_audio_thread.py`) still green.

### Rollback story
Disable `sendReply` enqueue with CMake flag `SPATIAL_ENGINE_OSC_OUTBOUND=OFF`; inbound path and `HeartbeatPublisher` continue working untouched. Schema bump is additive — old clients ignore `reply_port`. Single-file revert reactivates the v0.5.0 behaviour.

---

## Phase Q2 — A3 Mode-Transition Crossfade

### Threading model (Architect-min-B5-iter3 — corrected wording)

- `BinauralMonitor::setRequestedMode()` at `core/src/output_backend/BinauralMonitor.cpp:302-323` **already** performs race-free atomic stores of BOTH `requested_mode_` (line 305) AND `effective_mode_` (line 310/317/319). The probe path (lines 379-405) also writes `effective_mode_`. No change to those control-thread writes is required for Q2.
- **Audio thread** owns the crossfade state entirely, all in audio-thread-local non-atomic ints (no extra atomic stores on the hot path):
  - `prev_effective_mode_` — last-rendered mode.
  - `xfade_blocks_remaining_` — counter.
  - `outgoing_mode_`, `incoming_mode_` — branch identifiers used during ramp.
- On each block, audio thread snapshots `effectiveMode()` (already an atomic load — see existing call sites). If `effective != prev_effective_mode_` and `xfade_blocks_remaining_ == 0`, arm the ramp: `xfade_blocks_remaining_ = kXfadeBlocks; outgoing_mode_ = prev_effective_mode_; incoming_mode_ = effective;`.
- During the ramp, both branches render; sum with linear envelopes; decrement counter at end of block.
- When counter reaches 0, drop the outgoing branch and set `prev_effective_mode_ = incoming_mode_`.
- A second mode flip mid-ramp updates `requested_mode_` (and thus `effective_mode_`), but the audio thread defers acting on it until the current ramp completes — the next flip is observed at the top of the next block after `xfade_blocks_remaining_ == 0`.

### Crossfade duration (R7 mitigation, MB + MAJOR 1-iter3)

- Default: `kXfadeBlocks = 2` (two-block linear ramp).
- **When the throughput probe is in the "clamped" state** (`probe_warning_set_ == true` → effective mode is forced to Direct regardless of requested): set `kXfadeBlocks = 1`. Rationale: with probe-clamped (borderline) hardware, running both branches for 2 blocks doubles binaural CPU and risks underrunning bus 0. With probe-clamped, a 1-block ramp accepts a residual click in exchange for headroom safety. Documented in inline comment + Q2 acceptance below.
- **Iter 3 (MAJOR 1):** on each truncation event (i.e., a ramp armed with `kXfadeBlocks = 1` because `probe_warning_set_ == true` at arm time), enqueue `/sys/binaural_warning ,s "xfade_truncated_cpu"` via the Q1 SPSC ring — **one emission per arm event** (no per-block flood). Suppress repeat emissions while `probe_warning_set_` remains true and no further ramp arms occur.

### Files touched
- `core/src/output_backend/BinauralMonitor.cpp` / `.h` — add `prev_effective_mode_` (audio-thread-local int), `xfade_blocks_remaining_` (audio-thread-local int), `outgoing_mode_`, `incoming_mode_`. In `processBlockB1` / `processBlockB2` callers (`SpatialEngine.cpp`), arm and step the ramp per the threading model above. The 2-block scratch buffer for the outgoing branch reuses existing `binaural_l_buf_` / `binaural_r_buf_` aliases — no new allocation.
- `core/src/core/SpatialEngine.cpp` — remove `TODO(A3, P4.2)` at line 659. Replace the bare mode-flip with the audio-thread arm-and-step logic. **Iter 3 (MAJOR 1):** on each ramp arm where `probe_warning_set_ == true`, set a one-shot flag and enqueue `xfade_truncated_cpu` via `OSCBackend::sendReply` from the IO-thread drain (NOT the audio thread — audio thread sets a `std::atomic<bool> xfade_truncated_pending_{false}` flag; the 1-Hz IO-thread timer added in Q1 reads + emits + clears).
- `core/src/core/SpatialEngine.h` — add `binauralXfadeActive()` accessor (audio-thread state visible to tests via control-thread atomic snapshot of `xfade_blocks_remaining_`).

### API / wire surface
- `/sys/binaural_warning ,s "xfade_truncated_cpu"` (new — iter 3 MAJOR 1). One-shot per truncated-ramp arm.

### Tests added / modified
- `core/tests/core_unit/test_b1_b2_mode_transition_smooth.cpp` (new):
  - Drive engine with a sustained 440 Hz pure tone on one active object.
  - Flip `/sys/binaural_mode` at a known block boundary.
  - Assert (a) `|y[boundary] - y[boundary-1]| < 1e-3` on both L and R, (b) outgoing-branch energy envelope is non-increasing AND incoming-branch envelope is non-decreasing across the ramp window, (c) **RMS difference vs a pure-incoming-mode reference rendered separately ≤ -60 dBFS** over the 2 blocks immediately following the ramp end (MD correction — bit-equality is unachievable because OlaConvolver overlap-add tails differ between "started fresh" and "ramped in").
  - **Iter 3 (MAJOR 2):** Normalized cross-correlation (Pearson coefficient) over a **256-sample window** centred on the boundary, threshold ≥ 0.99 (≥ 2 full cycles at 440 Hz @ 48 kHz; comfortably above the per-cycle floor; normalization removes amplitude bias).
- `core/tests/core_unit/test_b1_b2_mode_transition_probe_clamped.cpp` (new — iter 3 MAJOR 1):
  - Force `probe_warning_set_ = true` via a synthetic low-throughput probe.
  - Flip `/sys/binaural_mode` and assert `xfade_blocks_remaining_` is armed to **1**, not 2.
  - Assert exactly **1 emission** of `/sys/binaural_warning ,s "xfade_truncated_cpu"` per ramp event (no per-block flood).
- Extend `tests/perceptual/test_binaural_modes_perceptual.py` with the normalized cross-correlation test (256-sample window).

### Acceptance criteria
- `ctest -R b1_b2_mode_transition_smooth` passes for both B1→B2 and B2→B1.
- RMS diff vs pure-incoming reference ≤ -60 dBFS over 2 post-ramp blocks.
- Normalized cross-correlation (256-sample window) ≥ 0.99 across the boundary (iter 3 MAJOR 2).
- `ctest -R b1_b2_mode_transition_probe_clamped` passes: 1-block ramp when probe-clamped; exactly 1 `xfade_truncated_cpu` emission per ramp arm.
- RT-alloc test stays green.
- Bus 0 speaker output bit-identical to v0.5 fixtures.

### Rollback story
Single-file revert of `SpatialEngine.cpp` mode-flip block + BinauralMonitor delta. The `TODO(A3, P4.2)` comment restores v0.5.0 behaviour. The `xfade_truncated_cpu` wire code remains harmless if the ramp logic is reverted (it is simply never emitted).

---

## Phase Q3 — SOFA-Missing Fallback Policy

### Bypass vs active split (B7)

`writeBinauralPlaceholder()` at `vst3/SpatialEngineProcessor.cpp:971` is currently called from THREE sites:
1. **Line 909** — bypass path. Intent (per comment lines 906–907): "users still hear a recognisable signal" under bypass. **Keep -6 dB downmix here.**
2. **Line 943** — active path, `engine_ == nullptr` (engine never instantiated). **Mute + warn.**
3. **Line 949** — active path, `engine_` exists but `!engL || !engR || !enabled` (no SOFA loaded or binaural disabled). **Mute + warn.**

Split the function into two:
- `writeBinauralPlaceholderBypass(data)` — body identical to today's `writeBinauralPlaceholder` (-6 dB downmix). Called from line 909 only.
- `writeBinauralPlaceholderNoSofa(data)` — new body: zero bus 1 L/R, emit `/sys/binaural_warning ,s "no_sofa_loaded"` once per `prepareToPlay` lifetime, set `/sys/state.fallback_mode = "muted"`. Called from lines 943 and 949.

This preserves the bypass-path behaviour AND keeps `vst3/tests/test_v04_binaural_bus_placeholder.cpp` valid (that test runs under `proc.setBypass(true)` at line ~120, which hits line 909, which still calls the bypass variant returning the -6 dB downmix — assertions `bus1_L_eq_R_bitwise`, `bus1_eq_half_sum_of_bus0_ch0_ch1`, `bus1_downmix_audible_nonzero`, `bus1_peak_le_bus0_peak` all stay green unchanged).

**Iter 3 (WM-1, RT-alloc audit):** the no-SOFA mute path is a pure `std::fill(0)` on bus 1 L/R inside `writeBinauralPlaceholderNoSofa()` and does NOT call any OSC emission code on the audio thread. The `/sys/binaural_warning` emission is performed from `prepareToPlay` (control thread → Q1 SPSC ring → IO-thread `sendto`). Audio thread emission paths are NOT introduced by this phase.

### Files touched
- `vst3/SpatialEngineProcessor.cpp` — split `writeBinauralPlaceholder` into `writeBinauralPlaceholderBypass` (keep current body) and `writeBinauralPlaceholderNoSofa` (mute + warn). Update call sites at lines 909 / 943 / 949.
- `vst3/SpatialEngineProcessor.hpp` — declare both new functions; add `bool no_sofa_warning_emitted_{false}` member; reset in `prepareToPlay`.
- `core/src/output_backend/StateModel.cpp` (if `/sys/state` serializer lives here; otherwise `core/src/core/SpatialEngine.cpp`) — add `fallback_mode: "muted"` to the snapshot dict when active-path fallback is engaged.
- `docs/USER_GUIDE.md` — one-paragraph note (R6 mitigation): "If no SOFA is loaded while the plugin is active, bus 1 is silent and `/sys/binaural_warning ,s no_sofa_loaded` is emitted once per prepare cycle. **Some DAWs (Logic, Cubase) auto-collapse silent stereo tracks** — keep an OSC subscriber on `/sys/binaural_warning` to detect the configuration error. Under bypass, bus 1 reverts to the -6 dB diagnostic downmix."

### API / wire surface
```
/sys/binaural_warning ,s "no_sofa_loaded"   # one-shot per prepareToPlay, active path only
/sys/state ... "fallback_mode": "muted" ... # active-path no-SOFA only
```

### Tests added / modified
- `vst3/tests/test_writebinaural_no_sofa_muted.cpp` (new) — drive the processor WITHOUT bypass and WITHOUT loading a SOFA, render N blocks, assert bus 1 L/R are all zero AND `/sys/binaural_warning ,s "no_sofa_loaded"` is observed exactly once per prepare cycle. **Iter 3 (MAJOR 3):** the test must perform **≥ 2 `prepareToPlay` cycles** (e.g., re-prep after a sample-rate change from 48000 → 44100 → 48000, OR a deactivate/reactivate round-trip) and assert **exactly 2 emissions** of `/sys/binaural_warning ,s "no_sofa_loaded"` across the full run.
- `vst3/tests/test_v04_binaural_bus_placeholder.cpp` — **NO changes required** (the test runs under bypass, which now routes through `writeBinauralPlaceholderBypass` whose body is the old `-6 dB downmix`). **Iter 3 (WM-3) regression-gate clarification:** the assertions in this test already tolerate ≤ 1e-6f per-sample error; that tolerance IS the formal v0.5.1 regression gate for the bypass path. The plan's earlier "bit-for-bit" language is replaced with "within ≤ 1e-6f per-sample tolerance vs the existing fixture, as enforced by the unchanged `test_v04_binaural_bus_placeholder.cpp` assertions".
- Audit other `vst3/tests/` files for `writeBinauralPlaceholder` references; if any non-bypass test asserts the old downmix behaviour, migrate it to expect mute + warn.

### Acceptance criteria
- `ctest -R writebinaural_no_sofa_muted` passes; exactly 2 `no_sofa_loaded` emissions across ≥ 2 `prepareToPlay` cycles (MAJOR 3).
- `ctest -R v04_binaural_bus_placeholder` passes unchanged (regression gate for B6); bypass-path output within ≤ 1e-6f per-sample of fixture (WM-3).
- Bus-0 output bit-identical to v0.5 fixtures.
- Warning emitted exactly once per prepare cycle (not per block — verified by counting in the new test).
- `/sys/state` snapshot includes `fallback_mode` field when active-path fallback engaged.

### Rollback story
Single-file revert restores `writeBinauralPlaceholder` (line 971); the line-909 / 943 / 949 call sites are reset to the unified function. No legacy `_LEGACY` stub retention is needed because the bypass-path body is preserved as `writeBinauralPlaceholderBypass`.

---

## Phase Q4 — Test-Infra Cleanup

### Files touched
- `core/tests/core_unit/test_ola_convolver_loadinto_capacity_violation_release.cpp` (**extend, not new** — B9, path corrected per B2-iter3) — add a second TEST case after the existing one. The existing test exercises "prepared capacity (`small_ir_len = 256`) < requested `ir_len = 1024`"; the new case exercises **"`ir_len > kOlaMaxIRLength`"** (the absolute-cap path at `core/src/hrtf/OlaConvolver.h:46`). The test calls `OlaConvolver::loadInto(ir, ir_len > kOlaMaxIRLength)` **directly** (bypassing `BinauralMonitor`). **Iter 3 (B2-iter3):** documented inline as **defense-in-depth coverage of an internal `OlaConvolver` boundary that is currently unreachable from production** — `BinauralMonitor.cpp:278` clamps via `std::min(p.ir_length, hrtf::kOlaMaxIRLength)` before any call to `OlaConvolver::loadInto`, so no production caller can hit this path today; the test guards against a future regression where a caller forgets the clamp. Asserts (a) function returns without crash, (b) `loadIntoFailures()` increments by exactly 1, (c) subsequent `process()` output is bit-identical to pre-`loadInto` output. Preserves the existing `#ifndef NDEBUG ... #else ... #endif` skip-in-debug structure.
- `core/tests/core_unit/test_kdtree3d_8_canonical_directions.cpp` (new) — build a `KdTree3D` from a small known SH-direction dataset; query 8 canonical points (front, back, left, right, up, down, FL30 az=-30° el=0°, FR30 az=+30° el=0°); assert nearest-neighbour indices match a hand-computed reference table.
- `tests/soak_harness/run_soak_webgui.py` — **reuse existing `--osc-sink-port` flag** (B3); already accepts an integer (line 595, default 9100). Passing `0` causes `OscSink.start()` to bind ephemerally because `socket.bind(("127.0.0.1", 0))` is standard POSIX behaviour. Add: after `sink.start()`, read `self.sock.getsockname()[1]` and store as `self.actual_port`; export to `report["osc_sink_port_actual"]` field (always present in report).
- `tests/soak_harness/test_soak_webgui_schema.py` — add `osc_sink_port_actual` to `REQUIRED_TOP_FIELDS` (line 35–51). Update the schema test invocation (lines 73–80) to pass `--osc-sink-port 0` so concurrent runs (or post-playwright runs) never collide on the fixed default `9100`.
- `vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp` and **`vst3/tests/soak_vst3_console_flood.cpp`** (B1-iter3 — corrected filename; ctest target name is `vst3_console_flood` per `vst3/tests/CMakeLists.txt:261`, binary is `soak_vst3_console_flood` per line 248) — add 3-line comment + `std::quick_exit(0);` immediately before the existing `return 0;` in `main()`. The comment explains: "ASan workaround — Steinberg SDK static dtor raises glibc SIGABRT (`munmap_chunk: invalid pointer`) before ASan's exit handler runs. `quick_exit(0)` skips static destruction; per-allocation ASan tracking during the test body is unaffected." The workaround applies in both default ctest invocations (the 5s mini-soak of `vst3_console_flood`) AND the SOAK_ON 60s lane (`SPATIAL_ENGINE_SOAK=ON ctest -R vst3_console_flood -V`).
- `vst3/tests/CMakeLists.txt` (lines 166–176 + 245–262) — NO ASan-suppression env var changes (B2 revision). Add a one-line comment pointing to `docs/CI_QUARANTINE.md` for the `quick_exit` rationale next to both `add_test()` calls.
- `docs/CI_QUARANTINE.md` (new, ~30 lines) — document the two ASan `quick_exit(0)` workarounds, the corrected root cause (glibc SIGABRT raised before ASan exit, not interceptable via `ASAN_OPTIONS=exitcode=0`), the corrected target names (`test_vst3_intra_plugin_spsc_drain` and `soak_vst3_console_flood` — the latter binary is registered as ctest test `vst3_console_flood`), and the v0.6 follow-up (attempt upstream Steinberg patch or vendor fork).

### API / wire surface
- `--osc-sink-port 0` (existing flag, new accepted value) — backward-compatible (default 9100 unchanged).
- `report["osc_sink_port_actual"]` (new field, always present).

### Tests added / modified
- The new + extended tests above ARE the deliverable.
- Full pytest invocation (`pytest tests/`) must pass clean across 3 consecutive runs.
- Full ctest invocation (`ctest --output-on-failure`) must pass clean (the OlaConvolver test count stays 1 file; the new KdTree test is +1; Q1 / Q2 / Q3 add the rest).

### Acceptance criteria
- `ctest -R ola_convolver_loadinto_capacity_violation_release` passes both cases (existing prepared-capacity + new `ir_len > kOlaMaxIRLength` boundary case).
- `ctest -R kdtree3d_8_canonical_directions` passes.
- `pytest tests/soak_harness/test_soak_webgui_schema.py -v` passes when run AFTER `pytest tests/webgui/` (the historic flake trigger). Verified by 3 consecutive runs.
- `ctest -R "vst3_intra_plugin_spsc_drain|vst3_console_flood"` passes under the `std::quick_exit(0)` workaround. (B1-iter3: ctest name `vst3_console_flood` corresponds to the `soak_vst3_console_flood` binary; default invocation is the 5s mini-soak.)
- Optional secondary verification: `SPATIAL_ENGINE_SOAK=ON ctest -R vst3_console_flood -V` (60s soak lane) also passes under the same workaround. (Not a v0.5.1 blocker, but documented in `docs/CI_QUARANTINE.md`.)
- `docs/CI_QUARANTINE.md` exists, references the glibc SIGABRT root cause and the v0.6 follow-up.

### Rollback story
Each item is independent — soak-port change is a 5-line patch, KDtree test is additive, OlaConvolver extension is additive, `quick_exit(0)` is a 3-line per-test revert. Quarantine doc can stay in place even on revert.

---

## Q5 (Deferred to v0.6) — Runtime Sticky-Underrun Auto-Demote

**Iter 3 status: DEFERRED to v0.6 (was a stretch phase in iter 2).** Critic correctly noted that iter 2's ME mechanism is unimplementable in the v0.5.1 VST3 deployment target: `XrunCounter::record_underrun()` is only called from `core/src/audio_io/NullBackend.cpp:125` and `core/src/audio_io/DanteBackend.cpp:155` — neither path runs inside the VST3 plugin process, so there is no underrun signal that can flip a `runtime_demoted_` atomic inside `BinauralMonitor`. A v0.6 design spike is required to either (a) add a wall-clock RT-safe underrun detector inside `BinauralMonitor::processBlockB1/B2` using `std::chrono::steady_clock` with documented per-OS precision caveats, or (b) route VST3 host-reported xruns into `XrunCounter` via a new VST3-side adapter. The `ambivs_demoted_runtime` warning code is intentionally NOT reserved in the v0.5.1 wire surface (it can be added cleanly in v0.6 without breaking back-compat). No code, tests, or risk-register entry are landed for this in v0.5.1.

---

## Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|------------|--------|------------|
| R1 | Q1 `sendReply` enqueue allocates on the audio thread (e.g. `std::string` copy) | Medium | High (breaks RT contract) | SPSC ring of fixed-size `char[256]` packet buffers pre-allocated at `prepareToPlay`. RT-alloc test must stay green. |
| R2 | Q2 crossfade window straddles a second `/sys/binaural_mode` flip → undefined sample landing | Medium | Medium | Audio-thread defers acting on a new `effective_mode_` until current ramp completes (`xfade_blocks_remaining_ == 0`). Test exercises double-flip explicitly. |
| R3 | Q3 active-path mute breaks an undocumented integrator who depended on the placeholder downmix | Low | Medium | `/sys/state.fallback_mode` field gives DAWs a non-warning channel to detect new behaviour; release-notes call-out; bypass-path downmix preserved (B7 split). |
| R4 | Q4 `quick_exit(0)` hides a future real bug in the ASan-quarantined tests' bodies | Low–Medium | Medium | `quick_exit` only skips end-of-process leak reporting; per-allocation ASan tracking during test body still flags bugs. Add a CI job that runs other ASan-built tests with normal exit to catch regressions in the larger suite. |
| ~~R5~~ | ~~Q5 underrun-detection false positives trigger spurious auto-demote~~ | — | — | **Iter 3: removed — Q5 deferred to v0.6.** |
| **R6** (MA) | DAW silent-track auto-collapse (Logic, Cubase) on muted bus 1 may downgrade/collapse the binaural track, exacerbating the "did I configure this wrong?" failure mode | Medium | Medium | (i) Prominent USER_GUIDE.md note (added in Q3 docs work); (ii) OSC `/sys/binaural_warning` is the primary signal channel — DAW integrators MUST subscribe per spec; (iii) `/sys/state.fallback_mode = "muted"` field for UIs. |
| **R7** (MB) | Q2's 2-block both-branches-active doubles binaural CPU. On probe-clamped (borderline) hardware this can underrun bus-0 via missed deadline | Medium | High | Selected mitigation: bound crossfade to 1 block when `probe_warning_set_ == true` (probe-clamped state). Trades a small residual click for headroom safety on the marginal-CPU path. **Iter 3 (MAJOR 1):** emit `/sys/binaural_warning ,s "xfade_truncated_cpu"` once per truncation event so the DAW can observe the trade-off. |
| **R8** (iter 3 WM-4) | Future v0.6+ client interprets `reply_port = 0` literally as port-0 rather than as "reply to source port" sentinel | Low | Medium | Documented in ADR-v051-01 Consequences (the "0 = reply to source" semantic is part of the v0.5.1 wire contract). v0.6 may introduce a named constant `kReplyToSource = 0` plus a receiver-side assertion that rejects a literal `bind(port=0)` derived from a handshake (vs an OS-assigned ephemeral source captured by `recvfrom`). |

---

## ADR Stub (fill on Critic APPROVE; tag policy = explicit user request only)

### ADR-v051-01: OSC Outbound Channel via Reused Inbound Socket (recvfrom + last-peer)

**Decision:** Add outbound OSC replies using the existing `OSCBackend` UDP socket. Migrate `recv()` → `recvfrom()` on the stub path (and the JUCE path when wired) to capture the sender's endpoint. Replies go to `last_peer_endpoint_`. Schema bump (additive): `PayloadSysHandshake` carries an optional `reply_port` (0 = same port as sender). (Option A from D1.)

**Drivers:**
1. DAW observability is the #1 release blocker — must ship in v0.5.1.
2. Single-DAW topology is the only supported v0.5.1 use case.
3. Schema bump is additive and zero-cost for legacy v0.4/v0.5 clients (default `reply_port = 0` preserves the implicit "reply to source" behaviour).

**Alternatives considered:**
- Dedicated outbound socket bound to `0.0.0.0:0` with configurable `reply_port` (D1 Option B) — rejected: ergonomic gain pays for permanent user-facing config surface and the same schema bump.
- Critic-raised separate outbound-only socket without schema bump (D1 Option C) — rejected: requires the same `recvfrom()` migration; eliminates a microsecond-scale race the SPSC drain already resolves; doubles socket lifecycle complexity for negligible benefit. Reconsider in v0.6 if a concrete failure scenario emerges.

**Why chosen:** Smallest patch surface that satisfies success criteria; single FD lifecycle; no new firewall hole; legacy clients unchanged.

**Consequences:**
- Future multi-subscriber topologies will need to migrate to Option B or C (planned for v0.7 if it materialises).
- The IO thread's bounded `sendto()` latency window is acceptable on loopback/LAN; document the SPSC drain ordering.
- **Iter 3 (MAJOR 4):** Under sustained outbound burst (>10 warnings/sec), inbound `recvfrom` may be starved by `sendto` head-of-line blocking on the shared FD. v0.5.1 mitigation: outbound warnings are one-shot-per-event (no repeated emission per warning state — `ambivs_disabled_cpu` fires once on probe-clamp, `no_sofa_loaded` fires once per `prepareToPlay`, `xfade_truncated_cpu` fires once per ramp arm). Reconsider Option C in v0.6 if telemetry frequency increases.
- **Iter 3 (WM-4 / R8):** `reply_port = 0` is interpreted as a "reply to source port" sentinel, NOT a literal port-0 bind. v0.6 may introduce a named constant `kReplyToSource = 0` plus a receiver-side guard.

**Follow-ups:**
- v0.6: revisit whether `/sys/binaural_status` should move to a separate higher-frequency channel.
- v0.6: introduce `kReplyToSource` named constant (R8 mitigation).
- v0.7 (conditional): multi-subscriber topology → reply-port migration.

### ADR-v051-02: SOFA-Missing Fallback = Mute + Warn (active path only; bypass keeps downmix)

**Decision:** When `engine_` is active and no SOFA is loaded, bus 1 is muted (zero L/R) and `/sys/binaural_warning ,s "no_sofa_loaded"` is emitted once per `prepareToPlay` lifetime. Under bypass, bus 1 retains the existing -6 dB downmix (preserves `test_v04_binaural_bus_placeholder` regression coverage and matches the bypass intent of "user still hears a recognisable signal"). (Option A from D2 + B7 split.)

**Drivers:**
1. The v0.5.0 active-path transparent-downmix behaviour is the bug this hotfix exists to fix.
2. "Fail loud over silent fallback" principle.
3. Bypass path must remain a recognisable monitoring fallback.

**Alternatives considered:**
- Keep downmix on the active path + `/sys/state.fallback_mode` (Option B) — rejected: restates the bug.
- Identity stereo bypass (L=in_L, R=in_R, gain 1.0) on the active path (Option C) — rejected for v0.5.1 because it re-introduces "you can't tell by listening" failure mode in milder form. Reconsider as a user-selectable fallback policy in v0.6.

**Why chosen:** Unambiguous user-facing signal (silence + warning); preserves bypass-path diagnostic intelligibility; B7 split keeps the `test_v04_binaural_bus_placeholder` regression test valid.

**Consequences:**
- First-time integrators who forget to load a SOFA hear silence on the active path; relies on documentation (USER_GUIDE.md, R6 mitigation) + OSC warning for diagnostics.
- R6 (DAW silent-track auto-collapse) is a known and documented trade-off.
- **Iter 3 (WM-3):** bypass-path regression gate is "within ≤ 1e-6f per-sample of v0.5.0 fixture", as enforced by the unchanged `test_v04_binaural_bus_placeholder.cpp` assertions (not strict bit-equality).

**Follow-ups:**
- v0.6: consider shipping a default fallback `.speh` (anechoic identity HRTF) instead of silence, configurable via env, OR a user-selectable fallback policy (mute / passthrough / downmix).

### ADR-v051-03: ASan Test Quarantine via `std::quick_exit(0)`

**Decision:** Add `std::quick_exit(0)` immediately before `return 0;` in the two ASan-flaky test mains: `test_vst3_intra_plugin_spsc_drain` AND `soak_vst3_console_flood` (binary name; ctest test name is `vst3_console_flood`, registered at `vst3/tests/CMakeLists.txt:261`). Document in `docs/CI_QUARANTINE.md`. (Option A from D3.) **Iter 3 (B1-iter3): target names corrected — earlier iterations called this `test_vst3_console_flood` which is not the actual binary or ctest name.**

**Drivers:**
1. CI fully green is a v0.5.1 release blocker (the `vst3_console_flood` 5s mini-soak runs in the default ctest invocation).
2. Root cause is upstream Steinberg SDK + glibc: Steinberg's static destructor triggers glibc's `__libc_message → abort()` with `munmap_chunk(): invalid pointer`, raised BEFORE ASan's exit handler runs.
3. `ASAN_OPTIONS=exitcode=0` is a real flag (Critic correctly caught Architect's earlier overclaim that the flag is fictional) but it controls ASan's *own* exit code — it does NOT intercept the SIGABRT raised from glibc inside the static dtor. So `exitcode=0` is the wrong tool here.

**Alternatives considered:**
- Per-test bash wrapper translating SIGABRT (D3 Option B) — rejected: fragile cross-OS shell, harder to audit.
- `ASAN_OPTIONS=exitcode=0` CTest property (iter-1 proposal) — rejected: technically the flag exists, but it does not intercept the glibc-raised SIGABRT, so it cannot solve the problem.

**Why chosen:** Surgical (3 lines per test), self-documenting, no CMake gymnastics, no shell wrapper, preserves per-allocation ASan tracking during test body. Workaround applies uniformly to the default 5s mini-soak AND the optional SOAK_ON 60s lane.

**Consequences:**
- These two tests no longer report end-of-process leaks; per-allocation tracking during the test body is unaffected.
- `docs/CI_QUARANTINE.md` becomes the canonical place to track this debt.

**Follow-ups:**
- v0.6: attempt upstream Steinberg SDK patch or vendor a fork that fixes the static-dtor double-free.

---

## Final Checklist (Planner self-audit before Architect handoff)

- [x] Plan saved to `.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md`.
- [x] 4 actionable phases (Q1–Q4); Q5 demoted to v0.6 deferral note (iter 3 B3-iter3).
- [x] ≥2 viable options for each material decision (D1: A/B/C; D2: A/B/C; D3: A/B), with bounded pros/cons and explicit invalidation rationale.
- [x] RALPLAN-DR summary block precedes phases; mode upgraded to capture iter-3 normalized cross-correlation gate.
- [x] Risk register present (R1–R4, R6–R8; R5 removed with Q5 deferral), each with mitigation.
- [x] ADR stubs present for all three material decisions; `/hb/pong` precedent claim removed in iter 2; iter-3 sustained-burst caveat (MAJOR 4) and bypass-path tolerance clarification (WM-3) added.
- [x] Bus 0 invariant explicitly called out as constraint AND acceptance criterion in Q2 / Q3.
- [x] No JUCE-required paths introduced; NO_JUCE build stays green; `recvfrom()` migration is gated on the same `SPE_HAVE_JUCE` switch as the existing `recv()`.
- [x] B2 -20 dBFS gate strengthening explicitly deferred to v0.6.
- [x] Hardware/DAW manual smoke explicitly out of scope.
- [x] BLOCKERs B1–B9 (iter 2) addressed in iter 2; iter-3 BLOCKERs B1-iter3, B2-iter3, B3-iter3 addressed here.
- [x] MAJORs MA–MG (iter 2) addressed in iter 2; iter-3 MAJORs 1–4 addressed here.
- [x] Architect minor flags B5/B9/B4-socket-count addressed in iter 3.
- [x] Critic "What's Missing" items WM-1 through WM-4 incorporated (RT-alloc audit note, legacy reply_port test, bypass tolerance clarification, R8 schema sentinel row).
- [x] Iter 3 grounded every Critic claim by repo evidence (CMake line numbers, source line numbers, `record_underrun` call-site enumeration).

## Open Questions

- **OQ1 (resolved, iter 2):** `/sys/binaural_status` is emitted at 1 Hz on the same IO-thread timer that drives `HeartbeatPublisher::tick()` (per MF wiring).
- **OQ2 (resolved, iter 2):** Q5 deferral confirmed — iter 3 promotes "stretch" to "v0.6 deferred" because the v0.5.1 VST3 process has no `record_underrun()` feed into `BinauralMonitor`.
- **OQ3 (resolved, iter 2):** Q3 user-facing documentation lives in BOTH `docs/USER_GUIDE.md` (one paragraph, R6 mitigation — required for the silent-track warning) AND the v0.5.1 release notes (full Q1–Q4 change summary).

(Open questions are fully resolved within this plan; nothing to append to `.omc/plans/open-questions.md` for v0.5.1. No new open questions introduced in iter 3.)
