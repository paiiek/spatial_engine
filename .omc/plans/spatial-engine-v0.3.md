# Spatial Engine v0.3.0 — Phase C4 Track A implementation (Round-3 RALPLAN-DR revision)

**Status**: Round-3 revision (post-Architect REVISE-AND-RESUBMIT + post-Critic ITERATE; this revision realigns plan with ADR 0010 frozen A1-ε contract)
**Date**: 2026-05-11
**Owner**: Planner agent (consensus loop, awaiting Round-3 Architect / Critic re-review)
**Mode**: DELIBERATE (pre-mortem + expanded test plan + ADR ratification required; cert-risk + state-ABI bump inherited)
**Track**: A only — Phase C4 implementation (S2 → S8). Track B (v0.2.0 release) already shipped per `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` §1.4 split.
**ETA banner**: ~6.5d (9.5–10.5d wall with cert-eval slack), single-sprint scope — revised down from Round-2 9d after sidecar deletion
**Related**:
* `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` (parent plan — §1.5 Final Decision Freeze line ~173 — locks A1-ε)
* `.omc/plans/architect-r-v0.3-review.md` (Round-2 Architect REVISE-AND-RESUBMIT — §2 steelman, §4 synthesis, §6 v03-Q8/Q9/Q10)
* `.omc/plans/critic-r-v0.3-review.md` (Round-2 Critic ITERATE — 15 revision asks, §7 Loop closure conditions)
* `docs/adr/0010-vst3-osc-binding-model.md` (Draft — esp. line 53 A1-δ rejection, line 250 "no sidecar in v0.3 Linux fast path"; promoted to Accepted by S1 ratification)
* `docs/adr/0011-vst3-osc-multi-instance-discovery.md` (Draft — writer-side GC, no daemon; promoted to Accepted by S1 ratification)
* `docs/adr/0012-adm-osc-vendor-quirks.md` (Reserved slot — first-quirk fill scheduled in D4-α day-60 capture)
* `core/src/bin/spatial_engine_core.cpp:99,134` (standalone OSC port 9100 binding model — A1-ε reference pattern)
* `vst3/SpatialEngineProcessor.cpp:32-34` (engine ctor — extension point for A1-ε in-plugin UDP socket)
* `vst3/SpatialEngineProcessor.cpp:201-323` (state reader/writer — extends to 3-way v1/v2/v3 fork; reader at S2.5, writer at S7)
* `vst3/SpatialEngineProcessor.cpp:481-554` (`dispatchParamChange` — unchanged in this sprint; reverse path is per-instance, not from dispatch)
* `vst3/SpatialEngineProcessor.cpp:560-579` (`notify` reversal site for in-process reverse-path)
* `vst3/CMakeLists.txt:16-38` (current build target — extends with `SPATIAL_ENGINE_VST3_OSC` flag in S2)
* `core/src/ipc/CommandDecoder.cpp:317-373` (ADM-OSC decode — re-used by in-plugin UDP I/O thread)
* `core/src/ipc/OSCBackend.cpp:75-83` (POSIX UDP recv pattern — mirrored in plugin recv thread per A4-β)
* `core/src/ipc/AdmOscConstants.h` (`ADM_OSC_MAX_DIST=20.0f` — single-source-of-truth re-used)

---

## 1. RALPLAN-DR Summary (top of file, per /plan --consensus contract)

### 1.1 Principles (5, non-negotiable invariants — inherited from v0.2, unchanged)

1. **JUCE-free preserved.** No new JUCE includes in `vst3/` or any new file. Plugin remains Option-B vst3sdk hand-roll with `SPE_HAVE_JUCE=0`. Verified via `grep -r '#include.*juce' vst3/` → must return 0.
2. **ctest + pytest stays green.** Current baseline is 109 ctest (51 core + 58 vst3) + 193 pytest. v0.3 may only **add** tests; none are skipped or marked xfail. CI run on every PR must SUCCESS on all jobs.
3. **OFF byte-baseline dual-gate untouched (unless intentional).** `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` hashes for `libspe_core.a` + `spatial_engine_core` must remain byte-identical when `SPATIAL_ENGINE_VST3=OFF` and `SPATIAL_ENGINE_VST3_OSC=OFF` (defaults). Any v0.3 work that touches `core/` requires a paired re-pin commit (mirror `8c0ca2d`/`587815c` workflow). S7 is the explicit re-pin gate.
4. **ADM-OSC v1.0 spec verbatim (ADR 0006 freeze).** Plugin-side recv path must speak the exact same wire dialect as standalone — same addresses, same type-tags, same `ADM_OSC_MAX_DIST=20.0f`. Zero plugin-side schema invention. `Command` variant is the single source of truth; both bind points share it.
5. **v0.2.0 wire-ABI + state-ABI preserved.** `--osc-port`/`--osc-dialect`/Component IID/Controller IID/CLI surface unchanged. State format extends v2 → v3 with **forward-compat reader landing day 1**: v1/v2/v3 all read cleanly (3-way fork at `Processor.cpp:201-323`); writer emits v3 only and lands at S7. v0.1.0 + v0.2.0 `.vstpreset` files load through the v1/v2 reader paths. Renaming/reordering existing params is forbidden; kMute is appended as id=7.

### 1.2 Decision Drivers (top 3, why this work happens NOW — unchanged)

1. **Korean live-venue customer dependency — plugin↔console path (primary driver).** v0.2.0 satisfied the standalone-only path. v0.3 unblocks the in-DAW first-contract workflow: Reaper/Bitwig running plugin instance + console (DiGiCo/Avid/Yamaha) routing `/adm/obj/N/...` directly to the plugin. Without C4, the manual's OSC chapter remains standalone-only and customers must run two processes side-by-side.
2. **ADR 0010/0011 protocol cache freshness.** Drafts authored 2026-05-10. Ratifying + implementing within ~30 days of draft prevents stale-knowledge handoff and architectural drift. Every week of delay risks competing v0.3+ work touching the same surface (state format, threading, registry) and creating merge conflicts.
3. **Reduce cross-platform debt before Phase D macOS/Windows port.** v0.4+ will add macOS/Windows CI matrices. Locking the Linux A5-α fast path NOW (with A5-β sidecar everywhere reserved as v0.4+ contingency) means the cross-platform port revisits a stable, tested baseline rather than designing concurrently.

### 1.3 Mode classification (DELIBERATE — unchanged)

Track A introduces new audio-plugin network surface (`bind()` inside DAW process) — cert-risk on macOS/Windows is real even though v0.3 is Linux-only (A5-α gates it). State format v2 → v3 bump is a wire/file ABI change. Multi-instance JSON registry is shared persistent state. Therefore: pre-mortem (3 scenarios) + expanded test plan (unit / integration / e2e / observability / soak / RT-safety) + ADR ratification all required.

### 1.4 Deliverable matrix (v0.3 scope, post-v0.2 inheritance — restated prominently)

**Inherited as FROZEN from parent plan §1.5 + ADR 0010 §A1/A2/A2.1/A3/A4/A5 (no re-litigation):**

* **A1-ε** per-instance recv-only UDP socket — `bind()` directly inside the VST3 plugin process. **NO sidecar binary**, **NO Unix domain socket**, **NO systemd unit** in v0.3 Linux fast path. ADR 0010 line 53: "A1-δ was the Round-1 recommendation but rejected post-Architect §6 synthesis"; line 250: "no sidecar in v0.3 Linux fast path".
* **A2-α** recv-only direction; plugin reads ADM-OSC, plugin does NOT publish state back over OSC.
* **A2.1-β** kMute id=7 + state v3 (40 bytes, magic `'SPE1'`, version=3), writer lands S7.
* **A3-β** file-based registry at `~/.config/spatial_engine/instances.json`; **writer-side GC by each plugin instance** (ADR 0011 rule 3 — no daemon needed); readers tolerant of stale entries (ADR 0011 rule 4); `flock(LOCK_EX|LOCK_NB)` discipline (ADR 0011 rule 2).
* **A4-β** dedicated `std::thread` per plugin instance for UDP I/O; intra-process SPSC ring between UDP thread (producer) and audio callback (consumer).
* **A5-α** Linux-only `SPATIAL_ENGINE_VST3_OSC=ON` build flag (default OFF). A5-β (sidecar-everywhere) reserved as v0.4+ contingency only.

**Reverse path under A1-ε**: console sends `/adm/obj/N/...` to standalone:9100; standalone's existing OSC dispatch consults `~/.config/spatial_engine/instances.json` and `sendto()`s the encoded packet to each matching instance's bound port. Plugin's dedicated UDP thread receives, decodes via `CommandDecoder.cpp:317-373`, pushes to per-instance SPSC ring, and (subject to v03-Q8 thread-safety resolution) marshals an `IComponentHandler::performEdit` call to the host's message thread so the DAW writes the value into its automation lane. **In-process, no cross-process IPC.**

**What v0.3 must additionally decide (this plan, see §2):**

* **D1**: how does the plan honour the no-sidecar frozen contract — the only viable option is D1-γ (no sidecar). D1-α/D1-β eliminated by frozen contract.
* **D2**: moot under A1-ε — retained for traceability only.
* **D3**: state v3 migration timing — D3-γ hybrid (reader early at S2.5, writer late at S7) recommended over pure-α or pure-β.
* **D4**: vendor-quirks real-data capture schedule (hard 60-day vs slack) — unchanged from Round-2 (D4-α recommended).
* **D5**: macOS/Windows CI matrix scope (v0.3 expansion vs v0.4 defer) — unchanged from Round-2 (D5-β recommended).

### 1.5 Non-goals (explicit, locked — refreshed)

* **No sidecar binary** in v0.3 Linux fast path (frozen by ADR 0010 §A1-ε; A5-β contingency is v0.4+ only).
* **No systemd unit** in v0.3 packaging (was previously coupled to sidecar; dropped).
* **No editor view (`createView`)** — stays nullptr until v0.4+ Phase D6.
* **No standalone OSC port 9100 semantics change.** `spatial_engine_core` continues to bind 9100 as the canonical bidi endpoint; plugin recv-only is *additive*.
* **No additional VST3 params beyond 8th (kMute).** Param count goes 7→8 in this sprint and stops.
* **No cross-host plugin hosting.** mDNS / Bonjour discovery (A3-α) is v1.0 candidate, not v0.3.
* **No sample-accurate DAW automation.** Block-rate param dispatch continues unchanged; sample-accurate is Phase D6.

### 1.6 Round-3 changelog (every Round-2 ask CLOSED / DEFERRED + rationale)

**CRITICAL (escape-ITERATE blockers):**

