# Architect Review — Phase C4 + v0.2.0 Release Plan (Round 1)

**Reviewer**: Architect (RALPLAN consensus, DELIBERATE mode)
**Date**: 2026-05-10
**Plan**: `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` (1098 lines)
**Verdict**: **ITERATE** — 7 blockers, ratify ADR 0008 with edits, hold ADR 0007 pending evidence + scope decoupling.

---

## 0. Summary

The Planner has done a strong job framing tradeoffs and producing a testable acceptance index. However, two structural problems prevent APPROVE:

1. **A1-δ (sidecar) is recommended on cert-risk projection without falsifiable evidence** (C4-Q1 explicitly admits this). The sidecar adds substantial deployment + IPC + registry complexity to solve a problem that may not exist on the v0.2 Linux-only target.
2. **Scope coupling is mis-stated**. The Planner claims B1-β + B2-β decouples Track A and B, but R2 (version macro bump → OFF re-pin) and the manual-Ch.5-vs-disclaimer fork mean the decision still drives release-day work. More importantly, A2.1-β (state v3) is recommended in the Planner's combined ADR §8 even though it's wasted work if v0.2 ships C3-only — this is internally inconsistent.

Track B (release packaging) is largely sound; Track A needs a Round-2 patch.

---

## 1. Steelman antithesis: A1-α and A1-ε deserve more credit than the Planner gives

### 1.1 The Planner's case for A1-δ rests on cert-risk projection, not evidence

The Planner writes (`spatial-engine-phaseC4-and-v0.2-release.md` line 156-161):
> "cert-risk is the single biggest unknown — every 'DAW host blocks bind()' anecdote is worth weeks of debugging on a closed system. δ avoids the problem entirely…"

This is a projection argument, not an evidence argument. The Planner's own C4-Q1 (`open-questions.md:99`) explicitly admits: *"Concrete proof that A1-α … would fail on Pro Tools / Logic / Ableton, vs projection?"* And the v0.2 release target per `vst3/.github/workflows/vst3.yml` is **Linux-only ubuntu-24.04** (B3-β confirms this at line 485). Pro Tools does not run on Linux. Logic Pro does not run on Linux. The cert-risk pre-mortem (Scenario 1 at line 667) is about v0.3+ targets.

**Strongest steelman for A1-α (per-instance auto-port UDP)**:

> "We are shipping v0.2 to a Korean live-venue customer running Reaper + Bitwig **on Linux**. The Linux JUCE-free `OSCBackend` already binds UDP on port 9100 from any process — including a DAW plugin process — without sandbox interference, because Linux has no audio sandbox profile equivalent to macOS Core Audio sandbox or AAX. The `bind()` call at `core/src/ipc/OSCBackend.cpp:69` will succeed in a Reaper plugin process exactly as it succeeds in a standalone process. The cert-risk argument is for **v0.3+ macOS/Windows ports**, which are explicitly out of scope for v0.2 (`B3` declares Linux-only at line 485). Therefore A1-δ is **paying today for a problem we'll only have at v0.3+**, while creating new problems today (sidecar binary, JSON registry race conditions, UNIX-socket IPC primitive, two-process workflow novelty for users)."

> "The pattern at `OSCBackend.cpp:75-83` is **already proven** as a JUCE-free `std::thread` UDP listener. Wiring an instance of it into `SpatialEngineProcessor`'s ctor (replacing the `0 /*no UDP*/` at line 33) is **~50 LOC of net-new code** plus an auto-port discovery mechanism (`bind(port=0)` returns kernel-assigned port; surface it via a read-only `kOscPort` ParamId or via a new `/sys/state` reflection). Compare this against the Planner's S2..S6 = **~7 days of new code surface** including a new binary + registry + reverse-channel + e2e harness. A1-α is **1/10 the engineering cost**."

> "Multi-instance port collision (the Planner's stated A1-α weakness) is a non-problem with auto-port: each instance gets a unique kernel-assigned ephemeral port. The 'console operator must learn N port numbers' concern is solvable with a one-line `/sys/state` reflection (already an OSC mechanism we have)."

**Strongest steelman for A1-ε (recv-only socket per instance)**:

