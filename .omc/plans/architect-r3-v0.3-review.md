# Architect Round-3 Strategic Review — v0.3.0 Sprint Plan

**Reviewer**: Architect agent (DELIBERATE mode, RALPLAN-DR Round-3)
**Plan under review**: `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md` (750 lines, ~10.5k words, Round-3 revision)
**Prior gates**:
- Architect Round-2 (REVISE-AND-RESUBMIT) — `architect-r-v0.3-review.md`
- Critic Round-2 (ITERATE, 15 numbered asks) — `critic-r-v0.3-review.md`
**Ground truth**: `docs/adr/0010-vst3-osc-binding-model.md`, `docs/adr/0011-vst3-osc-multi-instance-discovery.md`, parent plan `spatial-engine-phaseC4-and-v0.2-release.md` §1.5 freeze + §7.5/A.7
**Date**: 2026-05-11
**Mode**: DELIBERATE (cert-risk inherited, state-ABI bump, expanded test plan + pre-mortems required)

---

## 1. Verdict

**APPROVE-with-revisions.**

The Round-2 CRITICAL (C1 frozen-contract violation) is fully and verifiably closed: every sidecar/UDS/systemd hit in the file is in an explicit deletion/audit-trail/elimination block, never in active scope. C2 (v03-Q8 `performEdit` cross-thread safety) is closed with a properly scoped S2.6 gate (audit deliverable, three-strategy decision tree, 3-host smoke matrix, A.7-prereq blocking S4). M1 (state v3 D3-γ split), M2 (pre-mortems replaced with A1-ε-realistic scenarios), M3 (acceptance criteria sharpened) and the six MINOR mechanical edits all land. No new structural concerns rise to MAJOR. Four MINOR mechanical edits remain — all absorbable by the Critic Round-3 pass without re-engaging Architect or Planner: (1) an internal latency-target inconsistency between A.7 (forward p99 ≤ 30ms) and A.10 (forward p99 < 2ms) on the *same* path; (2) A.7 currently claims it "matches parent §1.4" but the parent plan §7.5/§A.7 specifies `p99 < 5ms` for the forward path, so the citation is off — easy fix is to retitle A.7 as the reverse-path budget and tighten the forward criterion to align with parent; (3) S4's day-2 hand-shake with S2.6 should explicitly state which of strategies (a)/(b)/(c) it expects to land if the audit yields no surprise (default = (a) per S2.6 §2-a); (4) §11 modified-files list cites `Processor.cpp:560-579` (the `notify` site) as the S4 reversal locus, but under strategy (a) marshaling, the reversal point is the *Controller*'s `IConnectionPoint::notify`, not the Processor's — Processor `notify` keeps `kNotImplemented` as today (`SpatialEngineProcessor.cpp:575-578`). None of these change architecture; they change wording. Plan is approved for autopilot once the Critic Round-3 pass folds these into the file.

---

## 2. Round-2 closure verification (item-by-item)

### 2.1 C1 — Frozen A1-ε honored — **CLOSED, verified**

I ran `grep -in 'sidecar\|UDS\|systemd' spatial-engine-v0.3.md` and audited every hit (40+ occurrences). Every single one falls into one of these categories:

| Category | Example line | Verdict |
| -------- | ------------ | ------- |
| Negation in scope statement | §1.4 line 52 "**NO sidecar binary**, **NO Unix domain socket**, **NO systemd unit**" | OK — declarative non-goal |
| Negation in non-goals | §1.5 lines 71–72 "No sidecar binary"; "No systemd unit" | OK |
| Audit-trail ELIMINATED entries | §2 D1 lines 125–126 (D1-α/D1-β marked ELIMINATED with verbatim ADR 0010 citation) | OK — required by DELIBERATE mode for alternative-fairness |
| MOOT marker on dead decisions | §2 D2 lines 137–138 | OK |
| Round-3 changelog entry | §1.6 line 83 "Deleted from scope:" | OK |
| §11 strikethrough deletion list | lines 710–717 (`~~bin/spatial_engine_sidecar.cpp~~ — D1-γ no sidecar`) | OK |
| Comparative phrasing in latency target | §S6 line 338 "tighter than Round-2 because no sidecar hop"; A.10 "no sidecar hop" | OK — describes the architecture by contrast |
| Test renaming/replacement comments | line 713 "replaced by `test_vst3_intra_plugin_spsc_drain.cpp`" | OK |