* **C1 (frozen A1-ε honoured)** — **CLOSED.** Deleted from scope: `bin/spatial_engine_sidecar.cpp`, all `vst3/sidecar_bridge/UdsServer.{h,cpp}`, `PluginToSidecarChannel.{h,cpp}`, `AutomationReflect.{h,cpp}`, `ControllerReverseHandler.{h,cpp}`, `tools/systemd/spatial-engine-sidecar.service`, `test_vst3_sidecar_dispatch.cpp`, `test_vst3_sidecar_reverse_path.cpp`, `test_vst3_sidecar_oom_recovery.cpp`, `test_vst3_handshake_protocol.cpp`, `soak_vst3_storm.cpp`. Retained (moved out of `sidecar_bridge/` to `vst3/osc/`): `PluginInstanceRegistry.{h,cpp}` (header-only file-based, writer-side GC per ADR 0011), `RegistryPath.h`, `SpscRing.h` (intra-plugin ring). Added: `vst3/SpatialEnginePluginUdp.{h,cpp}` (in-plugin UDP I/O thread per A4-β), `vst3/SpatialEngineRegistry.{h,cpp}` (header-only registry client). Reverse path is now in-process per §4.2 of Architect Round-2 review. ETA drops 9d → ~6.5d.

* **C2 (v03-Q8 performEdit cross-thread safety)** — **CLOSED (resolution discipline mandated).** S2.6 new gate (0.5d, day 2 of sprint) audits Steinberg SDK headers (`pluginterfaces/vst/ivsteditcontroller.h`) for the documented thread contract, decides one of three resolution strategies (host message-thread queue marshaling / `IRunLoop` if available / read-only state propagation via `restartComponent(kParamValuesChanged)`), implements the chosen marshaling path, and runs a per-host smoke matrix (Reaper 7.x + Bitwig 5.x + Ardour 8.x Linux) before S8 begins. S4's reverse-path acceptance is conditional on S2.6 PASS. See §3 S2.6 + Risk row 2.

**MAJOR (Round-3 review blockers):**

* **M1 (state v3 reader/writer split — D3-γ hybrid)** — **CLOSED.** Reader-early at new S2.5 (day 1 of sprint, 0.5d) implements 3-way v1/v2/v3 fork at `Processor.cpp:201-323` defaulting kMute=0 for v1/v2 inputs; reader throws on `version > 3` by design. Writer + kMute (id=7) + Controller's 8th `ParameterInfo` lands at S7 (day ~5.5). Reader gets ~5 days of soak with v0.2 preset fixtures before customer DAW hands-on (S8). D3-γ added to §2 decision option-set as ★ recommended.
* **M2 (pre-mortems replaced)** — **CLOSED.** Old PM1 (sidecar OOM) + PM2 (UDS storm) deleted (strawman the rejected architecture). New PM1: `bind()` collision (port already held by stale instance or non-plugin app). New PM2: registry file corruption mid-write (power loss / ENOSPC / multi-writer contention). PM3 (state v3 interop break on v0.2 preset) retained — sharpened with D3-γ split mitigation. See §5.
* **M3 (acceptance criteria sharpened)** — **CLOSED.** A.7 forward-latency: replaced `≤100ms` with **`p99 ≤ 30ms wall, p50 ≤ 5ms`** under 64-obj × 1kHz load (matches parent §1.4). A.9 reverse-path latency: replaced `≤100ms` with **`p99 ≤ 50ms host-perceived`** (DAW automation tick granularity). A.11 OFF re-pin: replaced "preserved OR re-pinned, possibly no-op" with binary criterion — **hash match OR paired re-pin commit referencing a deliberate symbol/data change**. A.12 quantified: minimum 1 screenshot per host (8 params visible) + 1 WAV per host (kMute audible distinction from kBypass) + 8-step Y/N checklist per host (per Critic ask 7 + 13).

**MINOR (mechanical edits — Critic asks 6, 8, 9, 10, 11, 12, 14, 15):**

* **m1 (D1 option-space corrected)** — **CLOSED.** D1-α (separate exe) + D1-β (subcommand) marked eliminated with frozen-contract citation (ADR 0010 line 53 + line 250). D1-γ (no sidecar) — ★ recommended. See §2 D1.
* **m2 (D2 marked moot)** — **CLOSED.** D2 retained for audit trail with one-line invalidation rationale; both D2-α (UDS DGRAM) and D2-β (shm+futex) eliminated by A1-ε (no plugin↔sidecar channel exists).
* **m3 (D3-γ hybrid added)** — **CLOSED.** §2 D3 now shows D3-α/β/γ trichotomy with D3-γ as ★ recommended.
* **m4 (open questions updated)** — **CLOSED.** v03-Q8/Q9/Q10 added to §12; v03-Q2 + v03-Q6 dropped as moot. `.omc/plans/open-questions.md` updated in parallel.
* **m5 (files-touched appendix)** — **CLOSED.** §11 rewritten: all sidecar/UDS/systemd entries removed; `vst3/SpatialEnginePluginUdp.{h,cpp}` + `vst3/SpatialEngineRegistry.{h,cpp}` + `vst3/osc/PluginInstanceRegistry.{h,cpp}` added.
* **m6 (ETA banner update)** — **CLOSED.** §9 rewritten: per-step ETAs match Architect §4.3 synthesis. Total ~6.5d, wall ~9.5–10.5d.
* **(Critic ask 9) v03-Q9 stale-PID liveness** — **CLOSED via design choice + open-question retention.** S2 mitigation: embed `boot_id` (`/proc/sys/kernel/random/boot_id`) in each registry entry; GC drops any entry with stale `boot_id` regardless of `/proc/{pid}/comm` match. Implementation listed in S2 files. Test added to `test_p_instances_registry.cpp` (S2). v03-Q9 retained in §12 as design-intent reference.
* **(Critic ask 10) v03-Q10 XDG empty-string** — **CLOSED.** S2's `RegistryPath.h` handles `XDG_CONFIG_HOME=""` (set-but-empty) by falling back to `~/.config` per XDG spec — explicit unit test in `test_p_instances_registry.cpp`.
* **(Critic ask 11) D4-α cancellation fallback** — **CLOSED.** D4-α invalidation block now reads: *"If lab session cancels within 7 days of booked date, ADR 0012 commit at day-60 is a 'no-quirk-observed: synthetic fixture extension to day-90' note, and Notion task auto-files for day-90 re-booking."*
* **(Critic ask 14) S2 log-format fork** — **CLOSED.** Resolved to **fixed-format** (one log line per event: `tag=value tag=value`); reasoning: easier for downstream parser tooling (grep + awk) and consistent with existing standalone log style. JSON deferred to v0.4+ if Notion ops integration requires it.
* **(Critic ask 15) Risk-row D2-β forward-reference** — **CLOSED via deletion.** Old risk row referencing "D2-β fallback documented in ADR 0010 Follow-ups" deleted (D2-β is moot under A1-ε); replaced with `bind()` collision risk for PM1.

**DEFERRED (with rationale):**

* **Architect §3.4 storm-soak extension to 300s** — **DEFERRED (now moot).** Architect's own §4.4 acknowledged this dissolves under §2 sidecar deletion; no long-running daemon = no memory-growth surface to stress beyond 60s. Combined 60s soak retained as regression gate per Critic asks 9 (closure conditions). If a stale-PID-related leak surfaces, addressed in v0.3.1 patch.
* **S4 ETA further compression to 0.5d** (Architect optional) — **DEFERRED.** Retained at 1.0d to absorb v03-Q8 marshaling-path implementation effort; if S2.6 audit reveals `IRunLoop` available without editor view (unexpected), S4 can compress to 0.5d post-hoc. ETA banner shows 1.0d as honest upper bound.
* **S6 soak compression** (Architect optional) — **DEFERRED.** Retained at 1.0d (from Round-2 1.5d; Architect §4.3 synthesis). Combined 60s soak still needed as in-process regression gate even without IPC stress; A.10 latency target is now tighter (`p99 < 2ms` per Architect §4.1 line 95).

**Open questions remaining (≤5)**: see §12.

---

## 2. Decision points (5, with ≥2 options each, recommendation marked ★)

### D1 — Sidecar build target (eliminated by frozen contract; retained for audit trail)

ADR 0010 §A1-ε line 53 + line 250 freezes "no sidecar in v0.3 Linux fast path". Architect Round-2 §2 steelman + Critic Round-2 §3.1 both confirmed the frozen contract eliminates this decision. Options retained for traceability:

| Opt | Description | Verdict |
| --- | ----------- | ------- |
| **D1-α (separate `spatial_engine_sidecar` executable)** | New `bin/spatial_engine_sidecar.cpp` + dedicated `add_executable` target. | **ELIMINATED** by ADR 0010 line 53 verbatim: *"A1-δ (sidecar binary + UDS) was the Round-1 recommendation but rejected post-Architect §6 synthesis: ~7 days of new infra to mitigate a cert risk that does not materialize on the v0.2/v0.3 Linux-only target."* |
| **D1-β (integrated `--sidecar` subcommand of `spatial_engine_core`)** | Same binary, different behaviour. | **ELIMINATED** by ADR 0010 line 250 verbatim: *"written + GC'd by sidecar (or by plugin under A5-α direct bind, no sidecar in v0.3 Linux fast path)"*. Subcommand is still a sidecar process; frozen contract rejects all sidecar variants for v0.3. |
| **D1-γ (no sidecar — direct in-plugin `bind()` per A1-ε)** ★ | Plugin process binds `127.0.0.1:9100+N` (N = instance index from registry) directly. Reverse path is in-process callback inside plugin, not cross-process. Registry GC happens writer-side per ADR 0011 rule 3. | **★ RECOMMENDED — and the only frozen-contract-compliant option.** |

**Recommendation**: **D1-γ.**

**Why γ**: Frozen by ADR 0010 §A1-ε post-RALPLAN Round-2 APPROVE. Architect Round-2 §2 + Critic Round-2 §3.1 confirmed. No new binary, no UDS, no systemd. Registry is file-only (writer-side GC); SpscRing is intra-plugin (UDP I/O thread producer, audio callback consumer per A4-β). Net delta vs Round-2 plan: −2.5 sprint days; eliminates 5 sidecar source files + 5 sidecar tests + 1 systemd unit; replaces with ~150 LOC added to `core/src/bin/spatial_engine_core.cpp` (standalone-internal forwarder) + ~80 LOC added to `vst3/SpatialEngineController.cpp` (notify reversal).

### D2 — SPSC channel medium (moot under A1-ε — retained for audit trail)

| Opt | Description | Verdict |
| --- | ----------- | ------- |
| **D2-α (UDS SOCK_DGRAM)** | Plugin↔sidecar via Unix domain socket. | **MOOT** — no plugin↔sidecar channel exists under D1-γ; SPSC ring is intra-process (UDP I/O thread → audio callback inside the plugin process). |
| **D2-β (POSIX shm + futex ringbuffer)** | Lock-free shm between plugin and sidecar. | **MOOT** — same reason as α. |

**Recommendation**: section MOOT under A1-ε. The intra-plugin SPSC ring uses `core/src/util/SpscRing.h` (fixed-size, lock-free, single-producer single-consumer, `memory_order_acquire/release` — pattern mirrors C1.b commit `6145a53`). RT-safety: audio thread pops only (alloc=0); UDP thread pushes and may alloc per ADR 0010 §A4-β line 80–82.

### D3 — State v2 → v3 migration timing