> "If we want to be conservative on cert-risk while still delivering v0.2 plugin parity, A1-ε is strictly safer than A1-α. A `recv()` socket on macOS does not require notarisation network entitlements — only `bind()` to privileged ports (<1024) or outbound `connect()` to specific endpoints does (per Apple Notarization Hardened Runtime docs). Linux has no restriction on either. So A1-ε on Linux + macOS is feasible **today** with current evidence. The Planner dismisses A1-ε as 'strict subset of α with same cert risk' (line 175-176) — this is wrong. `bind()` for recv-only is a different code path than `bind()+sendto()` from a host-sandbox perspective."

**Strongest steelman for A1-γ (continued deferral)**:

> "Per the Planner's own B1-β recommendation (v0.2 ships C3-only), the plugin OSC story is **already deferred to v0.3** in the release-notes "known limitations" section (R4 §4 at line 585). If v0.2 ships without C4 anyway, **why are we doing C4 at all in this sprint?** The honest answer is: do C4 in v0.3 with the macOS/Windows port (where the cert evidence will become real, not projected) and use the v0.2 → v0.3 cycle to gather actual customer feedback on what plugin-OSC workflow they need. Shipping a sidecar architecture today, before we know whether customers want bidirectional or recv-only, is premature optimisation."

### 1.2 Why A1-δ might still be right despite the steelman

The Planner has one strong card: **A1-δ delivers a single code path across Linux + macOS + Windows** (line 256, A5-β). If the project is truly committed to cross-platform v0.3 and beyond, locking the architecture now while C3 is fresh in cache prevents two architectures (per-platform UDP on Linux, sidecar on macOS) coexisting forever. This is Driver 3 (debt reduction). If the team buys this argument, A1-δ is justified — but the cost (sidecar binary, registry, two-process workflow) must be acknowledged as real, not minimised.

**Hybrid synthesis** (see §6 below): A1-α as opt-in `SPATIAL_ENGINE_VST3_DIRECT_OSC=ON` for Linux v0.3 fast path; A1-δ as the cross-platform default. Or: A1-ε in v0.3 (Linux + cert-evidence on macOS), A1-δ deferred to v0.4 if A1-ε proves cert-blocked.

---

## 2. Tradeoff tensions (named, not avoided)

### Tension 1 — Cert safety vs deployment complexity (A1-δ vs A1-α)

| Axis | A1-δ sidecar | A1-α direct UDP |
| --- | --- | --- |
| Cert safety (macOS v0.3+) | High (no plugin-side network) | Unknown (projection) |
| Cert safety (Linux v0.2) | Equal (no Linux audio sandbox) | Equal |
| LOC delta | ~7d new code (S2..S6) | ~50 LOC + 1 auto-port test |
| User workflow novelty | High (4-process: DAW + plugin + sidecar + standalone) | Low (3-process: DAW + plugin + standalone) |
| Multi-instance | Discovery via JSON registry (race + GC) | OS-assigned ephemeral ports |
| Dep on standalone running | Hard (sidecar relays to standalone:9100) | Soft (plugin can ingest console directly) |
| Failure modes | Sidecar crash, stale registry, socket-path collision | Port already in use, firewall block |

**This tension is real and must be resolved by the deployment-target decision** (Linux-only v0.2 + Linux-first v0.3, vs cross-platform v0.3). Planner has assumed the latter; user-direction needed to confirm.

### Tension 2 — Ship now (1-customer ETA pressure) vs ship complete (one tag, one changelog)

Planner correctly identifies B1-β as the right answer for cycle time. **But** this creates a manual-Ch.5 fork (line 642): C4 deferred → manual disclaimer; C4 in v0.2 → manual Ch.5. If the Korean customer integrates against v0.2's manual today and v0.3 changes the workflow (sidecar mode), the customer must re-train. **A 1-2 week delay to land C4 in v0.2 may be cheaper than a customer doc-churn cost.** This is not addressed by the plan.

**Concrete question for user**: is the Korean customer's first integration date constrained? If yes (e.g., contract signed, "must run in venue by 2026-05-30"), B1-β. If no, B1-α may save total cycle time.

### Tension 3 — DAW hands-on gate confidence vs cycle time (B4-α vs B4-β)

B4-α requires hands-on in Reaper + Bitwig + (possibly) standalone OSC console smoke. Planner allocates 1d (R3 line 547) for this. **This is optimistic** if the developer doesn't already own Reaper + Bitwig licenses or if hands-on uncovers the kind of issue Pre-mortem 3 covers (kIsBypass + kCanAutomate flag interaction with Reaper). The plan should add a **falsifiable success criterion** for each of the 7 params: "param X moves audibly when automated over 4 bars at 120 BPM" — currently the plan says "automatable" which is imprecise.