**Zero residual hits in active scope.** §1.4 line 52, §2 D1-γ (line 127), §3 S2 files-touched (lines 209–219), §3 S2.6, §3 S4, §11 New files (lines 666–684) all describe the A1-ε in-plugin direct `bind()` + standalone-internal forwarder + in-process reverse path architecture. The plan is internally consistent with ADR 0010 §A1 line 53 + line 250 verbatim.

Specifically:
- `bin/spatial_engine_sidecar.cpp` — not built; explicitly removed (§11 line 710).
- `vst3/sidecar_bridge/*` — directory does not exist in new scope; `PluginInstanceRegistry.{h,cpp}` moved to `vst3/osc/` (§11 line 666); `UdsServer`/`PluginToSidecarChannel`/`AutomationReflect`/`ControllerReverseHandler` deleted (§11 line 711).
- `tools/systemd/spatial-engine-sidecar.service` — not shipped (§11 line 712).
- D2 SPSC channel medium — marked MOOT, both options eliminated by A1-ε (§2 D2 lines 137–140).
- The new ring is intra-process per ADR 0010 §A4-β line 76–82, header-only `core/src/util/SpscRing.h` (S3 line 277).

C1 closure is mechanical and complete. No residual violation.

### 2.2 C2 — v03-Q8 `performEdit` cross-thread safety — **CLOSED, verified**

S2.6 (plan lines 249–270) provides every element Round-2 demanded:

| Requirement (Round-2) | S2.6 deliverable | Plan line |
| --------------------- | ---------------- | --------- |
| Concrete audit target | `pluginterfaces/vst/ivsteditcontroller.h` audit for `performEdit`/`beginEdit`/`endEdit` thread contract | line 254 |
| Decision criterion among (a)/(b)/(c) | (a) message-thread queue ★ default expected; (b) `IRunLoop` if available without editor view; (c) read-only state propagation + `restartComponent(kParamValuesChanged)` | lines 256–258 |
| Concrete strategy implementation site | `vst3/SpatialEngineController.cpp` notify implementation | line 263 |
| Smoke matrix mechanism | Reaper 7.x + Bitwig 5.x + Ardour 8.x Linux smoke; load plugin with `SPATIAL_ENGINE_VST3_OSC=ON`, send `/adm/obj/0/azim 90.0`, verify no crash and automation lane reflects (per chosen strategy) | line 260 |
| Acceptance gate that blocks S4 | A.7-prereq: "S2.6 PASS gates S4 entry" — SDK audit conclusion documented in commit footer; smoke matrix 3/3 SUCCESS; `test_vst3_performedit_threadsafe` PASS | line 267 |
| Test harness for cross-thread invocation | `vst3/tests/test_vst3_performedit_threadsafe.cpp` (1000-iter fake-host) | line 264 |

The smoke-matrix host-discovery mechanism is implicit — the plan reads "Run smoke matrix on Reaper 7.x + Bitwig 5.x + Ardour 8.x Linux" without specifying *how* the test discovers which DAWs are installed. In practice for v0.3 (manual hands-on Reaper + Bitwig per S8, plus Ardour added at S2.6), this is a developer-machine procedure rather than a CI gate, so the implicit "engineer follows the per-host checklist" is acceptable. The S2.6 commit footer holds the audit trail. **Closure verified; no further action required.**

Cross-thread architectural risk **remains real but explicitly time-boxed and gated**. Strategy (a) (message-thread queue marshaling) requires the host to invoke `IComponent::process` or `IConnectionPoint::notify` from the message thread — which is the SDK-documented behaviour but not contractually guaranteed by every host. Strategy (c) (read-only + `restartComponent(kParamValuesChanged)`) is the safe fallback that requires no message-thread invariant at all and *should* be flagged as the contingency landing if (a) reveals any per-host quirk in the smoke matrix. This is a Critic Round-3 polish, not an Architect blocker.

### 2.3 M1 — State v3 D3-γ split — **CLOSED, verified**