The state format must bump v2 (36 bytes, 7 floats) → v3 (40 bytes, 8 floats including kMute) per A2.1-β. Question: when in the sprint does each side land?

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **D3-α (both bundled with S3, early)** | State v3 reader/writer + new kMute param all land in S3 alongside SPSC plumbing. | Maximum v3 exposure to test cycles | Compounds debugging surface during most experimental part of sprint; preset interop risk under-exercised before customer hands-on |
| **D3-β (both bundled with S7, late)** | Reader + writer land together at S7, one day before S8 hands-on. | Decouples ADM-OSC from schema bump | **Zero soak time on reader before customer DAW hands-on**; Critic Round-2 §3.5 + Architect §3.3 both flagged as Principle 5 risk |
| **D3-γ (hybrid — reader early at S2.5, writer late at S7)** ★ | 3-way reader (v1/v2/v3) lands at S2.5 day 1 of sprint, defaults kMute=0 for v1/v2 inputs. Writer + kMute param + Controller's 8th `ParameterInfo` lands at S7. | Reader gets ~5 days of soak with v0.2 preset fixtures before S8; writer's debugging-blast-radius isolation preserved | One commit splits into two; minor merge discipline cost |

**Recommendation**: **D3-γ (hybrid).**

**Why γ**: Pareto-dominates both α and β per Architect Round-2 §3.3 synthesis and Critic Round-2 ask 5. The reader is purely defensive backward-compat code; landing it day 1 means v0.1.0 + v0.2.0 preset interop is regression-tested across the entire sprint. The writer is what produces new v3 bytes; deferring it to S7 keeps it bundled with kMute param + Controller `ParameterInfo` change so all "actual schema bump" effects land in one isolated commit just before DAW hands-on. Cost: zero net effort (one S7 commit splits into S2.5-reader + S7-writer); benefit: ~5 days of v2-fixture soak before customer-facing test.

**Why not α**: Compounds risk surface during S3 — a Reaper preset round-trip bug in S3 would be ambiguous (kMute schema OR SPSC ring? OR both?). γ isolates.

**Why not β**: Pre-mortem 3 (v0.2 preset interop break) has effectively zero soak time before S8 under β. γ closes this gap.

**Invalidation rationale**: D3-α invalidated by debugging-blast-radius argument; D3-β invalidated by Principle 5 (state-ABI preservation) soak-time argument.

### D4 — Vendor-quirks real-data capture schedule (unchanged from Round-2 — Critic asks 11 sharpening applied)

ADR 0006 follow-ups commit to "60-day post-first-contract real vendor capture" replacing the current synthetic CSV fixture. Question: do we hard-schedule, or treat 60-day as soft slack?

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **D4-α (hard 60-day deadline + booked lab session)** ★ | Reserve Korean lab access for day-60-post-tag. Pre-coordinate with first-customer venue to lend console for 4 hours. Block out engineer time. If capture is missed, escalate per cancellation fallback. | Real-data ADR 0012 quirks shipped or known-missing by day-60; no indefinite drift | Requires lab access booking now |
| **D4-β (soft 60-day, slack to 90 days for cert-eval activity)** | Document the 60-day target in release notes; capture happens organically. | Less rigid | Risk of indefinite drift; ADR 0012 stays empty for >90 days |

**Recommendation**: **D4-α.**

**Cancellation fallback (Critic ask 11)**: *"If lab session cancels within 7 days of booked date, ADR 0012 commit at day-60 is a 'no-quirk-observed: synthetic fixture extension to day-90' note, and Notion task auto-files for day-90 re-booking."* This preserves the falsifier discipline of α even under venue cancellation.

**Invalidation rationale**: D4-β invalidated by "OSS-slack drift" pattern; without a hard deadline, real-data capture happens 12+ months later, defeating the purpose of the reserved ADR 0012 slot.

### D5 — macOS / Windows CI matrix scope (unchanged from Round-2)

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **D5-α (v0.3 expansion: add macOS + Windows CI jobs with OSC=OFF)** | Extend `.github/workflows/vst3.yml` with `macos-14` + `windows-2025` runners. | Validates cross-platform compile surface continuously | 1.5–2.5d orthogonal CI work; risk of v0.3 sprint scope creep |
| **D5-β (defer to v0.4 — keep Linux-only CI in v0.3)** ★ | v0.3 ships Linux-only per parent plan §A5-α; macOS/Windows CI starts in v0.4 sprint with full A5-β sidecar-everywhere story. | Keeps v0.3 sprint focused; deferred CI matrix lands as integrated piece with v0.4 cert evidence | Cross-platform compile drift accumulates until v0.4 |

**Recommendation**: **D5-β (defer to v0.4).**

**Invalidation rationale**: D5-α invalidated by sprint-scope-creep math; with the sprint now at ~6.5d (down from 9d after sidecar deletion), there is slack for v03-Q8 marshaling-path work + v03-Q9 mitigation + v03-Q10 XDG fix — but not for orthogonal CI work that would still budget 1.5d minimum.

---

## 3. Implementation steps S2..S8 (revised under D1-γ no-sidecar)

> **Inheritance from v0.2**: S1 (ADR drafts) shipped in v0.2.0. S1's first task in this sprint is *ratification* (Draft → Accepted) of ADRs 0010 + 0011; first content for ADR 0012 reserved for D4-α capture day.

### S1 — ADR ratification + spec-pin update (0.5 day, inherited gate)

**Goal**: Promote ADRs 0010 + 0011 from Draft → Accepted; freeze spec pin to `v0.3.0-c4-final` post-Round-3 APPROVE.

**Files (modified)**:
* `docs/adr/0010-vst3-osc-binding-model.md` — Status: Draft → Accepted; spec pin updates.
* `docs/adr/0011-vst3-osc-multi-instance-discovery.md` — Status: Draft → Accepted.

**Acceptance criteria**:
* A.1 (inherited): ADR Status field flipped, commit references this plan's Round-3 APPROVE consensus.

### S2 — JSON registry library + in-plugin UDP bind + standalone-internal forwarder (1.5 days)

**Goal**: Create the JSON registry I/O library (writer-side GC per ADR 0011 rule 3); add in-plugin UDP `bind()` at `127.0.0.1:9100+N` where N = instance index from registry; add standalone-internal forwarder so console packets reaching `spatial_engine_core:9100` are relayed to each matching instance per `obj_id_subset`.

**Files (new)**:
* `vst3/osc/PluginInstanceRegistry.h` + `.cpp` — Writer (used by plugin instance ctor) + Reader (used by standalone forwarder + ADM-OSC senders). Implements ADR 0011 §Hardening rules 1–6: atomic `tmpfile + rename(2)`, `flock(LOCK_EX|LOCK_NB)` with 10-retry × 50ms backoff, writer-side GC of dead PIDs via `/proc/{pid}/comm` + `boot_id` (v03-Q9 mitigation), reader tolerance for stale entries, `schema_version` fail-closed. **Header-only registry client, no daemon.**
* `core/src/util/RegistryPath.h` — shared constant resolving `$XDG_CONFIG_HOME/spatial_engine/instances.json` (default `~/.config/spatial_engine/instances.json`). Handles `XDG_CONFIG_HOME=""` (set-but-empty) by falling back to `~/.config` per XDG spec (v03-Q10 mitigation, ~5 LOC).
* `vst3/SpatialEnginePluginUdp.h` + `.cpp` — in-plugin UDP I/O thread (A4-β pattern): dedicated `std::thread` at instance construction; owns UDP socket lifecycle; calls `recv()`; decodes via `core/src/ipc/CommandDecoder.cpp:317-373`; pushes decoded `Command`s to per-instance SPSC ring. UDP thread allocates freely per ADR 0010 §A4-β line 80; audio callback pops only (alloc=0).

**Files (existing, additive only)**:
* `vst3/CMakeLists.txt:16-38` — extend `add_library(spatial_engine_vst3 ...)` source list with `osc/*.cpp` + `SpatialEnginePluginUdp.cpp` when `SPATIAL_ENGINE_VST3_OSC=ON`. New CMake option default OFF preserves current build behaviour and OFF byte-baseline.
* `core/CMakeLists.txt` — **no new executable target.** `RegistryPath.h` is header-only and does NOT enter `libspe_core.a` symbol table (verified at A.4).
* `vst3/SpatialEngineProcessor.cpp:32-34` — ctor: when `SPATIAL_ENGINE_VST3_OSC=ON` (compile-time flag), bind UDP socket at `127.0.0.1:9100+N` (A1-ε), register in `instances.json`, spawn UDP thread. When flag OFF, current `0 /*no UDP*/` behaviour preserved.
* `core/src/bin/spatial_engine_core.cpp:99,134` — extend existing OSC dispatch with a "broadcast to plugin registry" hook: on each decoded `Command`, read `~/.config/spatial_engine/instances.json`, look up instances whose `obj_id_subset` covers the command's `obj_id`, and `sendto()` the encoded packet to each instance's `bind_port`. ~150 LOC addition.
* `core/tests/core_unit/test_p_instances_registry.cpp` (new) — atomic write under multi-writer `fork()` stress; reader tolerance for stale entries; writer-side GC of dead PIDs; `boot_id`-based stale-PID-after-reboot scenario (v03-Q9); `XDG_CONFIG_HOME=""` fallback (v03-Q10); `schema_version` fail-closed for forward-incompat.

**Acceptance criteria**:
* **A.3** (Appendix A.3, revised): `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_OSC=ON` builds clean on `ubuntu-24.04 GLIBC 2.39 + GCC 13.3.0`. `grep -rE '#include.*juce' vst3/osc/ vst3/SpatialEnginePluginUdp.{h,cpp}` → 0 hits.
* **A.4** (Appendix A.4): `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_OSC=OFF` produces byte-identical `libspe_core.a` + `spatial_engine_core` artifacts vs current `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`. Additionally: `nm libspe_core.a | grep -E 'RegistryPath|SpscRing'` returns 0 (header-only headers do not enter symbol table).
* **A.5** (Appendix A.5, revised name): `test_p_instances_registry` PASS — atomic write under multi-writer fork, reader tolerance for stale entries, writer-side GC of dead PIDs (including `boot_id`-based stale-PID-after-reboot), XDG empty-string fallback, schema_version refusal for forward-incompat.
* Standalone log emits in **fixed-format** style (`tag=value`): `registry_active_instances=N forwarded_to_count=M dropped_due_unknown_obj_id=K` per dispatch tick.

**File:line citations**: extension points at `vst3/SpatialEngineProcessor.cpp:32-34`, build hooks at `vst3/CMakeLists.txt:16-38`, ADR 0011 hardening rules at `docs/adr/0011-vst3-osc-multi-instance-discovery.md:63-166`.

### S2.5 — Interface freeze + state v3 reader-only (0.5 day, NEW gate per D3-γ + Architect §3.2)

**Goal**: Lock registry library API headers (so S3/S4 can begin); land state v3 *reader-only* (3-way v1/v2/v3 fork at `Processor.cpp:201-323`) defaulting kMute=0 for v1/v2 inputs.

**Files (modified, additive only)**:
* `vst3/SpatialEngineProcessor.cpp:201-323` — extend 2-way (v1/v2) reader to 3-way (v1/v2/v3):
  * v1 reader path: 6 floats, kBypass=0 default, kMute=0 default.
  * v2 reader path: 7 floats, kBypass loaded, kMute=0 default.
  * v3 reader path: 8 floats, all loaded.
  * Forward-incompat (version > 3): throw / fail-closed (consistent with ADR 0011 rule 6).
  * **Reader-only.** Writer still emits v2 in this commit; writer bump waits for S7.