### Tension 4 — Prebuilt .so (B3-β) vs source-only (B3-α)

B3-β is the right call but introduces a binary support burden the project has not had to date. Specifically: **GLIBC 2.39 prebuilt .so will not load on ubuntu-22.04 (GLIBC 2.35) or older distros.** Korean live-venue mixing PCs may run on stable LTS distros, not the latest ubuntu. C4-Q8 acknowledges this but defers the matrix to v0.3. **Recommended**: release notes (R4) must explicitly state ubuntu-24.04 + GLIBC 2.39 minimum.

---

## 3. Architectural soundness checks

### 3.1 Does A1-δ violate ADR-0003 "single OSC schema, single port"?

**No, but only because the sidecar re-uses standalone port 9100.** ADR 0003 (`docs/adr/0003-ipc-osc-udp.md:18-20`) pins ports 9100 (cmd) / 9101 (state). The Planner's S2/S6 (line 296-297, 384) uses port **9101** for tests but **9100** in production for the sidecar→standalone relay. **Verified consistent with ADR 0003.**

However, the sidecar→plugin channel (UNIX domain socket at `~/.config/spatial_engine/sock-{pid}`, line 293) is a **new IPC primitive** outside ADR 0003's scope. ADR 0003 is about OSC over UDP for UI IPC + external OSC + VST3. Domain sockets are a different transport.

**Architect requires**: ADR 0007 must explicitly carve out "the plugin↔sidecar control channel is a separate transport (UNIX domain socket) outside ADR 0003's OSC-over-UDP scope, used because the sidecar acts as a multiplexer + bridge to the plugin process". Without this carve-out, ADR 0007 contradicts ADR 0003's "single transport" line (ADR 0003 line 16-17). **Blocker #1** below.

### 3.2 `~/.config/spatial_engine/instances.json` — race conditions

This is a **multi-writer / multi-reader file** without a stated locking strategy. Failure modes:
- Plugin instance A writes registry, plugin instance B writes simultaneously (TOCTOU on read-merge-write)
- Sidecar reads registry while plugin C writes (truncated read returning malformed JSON)
- DAW crashes; plugin instance never deletes its entry; sidecar relays to dead UNIX socket

ADR 0008 draft (line 1019-1041) does not mention:
- File-locking strategy (`flock(LOCK_EX)` advisory? `O_EXCL` rename-based atomic write?)
- Read-side handling of partially-written JSON (retry with backoff? `flock(LOCK_SH)`?)
- PID liveness check semantics (`kill(pid, 0)` returns ESRCH? Cross-platform note for v0.3?)
- Stale lock file from segfaulted plugin

**Blocker #2**: ADR 0008 must specify the locking pattern. Recommended: write to `instances.json.tmp`, `rename()` to `instances.json` (atomic on POSIX). Reads proceed without lock; readers tolerate "instance entry refers to dead PID" by skipping. GC scan every 5s (already in draft) cleans stale entries. UNIX socket path uses PID; if PID re-used by an unrelated process, sendmsg() fails — sidecar must handle EPIPE.

### 3.3 State format v2 → v3 timing

The Planner recommends A2.1-β (kMute as 8th param, state v3) **if A1-δ chosen**, in §A2.1 line 217. But under B1-β (v0.2 = C3-only, no C4), v3 is **wasted work** for v0.2.

The plan is internally inconsistent here:
- §A2.1 recommends v3 (line 217)
- §8 ADR adopts A2.1-β (line 807)
- §6 Risks row 2 acknowledges 3-way reader fork
- But §B1-β recommends v0.2 = C3-only (line 457) — kMute lives in C4
- C4-Q2 (`open-questions.md:100`) explicitly raises the timing question

**Architect's call**: defer A2.1-β / state v3 to v0.3. v0.2 keeps state v2 (just shipped in C2B at acb8c27). Avoids:
- OFF re-pin twice (once for v3 schema constants, once if/when sidecar lands)
- 3-way reader fork shipped before v3 is actually used
- `test_vst3_state_persist.cpp` extension
- C2B was 2 days ago; bumping again signals churn to anyone reading the changelog