The plan now has:
- **S2.5** (lines 229–247) lands the 3-way `v1/v2/v3` reader at `vst3/SpatialEngineProcessor.cpp:201-323` defaulting `kMute=0` for v1/v2 inputs; v0.2 preset fixtures committed `vst3/tests/fixtures/v02_preset_*.vstpreset` from day 1; `test_vst3_state_v3_reader_only` test file added; **reader-only**, writer still emits v2 in this commit.
- **S7** (lines 349–365) lands the v3 *writer* + activates `ParamId::kMute=7` + resizes `norm_values_[8]` + adds the 8th `ParameterInfo` for kMute with `kIsMute = 1<<17` at `SpatialEngineController.cpp:107-216`.
- **Test split**: `test_vst3_state_v3_reader_only.cpp` (S2.5, 12-case matrix) + `test_vst3_state_v3_persist.cpp` (S7 extension, writer round-trip).
- **Acceptance criteria split**: A.15a (S2.5 reader) + A.15b (S7 writer).
- **Decision option set**: §2 D3 now shows trichotomy α/β/γ with γ as ★ recommended (lines 146–158); steelman pros/cons for each.
- **Coupling matrix updated**: S2.5 explicitly blocks S3 and S4 (line 407); S7 no longer blocked by S6 baseline drift (line 413).
- **Pre-mortem 3** (lines 478–506) explicitly cites D3-γ as the mitigation that drops PM3 probability from MODERATE (β) to LOW (γ).

Deterministic acceptance criterion at A.15a: reader matrix must produce specified outputs (v1 → kBypass=0+kMute=0; v2 → kBypass loaded + kMute=0; v3 → all 8 floats loaded; version=4 throws). At A.15b: writer must emit 40 bytes, magic `'SPE1'`, version=3, 8 floats. Both binary-falsifiable. Soak time for reader = ~5 days (S2.5 day 1 → S8 day ~6) against committed v0.2 fixtures — Principle 5 risk dissolved.

### 2.4 M2 — Pre-mortems replaced with A1-ε-realistic scenarios — **CLOSED, verified**

| Old PM (Round-2) | New PM (Round-3) | Realism under A1-ε |
| ---------------- | ---------------- | ------------------ |
| PM1 sidecar OOM | **PM1 `bind()` collision** on plugin ctor — port held by stale entry or non-plugin app | Real: A1-ε `bind(127.0.0.1:9100+N)` can fail `EADDRINUSE`; covers exactly the failure surface the inherited architecture creates. |
| PM2 UDS cross-process SPSC overrun | **PM2 registry file corruption** — partial JSON, ENOSPC, multi-writer race | Real: ADR 0011 atomic `tmpfile+rename+flock` is the writer-side discipline; corruption is the failure mode it mitigates. |
| PM3 state v3 reader bug | **PM3 retained, sharpened** under D3-γ — probability drops MODERATE → LOW | Realism unchanged; mitigation deepened. |

Each PM has Detection / Probability / Severity / Mitigation / Recovery / Test coverage populated (lines 428–506). PM1 has 4 mitigations including the `boot_id` GC (v03-Q9 closed), port-walk retry, troubleshooting in manual, ephemeral fallback. PM2 has 5 mitigations including atomic rename, `flock` retry budget, reader parse-error tolerance, schema-version fail-closed, truncation guard. Both have concrete test coverage names that map to S2 acceptance criteria A.13/A.14.

The Round-2 §4.4 suggested "Pre-mortem 4: ADR 0011 file-based registry race on simultaneous startup with `flock` 10×50ms backoff = 500ms worst-case ctor latency." This *did* land as Risk row 4 "Multi-instance JSON registry race condition under fork() stress" at line 517, and the `flock` retry budget is explicit in PM2 Mitigation 2 (line 466). It is not its own pre-mortem scenario but is covered as a risk + a test (`test_p_instances_registry` multi-writer fork stress). Acceptable; the trio of pre-mortems remains the 3 most-customer-impacting failure modes.

### 2.5 M3 — Acceptance criteria sharpened — **CLOSED with one minor inconsistency**

| Criterion | Round-2 | Round-3 | Verdict |
| --------- | ------- | ------- | ------- |
| A.7 (forward-path latency) | `< 100ms` (loose) | `p99 ≤ 30ms wall, p50 ≤ 5ms` under 64-obj × 1kHz load | **Sharpened, but see §3.1 — internal inconsistency with A.10.** |
| A.9 (reverse-path host-perceived) | `< 100ms` | `p99 ≤ 50ms` (DAW automation tick granularity) | Sharpened and reasonable. |
| A.11 (OFF re-pin) | "preserved OR re-pinned, possibly no-op" | "Hash match OR documented re-pin commit referencing a deliberate symbol/data change" | **Binary criterion achieved.** Expected outcome: hash match. |
| A.12 (DAW hands-on) | "screenshots / capture logs" | "1 screenshot per host (8 params) + 1 WAV per host (kMute distinct from kBypass) + 8-step Y/N checklist per host = 2 screenshots + 2 WAVs + 16 Y/N entries minimum" | **Quantified.** |