* `vst3/SpatialEngineProcessor.hpp` — `ParamId` enum reserves `kMute = 7` slot in comment block (not in active enum yet); enum activation lands at S7.

**Acceptance criteria**:
* **A.15a** (new, split from Round-2 A.15): `test_vst3_state_v3_reader_only` PASS — 3-way reader matrix: v1 → defaults kBypass=0+kMute=0; v2 → kBypass loaded + kMute=0; v3 → all 8 floats loaded; version=4 fixture → throws.
* `vst3/tests/fixtures/v02_preset_*.vstpreset` committed (snapshotted from a v0.2.0-built plugin save) as fixture for ongoing soak from S2.5 day 1 to S8.
* Interface freeze: `vst3/osc/PluginInstanceRegistry.h` headers reviewed and marked stable; downstream S3/S4 may begin.

**File:line citations**: state reader/writer at `vst3/SpatialEngineProcessor.cpp:201-323`; fixture path `vst3/tests/fixtures/v02_preset_*.vstpreset`.

### S2.6 — VST3 SDK `performEdit` thread-safety audit + marshaling path (0.5 day, NEW gate per Critic ask 3 / Architect v03-Q8)

**Goal**: Resolve v03-Q8 cross-thread `IComponentHandler::performEdit` safety before S4 implements the reverse path.