**Blocker #3**: ADR 0007 / §A2.1 must re-recommend deferring v3 to v0.3. The kMute decision is independent of A1-δ — kMute is a semantic improvement; the bridge architecture is a transport choice. They should not be coupled in the plan.

### 3.4 Sidecar binary needs OFF baseline pin?

**No, by current OFF-baseline scope.** `.ci/off_baseline.bytes.sha256` covers `spe_core` + `spe_util` static archives only (per Principle 3 at line 84-88). The sidecar binary is in `bin/spatial_engine_sidecar.cpp` — outside scope. **Verified consistent with current pin coverage.**

However, if the sidecar **links** anything from `core/`, the linkage flags / link-order changes could perturb the static archive's emitted symbols. **Mitigation**: sidecar must link only headers + already-baselined `spe_core.a`; new `core/src/util/SpscRing.h` (line 322-324, already exists per `core/src/util/SpscRing.h:1`) means S3 can header-only-include without growing OFF baseline. **OK.**

### 3.5 Threading: A4-β consistency with OSCBackend.cpp

A4-β (dedicated `std::thread`) at line 240-241 cites `OSCBackend.cpp:75-83` as the pattern. **Verified at `core/src/ipc/OSCBackend.cpp:75-83`** — matches: `std::thread`, 100ms `SO_RCVTIMEO`, `running_=false` + `close(fd)` shutdown. **Consistent.**

But the plan does not address **two threads in the plugin process** (S3 line 318-326 implies one SPSC thread; S4 line 339-352 implies a reverse-channel reader). Are these two distinct `std::thread`s, or one thread doing duplex via `select()`/`poll()`? **Blocker #4**: S3+S4 must explicitly state the thread topology in the plugin process. Recommended: one SPSC ring (audio→sidecar), one reverse-channel thread (sidecar→controller `notify`). Two threads per plugin instance + multi-instance = N×2 threads. Document the upper bound.

### 3.6 Deployment discovery for Korean live-venue user

The plan does not address: **how does the Korean live-venue user know they need the sidecar binary?** Currently:
- Standalone binary: `spatial_engine_core` (in `bin/`) — user already knows this
- Plugin: `spatial_engine_vst3.so` — user installs to `~/.vst3/`
- Sidecar: `spatial_engine_sidecar` (new) — where does it live? When does it auto-start?

Manual Ch.5 (S5 line 363) describes the workflow but not the **install step**. Bundled with plugin .vst3? Bundled with standalone? Separate `apt install spatial-engine-sidecar`? B3-β release artifact is "source + prebuilt .so" — sidecar binary should be added.

**Blocker #5**: B3-β must include `spatial_engine_sidecar` as a third binary artifact, OR the plan must explicitly state "sidecar is built from source by user" with manual update. Currently silent.

---

## 4. v0.2 release tension (B1-β + B2-β + B4-α)

### 4.1 Is 1-2d hands-on realistic without Reaper/Bitwig licenses?

Reaper has a 60-day evaluation that nags but does not lock features. Bitwig has a 30-day demo. **1-2d is feasible** if the developer can install both today. Concern: if hands-on (R3) reveals an issue that takes 3+ days to fix, the v0.2 release slips beyond the 1-2d banner. Pre-mortem 3 (line 718) addresses this with a v0.1.1 escape, but **v0.1.1 has no plan today** — the Planner mentions B1-γ (line 454) but doesn't implement it. **Blocker #6**: if Pre-mortem 3 fires (R3 fails 2 retries per line 745), the v0.1.1 fallback path needs a documented rollback procedure. Specifically: how is C3 (`/adm/obj/N/` decoder) packaged into v0.1.1 if v0.1.1 was supposed to be patches only?

### 4.2 B4-α falsifiable success criteria

The plan currently says "automatable" which is non-falsifiable. **Architect requests**: each of 7 (or 8) params must have an audible/visible test:

