# Architect Round-2 Strategic Review — v0.3.0 Sprint Plan

**Reviewer**: Architect agent (DELIBERATE mode)
**Plan under review**: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md` (668 lines, 8.4k words, 41 sections)
**Date**: 2026-05-11
**Mode**: DELIBERATE (pre-mortem present, cert-risk inherited, state-format ABI change)
**Verdict (TL;DR)**: **REVISE-AND-RESUBMIT**

## Summary

The v0.3 plan is structurally sound (ADR ratification table, 3-scenario pre-mortem, expanded test plan, 100% testable acceptance index), but it **silently re-introduces the A1-δ sidecar architecture that ADR 0010 explicitly rejected** (`docs/adr/0010-vst3-osc-binding-model.md:53,146-153`). The frozen contract (per the user brief and parent plan §1.5) is A1-ε direct per-instance recv-only UDP `bind()` inside the plugin process — no sidecar in the v0.3 Linux fast path. Yet S2 builds `bin/spatial_engine_sidecar.cpp`, S3 wires a plugin→sidecar SPSC over UDS DGRAM, and S4 routes the reverse path through that sidecar. This is not the "bridge" referenced in the brief; it is the architecture ADR 0010 §A1-δ rejected, just relabelled. Decisions D1, D2, and large parts of D3/D4 (and Pre-mortems 1 + 2) are downstream consequences of that error. Recommendation: collapse the sidecar binary out of the v0.3 Linux fast path, keep the JSON registry + RegistryPath + state v3 bump + DAW hands-on as the real v0.3 deliverables, and re-scope ETA from 9d to ~5d. If a sidecar truly is needed for the reverse-automation path (S4), that is a separate, smaller standalone-relay change in `spatial_engine_core` — not a new binary.

---

## 1. Verdict

**REVISE-AND-RESUBMIT.**

Reasoning: the plan as written contradicts an inherited frozen contract (Principle 5 inheritance from v0.2 ralplan: "A1-ε direct bind, no sidecar in v0.3 Linux fast path" — verbatim in `docs/adr/0010-vst3-osc-binding-model.md:250`). DELIBERATE mode cannot ratify a plan whose Decision Points D1+D2 re-litigate a frozen axis. The cleanup is mechanical (delete sidecar binary scope, retain registry + state v3 + DAW hands-on) and once applied, the plan is a strong APPROVE-with-revisions candidate.

---

## 2. Steelman antithesis — D1 (separate sidecar binary)

**Plan recommends**: D1-α — separate `bin/spatial_engine_sidecar.cpp` executable.

**The strongest counter-position is not D1-β (subcommand). It is "no sidecar at all in v0.3 Linux fast path."** Steelman:

1. **The frozen contract already eliminated the sidecar.** ADR 0010 §A1 (line 53) names A1-δ explicitly: *"A1-δ (sidecar binary + UDS) was the Round-1 recommendation but rejected post-Architect §6 synthesis: ~7 days of new infra to mitigate a cert risk that does not materialize on the v0.2/v0.3 Linux-only target."* The Follow-ups section (line 250) frames the registry GC as *"written + GC'd by sidecar (or by plugin under A5-α direct bind, **no sidecar in v0.3 Linux fast path**)"* — explicit. The plan's §1.4 says "A1-ε per-instance recv-only UDP socket" is inherited and frozen, then proceeds to build a sidecar anyway.

2. **The functional need the sidecar claims to serve doesn't exist.** The plan justifies the sidecar with three jobs:
   - **Registry GC loop**: ADR 0011 §3 (line 116 onward) puts GC on the *writer side* — i.e. each plugin instance on its own startup/shutdown, with `/proc/{pid}/comm` liveness check. No GC daemon is required. The "5s GC cadence" the plan introduces in S2 is invention, not inheritance.
   - **Reverse-automation path (S4)**: this is the only real reverse-path requirement. But the natural owner is `spatial_engine_core` standalone (which already binds 9100, already decodes ADM-OSC, already has the registry path read access). Standalone reads the registry, opens one UDS connection (or just `sendto` to the plugin's bound port — A1-ε exposes one!) per instance, and forwards. That's ~80 LOC in standalone, not a new binary.
   - **OOM-recovery isolation (Pre-mortem 1)**: the entire pre-mortem only exists because the plan introduces the sidecar. Without the sidecar, there is no UDS peer to lose, no `EPIPE` to handle, no systemd unit to ship.

3. **The cost is real.** A new binary means: new `add_executable` target; new install/packaging path (deb, RPM, AppImage all separate); new systemd unit shipped; manual Ch.5 must document a 4-process topology (Pre-mortem 1 mitigation 3); LD_DEBUG audit on a second binary; OFF baseline scope expands (or, per the plan's hedge, "stays the same if all v0.3 surface stays in `vst3/`+`bin/`" — meaning the plan itself isn't sure). All to deliver a feature that ADR 0010 explicitly says is unnecessary for the v0.3 Linux target.

4. **The "process isolation" argument is circular.** D1-α's stated benefit (process boundary) is mitigation for a failure mode (Pre-mortem 1) the sidecar itself creates. Without the sidecar, the plugin's dedicated UDP thread is the only OSC surface; if it dies, only that one plugin instance loses OSC, and the host already handles plugin crashes. The sidecar makes failure *worse*, not better, because now one process death affects N plugin instances.

**Conclusion**: D1-α and D1-β are both wrong. The right answer is **D1-γ (no sidecar)**, which is the answer ADR 0010 already picked. The plan reopened a frozen axis.

---

## 3. Tradeoff tensions surfaced

### 3.1 D2 UDS DGRAM tension (planner-acknowledged but understated)

If the sidecar is kept (against my §2 steelman), D2-α UDS DGRAM at 64 obj × 1 kHz costs ~64 syscalls/ms = ~64k/s `sendmsg` + `recvmsg` round-trips through the kernel + context switches. The plan's hand-wave at line 109 ("UDS DGRAM at ~1µs/message is more than fast enough") understates this: `sendmsg` on Linux UDS DGRAM with small payload is ~1.5–3µs uncontended, plus scheduler wake-up latency on the receive side which is ~5–15µs under typical CFS scheduling, before the audio callback even sees the message. The end-to-end p99 target (5ms) probably holds, but the headroom is thinner than the plan implies, and the soak harness must actually instrument this. **This tension disappears entirely if you delete the sidecar (§2): plugin's own dedicated thread `recv()`s directly from the UDP socket, zero IPC layer.**

### 3.2 S2 + S3 paired ETA (4d) parallelisability claim

§4 "Parallelisation opportunities" claims S4 + S3 can be implemented concurrently after S2 lands. That is **only true if** S2's registry library API is locked before S3 starts, and if the SPSC ring header (`core/src/util/SpscRing.h`) is independent of S2's `PluginInstanceRegistry`. The plan does not enforce an interface-freeze step between S2 and S3. In practice this means whichever engineer picks S3 first will fork the registry API mid-stream and the merge will be painful. **Mitigation**: insert an explicit "S2.5 (0.25d) — API headers frozen and reviewed; downstream steps may begin" gate between S2 and S3/S4.

### 3.3 D3-β late v3 vs net-debt of v2 backward-compat code

The plan picks D3-β (state v3 lands in S7). Tension: every day v3 is deferred, the v2 reader path stays as load-bearing code in `Processor.cpp:266-283`. With v3 finally landing in S7 (1 day before S8 DAW hands-on), there is **zero soak time on the v3 reader between merge and customer**. Pre-mortem 3 ("v3 reader bug breaks v0.2 preset interop") is exactly this risk, and the plan's mitigation (`test_vst3_state_v3_persist.cpp` + S8 step 6) all run on day-1-of-v3-existing. The β rationale ("decouple v3 from ADM-OSC plumbing for debugging clarity") is correct, but the cost is unmitigated.

**Synthesis**: split the difference. State v3 *reader-only* lands at start of sprint (S2.5, 0.5d), so the 3-way reader fork gets all 9 days of soak. State v3 *writer* + kMute parameter wiring lands in S7 as planned. This costs no debugging clarity (writer is what produces v3 bytes; reader path is purely defensive backward-compat) and gains 9 days of v2-fixture regression exposure. **Concrete change**: S7's `vst3/SpatialEngineProcessor.cpp:201-323` modification splits into two commits, reader at S2.5 and writer+kMute at S7.

### 3.4 S6 soak coverage — 60s is not enough for sidecar memory growth

If sidecar stays (against §2): a 60s soak is *C3's* spec for standalone soak (idempotent steady-state). Sidecar adds a long-running daemon with per-instance state; classic memory-leak signatures (per-connection malloc not freed on `EPIPE`, growing `std::vector<InstanceEntry>` from registry rescans, accumulated metric counters) typically show at the 5–30 min mark, not 60s. The plan's mitigation 5 ("sidecar memory steady-state, no growth >5% over 60s") is too short. **Concrete fix (only if sidecar kept)**: extend `soak_vst3_console_flood` to a 5-minute run (300s) for the sidecar-memory column, while keeping 60s for the audio-thread-alloc column. Total ctest runtime goes from ~60s to ~300s, still inside a single GHA job. **If §2 steelman applied**: this tension dissolves; no daemon = no long-running memory growth surface.

### 3.5 Critical-path single-point-of-failure (D4 lab booking)

D4-α schedules a real-vendor capture session 60 days post-tag, with the explicit "if missed, escalate to user" recovery. This is fine as written **except** the plan never names a fallback action if the lab booking falls through. Real venues cancel for live-event reasons (the customer's own show takes priority). The ADR 0012 slot needs a slack path: synthetic-fixture-continuance with explicit, dated "real capture deferred to day-90" follow-up commit. **Concrete fix**: add to D4-α's invalidation block: *"If lab session cancels within 7 days of booked date, ADR 0012 commit at day-60 is a 'no-quirk-observed: synthetic fixture extension to day-90' note, and Notion task auto-files for day-90 re-booking."*

---

## 4. Synthesis / proposed modifications (concrete)

### 4.1 Mandatory: align plan with ADR 0010 frozen contract

**Delete from scope** (S2/S3/S4/S5/S6):
- `bin/spatial_engine_sidecar.cpp` (new binary)
- `vst3/sidecar_bridge/UdsServer.{h,cpp}` (sidecar UDS server)
- `vst3/sidecar_bridge/PluginToSidecarChannel.{h,cpp}` (plugin→sidecar SPSC over UDS)
- `vst3/sidecar_bridge/AutomationReflect.{h,cpp}` (sidecar reverse path)
- `vst3/sidecar_bridge/ControllerReverseHandler.{h,cpp}` (separate from notify reversal — see below)
- `tools/systemd/spatial-engine-sidecar.service`
- `vst3/tests/test_vst3_sidecar_dispatch.cpp`
- `vst3/tests/test_vst3_sidecar_reverse_path.cpp`
- `vst3/tests/test_vst3_sidecar_oom_recovery.cpp`
- `vst3/tests/test_vst3_handshake_protocol.cpp`
- `vst3/tests/perf/soak_vst3_storm.cpp` (the storm test was designed to stress the UDS ring; without it the test is duplicate of standalone C3 soak)

**Keep** (rename out of `sidecar_bridge/` subdirectory):
- `vst3/sidecar_bridge/PluginInstanceRegistry.{h,cpp}` → rename to `vst3/osc/PluginInstanceRegistry.{h,cpp}` (writer used by plugin, reader used by standalone — no sidecar needed). The "sidecar_bridge" subdirectory name itself is a misnomer given ADR 0010 §A1-ε.
- `core/src/util/RegistryPath.h` — shared constant, useful regardless.
- `core/src/util/SpscRing.h` — still useful: plugin's *internal* per-instance SPSC ring (dedicated UDP thread → audio callback) is precisely what ADR 0010 §A4-β line 80–82 describes.
- `vst3/tests/test_vst3_state_v3_persist.cpp` — D3-β bump, modified per §3.3 synthesis.
- `vst3/tests/test_vst3_registry_stale_cleanup.cpp` — exercises writer-side GC at plugin shutdown.
- `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` — now a 3-process e2e (console UDP → plugin direct, no sidecar hop).
- `vst3/tests/perf/soak_vst3_console_flood.cpp` — same 1 obj × 100 Hz × 60s × 8 instances; without sidecar hop, p99 target tightens to `< 2 ms` (matches ADR 0003 IPC budget).

### 4.2 Reverse-automation path (S4) — concrete redesign

S4's real requirement: console sends `/adm/obj/0/azim 90.0` to standalone:9100; the value must land in the DAW's automation lane for plugin instance whose `obj_id_subset` includes obj 0.

Without a sidecar, the path becomes:

1. `core/src/bin/spatial_engine_core.cpp` standalone's existing OSC dispatch (the line referenced by the plan at line 99,134) — augment with a "broadcast to plugin registry" hook. On each decoded `Command`, look up the plugin instances in `~/.config/spatial_engine/instances.json` whose `obj_id_subset` covers the command's obj_id, and `sendto()` the encoded packet to each instance's registered port.
2. Plugin's dedicated UDP thread already receives this (it's an A1-ε recv-only socket per ADR 0010). The thread decodes, pushes to its per-instance SPSC ring, audio callback drains.
3. For the DAW-automation-lane reflection (so the host records the new value), the plugin's UDP thread (which can allocate freely per ADR 0010 §A4-β) calls `IComponentHandler::performEdit(paramId, value)` via the existing Controller's componentHandler pointer. This is the `notify` reversal mentioned at `vst3/SpatialEngineController.cpp` (currently `kNotImplemented` per AM-R3-10) — but it's a *plugin-internal* dispatch, not a sidecar-driven one. No new file `ControllerReverseHandler.cpp` needed; the Controller's existing `notify` impl is replaced with one that reads from a shared per-instance command queue populated by the UDP thread.

**This is ~150 LOC of additions to `core/src/bin/spatial_engine_core.cpp` + ~80 LOC to `vst3/SpatialEngineController.cpp`, in lieu of ~6 new sidecar files + 5 new tests. Net delta: −1.5 sprint-days.**

### 4.3 Revised step list and ETA (with §2 + §4.1 + §4.2 applied)

| Step | Old ETA | New ETA | Notes |
| ---- | ------- | ------- | ----- |
| S1 (ratification) | 0.5d | 0.5d | unchanged |
| S2 (registry + RegistryPath + plugin UDP thread + SPSC) | 2.0d | 1.5d | sidecar deleted; plugin owns UDP recv directly per ADR 0010 §A4-β |
| S2.5 (interface freeze + state v3 *reader only*, §3.2 + §3.3) | — | 0.5d | new gate |
| S3 (plugin internal SPSC + audio drain) | 2.0d | 0.5d | reduces to internal SPSC ring only (audio thread pops; UDP thread pushes); no UDS hop |
| S4 (standalone→plugin direct relay + `notify` reversal) | 1.5d | 1.0d | §4.2 redesign |
| S5 (manual Ch.5, simplified to 3-process topology not 4) | 0.5d | 0.5d | unchanged effort, simpler diagram |
| S6 (e2e + soak; storm test deleted) | 1.5d | 1.0d | one fewer soak, tighter p99 target |
| S7 (OFF re-pin + state v3 *writer* + kMute param) | 0.5d | 0.5d | unchanged |
| S8 (DAW hands-on) | 1.0d | 1.0d | unchanged |
| **Total** | **9.0d** | **~6.5d** | freed 2.5d of slack |

Cert-eval slack remains +3-4d → **wall ETA ~9.5-10.5d** (was 12-13d), still inside single sprint with healthy buffer.

### 4.4 Pre-mortem revision

- **Pre-mortem 1 (sidecar OOM)**: delete entirely (no sidecar to OOM-kill).
- **Pre-mortem 2 (SPSC overrun at 64 obj × 1 kHz storm)**: scope reduced. The remaining surface is the plugin's *internal* SPSC ring between its own UDP thread and its own audio callback. Backpressure is now bounded by `recv()` blocking on the socket if the ring is full, not by `EAGAIN` on a UDS hop. Still worth testing (the audio thread can fall behind block-rate drains), but the test profile changes — keep `soak_vst3_console_flood`, delete `soak_vst3_storm`.
- **Pre-mortem 3 (state v3 reader bug)**: now further mitigated because §3.3 synthesis lands the reader on day 1 of the sprint, not day 8.
- **Add new Pre-mortem 4**: "ADR 0011 file-based registry race: two plugin instances start simultaneously on a fresh boot; both call `flock(LOCK_EX | LOCK_NB)` at the same moment; ADR 0011 specifies 10-retry × 50ms backoff. If two instances hit the worst case (10 retries × 50ms = 500ms), the second plugin's ctor is blocked half a second on the audio thread — wait, **ctor is NOT on the audio thread**, it's on the host-managed initialization thread, so this is just a startup-latency UX issue, not RT. Document and move on. *But add a test for it.*"

---

## 5. Principle violation flags (DELIBERATE-mode required)

Reviewing the five inherited Principles (`spatial-engine-v0.3.md:30-34`):

| Principle | Status under current plan | Status after §4 revision |
| --------- | ------------------------- | ------------------------- |
| **1. JUCE-free** | Compliant (plan calls out `grep -r '#include.*juce'` in S2/A.3). | Compliant. |
| **2. ctest + pytest stays green** | At-risk: 11 new tests + 2 soaks land all at once at S6. Flake risk on first GHA run is high. Plan §7.4 has a "flaky test blocks tag" clause but no smoke-stage gate before S6. | Reduced risk: 6 new tests, distributed earlier (S2.5 + S3 + S6). |
| **3. OFF byte-baseline dual-gate** | **AT RISK (yellow flag)**. S7 says "re-pin if drift; expected: minimal change, possibly no-op". The plan does not specify *which file* in `core/` would trigger drift. Looking at the new-files list, `core/src/util/RegistryPath.h` (header-only constant) does NOT enter `libspe_core.a`/`spatial_engine_core` link sets if used only by `vst3/` + `bin/spatial_engine_sidecar.cpp`. **However**, `core/CMakeLists.txt` adds a new `add_executable(spatial_engine_sidecar ...)` target gated by `SPATIAL_ENGINE_VST3_OSC=ON` — but if the option default is OFF and gating is correctly conditional, OFF baseline is safe. The plan should *explicitly assert* this in S7's acceptance criteria, not "possibly no-op". | Safe: with §4.1 deletions, the only `core/` change is `core/src/util/RegistryPath.h` (header-only, not entering OFF link set) and `core/src/util/SpscRing.h` (likewise). No new `core/` executables. Explicit assertion in S7. |
| **4. ADM-OSC v1.0 spec verbatim** | Compliant (plan-internal reuse of `CommandDecoder.cpp:317-373`). | Compliant. |
| **5. v0.2.0 wire-ABI + state-ABI preserved** | **AT RISK (yellow flag)**. v2 → v3 reader fork lands in S7 (day 8 of 9). Zero soak time before customer DAW hands-on (S8). Pre-mortem 3 mitigation rests on a single test file landing on the same day as the customer-facing test. | Safe: §3.3 synthesis puts the v3 *reader* at S2.5 (day 1), giving 8 days of soak before customer hands-on. v3 *writer* still lands at S7 as planned. |

**Net**: plan-as-written has 2 yellow flags (Principles 3 + 5). With §4 revisions, both are resolved.

---

## 6. Open question coverage

### 6.1 Architect concurrence / corrections on Planner's 7 questions

| Q | Planner stance | Architect verdict |
| - | -------------- | ----------------- |
| **v03-Q1 (D1 sidecar binary build target)** | D1-α separate exe | **REJECTED. Right answer is D1-γ "no sidecar in v0.3 Linux fast path"** per ADR 0010 §A1-ε. See §2 steelman. |
| **v03-Q2 (D2 SPSC channel medium)** | D2-α UDS DGRAM | **Moot question.** With §2 applied, there is no plugin↔sidecar channel. The remaining SPSC ring is *intra-process* between plugin's UDP thread and its audio callback — D2 doesn't apply. |
| **v03-Q3 (D3 state v3 migration timing)** | D3-β late S7 | **REVISE per §3.3 synthesis.** Reader at S2.5, writer at S7. Hybrid is strictly better than either pure α or pure β. |
| **v03-Q4 (D4 vendor-quirks capture)** | D4-α hard 60-day | **APPROVE with §3.5 addition**: cancellation fallback wording. |
| **v03-Q5 (D5 macOS/Windows CI)** | D5-β defer to v0.4 | **APPROVE.** Strongest decision in the plan; CI matrix expansion is the right v0.4 entry-point and pairs naturally with A5-β contingency activation. |
| **v03-Q6 (systemd dependency)** | flagged, no decision | **Moot question with §2 applied.** No systemd unit needed without sidecar. |
| **v03-Q7 (A1-δ contingency activation criteria)** | flagged, no decision | **Genuine open question, retain.** Belongs in v0.4 planning, not v0.3. Suggest concrete deferral: "Activation criterion = single Apple/Steinberg/Avid forum thread OR cert-eval rejection citing `bind()` as cause." |

### 6.2 Architect-flagged questions Planner missed

- **v03-Q8 (Architect-new): `IComponentHandler::performEdit` thread-safety.** S4 (per §4.2 redesign) has the plugin's UDP thread calling `IComponentHandler::performEdit(paramId, value)`. VST3 SDK conventionally requires `performEdit` from the message thread, not arbitrary threads. The Controller must marshal the call via `IRunLoop` or via the host's UI-thread post mechanism. The plan does not address this. Concrete check needed: `pluginterfaces/vst/ivsteditcontroller.h` thread-safety doc, plus AM-R3-10's original rationale for `kNotImplemented` to confirm reversal is safe under all hosts. **Blocker if unresolved before S4 implementation.**
- **v03-Q9 (Architect-new): registry stale entries on power-loss.** ADR 0011 §3 mandates writer-side GC at startup using `/proc/{pid}/comm`. But on a hard power-loss / kernel panic, no writer ran cleanly. Next boot, the registry file contains stale entries with PIDs that may now belong to unrelated processes. `/proc/{pid}/comm` could match (e.g. another long-running process named `reaper` happens to inherit a stale PID). False-positive liveness is real. Mitigation candidates: (a) include `boot_id` (`/proc/sys/kernel/random/boot_id`) in registry entries and treat entries with stale boot_id as GC-eligible regardless of PID match; (b) include process start-time (`/proc/{pid}/stat` field 22 `starttime`) alongside PID. ADR 0011 line 243-244 acknowledges this risk ("MAY cause false-positive liveness") but does not mandate a fix. **S2 should pick one mitigation.**
- **v03-Q10 (Architect-new): `XDG_CONFIG_HOME` empty-string semantics.** Per XDG spec, if `XDG_CONFIG_HOME` is *set but empty*, fallback is `~/.config`. The plan's `RegistryPath.h` must handle this; otherwise plugin instances in some shells (containers, locked-down corporate workstations) will silently write to `./instances.json` in the host's cwd. **Quick fix, but mention it before S2.**

### 6.3 Planner-listed questions that are non-blocking

- v03-Q6 (systemd): moot with §2.
- v03-Q7 (A1-δ contingency): deferrable to v0.4 planning, not v0.3.

---

## 7. Acceptance criteria audit (A.3–A.15, §10 of the plan)

| ID | Criterion | Verdict | Issue / sharpening required |
| -- | --------- | ------- | --------------------------- |
| **A.3** | Sidecar binary builds + 0 JUCE includes | **Delete if §2 applied.** Replace with "plugin UDP thread compiled under SPATIAL_ENGINE_VST3_OSC=ON, 0 JUCE includes in `vst3/osc/` (renamed sidecar_bridge/)". |
| **A.4** | Byte-identical OFF baseline | **Sharpen.** Currently checks SHA256 against `.ci/off_baseline.{bytes,symbols}.sha256`. Need to also assert that the new `core/src/util/{RegistryPath,SpscRing}.h` headers do NOT enter `libspe_core.a` symbol table (they're header-only, but include guard regressions could leak). Add: `nm libspe_core.a \| grep -E 'RegistryPath\|SpscRing'` returns 0. |
| **A.5** | `test_vst3_sidecar_registry` PASS | **Rename** to `test_vst3_osc_registry` if §2 applied. Test content (atomic write, GC, PID liveness, schema_version) is correct and well-defined. **Sharpen**: add v03-Q9 mitigation test (stale-PID-after-reboot scenario). |
| **A.6** | 1000-iter dispatch, alloc==0 | **Testable** as written. Sharpening: explicitly state which thread is the "audio thread" in the fake harness (currently ambiguous — is it the per-instance UDP thread that pushes, or the audio callback that pops?). The RT_ASSERT_NO_ALLOC must apply to the *pop* side (audio callback), not the *push* side (UDP thread which is allowed to alloc per ADR 0010 §A4-β). |
| **A.7** | Reverse-path latency <100ms | **Testable**, but the 100ms threshold is loose. ADR 0010 §Drivers + the parent plan's "p99 < 5ms" soak target suggest reverse-path should also be sub-10ms. Tighten to `p99 < 50ms` or document why reverse path is allowed 20× the forward-path budget. |
| **A.8** | Manual Ch.5 with topology + TOC | **Testable** (markdown render + grep). No issue. |
| **A.9** | e2e console UDP → plugin within 100ms | **Same 100ms looseness as A.7.** With §4.2 redesign (no sidecar hop), tighten to `< 30ms` (one network roundtrip + one block-rate drain @ 48k/64). |
| **A.10** | `soak_vst3_console_flood` PASS — alloc==0, p99 <5ms | **Testable** as written. With §3.4 applied, extend duration to 300s for sidecar-memory growth column (or delete if sidecar deleted per §2; then 60s suffices). |
| **A.11** | OFF byte-baseline preserved or re-pinned | **Vague**: "preserved OR re-pinned cleanly" with no decision criterion stated up-front. Sharpen: "**MUST be preserved.** Re-pin only permitted if a paired core/ change is explicitly listed in this section. Currently no core/ changes are expected; any drift is a CI fail." |
| **A.12** | DAW hands-on log committed; 8 params verified | **Testable** but the "screenshots / capture logs" deliverable is fuzzy. Sharpen: "minimum 1 screenshot per host showing all 8 params in plugin UI; 1 WAV recording per host showing kMute audible distinction from kBypass; checklist Y/N entries for each numbered step." |
| **A.13 (new)** | Sidecar OOM recovery within 10s | **Delete if §2 applied.** Otherwise: testable, but "within 10s" needs a percentile (worst-case? p99?). |
| **A.14 (new)** | 64 obj × 1 kHz storm tolerated | **Delete if §2 applied** (storm test deletion). Otherwise: testable but `drop_count metric matches kernel-reported overrun count` is hard to assert deterministically because kernel overrun counters are timing-dependent. Sharpen: "drop_count is monotonic, non-zero under storm, and bounded by `(packets_sent - blocks_processed × block_size)`." |
| **A.15 (new)** | v0.2 preset loads cleanly | **Testable**, this is the strongest of the new criteria. Sharpening: explicit fixture file paths needed (`vst3/tests/fixtures/v02_preset_*.vstpreset` — plan §11 lists these but they don't exist in repo yet; mention that S7 creates them as part of the test, snapshotted from a v0.2.0-built plugin save). |

**Testability score**: 14/15 testable as written or with light sharpening. A.11 ("preserved OR re-pinned") is the only structurally vague item — needs binary criterion.

---

## 8. Top-3 highest-impact concerns ranked

1. **[CRITICAL — blocks plan acceptance]** ADR 0010 frozen-contract violation. The plan re-introduces the A1-δ sidecar architecture that ADR 0010 §A1 (line 53, 146-153) and Follow-ups (line 250) explicitly rejected. The user brief restates A1-ε as the frozen axis. Resolution: apply §4.1 deletions; collapse 2.5 sprint-days; clarify that "bridge" in S4 is a standalone-internal relay, not a new binary. Without this fix, every downstream Decision (D1, D2, parts of D3, all of Pre-mortems 1+2) is built on a phantom architecture.

2. **[MAJOR — Principle 5 risk]** State v3 reader lands on day 8 of 9. Customer-impacting Pre-mortem 3 has zero soak time. Resolution: §3.3 synthesis splits v3 reader (S2.5, day 1) from v3 writer + kMute (S7, day 8). Reader gets 8 days of soak before DAW hands-on; writer's blast radius stays isolated as the planner intended for D3-β. No new tests, no extra effort — just splitting one commit into two.

3. **[MAJOR — undefined VST3 SDK thread contract]** S4's reverse path implies cross-thread `IComponentHandler::performEdit` calls from a non-message thread. The plan does not address SDK thread-safety. AM-R3-10's original `kNotImplemented` was likely defensive cover for exactly this concern. Resolution: blocker on S4 entry — read `pluginterfaces/vst/ivsteditcontroller.h` thread-safety doc, decide whether to marshal via `IRunLoop`, post to a host message-thread queue, or restrict reverse path to read-only state propagation (controller reads from atomic, doesn't call performEdit). Document in `vst3/SpatialEngineController.cpp` notify-reversal commit footer.

---

## Consensus Addendum (ralplan review)

- **Antithesis (steelman)**: The strongest counter-position is "no sidecar at all in v0.3 Linux fast path" — and it's not even Architect-novel; ADR 0010 already adopted it. The plan reopens the frozen axis. See §2.
- **Tradeoff tension**: D3-β late v3 trades debugging clarity for zero soak time on a customer-impacting reader path. The synthesis at §3.3 dissolves the tension (reader early, writer late).
- **Synthesis (viable)**: §4 in full. Net effect: −2.5 sprint-days, +0 risk, alignment with frozen contract restored.
- **Principle violations (DELIBERATE mode)**: Principles 3 + 5 are yellow-flag at-risk in the current plan; both resolve under §4. No principle is structurally violated (i.e. no JUCE includes, no ADM-OSC schema drift), but Principle 5 timing creates a near-miss on the customer-facing preset interop story.

---

## References

- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:30-34` — Five inherited Principles
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:53` — A1-ε frozen claim
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:79-114` — D1, D2 decision blocks (root of the violation)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:178-200` — S2 sidecar files list
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:370-446` — Pre-mortems 1+2 (downstream of sidecar)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:574-590` — Acceptance criteria index
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:40-53` — A1-ε decision, A1-δ rejection
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:76-93` — A4-β thread model: dedicated thread *inside plugin*, SPSC ring drained by audio callback
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:146-153` — A1-δ rejection rationale ("~7 days of new infra")
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:246-260` — Follow-ups: explicit "no sidecar in v0.3 Linux fast path"
- `/home/seung/mmhoa/spatial_engine/docs/adr/0011-vst3-osc-multi-instance-discovery.md:99-138` — Writer-side GC, no daemon needed
- `/home/seung/mmhoa/spatial_engine/docs/adr/0011-vst3-osc-multi-instance-discovery.md:243-244` — Stale-PID false-positive risk (v03-Q9)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:32-34` — Plugin ctor extension point (A1-ε bind here, no UDS)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:201-323` — State reader/writer (D3 split point)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:465-466` — Existing `// SPSC ring not needed` comment (the new SPSC is per-instance UDP→audio, distinct from this audio-thread direct-dispatch)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:560-579` — `notify` reversal site for §4.2 redesign
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:173-178` — Parent-plan frozen-contract table for A1/A2/A3/A4/A5