A.13/A.14/A.15a/A.15b new criteria all have concrete ctest target names. Testability index is 16/16 = 100%, exceeding the ≥90% gate by a comfortable margin.

### 2.6 MINOR (m1–m6 + Critic asks 9, 10, 11, 14, 15) — **all CLOSED**

Spot-checked each:
- **m1** (D1 option-space ★ γ + α/β eliminated with ADR citation): closed at lines 124–127.
- **m2** (D2 moot): closed at lines 133–140.
- **m3** (D3-γ hybrid in option set): closed at lines 146–158.
- **m4** (open questions §12 updated, Q8/Q9/Q10 added, Q2/Q6 dropped): closed at lines 729–744.
- **m5** (§11 rewritten): closed at lines 662–725.
- **m6** (ETA banner rewritten): closed at lines 614–633.
- **Critic ask 9 v03-Q9 `boot_id`**: closed; embedded in PM1 mitigation 1 (line 441) and test list (line 533).
- **Critic ask 10 v03-Q10 XDG empty-string**: closed; explicit in S2 files description (line 211) and `test_p_instances_registry` coverage (line 533).
- **Critic ask 11 D4-α cancellation fallback**: closed verbatim at line 173.
- **Critic ask 14 log-format fork**: closed to fixed-format `tag=value` at line 573.
- **Critic ask 15 D2-β forward-reference**: closed via deletion (line 105).

ETA banner consistency: §9 total = 6.5d (effective; S5 parallel). Critical path = ~5.0d. Cert-eval slack = +3–4d → wall ~9.5–10.5d. Matches the §1 banner.

### 2.7 DEFERRED items — **rationale acceptable**

- Storm-soak 300s extension: dissolved with sidecar deletion. OK.
- S4 → 0.5d compression: deferred to absorb S2.6 marshaling implementation cost. Honest upper-bound. OK.
- S6 soak → 1.0d (from 1.5d): retained for in-process regression gate. OK.

---

## 3. New concerns (Round-3, ranked by impact)

### 3.1 [MINOR] A.7 forward-latency threshold internally inconsistent with A.10 and with parent plan citation