| Param | Falsifiable test |
| --- | --- |
| `kPanAz` | Pan a 1 kHz sine from L (-π) to R (+π) over 4 bars at 120 BPM; recorded WAV shows correct LR balance shift |
| `kPanEl` | Layout `lab_8ch.yaml` has elevated speakers; sweep -π/2 → +π/2 produces audible elevation shift in headphone-binaural test |
| `kSourceWidth` | Width 0 → π changes spread of pink noise across speakers; measured by inter-speaker correlation |
| `kMasterGain` | -60 dB → +6 dB ramp produces 66 dB peak-meter delta in DAW |
| `kAmbiOrder` | Order 1/2/3 produces visibly different localisation precision (visual: VU meter pattern across 16 spks) |
| `kRoomPreset` | Phase D6 deferral — confirm "no audio side-effect" matches `Processor.cpp:545` |
| `kBypass` | Bypass on → output sample-identical to input (already tested in `test_vst3_bypass`); confirm via `cmp` of recorded WAV |
| `kMute` (if A2.1-β) | Mute on → silence (output is all zeros) |

**Blocker #7**: §R3 must be extended with these falsifiable criteria. Without them, "DAW hands-on PASS" is reviewer-subjective.

### 4.3 EN release notes quality

Driver 2 cites "ecosystem parity with L-ISA / Spat Revolution / d&b" (line 110-114). These are international vendors. The EN release notes (B5-γ) for v0.2 should be **sign-off-quality** for international ecosystem readers, not a brief summary, given the project is positioning itself in the L-ISA / Spat Revolution comparison space. Not a blocker, but a quality bar to set.

---

## 5. Pre-mortem critique

The 3 scenarios are reasonable but **miss one important risk**:

### Suggested Scenario 4 — Sidecar process killed by user / OS / OOM

**What goes wrong**: User runs DAW + plugin + sidecar + standalone. Memory pressure on a Korean mixing PC (often 8-16 GB RAM with large session files) causes the OS to OOM-kill the sidecar (lowest-priority of the four processes). Plugin's UNIX socket peer disappears mid-session. `send()` returns EPIPE. Plugin's RT-safety guarantee (no alloc, no log on audio thread) means the plugin **silently drops** all subsequent param changes until the sidecar restarts. User sees: "console moves work for 10 minutes, then suddenly stop, no error visible."

**Probability**: Moderate (sidecar is youngest, lightest process, most likely OOM victim).
**Severity**: High (silent failure; user troubleshooting is very expensive).
**Mitigation**:
- Plugin's `dispatchParamChange` extension (S3, line 318-326) must check socket health periodically and **set a Controller-side metric** (read by the editor view in v0.3, or polled via standalone `/sys/state` reflection in v0.2) — "sidecar disconnected, automation working but bridge inactive".
- Sidecar should include systemd unit file (or equivalent) for auto-restart.
- Manual Ch.5 must include "if sidecar dies, console→plugin path stops; restart sidecar" troubleshooting note.

**Architect requires**: §5 add Scenario 4, §S5 manual addresses this troubleshooting case, §S2-S3 implementation includes EPIPE-safe send + status metric.

---

## 6. Synthesis recommendation

If user-direction confirms **Linux-only v0.2 + Linux-first v0.3**:

> **Synthesis**: A1-ε (recv-only socket per instance) for v0.3 (Linux + early macOS evidence-gathering), A1-δ deferred to v0.4 IF A1-ε proves cert-blocked on macOS. v0.2 ships C3-only per B1-β. **No sidecar in v0.2; no sidecar in v0.3 unless macOS evidence demands.** This saves 7 days of speculative architecture work and gathers evidence before committing.

If user-direction confirms **cross-platform commitment v0.3+**:

> **Synthesis**: A1-δ for cross-platform default (matches Planner). **But** decouple from v0.2 — A1-δ is a **v0.3 deliverable**, not v0.2. v0.2 ships C3-only per B1-β. Track A's S1 (ADR drafts) is the only v0.2 work; S2..S8 land in v0.3 cycle with macOS port. This honors B1-β decoupling without abandoning A1-δ.

**Default recommendation in absence of user-direction**: option 2 (cross-platform commitment, A1-δ for v0.3, ADRs only in v0.2). This matches the Planner's B1-β recommendation while removing the A2.1-β state-v3 timing inconsistency (Blocker #3).

---

## 7. Verdict — ITERATE with 7 blockers

### Blockers (must address in Round-2 patch)

1. **`/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` §S1, ADR 0007 draft**: ADR 0007 must explicitly carve out the plugin↔sidecar UNIX domain socket as a transport outside ADR 0003's OSC-over-UDP scope, OR reduce sidecar IPC to the same OSC-over-UDP transport (sidecar binds 9101, plugin sends to 9101). Without this, ADR 0007 contradicts ADR 0003 line 16-17.

