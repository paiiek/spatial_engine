# Critic Round-2 Quality Evaluation — v0.3.0 Sprint Plan

**Reviewer**: Critic agent (DELIBERATE mode, RALPLAN consensus contract enforcement)
**Plan under review**: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md` (668 lines, 5 Principles, 5 Decision Points, 3-scenario Pre-mortem, 15-item acceptance index)
**Co-reviewer (prior)**: Architect Round-2 review at `/home/seung/mmhoa/spatial_engine/.omc/plans/architect-r-v0.3-review.md` — verdict REVISE-AND-RESUBMIT
**Frozen-contract ground truth**: ADR 0010 §A1 (line 53 + line 250 verbatim), ADR 0011, ADR 0012, parent plan `spatial-engine-phaseC4-and-v0.2-release.md` §1.5 (lines 164–187)
**Date**: 2026-05-11
**Mode**: DELIBERATE (cert-risk inherited, state-ABI bump, expanded test plan + pre-mortem required)

---

## 1. Verdict

**ITERATE.**

(One word, per output spec. Single word verdict.)

---

## 2. One-paragraph summary

The v0.3 plan is structurally well-formed (5 principles, 5 decision points each with ≥2 options, 3-scenario pre-mortem with detection/mitigation/recovery, 15 acceptance criteria, ADR ratification table, observability section, soak coverage), but it commits the single most serious failure DELIBERATE-mode is designed to catch: it silently re-litigates a frozen axis. ADR 0010 §A1 explicitly REJECTS A1-δ ("sidecar binary + UDS") and freezes A1-ε ("per-instance recv-only UDP bind directly inside plugin process, no sidecar in v0.3 Linux fast path") for v0.3. The parent plan §1.5 freeze table line 173 restates this. The plan's own §1.4 line 53 even lists "A1-ε per-instance recv-only UDP socket" under *inherited as fixed*. Yet S2 builds `bin/spatial_engine_sidecar.cpp`, S3 routes plugin→sidecar through UDS DGRAM (D2-α), S4 routes the reverse path through that sidecar, two of three Pre-mortems exist only because the sidecar exists, and D1/D2's entire option-space is "which kind of sidecar?" rather than "do we need a sidecar?" — the same A1-α-vs-A1-β straw-man pattern ADR 0010 already adjudicated. This is a self-contradicting plan: it cites A1-ε as inherited, then designs A1-δ. Architect's CRITICAL finding is correct and not rebuttable from the codebase. Verdict is ITERATE (not REJECT) because the cleanup is mechanical and the non-sidecar-dependent deliverables — JSON registry hardening, state v3 bump + kMute, manual Ch.5, OFF re-pin, DAW hands-on, vendor-quirks capture schedule — are sound and worth preserving in a Round-3 revision.

---

## 3. Per-criterion findings (the 7 enforcement points)

### 3.1 Principle-option consistency — **FAIL**

Every recommended option in a DELIBERATE plan must be derivable from a Principle or a frozen design axis. Here the chain breaks at the root:

- Plan §1.4 line 53 lists `**A1-ε** per-instance recv-only UDP socket` under the heading *"Inherited as fixed from parent plan §1.5 freeze (no re-litigation)"*.
- Plan §2 D1 (line 81–96) then recommends `D1-α (separate spatial_engine_sidecar executable)` — which is the structure of A1-δ, the explicitly REJECTED option per ADR 0010 §A1 line 53 verbatim: *"A1-δ (sidecar binary + UDS) was the Round-1 recommendation but rejected post-Architect §6 synthesis."*
- ADR 0010 line 250 verbatim: *"written + GC'd by sidecar (or by plugin under A5-α direct bind, **no sidecar in v0.3 Linux fast path**)"*. v0.3 is A5-α. Plan picks the sidecar branch anyway.
- D2 (SPSC channel medium) only exists as a question because D1 picked α; under the *frozen* A1-ε, the SPSC ring is intra-process (UDP thread → audio callback), so D2 is moot.
- The plan justifies the sidecar with "DAW-automation reverse path (S4) and registry GC (S2)" (line 83), but:
  - ADR 0011 §3 places registry GC on the writer side (each plugin instance on its own startup/shutdown) — no GC daemon needed. The "5s GC cadence" introduced in S2 line 183 is invention, not inheritance.
  - The reverse path's natural owner under A1-ε is `spatial_engine_core` standalone (which already binds 9100, decodes ADM-OSC, and has registry read access) — not a new binary.

**Conclusion**: D1, D2, and large portions of D3/D4 and Pre-mortems 1+2 are downstream consequences of contradicting the inherited A1-ε frozen axis. Principle-option chain is broken at the root.

### 3.2 Fair alternatives (≥2 steelmanned options + invalidation rationale) — **PARTIAL FAIL**

- **D1**: presents two options (D1-α exe vs D1-β subcommand). The eliminated option's invalidation rationale is explicit ("OFF byte-baseline drift + Pre-mortem 1 mitigation"). However, the option space is **wrong**: it should include "D1-γ no sidecar" (the actual frozen answer per ADR 0010). The presented two options are both downstream of an unstated A1-δ assumption that contradicts the inherited freeze. This is the textbook "false dichotomy" anti-pattern that DELIBERATE mode is designed to catch.
- **D2**: D2-α (UDS DGRAM) vs D2-β (shm+futex) — both steelmanned, invalidation explicit. But moot under §3.1.
- **D3**: D3-α (early v3) vs D3-β (late v3) — both steelmanned, invalidation explicit ("debugging-blast-radius"). Architect §3.3 surfaced a hybrid (reader at S2.5, writer at S7) the plan did not consider; this is a real gap.
- **D4**: D4-α (hard 60-day) vs D4-β (soft 60-90 day) — both steelmanned, invalidation explicit. Solid. Architect §3.5 surfaced a missing cancellation fallback (lab-booking falls through within 7 days of date) — minor gap but addressable.
- **D5**: D5-α (v0.3 CI matrix) vs D5-β (defer to v0.4) — both steelmanned, invalidation explicit. Solid.

**Score**: 3/5 decisions have fair, complete alternative analysis (D3 D4 D5 with caveats); 2/5 (D1, D2) operate on a false-dichotomy option-set. D1 in particular fails the "fair alternatives" test because the strongest counter-position ("no sidecar at all") is absent and is the position ADR 0010 already adopted.

### 3.3 Risk mitigation clarity — **PARTIAL PASS**

The Risk table (§6, lines 451–464) has 10 rows, each with Step / Impact / Mitigation. Format is consistent. But:

- Row 2 ("SPSC channel medium D2-α latency higher than target") mitigation says "fallback to D2-β shm+futex (held in reserve); documented in ADR 0010 Follow-ups" — but neither ADR 0010 nor this plan actually documents the D2-β fallback path. This is a forward-reference to a follow-up that does not exist. **MINOR** but a real gap.
- Row 4 ("Sidecar EPIPE/ECONNREFUSED handling silent failure") — mitigation references ADR 0011 rule 5 which addresses EPIPE on the registry-read side, not the plugin→sidecar SPSC channel side. The mitigation conflates two distinct EPIPE surfaces. **MINOR** at risk-row level, but **MAJOR** if the sidecar is kept and the bug ships.
- Row 6 ("notify reversal breaks host expectations") — mitigation says "reverse-channel code is opt-in via SPATIAL_ENGINE_VST3_OSC=ON build flag". But on Linux v0.3 builds, the flag is ON by default per A5-α; so the "opt-in" framing understates risk for the target deployment. **MINOR**.
- Pre-mortems (Scenario 1, 2, 3) — each has Detection / Probability / Severity / Mitigation / Recovery / Test coverage. Format compliance is high. **PASS** on format, **FAIL** on substance: Scenarios 1 and 2 mitigate failure modes that the *rejected* architecture creates, not the *frozen* one. They are straw-man pre-mortems.

### 3.4 Testable acceptance criteria (≥90%) — **PASS-WITH-RESERVATIONS**

Plan §10 lists A.3–A.15 (12 criteria, all marked testable). Spot-check audit:

| ID | Verdict | Notes |
| -- | ------- | ----- |
| A.3 | Pass | Concrete grep + build command. |
| A.4 | Pass | sha256 hash compare against checked-in baseline files. |
| A.5 | Pass | ctest target name explicit. |
| A.6 | Pass | RT_ASSERT_NO_ALLOC harness exists from C2B. |
| A.7 | **WEAK** (Architect §7 flagged) | "<100ms" threshold loose; ADR 0010 forward p99 < 5ms; reverse-path 20× budget unjustified. Tighten to `< 50ms` or document. |
| A.8 | Pass | mdcat render + grep TOC link. |
| A.9 | **WEAK** (Architect §7 flagged) | "<100ms" — with sidecar hop deleted, should tighten to `< 30ms` (one UDP roundtrip + one block-drain @ 48k/64). Under-spec'd as written. |
| A.10 | Pass | Concrete profile (1 obj × 100 Hz × 60s × 8 inst) + concrete metric. |
| A.11 | **VAGUE** (Architect §7 flagged, Critic concurs) | "preserved OR re-pinned cleanly" lacks a binary decision criterion. Sharpen: "MUST be preserved; any re-pin requires explicit paired core/ change listing." Current wording lets either outcome count as pass — that is not falsifiable. |
| A.12 | **FUZZY** (Architect §7 flagged) | "screenshots / capture logs" not quantified. Per-host minimum (1 screenshot, 1 WAV, 8 param checkboxes) should be explicit. |
| A.13 | Pass-if-kept / Delete-if-§3.1-fixed | ctest target exists; "within 10s" needs a percentile. |
| A.14 | Pass-if-kept / Delete-if-§3.1-fixed | drop_count metric vs kernel overrun count is timing-dependent; sharpen to "monotonic, non-zero, bounded by `(sent - blocks × bs)`". |
| A.15 | Pass (strongest of new entries) | Real v0.2 preset fixtures named at concrete paths. Note: fixtures don't exist yet (S7 creates them); plan should state this explicitly. |

**Score**: 12/15 testable as written, 3/15 (A.7, A.9, A.11) need sharpening before APPROVE, A.12 needs quantification. **Above the 90% threshold but only barely** — at 80% strict pass, 100% pass-with-sharpening.

### 3.5 Concrete verification steps (S2..S8 green-light criteria) — **PARTIAL PASS**

Each step has an Acceptance criteria block. Spot-check:

- **S2**: acceptance criteria reference A.3, A.4, A.5 plus "sidecar `--log-level debug` emits registry size + GC events every 5s ... structured (JSON or fixed-format) for observability §7.4." → JSON-OR-fixed-format is a fork that should be decided pre-execution. **MINOR**: pick one before S2 lands.
- **S3**: acceptance references A.6 + new `test_vst3_spsc_ring_overrun`. Solid.
- **S4**: acceptance references A.7 + `test_vst3_no_feedback_loop`. **GAP**: no acceptance criterion for the `IComponentHandler::performEdit` thread-safety question Architect raised as v03-Q8. Cross-thread `performEdit` from a UDP recv thread is undefined behaviour per VST3 SDK convention — must be marshalled via host's message thread. The plan never addresses this. **MAJOR if sidecar kept; MAJOR even under §3.1 fix because the reverse path through plugin's own UDP thread still needs to call `performEdit`.**
- **S5**: acceptance references A.8 — markdown render + TOC link. Solid for a doc step.
- **S6**: acceptance references A.9, A.10, plus `soak_vst3_storm`. Storm soak duration is 60s; Architect §3.4 flagged this as too short for sidecar memory growth (5–30 min typical leak signature). **MINOR if sidecar kept; moot if §3.1 fix applied**.
- **S7**: acceptance references A.11 (vague) + new `test_vst3_state_v3_persist`. State v3 lands day 8 of 9 with zero soak time before customer DAW hands-on (S8) — Architect §3.3 surfaced this as a Principle 5 yellow-flag. **MAJOR**: split reader to S2.5, writer to S7.
- **S8**: acceptance has a per-param falsifiable table + 4 categorical pass/fail criteria. Solid format. "DAW hands-on log committed" needs quantitative threshold per Architect §7 A.12 nit.

**Score**: 5/7 steps have fully verifiable green-light criteria. S4 missing thread-safety contract is the most serious gap; S7 timing is a Principle 5 risk.

### 3.6 Pre-mortem quality (DELIBERATE requires ≥3 realistic scenarios) — **PARTIAL FAIL**

| Scenario | Realism | Coverage of actual architecture failure mode |
| -------- | ------- | ---- |
| **1. Sidecar OOM mid-show** | Plausible *if sidecar exists*. | Straw-man: this scenario only exists because the plan introduces a sidecar. Under the frozen A1-ε architecture, there is no sidecar to OOM-kill. The plan creates the failure mode it pre-mortems. |
| **2. SPSC overrun under 64 obj × 1 kHz storm** | Partially realistic. | The actual surface under A1-ε is the plugin's per-instance UDP `recv` buffer + intra-process SPSC ring. The plan describes the *cross-process UDS* SPSC overrun, which doesn't exist under the frozen architecture. The intra-process variant is worth pre-morteming but is not what the plan covers. |
| **3. State v3 reader bug breaks v0.2 preset interop** | **Realistic**, well-mitigated. | This is the strongest pre-mortem. Survives §3.1 fix. Architect §3.3 sharpened it further (split reader to S2.5). |

**Score**: 1/3 pre-mortems pre-mortem the actual architecture; 2/3 pre-mortem the rejected architecture. DELIBERATE mode requires ≥3 scenarios; substantively only 1 is on-target. **FAIL on substance, PASS on count.**

Architect §4.4 proposed adding Pre-mortem 4 (ADR 0011 file-based registry race on simultaneous startup) which is a real failure mode of the *frozen* architecture. Critic concurs and considers it required.

### 3.7 Test plan completeness (unit/integration/e2e/observability/soak/RT-safety) — **PASS-WITH-RESERVATIONS**

§7 has all six sub-sections (7.1 unit, 7.2 integration, 7.3 e2e, 7.4 observability metrics, 7.5 soak, 7.6 RT-safety). Format is exemplary. Substance audit:

- **7.1 unit**: 7 tests listed, each with concrete what-it-asserts + step assignment. **PASS** on format. Substance: 4 of 7 are sidecar-dependent (`test_vst3_sidecar_dispatch`, `test_vst3_sidecar_reverse_path`, `test_vst3_no_feedback_loop`, `test_vst3_sidecar_oom_recovery`); these dissolve under §3.1 fix.
- **7.2 integration**: 3 tests. `test_vst3_handshake_protocol` (round-trip ack within 500ms) only makes sense for plugin↔sidecar handshake; moot under A1-ε.
- **7.3 e2e DAW hands-on**: 2 hosts × 8 params + WAV recording. Concrete and falsifiable. **PASS**.
- **7.4 observability**: 7 metrics + log policy. **PASS** on count and naming. `bridge_reconnect_count` is sidecar-specific (moot under A1-ε); `osc_drop_count{instance_id}` survives as plugin recv-overrun counter. **PASS** with relabeling.
- **7.5 soak**: 3 soak profiles (console_flood, storm, inherited C3). Storm duration 60s flagged §3.5. **PASS** with duration sharpening or §3.1 deletion.
- **7.6 RT-safety**: 3 probes (existing harness extended + new persist alloc-zero + negative controls). **PASS**.

**Score**: All 6 sub-sections present and populated. Substance is correct for ~half the tests and architecture-mismatched for the other half. Cleanup is mechanical: rename sidecar-specific tests to plugin-direct tests or delete them.

---

## 4. Specific revision asks (numbered, actionable)

1. **[CRITICAL — blocks APPROVE]** **Align §1.4 / §2 D1 / §2 D2 / §S2 / §S3 / §S4 / §S6 / Pre-mortem 1+2 / Risk row 2+4 / §11 Files-touched list with the inherited A1-ε frozen axis.** Concretely, delete from scope: `bin/spatial_engine_sidecar.cpp`, `vst3/sidecar_bridge/UdsServer.{h,cpp}`, `vst3/sidecar_bridge/PluginToSidecarChannel.{h,cpp}`, `vst3/sidecar_bridge/AutomationReflect.{h,cpp}`, `vst3/sidecar_bridge/ControllerReverseHandler.{h,cpp}`, `tools/systemd/spatial-engine-sidecar.service`, `vst3/tests/test_vst3_sidecar_dispatch.cpp`, `vst3/tests/test_vst3_sidecar_reverse_path.cpp`, `vst3/tests/test_vst3_sidecar_oom_recovery.cpp`, `vst3/tests/test_vst3_handshake_protocol.cpp`, `vst3/tests/perf/soak_vst3_storm.cpp`. Keep (rename out of `sidecar_bridge/`): `vst3/osc/PluginInstanceRegistry.{h,cpp}` (writer in plugin, reader in standalone), `core/src/util/RegistryPath.h`, `core/src/util/SpscRing.h` (used as *intra-plugin* ring per ADR 0010 §A4-β), `vst3/tests/test_vst3_state_v3_persist.cpp`, `vst3/tests/test_vst3_registry_stale_cleanup.cpp`, `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` (re-scoped to direct path), `vst3/tests/perf/soak_vst3_console_flood.cpp` (same profile; tighter p99 target). See Architect §4.1 deletions list verbatim.

2. **[CRITICAL — blocks APPROVE]** **Redesign S4 (reverse-automation path) per ADR 0010 §A1-ε + §A4-β.** Console sends `/adm/obj/0/azim 90.0` to standalone:9100; standalone's existing OSC dispatch reads `~/.config/spatial_engine/instances.json` and `sendto()`s to each matching instance's bound port; plugin's dedicated UDP thread (already required by §A4-β) receives, decodes, pushes to per-instance SPSC ring, audio callback drains. DAW-automation reflection: plugin's UDP thread (allowed to alloc per §A4-β) calls `IComponentHandler::performEdit(paramId, value)` via the Controller's componentHandler pointer — **but only after resolving v03-Q8 thread-safety question (see ask 3)**. Net delta: ~150 LOC additions to `core/src/bin/spatial_engine_core.cpp` + ~80 LOC to `vst3/SpatialEngineController.cpp`, replacing ~6 new sidecar files + 5 sidecar tests. See Architect §4.2.

3. **[CRITICAL — blocker on S4 entry]** **Resolve v03-Q8 (`IComponentHandler::performEdit` thread-safety) before S4 implementation begins.** VST3 SDK convention requires `performEdit` from the host's message thread, not arbitrary worker threads. Read `pluginterfaces/vst/ivsteditcontroller.h` thread-safety doc. Decide one of: (a) marshal via `IRunLoop` (unavailable without editor view per ADR 0010 §A4-γ), (b) post to a host-side message-thread queue, (c) restrict reverse path to read-only state propagation (Controller reads from a shared atomic populated by UDP thread; host's `restartComponent(kParamValuesChanged)` informs DAW). Document choice in `vst3/SpatialEngineController.cpp` notify-reversal commit footer. Add as A.7-prerequisite acceptance criterion before any S4 test PASS counts.

4. **[MAJOR — Principle 5 risk]** **Split state v3 reader/writer landing per Architect §3.3 synthesis.** State v3 *reader* (3-way fork at `vst3/SpatialEngineProcessor.cpp:201-323`, defaulting kMute=0 for v1/v2 inputs) lands at S2.5 (new 0.5d gate after S2, before S3 starts). State v3 *writer* + kMute param + Controller's 8th ParameterInfo lands at S7 as currently planned. Net: reader gets ~8 days of soak with v0.2 preset fixtures before customer DAW hands-on; writer's debugging-blast-radius isolation is preserved. Renumber S2.5 as an interface-freeze + reader-only commit (also addresses Architect §3.2 parallelisation gate).

5. **[MAJOR — D3 alternative analysis]** **Add the D3-γ hybrid option (reader early, writer late) to §2 D3 with steelman pros/cons.** Plan currently presents only D3-α (both early) and D3-β (both late); the hybrid Pareto-dominates both per Architect §3.3 and ask 4 above. Even if ask 4 is adopted as the recommendation, §2 D3 must show the option-space honestly so DELIBERATE-mode alternative-fairness is satisfied.

6. **[MAJOR — A.11 vagueness]** **Sharpen A.11 binary criterion**: change `OFF byte-baseline preserved OR re-pinned cleanly` to `OFF byte-baseline MUST be preserved. Re-pin permitted only if S7 explicitly enumerates the paired core/ change that justifies it. No paired change → no re-pin → drift is a CI fail.` Under §3.1 fix, expected state is "no core/ change touches OFF baseline scope; A.11 = baseline preserved unchanged."

7. **[MAJOR — A.7/A.9 latency thresholds]** **Tighten reverse-path and e2e latency thresholds**. A.7: change `<100ms` to `p99 < 50ms` for sidecar-relayed path OR `p99 < 30ms` for direct path under §3.1 fix. Document headroom rationale against ADR 0010 §Drivers + parent plan §7.5 `p99 < 5ms` forward-path target. A.9: change e2e console-UDP → plugin-param-write to `p99 < 30ms` post-§3.1 (one UDP roundtrip + one block-drain at 48k/64 = ~1.3ms baseline + slack).

8. **[MAJOR — pre-mortem coverage of actual architecture]** **Replace Pre-mortems 1+2 with pre-mortems of the frozen A1-ε architecture.** Architect §4.4 proposed Pre-mortem 4 (registry race on simultaneous plugin startup, `flock(LOCK_EX|LOCK_NB)` with 10×50ms backoff = 500ms worst-case ctor latency). Critic concurs. Suggested 3-scenario replacement: (a) per-instance UDP recv-buffer overflow at 64 obj × 1 kHz (the *real* storm surface — kernel drops oldest packets when `wmem_max` exceeded), (b) registry race on simultaneous startup (Architect §4.4), (c) state v3 reader bug (Scenario 3, sharpened per ask 4).

9. **[MAJOR — v03-Q9 stale-PID liveness]** **Pick a mitigation for the `/proc/{pid}/comm` false-positive risk before S2.** ADR 0011 line 243–244 acknowledges this risk but does not mandate a fix. Architect §6.2 v03-Q9 proposed: (a) embed `boot_id` in registry entries and GC all entries with stale boot_id regardless of PID match, (b) embed process start-time (`/proc/{pid}/stat` field 22 `starttime`) alongside PID. Pick one. Add a test in `core/tests/core_unit/test_p_instances_registry.cpp` for the stale-PID-across-reboot scenario.

10. **[MAJOR — v03-Q10 XDG empty-string]** **Document `XDG_CONFIG_HOME=""` semantics in `core/src/util/RegistryPath.h` before S2.** Per XDG spec, set-but-empty falls back to `~/.config`. Without this, plugin instances in some shells (corporate VPN containers, locked-down workstations) silently write to `./instances.json` in host cwd. ~5 LOC fix, prevents a known footgun.

11. **[MINOR — D4 cancellation fallback]** **Add explicit cancellation language to D4-α**. Per Architect §3.5: `"If lab session cancels within 7 days of booked date, ADR 0012 commit at day-60 is a 'no-quirk-observed: synthetic fixture extension to day-90' note, and Notion task auto-files for day-90 re-booking."` Single sentence; preserves the falsifier discipline of α even under venue cancellation.

12. **[MINOR — ETA banner update]** **Update §9 ETA banner once §3.1 fix is applied.** Architect §4.3 calculates revised total ETA ~6.5d (was 9d), wall ~9.5–10.5d with cert-eval slack (was 12–13d). Update §9 row by row to match revised step ETAs. This buys 2.5d of sprint-internal slack which can absorb ask 3 (performEdit thread-safety investigation), ask 9 (stale-PID mitigation), and ask 10 (XDG empty-string) without ETA growth.

13. **[MINOR — A.12 quantification]** **Quantify A.12 "screenshots / capture logs" deliverable**: minimum 1 screenshot per host showing all 8 params in plugin UI; 1 WAV recording per host showing kMute audible distinction from kBypass; checklist Y/N for each numbered S8 step (1–8 in §S8 per-host checklist).

14. **[MINOR — S2 log format fork]** Resolve S2's "JSON or fixed-format" log fork (line 198) to one explicit choice before S2 lands — affects downstream parser tooling and Notion ops integration.

15. **[MINOR — risk-row 2 forward-reference]** Add ADR 0010 §D2-β fallback documentation, or remove the forward-reference from risk-row 2. Currently the row promises a fallback documented in a non-existent ADR section.

---

## 5. Top-5 highest-impact issues ranked

1. **[CRITICAL]** **Frozen-contract violation: plan re-introduces A1-δ under cover of "bridge for reverse path + GC".** §1.4 line 53 inherits A1-ε; §2 D1 + §S2 + §S3 + §S4 implements A1-δ. ADR 0010 §A1 line 53 + line 250 verbatim REJECTS this. Parent plan §1.5 line 173 also locks A1-ε. The plan is self-contradicting on its own page. Resolution: ask 1 (deletions) + ask 2 (redesign S4). Without this fix, D1, D2, and Pre-mortems 1+2 are all built on a phantom architecture and the plan cannot proceed to autopilot.

2. **[CRITICAL]** **Undefined VST3 SDK thread contract on reverse path: `IComponentHandler::performEdit` from non-message thread.** S4 implies cross-thread `performEdit` from plugin's UDP recv thread. VST3 SDK convention requires this from the host's message thread; from arbitrary worker threads it is undefined behaviour. AM-R3-10's original `kNotImplemented` was likely defensive cover for exactly this concern. Plan does not address. Resolution: ask 3 (block S4 on SDK thread-safety doc audit + design choice + commit footer documentation).

3. **[MAJOR]** **State v3 reader lands day 8 of 9 with zero soak before customer DAW hands-on.** Pre-mortem 3 ("v0.2 preset interop breaks") is exactly this risk. Plan mitigates via D3-β late-bundle which is correct for *writer* but wrong for *reader*. Resolution: ask 4 (split reader to S2.5, writer to S7) + ask 5 (add D3-γ hybrid to option set). Cost: zero; reuse same test file split into two commits.

4. **[MAJOR]** **Pre-mortem coverage is straw-manned.** 2 of 3 pre-mortems pre-mortem the *rejected* architecture (sidecar OOM, cross-process SPSC overrun); only 1 pre-mortems the actual frozen architecture (state v3 reader bug). DELIBERATE-mode pre-mortem requirement is satisfied in count (3 ≥ 3) but failed in substance. Resolution: ask 8 (replace 1+2 with realistic A1-ε pre-mortems: per-instance UDP recv-buffer overflow, registry startup race, state v3 reader bug sharpened).

5. **[MAJOR]** **Acceptance criteria sharpening: A.7 + A.9 latency thresholds, A.11 binary decision, A.12 quantification.** 4 of 15 criteria need sharpening before they are operationally falsifiable. Resolution: asks 6, 7, 13. All are mechanical edits; no architecture impact.

---

## 6. Ralplan summary row

- **Principle/Option Consistency**: **FAIL** — D1 contradicts inherited A1-ε frozen axis (ADR 0010 §A1 line 53 + line 250 verbatim).
- **Alternatives Depth**: **PARTIAL FAIL** — D1 false-dichotomy (omits "no sidecar" = the actual frozen answer); D2 moot under §3.1 fix; D3 missing hybrid option; D4/D5 OK.
- **Risk/Verification Rigor**: **PARTIAL PASS** — risk-table format consistent and complete; 3 risk rows have substantive accuracy issues (rows 2, 4, 6). 5 of 7 step green-light criteria are concrete and verifiable; S4 missing SDK thread-safety; S7 timing creates Principle 5 yellow-flag.
- **Deliberate Additions (pre-mortem ×3 + expanded test plan)**: **PARTIAL FAIL** — count satisfied (3 pre-mortems, 6 test sub-sections all populated). Substance is half-correct: 2/3 pre-mortems strawman the rejected architecture; 4/7 unit tests are sidecar-dependent (dissolve under §3.1 fix); soak-storm duration too short if sidecar kept.

---

## 7. Loop closure conditions (what must change for Round-3 verdict = APPROVE)

Round-3 verdict is APPROVE iff ALL of the following hold (in order of priority):

**Mandatory (CRITICAL blockers):**

1. **§1.4 / §2 D1 / §2 D2 / §S2 / §S3 / §S4 / §S6 / §11 are rewritten to remove the sidecar binary from the v0.3 Linux fast path** per ask 1 + ask 2 (Architect §4.1 + §4.2). The "bridge" in S4 must be a `core/src/bin/spatial_engine_core.cpp` internal relay (~150 LOC) + plugin's own UDP thread (already required by ADR 0010 §A4-β), not a new binary. Files-touched list (§11) must reflect deletions. D1 must either be removed entirely (no decision needed — A1-ε already decides it) or rewritten as "D1: how does standalone forward `/adm/obj/N/...` packets to plugin instances?" with options like "sendto per instance via registry-discovered port (α)" vs "multicast to a well-known port plugin instances subscribe to (β)" — i.e. a real decision under the frozen axis, not a re-litigation of it.

2. **v03-Q8 `IComponentHandler::performEdit` cross-thread safety is resolved** per ask 3, with the chosen approach (marshal via host-message-thread queue, or restrict reverse path to atomic state + `restartComponent(kParamValuesChanged)`) documented in the plan as an S4-entry prerequisite + an explicit A.7 acceptance criterion.

**High-priority (MAJOR blockers that survive Realist Check):**

3. **State v3 reader splits to S2.5 (or first 0.5d of S2)** per ask 4. Writer + kMute param remain at S7 as planned. §2 D3 adds the hybrid as D3-γ with steelman per ask 5.

4. **Pre-mortems 1+2 are replaced** with realistic A1-ε architecture scenarios per ask 8 (Architect §4.4): (a) per-instance UDP recv-buffer overflow under burst, (b) `flock`-retry-storm under simultaneous plugin startup, (c) state v3 reader bug (Scenario 3, sharpened).

5. **A.7, A.9, A.11, A.12 are sharpened** per asks 6 + 7 + 13: A.7 + A.9 latency thresholds tightened to `p99 < 30ms` (direct path) or `p99 < 50ms` (justified relay); A.11 binary criterion (`MUST be preserved; re-pin only with paired core/ change`); A.12 minimum quantitative deliverables (1 screenshot + 1 WAV + 8-checkbox per host).

**Medium-priority (MAJOR but mechanical):**

6. **v03-Q9 stale-PID-across-reboot mitigation is picked** per ask 9 (boot_id or process-start-time), with a unit test in `core/tests/core_unit/test_p_instances_registry.cpp`.

7. **v03-Q10 XDG_CONFIG_HOME empty-string semantics handled** per ask 10 in `core/src/util/RegistryPath.h`.

**Minor (recommended for cleanliness):**

8. **§9 ETA banner updated** per ask 12 to reflect §3.1 deletions (~6.5d total, ~9.5–10.5d wall with slack).
9. **D4-α cancellation fallback added** per ask 11.
10. **S2 log-format fork resolved** per ask 14.
11. **Risk-row 2 forward-reference removed or backed up by ADR 0010 §D2-β fallback content** per ask 15.

**What does NOT need to change:**

- §3.5–§3.6 D4-α (hard 60-day vendor capture) + D5-β (defer macOS/Windows CI to v0.4) — Critic concurs with Architect that these are the strongest decisions in the plan as-written.
- §7.3 e2e DAW hands-on coverage matrix (Reaper 7.x + Bitwig 5.x Linux, 8 params).
- §8 ADR ratification table format.
- §10 testability score format and the 100% coverage claim (once asks 6/7/13 land).
- Principle list (§1.1 1–5) — inherited correctly from v0.2 freeze.

**Estimated Round-3 turnaround**: ask 1 + ask 2 are ~1–2 hours of plan-text editing (deletions + S4 redesign); ask 3 may need 0.5–1d of SDK doc audit *before* the plan can claim resolution. The remaining asks are mechanical (<1d total). Realistic Round-3 readiness: end of current day if ask 3 is delegated to architect parallel-track; otherwise next day.

---

## 8. Verdict Justification

Verdict is **ITERATE**, not REJECT, because:

- The plan's structural skeleton (5 Principles + 5 Decision Points + 3 Pre-mortems + 6 test sub-sections + 15 acceptance criteria + ADR ratification table + ETA banner + Files-touched manifest) is well-formed and exceeds DELIBERATE-mode requirements *on format*.
- Three of five Decision Points (D3 with §3.3 hybrid amendment, D4, D5) are substantively sound.
- The non-sidecar-dependent deliverables (JSON registry hardening, state v3 + kMute, manual Ch.5, vendor-quirks capture schedule, OFF re-pin discipline, DAW hands-on matrix) are correctly scoped and worth preserving.
- The CRITICAL violation is **mechanical to fix** (~6 file deletions + ~230 LOC of S4 redesign, leaving the rest of the plan intact). Per Realist Check, the realistic worst case if shipped as-written is *not* data loss or security breach; it is "Architect Round-3 rejects → Planner Round-3 cleanup → 1-day slip". Detection is immediate (Architect already caught it). Mitigation is straightforward.
- A REJECT verdict would force Planner to redo the structural work which is *correct*; ITERATE preserves that work and asks for targeted edits.

Verdict is **NOT APPROVE** because:

- The frozen-contract violation is a hard blocker — it cannot be hand-waved as a stylistic preference.
- v03-Q8 (`performEdit` thread-safety) is genuine architectural risk for the reverse path and the plan does not address it.
- Pre-mortems 1+2 strawman the rejected architecture; substituting them with realistic A1-ε scenarios is required for DELIBERATE-mode pre-mortem quality, not just count.

**Mode operated in**: DELIBERATE (per skill contract). Did not escalate to ADVERSARIAL because Architect already operates as the harshness peer and surfaced the CRITICAL finding; Critic's value-add is consensus + gap-analysis (asks 5, 8, 9, 10 are Critic-novel; asks 1, 2, 4, 11 concur with Architect findings and add specificity).

**Realist Check applied** to asks 1, 2, 3, 4:
- Ask 1 (frozen-contract violation): CRITICAL stays — affects the core architectural decision; not downgradable. Mitigation cost is low (mechanical edits) but blast radius if shipped is design debt that propagates into v0.4+ macOS/Windows planning where A5-β contingency design depends on knowing what *isn't* in v0.3. Keep CRITICAL.
- Ask 2 (S4 redesign): CRITICAL stays — directly downstream of ask 1; cannot be split. Keep CRITICAL.
- Ask 3 (performEdit thread-safety): CRITICAL stays — undefined behaviour in audio plugin = host crash potential; detection is in-DAW user-visible. Mitigated by the fact that A5-α restricts deployment to Linux v0.3 (no macOS host-thread audit needed); but Linux Reaper/Bitwig still has a message thread the plugin must respect. Keep CRITICAL.
- Ask 4 (state v3 reader timing): MAJOR stays — Principle 5 (state-ABI preservation) is a customer-impact axis; soak time matters. Realist worst case: v0.3.1 patch with corrected reader (Pre-mortem 3 §Recovery acknowledges this). But first-impression damage on first-customer upgrade is non-trivial. Keep MAJOR.

No findings downgraded by Realist Check; this is a high-stakes architectural-axis review where the standard severity calibration holds.

---

**End of Critic Round-2 review.**
**Next gate**: Round-3 Planner revision per asks 1–15 above. Architect Round-3 will re-verify §3.1 fix application; Critic Round-3 will re-score against this file's Loop closure conditions §7.