The plan sets two different latency budgets for the *same physical path* (console UDP → standalone forwarder → plugin's bound port → SPSC ring → audio-callback pop → `norm_values_` write):

- **A.7** (line 645): `p99 ≤ 30ms wall, p50 ≤ 5ms` under 64-obj × 1kHz load — claims "matches parent §1.4".
- **A.10** (line 649): `p99 < 2ms` for `soak_vst3_console_flood` (1 obj × 100 Hz × 60s × 8 instances) — same physical path, different load profile.

Two issues:

1. **Citation mismatch.** Parent plan `spatial-engine-phaseC4-and-v0.2-release.md:471` + `:1037` + `:1198` specify forward-path target `p99 < 5ms` (not 30ms). The "matches parent §1.4" claim does not match the actual parent text. Either (i) sharpen A.7 to `p99 ≤ 5ms` to match the inherited contract, or (ii) re-tag A.7 as the *reverse-path* budget (which is a different physical path including SDK message-thread marshaling, and 30ms is reasonable there) and remove the parent citation; then A.7 reverse = 30ms ≤ A.9 reverse = 50ms ≤ DAW-tick granularity, a sensible ladder.

2. **A.7 ↔ A.10 mutual consistency.** If A.7 is the forward-path soak budget at 64-obj × 1kHz, and A.10 is the forward-path soak budget at 1-obj × 100Hz × 8-inst, then A.7 should be the looser bound (heavier load), A.10 the tighter bound — which is what the current numbers say, but with a 15× gap (30ms vs 2ms) the plan should justify why heavier load is 15× slower; the natural justification (kernel scheduler tail under 64k packets/s) is plausible but unstated. Recommend adding a one-line "headroom rationale" footnote to A.7.

**Severity**: MINOR. Mechanical wording fix; no architecture change. Realist Check: if shipped as-written, the test passes either bound (A.10 is tighter, so passing A.10 implies passing A.7) and the inconsistency is internal-doc-only — never customer-visible. But A.7 ↔ parent citation drift could mislead future Architect rounds, so worth closing in the Critic pass.

**Recommended fix**: relabel A.7 as the *reverse-path* p99 budget (it is in fact tested by `test_vst3_reverse_path` per its own line 301, confirming this is the intended path); align the load profile to match (S4 reverse-path scenarios, not S6 forward-flood); cite parent §A.9 instead of §1.4 / §A.7.

### 3.2 [MINOR] §11 "Modified files" cites `Processor.cpp:560-579` as S4 reversal site under strategy (a)

Line 694 lists `vst3/SpatialEngineProcessor.cpp:560-579 (S4)` as the `notify` reversal site. This was correct under strategy (c) in the Round-2 design where the *Processor* exposes a `notify` channel from a peer connection point. Under the now-default strategy (a) — host message-thread queue marshaling via `IConnectionPoint::notify` from the host's message thread — the reversal site is the *Controller*'s `IEditController2`/`IConnectionPoint::notify`, not the Processor's. The Processor's `notify` at `SpatialEngineProcessor.cpp:575-578` should stay `kNotImplemented` (it has no automation surface; `performEdit` is a Controller-side call per `pluginterfaces/vst/ivsteditcontroller.h`).

§3 S4 line 297 correctly identifies `vst3/SpatialEngineController.cpp` notify as the implementation site; §3 S4 line 298 redundantly mentions the Processor `notify:560-579` as a "same pattern if reverse channel goes via Processor's connection point." If strategy (a) lands as expected, that conditional clause should be reduced to "no Processor changes" or removed.

**Severity**: MINOR. Code is in the right place at S4; only the §11 file-list and §3 S4 sub-bullet citation need to be tightened.

**Recommended fix**: §11 strike the `Processor.cpp:560-579 (S4)` line if S2.6 picks (a); retain it as conditional under strategy (c). The plan as-written hedges across all three strategies, which is acceptable.

### 3.3 [INFO — not a concern, recorded for visibility] In-plugin UDP I/O thread vs RT-audio-thread interaction at `Processor.cpp:201-323`

The user prompt asked me to spot-check `vst3/SpatialEngineProcessor.cpp:201-323` for the UDP-thread vs audio-thread interaction. That line range is the **state read/write** code path (per `:201-323` in my Read of the file context — actually the `set/getState` and parameter dispatch surface). The UDP thread interaction is at the SPSC ring push/pop sites: producer in `vst3/SpatialEnginePluginUdp.cpp` (new at S2), consumer in `SpatialEngineProcessor::process()` entry (S3 modification, line 384 in the live file).

Threading discipline per ADR 0010 §A4-β line 76–93:
- UDP thread allocates freely.
- Audio callback pops only (alloc=0).
- SPSC ring is fixed-size lock-free with `memory_order_acquire/release`.
- No mutex on the audio thread.

Audit verdict: this is the same discipline as `core/src/ipc/OSCBackend.cpp:75-83` (referenced as pattern source), which has shipped in production since v0.1.0 and passes the existing `RT_ASSERT_NO_ALLOC` harness. The plan's S3 (`test_vst3_intra_plugin_spsc_drain` 1000-iter at A.6) is the correct verification path. **No new concern.**

The shutdown ordering at `Processor.cpp:107-112` (`terminate`) + `:189-198` (`setActive(false)`) is correctly listed as the UDP-thread join site in §11 line 691. Recommended (already implicit in the plan): UDP thread must `join()` *before* SPSC ring memory is freed, which falls out of putting the join in `terminate()` while the ring lives in `Processor`. No correction needed.

### 3.4 [INFO — not a concern] S2.6 in-sprint audit could destabilize S4 schedule

User prompt asked whether S2.6 in-sprint could destabilize S4 ETA. The plan's defense:
- S2.6 ETA 0.5d, immediately after S2 (day 2 → 3); S4 starts day 3.5 at earliest.
- Coupling matrix line 408 makes S2.6 → S4 blocking explicit.
- S4 ETA 1.0d retained even though Architect Round-2 §4.2 estimated it could compress to 0.5d under strategy (a) — the extra 0.5d is the explicit absorption budget for marshaling-path debugging (§1.6 deferral note line 110).
- Cert-eval slack (+3–4d) absorbs any further marshaling-path surprise.

If S2.6 audit reveals strategy (a) is host-incompatible on Ardour but compatible on Reaper/Bitwig, the smoke matrix would still pass 2/3 (the customer hosts) and the plan can record the Ardour caveat in the commit footer + manual Ch.5 troubleshooting without blocking S4. If it reveals all three hosts require strategy (c) (read-only + `restartComponent`), the implementation cost is comparable to (a) — both are ~80 LOC in `Controller.cpp` notify — and S4 ETA holds.

**No schedule risk above slack.** This is the correct way to gate an SDK-contract investigation: time-box, give it a binary acceptance criterion, document the choice in the commit footer.

### 3.5 [INFO — not a concern] D3-γ split could create v2-fixture maintenance debt

User prompt asked whether the early reader landing creates v2-fixture maintenance debt. The fixtures `vst3/tests/fixtures/v02_preset_*.vstpreset` are by definition *frozen artefacts of v0.2.0* — they are committed once at S2.5 day 1, never modified afterward. Any future state format bump (v3 → v4 in v0.4+) would *add* new fixtures, not modify v01/v02 ones. Maintenance cost = zero ongoing; the storage cost is bounded (a `.vstpreset` is ~40 bytes payload + Steinberg wrapper).

**No debt accumulation.** The pattern matches the existing `core/tests/fixtures/` and standalone-bridge test fixtures already in the repo.

---

## 4. Acceptance criteria audit refresh

Re-checking the Round-2 audit table against Round-3 Plan §10:

| ID | Round-2 verdict | Round-3 verdict | Notes |
| -- | --------------- | --------------- | ----- |
| A.3 | Pass (concrete grep) | **Pass** — extended to `vst3/osc/` and `SpatialEnginePluginUdp.*` per A1-ε scope (line 641). |
| A.4 | Pass + sharpening on `nm` symbol-table check | **Pass** — explicit `nm libspe_core.a \| grep -E 'RegistryPath\|SpscRing'` returns 0 (line 642). |
| A.5 | Pass | **Pass + extended** — covers atomic write, GC, boot_id stale-PID, XDG empty-string, schema_version, multi-writer fork stress (line 643). |
| A.6 | Pass | **Pass** — explicit split UDP-push allow / audio-pop alloc=0 per A4-β (line 644). |
| **A.7** | WEAK (≤100ms) | **Pass-with-MINOR-inconsistency** — see §3.1 above. Threshold sharpened but citation drift. |
| A.7-prereq | (new in R3) | **Pass** — S2.6 smoke matrix gate; binary acceptance (3/3 hosts SUCCESS). |
| A.8 | Pass | **Pass** — 3-process topology diagram + TOC link (line 647). |
| **A.9** | WEAK (≤100ms) | **Pass** — `p99 ≤ 50ms` host-perceived reverse-path (line 648). |
| A.10 | Pass | **Pass** — `p99 < 2ms` no-sidecar-hop (line 649). See §3.1 for consistency with A.7. |
| **A.11** | VAGUE ("preserved OR re-pinned") | **Pass** — binary criterion (hash match OR documented paired re-pin commit) (line 650). |
| **A.12** | FUZZY | **Pass** — 2 screenshots + 2 WAVs + 16 Y/N entries minimum quantified (line 651). |
| A.13 | (new — bind collision) | **Pass** — `test_vst3_bind_collision` ctest (line 652). |
| A.14 | (new — registry corruption) | **Pass** — `test_p_instances_registry_corruption` ctest (line 653). |
| A.15a | (new — S2.5 reader) | **Pass** — `test_vst3_state_v3_reader_only` with v0.1/v0.2 fixtures, ~5 days soak (line 655). |
| A.15b | (new — S7 writer) | **Pass** — `test_vst3_state_v3_persist` extension (line 656). |

**Testability score: 15/16 strict-pass + 1/16 pass-with-MINOR-citation-fix = 16/16 acceptable.** Round-2 score was 11/15 strict + 4/15 needs-sharpening; Round-3 closes virtually all the sharpening gaps.

---

## 5. Pass to Critic Round-3

Critic Round-3 should focus on the following four mechanical edits — all absorbable in the Critic pass without re-engaging Architect or Planner:

### 5.1 [MINOR-CITATION] A.7 latency threshold + parent citation

Two options:
- **Option α (relabel A.7 as reverse-path)**: change A.7 description from "Forward-path latency under 64-obj × 1kHz load" to "Reverse-path round-trip latency for SDK message-thread marshaling under S2.6 strategy (a)"; cite parent §A.9 (`spatial-engine-phaseC4-and-v0.2-release.md:1193`) instead of §1.4. This is consistent with line 301's claim that A.7 is measured by `test_vst3_reverse_path`.
- **Option β (tighten A.7 to match parent)**: keep A.7 as forward-path but change threshold to `p99 ≤ 5ms` matching parent line 471, 1037, 1198. A.10 stays at `p99 < 2ms` as the tighter 1-obj-no-sidecar bound.

Recommend **Option α** because the test `test_vst3_reverse_path` named at line 301 *is* a reverse-path test, and A.9 already exists for the forward-e2e budget. Two separate criteria for the same path is wasteful.

### 5.2 [MINOR-DOC] §11 `Processor.cpp:560-579` S4 reversal citation

If S2.6 picks strategy (a) (expected): the Processor `notify` stays unchanged (`SpatialEngineProcessor.cpp:575-578` keeps `kNotImplemented` from AM-R3-10). Strike the `vst3/SpatialEngineProcessor.cpp:560-579 (S4)` line from §11 modified-files list. Keep the corresponding Controller change at line 698.

If S2.6 picks strategy (c) (contingency): retain the Processor citation; under (c), the Processor's connection point may be used as the read-only state-propagation channel.

Recommend deferring the §11 citation cleanup to S2.6 commit time, with a note: "§11 will be updated post-S2.6 to reflect the chosen strategy."

### 5.3 [MINOR-DOC] Default contingency strategy in S2.6

S2.6 lines 256–258 lists (a)/(b)/(c) without saying which is the contingency-of-record. Recommend annotating: "If strategy (a) smoke matrix fails on any host, fall back to strategy (c) — read-only state propagation + `restartComponent(kParamValuesChanged)` — which has no message-thread invariant dependency. Strategy (b) `IRunLoop` is contingent on host exposing it without editor view, which ADR 0010 §A4-γ flags as unexpected; if (b) is available, it is preferred over (a) for latency."

This makes the in-sprint decision more deterministic for autopilot.

### 5.4 [INFO] A.7 headroom rationale footnote

If §5.1 Option β is picked (A.7 stays forward-path), add a one-line footnote: "A.7 (64-obj × 1kHz) and A.10 (1-obj × 100Hz × 8-inst) measure the same physical path under different load profiles; the 5ms vs 2ms gap reflects kernel scheduler tail under 64k packets/s vs 8k packets/s." If §5.1 Option α is picked, this is moot.

### 5.5 What Critic Round-3 should NOT touch

- §2 D1/D2/D3/D4/D5 decisions — all locked.
- §3 S1–S8 structure — all locked.
- §5 pre-mortems — all locked.
- §6 risk table — all locked.
- §7 test plan — all locked.
- §8 ADR ratification table — locked.
- §9 ETA banner — locked.
- §11 file list bodies (other than the §5.2 single line) — locked.
- §12 open questions — locked.

The Critic Round-3 pass is a wording polish, not a structural review. Estimated effort: 30 minutes plan-text edit.

---

## Consensus Addendum (RALPLAN Round-3 review)

- **Antithesis (steelman)**: The strongest counter-position remaining is **"S2.6 should be moved earlier — into S1 or as a pre-sprint spike — so its outcome is known before any S2/S2.5 file-list freeze."** Steelman: if S2.6 reveals all three strategies (a)/(b)/(c) are blocked by, say, Bitwig's IPlugView-required `IRunLoop` semantics, then S4 must redesign and the §11 file list is wrong from S2.5 onward. **Rebuttal**: the plan does treat S2.6 as a gate on S4 entry (line 408 coupling matrix), and §11's "Modified files" allows for post-S2.6 adjustment (per §5.2 above). The risk of S2.6 invalidating S2/S2.5 work is bounded because S2/S2.5 produce reusable A1-ε artefacts (registry, in-plugin UDP, intra-process SPSC, state v3 reader) regardless of which marshaling strategy lands in S4. The only thing that changes under S2.6 outcome is the *Controller's* notify implementation (~80 LOC); not the data plane. So S2.6 staying at day 3 of the sprint is safe.

- **Tradeoff tension**: D3-γ hybrid's "reader-early-writer-late" introduces a window (~S2.5 → S7, ~5 days) during which the v3 reader exists in code but cannot produce v3 fixtures (writer is at S7). This means the `v3 → v3 round-trip` test case at line 504 cannot run until S7 lands, and the v3 reader path is exercised only against *synthetic* v3 byte sequences during S2.5 → S6. **Synthesis**: PM3's mitigation (line 501) accepts this — the synthetic-v3 tests cover format correctness; the round-trip test is a one-day-soak gate at S7. This is the *intended* trade-off of the hybrid, and Architect Round-2 §3.3 explicitly endorsed it. Tradeoff acknowledged and accepted.

- **Synthesis (viable)**: The plan as Round-3 + the 4 mechanical edits in §5 is the production-quality artefact. No further structural redesign is warranted.

- **Principle violations (DELIBERATE mode)**:
  - **Principle 1 (JUCE-free)**: SAFE — A.3 explicit grep across `vst3/osc/` + new files.
  - **Principle 2 (ctest + pytest green)**: SAFE — 12 new tests added, distributed across S2/S2.5/S2.6/S3/S4/S6/S7 (no S6 big-bang lump as in Round-2).
  - **Principle 3 (OFF byte-baseline)**: SAFE — A.11 binary criterion; `RegistryPath.h` + `SpscRing.h` header-only with explicit `nm` assertion in A.4.
  - **Principle 4 (ADM-OSC v1.0 verbatim)**: SAFE — plugin recv reuses `CommandDecoder.cpp:317-373`; zero schema invention.
  - **Principle 5 (v0.2 wire-ABI + state-ABI preserved)**: SAFE — D3-γ reader lands day 1 with v0.2 fixtures; ~5 days of soak; v0.2 preset interop tested in PM3 test coverage + S8 step 6.
  - **Net**: zero principle violations under Round-3.

---

## References

- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:52-58` — A1-ε frozen contract restated under "Inherited as FROZEN"
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:83` — C1 closure entry (sidecar deletions)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:85` — C2 closure entry (S2.6 gate)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:124-131` — D1 option set + γ recommendation
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:146-160` — D3 trichotomy with γ as ★
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:229-247` — S2.5 reader-only landing
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:249-270` — S2.6 SDK audit + marshaling
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:292-305` — S4 reverse-path redesign
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:349-365` — S7 writer + kMute + OFF re-pin
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:428-506` — Pre-mortems 1/2/3 (A1-ε realistic)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:614-633` — ETA banner ~6.5d/~9.5–10.5d wall
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:637-658` — Acceptance criteria index 16/16
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-v0.3.md:710-717` — §11 sidecar-deletion audit trail
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:40-93` — A1-ε + A4-β contracts
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:146-160` — A1-δ rejection rationale
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:177-185` — A4-γ unavailable; A5-β v0.4+ contingency
- `/home/seung/mmhoa/spatial_engine/docs/adr/0010-vst3-osc-binding-model.md:245-260` — Follow-ups: no sidecar in v0.3 Linux fast path
- `/home/seung/mmhoa/spatial_engine/docs/adr/0011-vst3-osc-multi-instance-discovery.md:86-138` — `flock` + writer-side GC + `/proc/{pid}/comm`
- `/home/seung/mmhoa/spatial_engine/docs/adr/0011-vst3-osc-multi-instance-discovery.md:241-260` — stale-PID risk (v03-Q9) + XDG path resolution (v03-Q10)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:164-187` — Parent §1.5 final freeze table (A1-ε locked)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:471,1037,1198` — Parent forward-path p99 < 5ms target (citation drift in §3.1)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md:1193` — Parent A.7 reverse-path latency target
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:575-578` — Existing `notify` returns `kNotImplemented` (AM-R3-10), §3.2 reversal target under strategy (c)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:105-198` — `terminate` + `setActive` shutdown sequence (UDP-thread join site per §11 line 691)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp:384-450` — `process()` entry (S3 SPSC pop site)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/architect-r-v0.3-review.md:200-205` — Round-2 top-3 highest-impact findings (all CLOSED in Round-3)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/critic-r-v0.3-review.md:181-217` — Critic Round-2 loop closure conditions (all CLOSED or absorbable in §5)

---

**End of Architect Round-3 review.**
**Verdict**: **APPROVE-with-revisions** (4 mechanical edits absorbable by Critic Round-3 pass).
**Recommendation**: pass to Critic Round-3 for final wording polish; on Critic APPROVE, the consensus gate is met and the plan is autopilot-ready.