**Tasks**:
1. Audit `pluginterfaces/vst/ivsteditcontroller.h` for the documented thread contract on `performEdit`/`beginEdit`/`endEdit`.
2. Decide one of:
   * **(a) Host message-thread queue marshaling** ★ default expected — UDP thread pushes `(paramId, value)` to a lockless ring; a host-managed callback (via `IComponent::process` or `IConnectionPoint::notify` from host's message thread) drains the ring and calls `performEdit`.
   * **(b) `IRunLoop`-based marshaling** — preferred over (a) for latency if (and only if) the host exposes `IRunLoop` without an editor view (unexpected per ADR 0010 §A4-γ; verify during audit). If available on all 3 hosts, becomes the chosen path.
   * **(c) Read-only state propagation** — **contingency-of-record if (a) smoke matrix fails on any host.** Controller reads from a shared atomic populated by UDP thread; host's `restartComponent(kParamValuesChanged)` informs DAW. No `performEdit` call; DAW automation lane reflects via restart-notification. No message-thread invariant dependency; safest fallback.
   * **Decision rule**: prefer (b) if universally available; else (a) if 3/3 smoke matrix SUCCESS; else (c) as fallback. Choice + reasoning documented in S2.6 commit footer.
3. Implement chosen marshaling path in `vst3/SpatialEngineController.cpp` notify implementation.
4. Run smoke matrix on Reaper 7.x + Bitwig 5.x + Ardour 8.x Linux: load plugin with `SPATIAL_ENGINE_VST3_OSC=ON`, send a single console `/adm/obj/0/azim 90.0` packet, verify host does not crash and DAW automation lane reflects (where applicable per chosen strategy).

**Files (modified)**:
* `vst3/SpatialEngineController.cpp` — implement marshaling path per (a)/(b)/(c) choice; commit footer documents thread-safety reasoning.
* `vst3/tests/test_vst3_performedit_threadsafe.cpp` (new) — 1000-iter fake-host harness invoking `performEdit` from worker thread, asserts no crash and ordering preserved.

**Acceptance criteria**:
* **A.7-prereq** (S2.6 PASS gates S4 entry): SDK audit conclusion documented in commit footer; smoke matrix 3/3 hosts SUCCESS (no crash); `test_vst3_performedit_threadsafe` PASS.
* Strategy (a)/(b)/(c) noted in §12 v03-Q8 follow-up.

**File:line citations**: notify reversal site at `vst3/SpatialEngineProcessor.cpp:560-579` + `vst3/SpatialEngineController.cpp` notify impl.

### S3 — In-plugin SPSC ring + audio-callback drain (0.5 day, was 2d in Round-2)

**Goal**: Wire the plugin's per-instance SPSC ring (between UDP I/O thread producer and audio callback consumer); audio callback pops at block-rate.

**Files (new)**:
* `core/src/util/SpscRing.h` — single-producer single-consumer ring, fixed-size, lock-free (`memory_order_acquire/release`). Pattern from C1.b commit `6145a53`. **Header-only.**

**Files (existing, additive only)**:
* `vst3/SpatialEngineProcessor.hpp` — add per-instance SPSC ring member (only allocated in ctor when `SPATIAL_ENGINE_VST3_OSC=ON`).
* `vst3/SpatialEngineProcessor.cpp` `process()` entry — audio callback pops queued `Command`s, applies to local `norm_values_` state. Pop is non-blocking; alloc=0; no mutex.
* `vst3/SpatialEnginePluginUdp.cpp` — UDP thread `push(Command)` to ring after decode.
* `vst3/SpatialEngineProcessor.cpp:107-112` (terminate) + `:189-198` (setActive) — clean shutdown of UDP thread + ring.

**Acceptance criteria**:
* **A.6** (Appendix A.6): `test_vst3_intra_plugin_spsc_drain` PASS — 1000-iter UDP-thread push → audio-thread pop; `RT_ASSERT_NO_ALLOC` holds across all 1000 iterations on the **pop side** (audio callback). UDP-thread push side is allowed to alloc per ADR 0010 §A4-β.
* `test_vst3_spsc_ring_overrun` PASS — explicit overrun behaviour: producer continues, oldest entry dropped, `osc_drop_count{instance_id}` increments.
* No additional allocations on audio thread vs C2B baseline (1000-iter probe confirms `alloc_total == 0`).

**File:line citations**: hook at `vst3/SpatialEngineProcessor.cpp:process` entry, ring at `core/src/util/SpscRing.h` (new).

### S4 — In-process reverse path (notify reversal — `performEdit` via S2.6 marshaling) (1.0 day, was 1.5d)

**Goal**: Plugin's UDP thread (or marshaled equivalent per S2.6 outcome) calls `IComponentHandler::performEdit(paramId, value)` so DAW writes the value into its automation state. No cross-process IPC.

**Files (existing, modified additively)**:
* `vst3/SpatialEngineController.cpp` notify implementation — was `kNotImplemented` per AM-R3-10; now implements the S2.6-chosen marshaling strategy. ADR 0010 §Consequences documents this reversal. ~80 LOC.
* `vst3/SpatialEngineProcessor.cpp:560-579` notify reversal site — same pattern if reverse channel goes via Processor's connection point.

**Acceptance criteria**:
* **A.7** (Appendix A.7, sharpened per Critic ask 7): reverse-path host-perceived latency under 64-obj × 1kHz: **p99 ≤ 30ms wall, p50 ≤ 5ms** (under S2.6-chosen marshaling strategy). Measured by `test_vst3_reverse_path` end-to-end timestamp instrumentation. Parent ref: `spatial-engine-phaseC4-and-v0.2-release.md:1193`.
* **A.7-prereq**: S2.6 marshaling strategy implemented and smoke-matrix PASS (gates this acceptance criterion).
* Forward-loop guard: Controller's `performEdit` call carries origin tag; plugin's own param-change broadcast must NOT bounce back. Test: `test_vst3_no_feedback_loop` — 100 round-trips, no exponential packet growth.

**File:line citations**: AM-R3-10 reversal at `vst3/SpatialEngineController.cpp` notify; performEdit call site referenced from `vst3/SpatialEngineController.cpp:561-565`; marshaling rationale in S2.6 commit footer.

### S5 — Korean manual Ch.5 (in-DAW workflow, 3-process topology) (0.5 day)

**Goal**: Document the **3-process topology** (standalone + plugin in DAW + console) — note: not 4-process; sidecar removed per D1-γ.

**Files (new + updated)**:
* `docs/manual_kr/operation/README.md` — new "Ch.5: VST3 플러그인 + ADM-OSC 콘솔 (직결 모드)" section:
  1. Standalone `spatial_engine_core` running on host (port 9100).
  2. Plugin loaded in DAW (Reaper / Bitwig), built with `SPATIAL_ENGINE_VST3_OSC=ON`. On instantiation, plugin binds `127.0.0.1:9100+N` and registers in `~/.config/spatial_engine/instances.json`.
  3. Console sends `/adm/obj/N/...` to standalone:9100; standalone consults registry and `sendto()`s to each matching instance's bound port; plugin's UDP thread decodes and pushes to audio callback.
* ASCII diagram of the 3-process topology.
* TOC link from `docs/manual_kr/operation/README.md` Ch.4 ADM-OSC reference.

**Acceptance criteria**:
* **A.8** (Appendix A.8): Ch.5 renders in `mdcat` / GitHub markdown viewer; cross-links resolve; TOC link added.
* Backward-compat note: standalone-only mode still documented as canonical path for v0.1.0 / v0.2.0 users.
* Troubleshooting subsection: "포트 충돌이 발생하면?" (port collision — PM1 mitigation in customer-facing language) + "레지스트리에서 stale 항목 어떻게 정리?" (stale entry GC).

**File:line citations**: insertion after current Ch.4 boundary in `docs/manual_kr/operation/README.md`.

### S6 — End-to-end integration test + soak (1.0 day, was 1.5d)

**Goal**: Full chain validation — console UDP → standalone forwarder → plugin direct → audio thread → output. Soak under representative load.

**Files (new)**:
* `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` — full chain (3 processes; no sidecar hop):
  1. Spawn `spatial_engine_core` on test port (e.g. 9201, not prod 9100).
  2. Instantiate plugin via existing C2B host fixture pattern (`vst3/tests/test_vst3_host_fixture.cpp` C2B B.2).
  3. Send `/adm/obj/0/aed (45.0, 30.0, 0.5)` via UDP `sendto()` to 9201.
  4. Assert plugin's `norm_values_[kPanAz]` ≈ `Controller::plainParamToNormalized(0, az_rad)` within tolerance, within latency target.
* `vst3/tests/perf/soak_vst3_console_flood.cpp` — soak: 1 obj × 100 Hz × 60s per plugin instance, 8 plugin instances concurrent (per ADR 0010 thread-budget invariant N≤8). Asserts:
  * Audio thread alloc==0 across all 8 instances (RT_ASSERT_NO_ALLOC harness).
  * **p99 console-OSC → plugin-param-write latency < 2ms** end-to-end (one UDP roundtrip + one block-rate drain @ 48k/64 = ~1.3ms baseline; tighter than Round-2 because no sidecar hop — matches Architect §4.1 line 95).
  * Per-plugin recv-buffer no growth over 60s.
  * Zero packet drops at registry layer.

**Acceptance criteria**:
* **A.9** (Appendix A.9, sharpened): `test_vst3_e2e_console_to_plugin` PASS — **p99 reverse-path host-perceived latency ≤ 50ms** (DAW automation tick granularity per Critic ask 7).
* **A.10** (Appendix A.10): `soak_vst3_console_flood` PASS — alloc==0, **p99 < 2ms**, 60s clean.
* GHA `.github/workflows/vst3.yml` extended with new ctest invocations; both jobs SUCCESS.

**File:line citations**: pattern reference `core/src/ipc/CommandDecoder.cpp:317-373` for ADM dispatch; fixture pattern from existing `vst3/tests/test_vst3_host_fixture.cpp` (C2B B.2).

### S7 — OFF byte-baseline re-pin + state v3 writer + kMute param (0.5 day)

**Goal**: Re-pin OFF dual-gate hashes only if a deliberate paired core/ change is enumerated; land state v3 *writer* + kMute param + Controller's 8th `ParameterInfo` per D3-γ.

**Files (modified)**:
* `vst3/SpatialEngineProcessor.cpp:201-323` — extend the v3 reader (already landed at S2.5) by adding the v3 *writer* branch: 40 bytes, 8 floats, magic `'SPE1'`, version=3. Writer emits v3 only from this commit onward.
* `vst3/SpatialEngineProcessor.hpp` — `ParamId` enum activates `kMute = 7`; `norm_values_` resized to `[8]`.
* `vst3/SpatialEngineController.cpp:107-216` — add 8th `ParameterInfo` for kMute with `kIsMute = 1<<17` flag (VST3 SDK convention from `pluginterfaces/vst/ivsteditcontroller.h`).
* `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` — re-pin **only if** a paired core/ change is explicitly enumerated in this commit (mirror pattern from `8c0ca2d`/`587815c`). **Expected: no re-pin** because all v0.3 work stays in `vst3/` + `core/src/bin/` + `core/src/util/` (header-only).

**Acceptance criteria**:
* **A.11** (Appendix A.11, sharpened per Critic ask 6): **Hash of `libspe_core.a` and `spatial_engine_core` matches baseline, OR a documented re-pin commit references a deliberate symbol/data change.** No paired change → no re-pin → drift is a CI fail. Expected outcome: hash match, no re-pin needed.
* `test_vst3_state_v3_persist` PASS (S7 extension of S2.5's reader-only test): v3 round-trip + writer emits magic/version/payload per schema.
* `grep -n 'kStateVersionV3' vst3/SpatialEngineProcessor.cpp` confirms new constant in place.
* `spatial_engine_vst3.so` size delta from v0.2 (891 KB) bounded — expect <30 KB growth from new code surface.

**File:line citations**: state writer modification at `vst3/SpatialEngineProcessor.cpp:201-323`; ParamId enum at `vst3/SpatialEngineProcessor.hpp:32-40`; Controller param table at `vst3/SpatialEngineController.cpp:107-216`.

### S8 — DAW hands-on (Reaper + Bitwig, 8 params incl. kMute v3) (1.0 day)

**Goal**: End-to-end validation in real DAW hosts. Captures the customer-facing first-impression smoke.

**Files (new)**:
* `docs/release/v0.3.0/daw-handson-log.md` — checklist with screenshots / WAV captures.

**Per-host checklist (Reaper 7.x Linux + Bitwig 5.x Linux)**:
1. Load `spatial_engine_vst3.so` (built with `SPATIAL_ENGINE_VST3_OSC=ON`) on a stereo track.
2. Verify 8 params appear in DAW's plugin UI: kPanAz, kPanEl, kSourceWidth, kMasterGain, kAmbiOrder, kRoomPreset, kBypass, kMute.
3. Automate kPanAz over 4 bars at 120 BPM; confirm engine output reflects.
4. Toggle kMute; confirm output goes silent (vs kBypass which is dry pass-through).
5. Save DAW project, close, reopen; verify all 8 params restore via state v3 reader.
6. Open project saved with v0.2 plugin (state v2); verify v3 reader falls back gracefully, kMute=0 default.
7. Direct reverse-path workflow: open Reaper + standalone (port 9100); send `/adm/obj/0/azim 90.0` from `oscchief`; observe param value update in DAW automation lane within ≤50ms (A.9 target).
8. Bypass + Mute interaction: kBypass=on + kMute=off → dry pass-through (host audible); kMute=on → silent regardless of bypass.

**Acceptance criteria** (falsifiable per-param table):

| Param | Falsifiable pass criterion |
| ----- | -------------------------- |
| kPanAz, kPanEl, kSourceWidth, kMasterGain, kAmbiOrder, kRoomPreset, kBypass | (inherits parent plan §R3 falsifiable criteria — already verified for v0.2) |
| **kMute** (new) | kMute=on → recorded WAV is digital silence (RMS < -120dBFS); kMute=off → audible audio matches non-muted reference. Distinct from kBypass (dry pass-through). |
| **Direct reverse path** (new) | Console sends `/adm/obj/0/azim 90.0` → DAW automation lane for kPanAz reflects within ≤50ms (A.9); DAW project save captures this value. |

* **A.12** (Appendix A.12, quantified per Critic ask 7 + 13): DAW hands-on log committed with: **minimum 1 screenshot per host showing all 8 params in plugin UI; 1 WAV recording per host showing kMute audible distinction from kBypass; checklist Y/N entries for each of 8 numbered steps per host.** Total deliverable: 2 hosts × (1 screenshot + 1 WAV + 8 Y/N) = 2 screenshots + 2 WAVs + 16 checklist entries minimum.
* All 0 crashes, 0 stuck params, 0 audio glitches at default block size 512.
* State save/load round-trip works: save project, close, reopen, params restore (state v3).
* v0.2 (state v2) project loads cleanly via 3-way reader fork (already soaked since S2.5); kMute defaults to off.

**File:line citations**: kMute integration verified against `vst3/SpatialEngineProcessor.cpp:421-447` bypass dry pass-through (must remain distinct); state v3 reader paths from S2.5 + writer paths from S7.

---

## 4. Cross-step coupling matrix

| Step | Blocks (downstream) | Blocked by (upstream) | Independent of |
| ---- | ------------------- | --------------------- | -------------- |
| **S1 (ratification, inherited)** | S2 (ADR-driven schema) | none (v0.2 ships drafts) | S5 |
| **S2 (registry + in-plugin UDP + standalone forwarder)** | S2.5 (registry API needed), S3 (UDP thread pushes to ring), S4 (Controller needs registry path), S6 (e2e needs all parts) | S1 (ADR-defined schema) | S5 |
| **S2.5 (interface freeze + state v3 reader-only)** | S3 (SPSC ring API frozen), S4 (Controller header frozen) | S2 (registry API) | S5 |
| **S2.6 (performEdit thread-safety audit + marshaling)** | S4 (reverse path implements chosen strategy) | S2 (SDK headers reviewable) | S5 |
| **S3 (intra-plugin SPSC ring drain)** | S6 (e2e exercises this path) | S2.5 (interface freeze) | S4, S5, S7 |
| **S4 (in-process reverse path)** | S6 (e2e exercises reverse), S8 (DAW hands-on tests reverse) | S2.6 (marshaling strategy) | S5, S7 |
| **S5 (manual Ch.5)** | none (doc-only) | S2–S4 (workflow described must match implementation) | S3, S6, S7 |
| **S6 (e2e + soak)** | S8 (hands-on confidence rests on automated soak) | S2, S3, S4 (all paths must exist) | S5, S7 |
| **S7 (OFF re-pin + state v3 writer)** | S8 (DAW must see kMute param) | S2.5 (reader landed; writer extends), no longer blocked by S6 baseline drift | S3, S4, S5, S6 |
| **S8 (DAW hands-on)** | none (terminal) | S2–S7 (all must land) | none |

**Critical path**: S1 → S2 → S2.5 → S3 → S6 → S8 (≈ 5.0d). S2.6 + S4 + S7 fork from S2.5; S5 floats anywhere from S2 onward.

**Parallelisation opportunities** (for ulw or team-mode execution):
* S5 (manual) can be drafted from §3 of this plan during S2-S4 sprint days.
* S7 (state v3 writer + OFF re-pin) can be drafted as branch off main while S6 runs; merged just before S8.
* S2.6 (SDK audit) can run in parallel with S3 (intra-plugin SPSC) since S3 doesn't touch Controller.
* S4 (reverse path) cannot start until S2.6 resolves the marshaling strategy.

---

## 5. Pre-mortem (3 scenarios, A1-ε-realistic, with detection + mitigation + recovery)

### Scenario 1 — `bind()` collision on plugin instantiation (NEW per Critic ask 8)

**What goes wrong**: User launches Reaper with a project that previously held 4 plugin instances; the project crashes / Reaper hangs / power loss leaves stale entries in `instances.json` referencing ports `9100`/`9101`/`9102`/`9103`. On next session, plugin instance 5 starts and tries to `bind(127.0.0.1:9100+N)` where N is allocated from registry. If the registry's writer-side GC missed (e.g. crash bypassed shutdown handler), the port may still be held by a process the kernel hasn't reaped, OR — more commonly — another non-plugin process (test harness leftover, `nc -l 9101`, etc.) holds the port. `bind()` returns `EADDRINUSE`. Plugin instance fails to construct; user sees plugin load error in DAW.

**Probability**: MODERATE (writer-side GC handles clean shutdown; covers ~95% of cases; the 5% is hard-power-loss + port-recycle by unrelated process).
**Severity**: MEDIUM (plugin load fails loudly; user-visible error message; not silent data corruption).

**Detection**:
* `bind()` returns `EADDRINUSE`; plugin ctor logs `bind_collision instance_id=X attempted_port=N` at stderr ERROR level.
* DAW reports plugin instantiation failure with an error code mapped to "OSC port unavailable".
* `osc_bind_failure_count{instance_id}` metric in `/sys/state` poll (incremented on each retry).

**Mitigation**:
1. **Writer-side GC with `boot_id` (v03-Q9)**: each registry entry includes `boot_id` from `/proc/sys/kernel/random/boot_id`. On next boot, all entries with stale `boot_id` are GC'd regardless of PID, freeing their ports for re-use.
2. **Port-walk retry**: plugin ctor tries `9100+N`, on `EADDRINUSE` walks to `9100+N+1` up to N+15 (16 attempts) before failing. Records the actual bound port in the registry entry. Standalone reads `bind_port` field (not the implicit `9100+N` formula) for forwarding.
3. **Documented troubleshooting** in manual Ch.5: "포트 충돌이 발생하면? Check `~/.config/spatial_engine/instances.json` for stale entries; restart `spatial_engine_core`; check `ss -tunlp | grep 91` for non-plugin holders."
4. **Pre-startup probe**: plugin ctor tries `socket(SOCK_DGRAM) + bind(0)` (kernel-assigned ephemeral) as fallback if walked range exhausted; registry stores ephemeral port. Loses port-predictability but preserves functionality.

**Recovery**:
* User restarts DAW → plugin ctor walks port range → succeeds.
* If stale `instances.json` entries persist: user runs `rm ~/.config/spatial_engine/instances.json` (re-created on next plugin ctor); documented in Ch.5 troubleshooting.

**Test coverage**: `test_vst3_bind_collision` (new in S2) — explicitly holds `127.0.0.1:9100` via separate `socket+bind` in test harness, then instantiates plugin, asserts plugin walks to 9101 and registers correctly; asserts standalone forwarder reads `bind_port=9101` from registry.

### Scenario 2 — Registry file corruption mid-write (NEW per Critic ask 8)

**What goes wrong**: Plugin instance is in the middle of an atomic `tmpfile + rename` registry update. Power loss, kernel panic, or filesystem ENOSPC interrupts the write. Alternatively, two plugin instances acquire `flock` in rapid succession (within microseconds), one's `rename` completes before the other's `tmpfile` write finishes — neither sees inconsistency on its own, but a third reader between the two renames sees an old-then-old-again sequence. More seriously: filesystem full causes `write()` to return short-count; the in-flight `tmpfile` is half-written, but the `fsync` followed by `rename` is short-circuited. On next read, standalone forwarder sees malformed JSON.

**Probability**: LOW (atomic `rename(2)` is POSIX-guaranteed; `flock` discipline handles contention; ENOSPC is rare on dev machines; power loss is rare in studio environments).
**Severity**: MEDIUM (forwarder stops working — console packets aren't routed to plugins; recovery requires manual intervention or next clean shutdown).

**Detection**:
* Standalone forwarder reads `instances.json`; JSON parse fails with line-column error → log WARN `registry_parse_failure path=~/.config/... err=...`.
* `osc_drop_count{instance_id=unknown}` increments at standalone since no instances are now visible.
* `/sys/state` exposes `registry_parse_failure_count` metric.

**Mitigation**:
1. **Atomic `tmpfile + rename(2) + fsync` discipline** (ADR 0011 rule 1): writer never modifies file in place; if interrupted before `rename`, the old file is preserved; if interrupted before `fsync` of tmpfile, the rename is never executed.
2. **`flock(LOCK_EX|LOCK_NB)` with 10-retry × 50ms backoff** (ADR 0011 rule 2): bounded contention worst-case 500ms; serializes concurrent writers; advisory but honoured by all plugin instances (cooperating processes).
3. **Reader-side parse-error tolerance**: if standalone forwarder fails to parse, retry after 1s; if still fails after 5 retries, log ERROR and stop forwarding (do not silent-drop; user-visible).
4. **Schema version fail-closed (ADR 0011 rule 6)**: forward-incompat schema versions cause refusal, not partial-read.
5. **Truncation guard**: if file size is 0 bytes (empty), treat as "no instances registered" and continue (don't error); this handles the gap between rm and next write.

**Recovery**:
* User runs `rm ~/.config/spatial_engine/instances.json` (documented in Ch.5).
* Next plugin instance ctor writes a fresh registry.
* Standalone forwarder re-reads fresh file on next 5s tick; forwarding resumes.

**Test coverage**: `test_p_instances_registry_corruption` (new in S2) — writes a partial JSON (truncated mid-array), asserts reader handles gracefully; writes empty file, asserts reader treats as zero instances; multi-writer fork stress (5 processes × 100 writes each), asserts no observed parse failure under `flock` discipline.

### Scenario 3 — State v3 schema migration breaks v0.2.0 preset interop on user upgrade (retained, sharpened with D3-γ)

**What goes wrong**: A first-customer engineer upgrades from v0.2.0 to v0.3.0. They have saved Reaper / Bitwig projects from v0.2 that embed plugin state in v2 format (36 bytes, 7 floats). v0.3 plugin's 3-way reader fork has a subtle bug — perhaps an off-by-one byte offset in the v2 branch, or `defaultNormalizedValue` for kMute is not zero, or the v2 reader fails to set `kMute` default cleanly. User's projects load with **wrong parameter values** (kPanAz reads 0 instead of 0.5, etc.). First impression of v0.3 upgrade: "the plugin is broken." Customer reverts to v0.2; v0.3 adoption stalled.

**Probability**: LOW under D3-γ (reader landed at S2.5 day 1 → ~5 days of v2-fixture soak before customer touches it). Was MODERATE under Round-2 D3-β.
**Severity**: HIGH (first-impression damage; trust erosion; rollback signals).

**Detection**:
* `test_vst3_state_v3_reader_only` (S2.5) covers v1→v3, v2→v3, v3→v3 round-trips. Each path's defaulting behaviour explicitly asserted.
* Real v0.2-saved `.vstpreset` files committed as test fixtures at `vst3/tests/fixtures/v02_preset_*.vstpreset` from S2.5 day 1 — exercised by every ctest run from S2.5 onward.
* DAW hands-on S8 explicitly includes step 6: "Open project saved with v0.2 plugin (state v2); verify v3 reader falls back gracefully."
* Pre-release: announce in v0.3 release notes "v0.2.0 presets load via v3 reader fallback; please report any param-value drift."

**Mitigation**:
1. **D3-γ hybrid timing**: state v3 *reader* lands at S2.5 day 1; gets ~5 days of soak with v0.2 fixtures before customer DAW hands-on S8. State v3 *writer* lands at S7 (isolated commit; debugging-blast-radius preserved).
2. Fixture-based regression tests use real v0.1.0 + v0.2.0 preset files (committed to repo from S2.5).
3. Reader fail-closed semantics: if version > 3 OR magic mismatch, throw rather than partial-read (consistent with ADR 0011 hardening rule 6).
4. Release notes §호환성 / Compatibility explicitly lists "v0.1.0 / v0.2.0 .vstpreset files supported via 3-way reader fork."

**Recovery**:
* If a v3 reader bug ships: v0.3.1 patch with corrected reader; users with broken projects regenerate from a v0.2 rollback session (no data loss — Reaper projects are XML, the `.vstpreset` blob is reproducible).
* Pre-shipping detection via S2.5-onward soak + S8 hands-on prevents this entirely.

**Test coverage**: `test_vst3_state_v3_reader_only` (new at S2.5) + extended to `test_vst3_state_v3_persist` (S7 writer); 12+ test cases:
* v1 → v3 (3 fixture-based + 3 synthetic)
* v2 → v3 (3 fixture-based + 3 synthetic) — exercised on every CI run from S2.5 day 1
* v3 → v3 round-trip (1, from S7)
* Forward-incompat (version=4) → throws (1)
* Magic mismatch → throws (1)

---

## 6. Risks and mitigations (rewritten — sidecar rows removed; A1-ε realistic rows added)

| Risk | Step | Impact | Mitigation |
| ---- | ---- | ------ | ---------- |
| `bind()` collision on plugin ctor (PM1) | S2 | Plugin load fails | Writer-side GC with `boot_id` (v03-Q9); 16-port walk; ephemeral fallback; manual troubleshooting Ch.5 |
| Registry file corruption mid-write (PM2) | S2 | Forwarder stops routing | Atomic `tmpfile+rename+fsync` (ADR 0011 rule 1); `flock` discipline (rule 2); reader-side parse-error tolerance with retry-or-fail-loud |
| `IComponentHandler::performEdit` cross-thread UB (v03-Q8) | S2.6/S4 | DAW crash on reverse-path | S2.6 SDK audit + marshaling strategy (a/b/c); 3-host smoke matrix (Reaper/Bitwig/Ardour) before S8; `test_vst3_performedit_threadsafe` 1000-iter |
| Multi-instance JSON registry race condition under fork() stress | S2 | Torn read / lost instance | ADR 0011 hardening rules 1-6 mandatory; `test_p_instances_registry` covers multi-writer fork stress + stale-PID-after-reboot (`boot_id`) |
| State v3 reader bug breaks v0.2 preset interop (PM3) | S2.5/S7 | First-customer upgrade pain | D3-γ hybrid: reader at S2.5 gets ~5 days of soak with v0.2 fixtures; writer at S7 isolated commit; hands-on S8 step 6 explicit |
| `IConnectionPoint::notify` reversal breaks host expectations | S4 | Reaper/Bitwig host code may have relied on `kNotImplemented` | S2.6 3-host smoke matrix; ADR 0010 §Consequences documents reversal; reverse-channel is opt-in via `SPATIAL_ENGINE_VST3_OSC=ON` build flag (host that doesn't opt-in unaffected — note: ON is default for v0.3 Linux build so framing is informational) |
| OFF byte-baseline drift from any leaked core/ change | S7 | CI fails post-merge | S7 binary criterion (A.11): hash match OR explicit re-pin commit citing paired change; pre-merge check via local `cmake -DSPATIAL_ENGINE_VST3=OFF` baseline diff; expected outcome = hash match (no re-pin) |
| Vendor quirks captured during D4-α 60-day session reveal core decode bug | post-S8 | Spec-compliance bug in `CommandDecoder.cpp` | ADR 0006 §Vendor note path: quirks live at bridge layer (`bridge/_adm_osc_common.py`), not core; ADR 0012 fills with first observation; v0.3.1 patch path for core if bridge can't compensate |
| N>8 plugin instances exceeds ADR 0010 thread-budget invariant | runtime | Pro Tools etc. thread cap exceeded | Deployment guidance documented in manual Ch.5 + ADR 0010; soak (S6) tests N=8 explicitly; runtime warning logged if N>8 detected via registry |
| Sprint slip from D5-α attempt (if Architect overrides) | sprint-level | v0.3 ETA blown | D5-β recommendation; if Architect picks α, ETA banner bumps to 11-15d (slack widened) |

---

## 7. Expanded test plan (DELIBERATE-mode — refreshed under A1-ε)

### 7.1 Unit tests

| Test file | What it asserts | Step |
| --------- | --------------- | ---- |
| `core/tests/core_unit/test_p_instances_registry.cpp` | ADR 0011 §Hardening rules 1-6 + v03-Q9 `boot_id` stale-PID + v03-Q10 XDG empty-string + multi-writer fork stress | S2 |
| `core/tests/core_unit/test_p_instances_registry_corruption.cpp` | PM2 corruption scenarios: truncated JSON, empty file, multi-writer race-induced parse failure | S2 |
| `core/tests/core_unit/test_p_spsc_ring.cpp` | SPSC ring: producer/consumer ordering, lock-free invariants, overrun (oldest entry dropped, drop_count++), wraparound, alloc-zero (pop side) under 1000-iter stress | S3 |
| `vst3/tests/test_vst3_bind_collision.cpp` | PM1: port already held → walk 16 ports → register with actual bound port; standalone forwarder reads `bind_port` field correctly | S2 |
| `vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp` | 1000-iter UDP-thread push → audio-thread pop; RT_ASSERT_NO_ALLOC on pop side; UDP-thread alloc OK per A4-β; ordering preserved | S3 |
| `vst3/tests/test_vst3_performedit_threadsafe.cpp` | S2.6 v03-Q8: 1000-iter `performEdit` from non-message thread via chosen marshaling strategy; no crash; ordering preserved | S2.6 |
| `vst3/tests/test_vst3_state_v3_reader_only.cpp` | 12-case matrix at S2.5: v1/v2/v3 readers, default-fill for missing params, forward-incompat fail-closed, magic mismatch fail-closed, fixture-based v0.1/v0.2 preset interop | S2.5 |
| `vst3/tests/test_vst3_state_v3_persist.cpp` | Extends S2.5 reader test with S7 writer: v3 round-trip + writer emits magic/version/payload per schema | S7 |
| `vst3/tests/test_vst3_no_feedback_loop.cpp` | Reverse-path Controller `performEdit` carries origin tag; plugin's own param-change broadcast does NOT bounce back; 100 round-trips bounded growth | S4 |

### 7.2 Integration tests

| Test file | What it asserts | Step |
| --------- | --------------- | ---- |
| `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` | Full chain (3-process; no sidecar): console UDP `/adm/obj/0/aed` → standalone:9201 (test port) → standalone forwarder via registry → plugin's `bind_port` → `norm_values_[kPanAz]` within tolerance within latency target | S6 |
| `vst3/tests/test_vst3_registry_stale_cleanup.cpp` | Start plugin A, start plugin B; kill plugin A's process; plugin C starts and writer-side GC sweeps A; registry has only B + C; standalone forwarder's view consistent | S2 |

### 7.3 End-to-end (DAW hands-on, S8)

| Host | Coverage |
| ---- | -------- |
| Reaper 7.x Linux | 8 params automatable; kMute distinct from kBypass; state v3 save/load + v2-preset compat; direct reverse-path observed in automation lane (≤50ms); 7 OSC paths smoked through standalone forwarder |
| Bitwig 5.x Linux | Same 8-param coverage; verify Bitwig's automation surface accepts new kMute param; verify reverse-path through Bitwig's host message-thread model (S2.6 strategy choice validated) |

**Deliverable (A.12 quantified)**: 2 hosts × (1 screenshot showing 8 params + 1 WAV showing kMute distinction from kBypass + 8 Y/N checklist entries) = 2 screenshots + 2 WAVs + 16 checklist entries minimum.

Output: `docs/release/v0.3.0/daw-handson-log.md` with screenshots, recorded WAV captures, polar log dumps via `scripts/dump_polar_log.py`.

### 7.4 Observability metrics

New endpoint surface: standalone exposes via existing `/sys/state` poll path (no new sidecar endpoint needed).

Metrics:
* `registry_active_instances` (count of active plugin instances, gauged from `instances.json`)
* `spsc_backlog{instance_id}` (per-instance ring fill level, 0..ring_capacity) — gauged per audio block
* `osc_drop_count{instance_id}` (cumulative recv-buffer / SPSC overrun drops)
* `osc_bind_failure_count{instance_id}` (PM1 detection: bind retries due to EADDRINUSE)
* `registry_parse_failure_count` (PM2 detection)
* `reverse_path_p50_ms`, `reverse_path_p99_ms` (rolling 60s window — S2.6 marshaling latency)

Logging (fixed-format, `tag=value` style per Critic ask 14):
* Standalone stderr at default log-level: registry-read events (every 5s) and forwarding events (decimated to 1Hz), errors at ERROR level.
* Plugin: bind result + UDP thread start/stop only (no audio-thread logs).

CI gate: any flaky test (>1 failure in 5 retries on `vst3.yml`) blocks the v0.3 tag.

### 7.5 Soak

| Soak test | Profile | PASS criteria |
| --------- | ------- | ------------- |
| `soak_vst3_console_flood` | 1 obj × 100 Hz × 60s per instance, 8 instances concurrent (matches ADR 0010 N≤8 ceiling) | audio thread alloc==0; **p99 console→plugin < 2ms** (no sidecar hop); per-plugin recv-buffer steady; zero crashes |
| Inherited from C3 | 64 obj × 1 kHz × 60s standalone-direct (no plugin instances) | 0 reject, 0 xrun (current v0.2.0 baseline preserved) |

(Storm soak `soak_vst3_storm` deleted with sidecar; the intra-plugin SPSC ring's overrun behaviour is unit-tested in `test_p_spsc_ring.cpp` + `test_vst3_intra_plugin_spsc_drain.cpp`. PM2 corruption is unit-tested separately.)

### 7.6 RT-safety

| Probe | Coverage |
| ----- | -------- |
| 1000-iter alloc-zero (existing harness `test_vst3_dispatch_rt_safety`) | Extended to cover SPSC consumer-side (audio callback's pop); alloc_total == 0 across 1000 iterations on the pop side. UDP-thread push side allowed to alloc per ADR 0010 §A4-β. |
| `test_vst3_state_v3_persist_alloc_zero` | State write/read paths (non-audio-thread, but still alloc-bounded); 100-iter, alloc bounded to expected `IBStream::read/write` allocations only |
| Negative controls | `probe_observes_malloc` + `probe_observes_calloc` (inherited from C2B) — probe itself verified to detect allocations |

---

## 8. ADR ratification table (unchanged from Round-2)

| ADR | v0.2.0 status | v0.3 sprint action | Post-sprint status |
| --- | ------------- | ------------------ | ------------------ |
| 0010 (VST3 OSC binding model) | Draft | S1 ratification (Round-3 ralplan APPROVE) + implement S2-S8 | **Accepted** |
| 0011 (multi-instance discovery) | Draft | S1 ratification + implement S2 | **Accepted** |
| 0012 (ADM-OSC vendor quirks) | Reserved slot (empty) | D4-α capture session day-60; fill with first observed quirk if any; otherwise status unchanged | **Reserved** (if no quirk found) or **Accepted with Quirk-1** (if quirk found) |
| 0003 (IPC: OSC over UDP) | Accepted | No change; ADR 0010 §Context paragraph reaffirms orthogonality | **Accepted** (unchanged) |
| 0006 (ADM-OSC v1.0 spec freeze) | Accepted | No change; v0.3 receive path reuses same `Command` schema | **Accepted** (unchanged) |

ADR 0010 §Status field changes from `Draft (v0.2.0 ships this design contract; v0.3.0 implements)` to `Accepted (v0.3.0 implementation landed; spec commit pin: v0.3.0-c4-final)` upon S8 PASS + v0.3 tag.

ADR 0011 same status transition.

---

## 9. ETA banner (per-step + cumulative + slack — revised under D1-γ)

| Step | ETA | Cumulative | Critical path? |
| ---- | --- | ---------- | -------------- |
| S1 (ratification, inherited from v0.2) | 0.5d | 0.5d | yes |
| S2 (registry + in-plugin UDP + standalone forwarder) | 1.5d | 2.0d | yes |
| S2.5 (interface freeze + state v3 reader-only) | 0.5d | 2.5d | yes |
| S2.6 (performEdit thread-safety audit + marshaling) | 0.5d | 3.0d | yes (gates S4) |
| S3 (intra-plugin SPSC ring drain) | 0.5d | 3.5d | yes |
| S4 (in-process reverse path) | 1.0d | 4.5d | partial (S2.6 already on critical) |
| S5 (manual Ch.5) | 0.5d | 4.5d (parallel) | no |
| S6 (e2e + soak; storm deleted) | 1.0d | 5.5d | yes |
| S7 (OFF re-pin + state v3 writer + kMute param) | 0.5d | 6.0d | yes |
| S8 (DAW hands-on) | 1.0d | 7.0d (S5 already parallel; treat as 6.5d effective) | yes |
| **Total (effective)** | | **~6.5d** | (was 9d in Round-2; freed 2.5d after sidecar deletion) |
| **Cert-eval slack** | +3-4d | **~9.5-10.5d wall** | for D4-α lab booking, observability endpoint integration, S2.6 marshaling-path debugging |

Critical-path total: ~5.0d (S1 → S2 → S2.5 → S3 → S6 → S8). S2.6 + S4 + S7 fork from S2.5. Other steps can parallelise with team-mode execution.

D4-α 60-day capture session is **post-sprint** (real-vendor data flows through ADR 0012 update on day-60, not blocking v0.3 tag). Cancellation fallback: day-90 re-booking per D4-α invalidation block.

---

## 10. Acceptance criteria index (100% testable; sharpened per Critic asks 6/7/13)

| ID | Criterion | Step | Test mechanism | Parent plan ref |
| --- | --------- | ---- | -------------- | ---------------- |
| A.3 | `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_OSC=ON` builds on ubuntu-24.04 GLIBC 2.39; 0 JUCE includes in `vst3/osc/` + `SpatialEnginePluginUdp.{h,cpp}` | S2 | `grep -rE '#include.*juce' vst3/osc/ vst3/SpatialEnginePluginUdp.*` returns 0 | Appendix A.3 |
| A.4 | `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_OSC=OFF` produces byte-identical OFF artifacts | S2/S7 | sha256 hash compare against `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`; also `nm libspe_core.a \| grep -E 'RegistryPath\|SpscRing'` returns 0 | Appendix A.4 |
| A.5 | `test_p_instances_registry` PASS (atomic write, GC, PID liveness via `boot_id`, schema_version, XDG empty-string) + `test_p_instances_registry_corruption` PASS (PM2 scenarios) | S2 | ctest | Appendix A.5 |
| A.6 | `test_vst3_intra_plugin_spsc_drain` PASS, 1000-iter, alloc==0 in audio-callback pop side; UDP-thread push side allowed to alloc per ADR 0010 §A4-β | S3 | ctest + RT_ASSERT_NO_ALLOC on pop | Appendix A.6 |
| **A.7** (sharpened, reverse-path budget per S2.6 marshaling strategy) | Reverse-path host-perceived round-trip latency under SDK message-thread marshaling (S2.6 (a)/(b)/(c) chosen path): **p99 ≤ 30ms wall, p50 ≤ 5ms** under 64-obj × 1kHz reverse-traffic load. Bounded above by A.9 (≤ 50ms DAW automation tick granularity); bounded below by A.10 (forward p99 < 2ms intra-process). Parent ref: `spatial-engine-phaseC4-and-v0.2-release.md:1193` (parent A.7 reverse-path). | S4/S6 | `test_vst3_reverse_path` + soak instrumentation | Appendix A.7 |
| **A.7-prereq** (NEW) | S2.6 PASS: SDK audit conclusion documented; 3-host smoke matrix (Reaper/Bitwig/Ardour) SUCCESS; `test_vst3_performedit_threadsafe` PASS | S2.6 | Smoke matrix log committed; ctest | — |
| A.8 | Manual Ch.5 added with 3-process topology diagram and TOC link | S5 | markdown render + TOC link grep | Appendix A.8 |
| **A.9** (sharpened) | `test_vst3_e2e_console_to_plugin` PASS — **p99 reverse-path host-perceived latency ≤ 50ms** (DAW automation tick granularity) | S6 | ctest with timing assertion | Appendix A.9 |
| A.10 | `soak_vst3_console_flood` PASS: 1 obj × 100 Hz × 60s × 8 instances, alloc==0, **p99 < 2ms** (no sidecar hop) | S6 | ctest perf job | Appendix A.10 |
| **Latency ladder note** (post-Option-α) | A.10 (forward, p99 < 2ms, intra-process) < A.7 (reverse, p99 ≤ 30ms, SDK message-thread marshaling overhead) < A.9 (reverse host-perceived, p99 ≤ 50ms, DAW automation tick granularity). Three distinct physical paths with strictly-monotonic latency budgets; SDK marshaling adds ~28ms over forward intra-process, DAW tick adds another ~20ms. | — | — | — |
| **A.11** (sharpened — binary criterion) | **Hash of `libspe_core.a` and `spatial_engine_core` matches baseline, OR a documented re-pin commit references a deliberate symbol/data change.** Expected outcome: hash match (no re-pin). | S7 | dual-gate sha256 compare; CI run SUCCESS | Appendix A.11 |
| **A.12** (quantified) | DAW hands-on log committed: **minimum 1 screenshot per host (8 params visible) + 1 WAV per host (kMute audible distinction from kBypass) + 8-step Y/N checklist per host** = 2 screenshots + 2 WAVs + 16 checklist entries total minimum | S8 | hands-on log + state v3 fixture round-trip | Appendix A.12 |
| A.13 (new — bind collision) | PM1 mitigation verified: 16-port walk on EADDRINUSE; ephemeral fallback documented | S2 | `test_vst3_bind_collision` ctest | — |
| A.14 (new — registry corruption) | PM2 mitigation verified: truncated/empty/multi-writer-stress registry handled gracefully | S2 | `test_p_instances_registry_corruption` ctest | — |
| A.15 (split into A.15a + A.15b) | PM3 mitigation verified: v0.2 preset (state v2) loads cleanly under v3 reader | — | — | — |
| A.15a (S2.5) | `test_vst3_state_v3_reader_only` PASS with v0.1/v0.2 fixtures committed at S2.5; ~5 days of soak before S8 | S2.5 | ctest (runs every CI from S2.5 onward) | — |
| A.15b (S7) | `test_vst3_state_v3_persist` PASS — extends A.15a with v3 writer round-trip | S7 | ctest | — |

Testable: 16/16 = 100%. (≥90% target exceeded.)

---

## 11. Appendix — Files touched summary (rewritten under D1-γ)

### New files (created during S2-S8)

* `vst3/osc/PluginInstanceRegistry.h` + `.cpp` (S2) — header-only registry client, writer-side GC, `boot_id` mitigation
* `vst3/SpatialEnginePluginUdp.h` + `.cpp` (S2) — in-plugin UDP I/O thread per A4-β
* `vst3/SpatialEngineRegistry.h` + `.cpp` (S2) — Controller-side registry client wrapper (read-only access for forward-loop guard)
* `core/src/util/RegistryPath.h` (S2) — header-only XDG path resolver (handles `XDG_CONFIG_HOME=""`)
* `core/src/util/SpscRing.h` (S3) — header-only intra-plugin SPSC ring; mirrors C1.b commit `6145a53` pattern
* `core/tests/core_unit/test_p_instances_registry.cpp` (S2)
* `core/tests/core_unit/test_p_instances_registry_corruption.cpp` (S2)
* `core/tests/core_unit/test_p_spsc_ring.cpp` (S3)
* `vst3/tests/test_vst3_bind_collision.cpp` (S2)
* `vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp` (S3)
* `vst3/tests/test_vst3_performedit_threadsafe.cpp` (S2.6)
* `vst3/tests/test_vst3_state_v3_reader_only.cpp` (S2.5)
* `vst3/tests/test_vst3_state_v3_persist.cpp` (S7)
* `vst3/tests/test_vst3_no_feedback_loop.cpp` (S4)
* `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` (S6)
* `vst3/tests/test_vst3_registry_stale_cleanup.cpp` (S2)
* `vst3/tests/perf/soak_vst3_console_flood.cpp` (S6)
* `vst3/tests/fixtures/v01_preset_*.vstpreset` + `v02_preset_*.vstpreset` (S2.5 — committed day 1; soaks from S2.5 to S8)
* `docs/release/v0.3.0/daw-handson-log.md` (S8)

### Modified files (additive only)

* `vst3/CMakeLists.txt:16-38` (S2) — add `osc/*.cpp` + `SpatialEnginePluginUdp.cpp` under `SPATIAL_ENGINE_VST3_OSC=ON`
* `core/CMakeLists.txt` (no new executable; `RegistryPath.h` is header-only and stays out of `libspe_core.a` symbol table); possibly bump version to 0.3.0
* `vst3/SpatialEngineProcessor.cpp:32-34` (S2) — ctor extension: bind UDP at `127.0.0.1:9100+N` per A1-ε; register in `instances.json`; spawn UDP I/O thread
* `vst3/SpatialEngineProcessor.cpp:107-112,189-198` (S3) — clean shutdown of UDP thread + ring
* `vst3/SpatialEngineProcessor.cpp:201-323` (S2.5 reader, S7 writer per D3-γ split) — 3-way state v1/v2/v3 fork; writer emits v3 only from S7
* `vst3/SpatialEngineProcessor.cpp:process` entry (S3) — audio callback pops from intra-plugin SPSC ring
* `vst3/SpatialEngineProcessor.cpp:560-579` (S4) — `notify` reversal site
* `vst3/SpatialEngineProcessor.hpp:32-40` (S7) — `ParamId` enum adds `kMute=7` (slot reserved at S2.5; enum activates at S7)
* `vst3/SpatialEngineProcessor.hpp:104` (S7) — `norm_values_` resize to [8]
* `vst3/SpatialEngineController.cpp:107-216` (S7) — add 8th `ParameterInfo` for kMute
* `vst3/SpatialEngineController.cpp` notify impl (S4, via S2.6 marshaling strategy) — reverse AM-R3-10, dispatch to performEdit via chosen path (a/b/c per S2.6)
* `core/src/bin/spatial_engine_core.cpp:99,134` (S2) — extend OSC dispatch with "broadcast to plugin registry" hook (~150 LOC); consults `instances.json`; `sendto()` per matching instance per `obj_id_subset`
* `docs/manual_kr/operation/README.md` (S5) — add Ch.5 (3-process topology) + TOC link
* `docs/adr/0010-vst3-osc-binding-model.md` (S1) — Status: Draft → Accepted; spec pin `v0.3.0-c4-final`
* `docs/adr/0011-vst3-osc-multi-instance-discovery.md` (S1) — Status: Draft → Accepted
* `docs/adr/0012-adm-osc-vendor-quirks.md` (post-S8, D4-α capture-day) — fill if quirk observed; "no-quirk-observed: synthetic fixture extension to day-90" note if lab cancels within 7 days
* `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` (S7) — re-pin **only if** paired core/ change enumerated; expected outcome = no re-pin
* `.github/workflows/vst3.yml` (S6) — extend with new ctest invocations for e2e + soak tests
* `CHANGELOG.md` (S8/release prep) — v0.3.0 entry under Keep-a-Changelog headings

### Removed from Round-2 scope (per D1-γ + Architect §4.1)

* ~~`bin/spatial_engine_sidecar.cpp`~~ — D1-γ no sidecar
* ~~`vst3/sidecar_bridge/*` (all 5 file-pairs: `UdsServer`, `PluginToSidecarChannel`, `AutomationReflect`, `ControllerReverseHandler`, plus the `PluginInstanceRegistry` from this dir → moved to `vst3/osc/`)~~ — D1-γ no sidecar
* ~~`tools/systemd/spatial-engine-sidecar.service`~~ — D1-γ no daemon
* ~~`vst3/tests/test_vst3_sidecar_dispatch.cpp`~~ — replaced by `test_vst3_intra_plugin_spsc_drain.cpp`
* ~~`vst3/tests/test_vst3_sidecar_reverse_path.cpp`~~ — replaced by `test_vst3_no_feedback_loop.cpp` + `test_vst3_performedit_threadsafe.cpp`
* ~~`vst3/tests/test_vst3_sidecar_oom_recovery.cpp`~~ — strawman of rejected architecture; replaced by `test_vst3_bind_collision.cpp` (PM1)
* ~~`vst3/tests/test_vst3_handshake_protocol.cpp`~~ — no plugin↔sidecar handshake exists
* ~~`vst3/tests/perf/soak_vst3_storm.cpp`~~ — no cross-process IPC to stress; intra-plugin overrun handled in unit tests

### Read-only references (not modified)

* `core/src/ipc/CommandDecoder.cpp:317-373` — ADM-OSC decode (consumed by in-plugin UDP thread + standalone forwarder)
* `core/src/ipc/CommandDecoder.cpp:422-602` — ADM-OSC encode (consumed by standalone forwarder)
* `core/src/ipc/OSCBackend.cpp:75-83` — UDP recv pattern reference for plugin dedicated thread
* `core/src/ipc/AdmOscConstants.h` — `ADM_OSC_MAX_DIST=20.0f` shared constant
* `pluginterfaces/vst/ivsteditcontroller.h` — SDK thread-safety doc for S2.6 audit

---

## 12. Open questions (updated per Round-3 — Critic ask 4)

(Mirrored to `.omc/plans/open-questions.md`.)

* **v03-Q1** (D1 sidecar build target): **CLOSED by frozen contract.** ADR 0010 §A1-ε rejects all sidecar variants for v0.3 Linux fast path; D1-γ (no sidecar) is the only compliant option. v03-Q1 removed from active list.
* ~~v03-Q2 (D2 SPSC channel medium)~~: **DROPPED — moot under A1-ε.** No plugin↔sidecar channel exists.
* **v03-Q3** (D3 state v3 migration timing): **CLOSED.** D3-γ hybrid chosen — reader at S2.5, writer at S7.
* **v03-Q4** (D4 vendor-quirks): **OPEN — logistics.** Does the user have specific lab venue / customer contact for the day-60 booking? Cancellation fallback specified.
* **v03-Q5** (D5 macOS/Windows CI): **CLOSED.** D5-β defer to v0.4.
* ~~v03-Q6 (Pre-mortem 1 systemd dependency)~~: **DROPPED — moot under A1-ε.** No systemd unit shipped.
* **v03-Q7** (ADR 0010 §A1-δ contingency activation criteria): **OPEN — deferrable.** When does macOS/Windows port (v0.4+) trigger fallback from A5-α to A5-β? Architect suggestion: "Activation criterion = single Apple/Steinberg/Avid forum thread OR cert-eval rejection citing `bind()` as cause." Defer concrete criterion to v0.4 sprint scoping.
* **v03-Q8** (NEW Architect Round-2 — `IComponentHandler::performEdit` thread-safety): **OPEN, gated by S2.6.** Three candidate strategies (a) host message-thread queue marshaling, (b) `IRunLoop`, (c) read-only state propagation. S2.6 SDK audit decides; commit footer documents reasoning.
* **v03-Q9** (NEW Architect Round-2 — registry stale-PID via boot_id): **CLOSED via design choice.** S2 implements `boot_id` field in registry entries; writer-side GC drops stale-boot entries regardless of PID match. Test: `test_p_instances_registry` covers stale-PID-after-reboot scenario.
* **v03-Q10** (NEW Architect Round-2 — XDG empty-string semantics): **CLOSED via design choice.** S2's `RegistryPath.h` handles `XDG_CONFIG_HOME=""` (set-but-empty) by falling back to `~/.config` per XDG spec; explicit unit test in `test_p_instances_registry`.

**Active open questions for Architect/Critic Round-3**: v03-Q4 (logistics), v03-Q7 (v0.4 deferral criteria), v03-Q8 (S2.6 audit outcome). 3 ≤ 5.

---

**Plan end.**
**Status**: Round-3 revision awaiting Architect / Critic Round-3 re-review per `/oh-my-claudecode:ralplan` consensus protocol.
**Next gate**: Architect Round-3 re-verifies frozen-contract alignment + S2.6 marshaling design + D3-γ split. Critic Round-3 re-scores against critic-r-v0.3-review.md §7 Loop closure conditions. On APPROVE: `/oh-my-claudecode:autopilot` for S1-S8 execution.