2. **§S1, ADR 0008 draft (lines 1019-1041)**: must specify file-locking strategy. Required additions: (a) atomic write via `rename()` from `instances.json.tmp`; (b) reader tolerance of stale PID entries; (c) UNIX socket path PID re-use protection (sidecar handles EPIPE on dead peer); (d) GC interval (5s in draft is fine, but document the worst-case dead-entry window).

3. **§A2.1 + §8 ADR + §B1-β**: resolve the internal inconsistency. Recommend defer A2.1-β / state v3 to v0.3 alongside C4. v0.2 keeps state v2. Update plan line 211-217 to recommend A2.1-α-temporary ("kMute reuses kBypass for now, semantics documented as 'bypass = mute' in C3 plugin scope") OR document v3 as v0.3-only deliverable. The state-format change must not ship in the same release as C3-only ADM-OSC if C4 doesn't ship.

4. **§S3 + §S4 + §A4-β**: explicitly document the thread topology in the plugin process. How many `std::thread`s per plugin instance? What is the upper bound for N instances? Add to ADR 0007 as a thread-budget invariant.

5. **§B3-β (line 485)**: include `spatial_engine_sidecar` as a third binary artifact, OR explicitly state "sidecar built from source" with manual update. Currently silent.

6. **§Pre-mortem 3 (line 718) + §B1-γ fallback**: document the v0.1.1 rollback procedure if Pre-mortem 3 fires. Specifically: how is C3 (4 new CommandTags + `--osc-dialect` flag) packaged into v0.1.1 if v0.1.1 is "patches only"? Either accept C3-into-v0.1.1 as a semver violation with explicit note, or define a different rollback (revert to v0.1.0 + cherry-pick docs only).

7. **§R3 (line 547)**: add falsifiable success criteria for each of the 7 (or 8) params per §4.2 above. Without these, B4-α "PASS" is subjective.

### Pre-mortem addition required

Add Scenario 4 (sidecar OOM-killed) with mitigation: plugin EPIPE-safe send, sidecar auto-restart, manual troubleshooting note.

### Steelman quote the Planner must address

> "We are shipping v0.2 to a Linux Reaper+Bitwig customer. Linux has no audio sandbox. The OSCBackend.cpp:75-83 pattern already binds UDP from any process. A1-α is ~50 LOC and one auto-port test. The Planner's A1-δ is ~7 days of new code surface + a new binary + a registry race condition + a two-process workflow novelty. The cert-risk argument is for v0.3+ macOS/Windows ports, which are explicitly out of scope for v0.2 per B3-β (Linux-only ubuntu-24.04). If A1-δ is the right answer for v0.3 cross-platform, **build it in v0.3** when the cert evidence is real. Building it in v0.2 spends today's engineering budget on tomorrow's projection."

The Planner must either (a) produce concrete cert evidence (Steinberg/Avid/Apple doc citation) that justifies A1-δ for v0.2, or (b) re-scope A1-δ to v0.3 with only ADR drafts (S1) landing in v0.2.

### ADR 0007 / 0008 ratification

- **ADR 0007**: do **not** ratify as drafted in §Appendix C (line 1006-1014). Requires Blocker #1 + #3 + #4 fixes before ratification. Draft is structurally OK; content needs the carve-out, the v3-deferral, and the thread-topology contract.
- **ADR 0008**: ratify as draft skeleton **with edits per Blocker #2**. The schema is right; the locking + GC semantics need filling in.
- **ADR 0009 (vendor quirks)**: reserved-slot pattern (line 1043-1046) is fine.

### Cross-track coupling — does C4 and v0.2 actually decouple?

**Partially.** B1-β + B2-β + R2 + S5/disclaimer-fork is **mostly decoupled**. The remaining couplings:

| Coupling | Severity | Resolution |
| --- | --- | --- |
| Manual Ch.5 vs disclaimer (line 642) | Low | Plan handles via S5 conditional; OK |
| State v3 if A2.1-β chosen, even with B1-β | **High** | Blocker #3 — defer v3 to v0.3 |
| OFF re-pin for R2 version bump | Low | Already in plan §4.4 line 656-661; OK |
| Single coherent "ADM-OSC fully integrated" story | Medium | Customer-facing; resolve via release-notes language §4.3 line 644-649 |

**Verdict on decoupling**: TRUE under the Architect's recommended synthesis (A1-δ deferred to v0.3, only ADRs in v0.2). FALSE under Planner's current §8 (A1-δ + A2.1-β both in v0.2 = state-v3 churn for unused param).

---

## 8. Recommendations summary (prioritized)

| # | Recommendation | Effort | Impact |
| --- | --- | --- | --- |
| 1 | Resolve Blocker #3 (defer state v3 to v0.3) — single-line fix in §A2.1 + §8 | 0.1d | Removes scope-coupling inconsistency |
| 2 | User-direction confirm: Linux-only v0.2 path? Cross-platform v0.3 commitment? | gate | Drives A1-δ vs A1-α/ε vs A1-γ decision |
| 3 | If cross-platform commitment confirmed: re-scope A1-δ to v0.3-only, S1 in v0.2 | 0.5d plan edit | Decouples Track A complexity from v0.2 cycle |
| 4 | If Linux-only confirmed: re-evaluate A1-α/ε vs A1-δ on actual evidence | 0.5d plan edit | May save 7d engineering |
| 5 | Resolve Blocker #1 (ADR-0003 carve-out) | 0.2d ADR draft | Architectural integrity |
| 6 | Resolve Blocker #2 (ADR 0008 locking) | 0.3d ADR draft | Production correctness |
| 7 | Resolve Blocker #4-7 | 1d combined | Quality + falsifiability |
| 8 | Add Pre-mortem Scenario 4 | 0.2d plan edit | Risk completeness |

---

## 9. Trade-offs

| Option | Pros | Cons |
| --- | --- | --- |
| Approve as-is | Fastest to autopilot; preserves Planner's structure | Internal inconsistency (state v3 in C3-only release); blocker #1 ADR contradiction; subjective B4-α gate |
| Iterate (Architect verdict) | Resolves 7 blockers; cleaner ADR ratification; falsifiable gates | 1-2d plan-rewrite cost before autopilot |
| Reject and re-plan from scratch | Forces user-direction first | Discards good Track B work + good acceptance index |

**Architect picks Iterate.** The Planner's structure is sound; targeted Round-2 patches resolve the issues without re-planning.

---

## 10. References

- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:33` — `engine_(std::make_unique<spe::core::SpatialEngine>(0 /*no UDP*/))` constructive absence (also at `vst3/SpatialEngineProcessor.cpp:33`, verified)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:457` — B1-β recommendation (v0.2 = C3-only)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:807` — §8 ADR adopts A2.1-β (state v3) — inconsistency
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:33` — `0 /*no UDP*/` ctor (Phase C3 deferral source)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:200-323` — state v2 format (just shipped acb8c27)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineController.cpp:593-596` — `notify` returns `kNotImplemented` per AM-R3-10 (would be reversed by S4)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineController.cpp:107-216` — `buildParamInfos` (current 7 params; would extend to 8 if A2.1-β)
- `/home/seung/mmhoa/spatial_engine/core/src/ipc/OSCBackend.cpp:53-93` — JUCE-free POSIX UDP listener pattern (cited by A4-β)
- `/home/seung/mmhoa/spatial_engine/docs/adr/0003-ipc-osc-udp.md:14-17` — single-OSC-schema + single-transport invariant (Blocker #1 carve-out target)
- `/home/seung/mmhoa/spatial_engine/docs/adr/0006-adm-osc-v1-spec-freeze.md:46-50` — `ADM_OSC_MAX_DIST=20.0f` shared constant (single source of truth)
- `/home/seung/mmhoa/spatial_engine/core/src/util/SpscRing.h:1` — SPSC primitive already exists (S3 reuse confirmed; not new code)
- `/home/seung/mmhoa/spatial_engine/core/src/bin/spatial_engine_core.cpp:79,97,114-118` — `--osc-dialect` flag location
- `/home/seung/mmhoa/spatial_engine/core/src/bin/spatial_engine_core.cpp:30` — banner already says "v0.2.0" — version bump partially landed!
- `/home/seung/mmhoa/spatial_engine/.ci/off_baseline.bytes.sha256` + `off_baseline.symbols.sha256` + `vst3sdk_sha.txt` — current OFF baseline pin set
- `/home/seung/mmhoa/spatial_engine/.omc/plans/open-questions.md:99-108` — C4-Q1..Q10 (all 10 questions reviewed in this analysis)

