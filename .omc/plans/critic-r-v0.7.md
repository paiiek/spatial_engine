# spatial_engine v0.7 — Critic review (RALPLAN-DR deliberate)

| | |
|---|---|
| **Date** | 2026-05-18 |
| **Reviewer** | Critic (omc-critic, deliberate mode) |
| **Plan under review** | `.omc/plans/spatial-engine-v0.7.md` (Planner, 490 lines, 8 scope items) |
| **Architect input** | `.omc/plans/architect-r-v0.7.md` (Verdict APPROVE-WITH-RECOMMENDATIONS, 4 Must-fix + 6 Should-have + 3 Nice-to-have) |
| **Companion** | `.omc/plans/critic-r-v0.6-retro.md` (own retro), `.omc/plans/open-questions.md` V07-Q1..Q8 |
| **Mode** | **DELIBERATE** — RT-safety code, new outbound OSC verb, cross-platform CI promotion, GPL legal-surface doc |
| **Verdict preview** | **ITERATE** — Architect's APPROVE is *almost* defensible, but I find: (1) 3 of 4 Must-fix confirm, 1 downgrades to Should-have; (2) plan contains 3 *new* MAJOR risks Architect missed; (3) the ctest count baseline in §3 / §4 is **factually wrong** (claimed 86 → actual 115); (4) one Architect Should-have (AS-1 block_size guard) is **already in production code at line 461** — Architect did not verify. ITERATE per deliberate-mode obligation. |

---

## §A. Architect's 4 Must-fix — meta-judgment

I read `core/src/output_backend/BinauralMonitor.cpp:457-510`, `.h:220-280,488-495`, `core/src/ipc/OSCBackend.h:100-291`, `.cpp:436-492`, `vst3/SpatialEngineProcessor.cpp:1280-1365`, `.github/workflows/cross-platform.yml`, `tests/soak_harness/test_osc_warning_channel.py:180-252`, `docs/ipc_schema.md` §"Binaural Telemetry", `docs/adr/0016-external-distribution-policy.md` §"Appendix A" + §"Limitations & legal review status", and `docs/weekly_progress_report_2026-05-18.md` §5.1 P-tag table.

### §A.1 AM-1 (D-S1 strike-counter reset must be explicit) — **CONFIRM, severity MAJOR (Must-fix retained)**

**Verification** (`BinauralMonitor.cpp`):
- Line 461: `if (block_size <= 0 || sample_rate <= 0.f) return;` (NB: this also moots Architect AS-1 — see §A.2.AS-1 below).
- Line 464: `if (runtime_demoted_.load(std::memory_order_acquire)) return;` — confirmed early-return on demote.
- Lines 474-481: strikes counter only resets to 0 on an *under-budget* block (`else` branch). After demote latch fires at line 488 CAS, recordB2BlockTiming early-returns and never reaches the reset branch.
- Lines 47-49 (`initialize()` D-M1 fix): the 3-atomic reset pattern. `runtime_demote_strikes_.store(0)` IS included.

Therefore: after `runtime_demoted_=true`, `runtime_demote_strikes_` is frozen at whatever value the CAS-winner observed (≥ kRuntimeDemoteStrikes). The plan's §2 Item #1 says the reset "performs the same 3-atomic-store reset as `initialize()` D-M1 pattern" but lists only `runtime_demote_last_reset_ns_` and `reset_rejected_count_` in the *fix description* sentence. The plan IS internally inconsistent — `initialize()` clears 3 atomics, the plan refers to "the same 3-atomic-store reset", but does not enumerate which 3 in the user-reset method. Architect is right that this must be explicit.

**Confirm Must-fix**. Add to §2 Item #1: "reset clears `runtime_demote_strikes_=0`, `runtime_demoted_=false`, `runtime_demote_warning_pending_=false`, snapshots `runtime_demote_last_reset_ns_=now_ns`. Also clear `runtime_demote_max_ratio_x1000_=0` and `runtime_demote_max_ratio_at_event_x1000_=0` introduced by D-S3, otherwise next demote would inherit stale telemetry." The Architect missed the D-S3 max-ratio atomics in his AM-1 list — this is a strictly stronger statement of the same fix.

### §A.2 AM-2 (D-S3 invert default — relaxed-store first, CAS later) — **CONFIRM with PARTIAL pushback, severity MAJOR (Must-fix retained)**

The Architect's reasoning ("the precision benefit is unmeasured; relaxed-store last-writer-wins loses at most 1 strike's worth") is sound and the mitigation-ordering inversion critique is correct. Plan §7.1 Scenario A's "mitigation if it fires" should be the *default* implementation, not the fallback. This is exactly the kind of design where "telemetry before tuning" (Principle 4) applies recursively: we shouldn't pay an unmeasured precision cost for telemetry we haven't yet used.

**Partial pushback** (not a downgrade — a refinement): the plan's CAS-update pattern (`if (new > current) CAS-update`) is itself not free of risk under contention but Architect's relaxed alternative `store(max(current, new), relaxed)` is **not strictly correct without a load**: the simple `store(new, relaxed)` will overwrite a *larger* previous value. The actual correct relaxed pattern is `int cur = load(relaxed); if (new > cur) store(new, relaxed);` — which is itself last-writer-wins under contention but preserves the "track the max" invariant under no contention (single audio thread). Since this method is only ever called from the single audio thread per `BinauralMonitor` instance (verified — `recordB2BlockTiming` is called only from `SpatialEngine::audioBlock` per `SpatialEngine.cpp:723-741` retro citation), there is no actual producer contention. **The Architect's relaxed-store proposal is correct in spirit but needs to read-then-store**, not a single `store`. Add this precision to the §2 Item #3 spec.

**Confirm Must-fix** with the read-then-store correction.

### §A.3 AM-3 (encode-API choice a/b/c) — **CONFIRM Architect's recommendation (b), severity MAJOR (Must-fix retained)**

**Verification** (`OSCBackend.cpp:436-492`):
- Current `sendReplyImpl(addr, types, s, have_f, f, have_i, i)` (line 436) is the v0.6 #8 unification — 7 parameters, 2 boolean flags, 3 typed payloads.
- The 3 public overloads (lines 477-492) are thin forwarders.

The Architect's option (a) would extend the flag set to `have_f, f, have_i, i, have_i2, i2` — 9 parameters, 3 flags. This breaks v0.6 #8's "thin forwarder" invariant only marginally but each call site now spells two extra `false, 0`. Option (b) is a dedicated `sendReplyImplIIF(addr, types, i1, i2, f)` — adds ~30 LOC of socket-plumbing duplication.

**My take, with stronger reasoning than the Architect's**: option (b) is correct for v0.7 *but* the plan must explicitly note the v0.6 #8 ADR (or equivalent design note) that this duplication is intentional and temporary. Without that note, a future maintainer who reads v0.6 #8 ("unify the impls") may re-fuse the two impls and pay the option (a) cost without realizing it was previously rejected. **Stronger fix than Architect**: add an inline `// v0.7 D-S3 — intentional duplication of sendReplyImpl. See ADR 0017 §B for rejection of flag-extension option (a).` comment block. Pin this in §2 Item #3 and §6 ADR 0017 sections.

The Architect's open-question §F.2 asks Critic to consider whether (a) is *actually* cleaner because it preserves "one impl, many overloads" as an invariant. **Cold-eye answer: no.** v0.6 #8's invariant was "the 3 simple typetags `,s/,sf/,i` share an impl" — not "every future typetag must share one impl." Mixed-type packets (`,iif` and any future `,sif`/`,iiif` etc.) are genuinely a different *shape* of packet and forcing them through the same impl creates more accidental coupling than it removes. **Confirm option (b)**.

**Confirm Must-fix** with the comment-block addition.

### §A.4 AM-4 (pytest packet-ordering invariant) — **DOWNGRADE to Should-have, severity MAJOR → MINOR**

**Verification** (`tests/soak_harness/test_osc_warning_channel.py:187-251`):

Line numbers and assertion structure I read:
- Lines 200-218: drain loop captures packets, filters by `addr != "/sys/binaural_warning"` (continue), parses tag (line 214). Captures `ambivs_disabled_cpu` (line 215) AND `no_sofa_loaded` (line 217) independently — **no ordering assertion between them**. Both must arrive within `PER_EMISSION_LATENCY_BUDGET_MS` (line 198) and a spread budget (line 248) — but ordering is NOT asserted.
- The current test captures `ambivs_disabled_cpu` (probe-CPU-fallback path, NOT `ambivs_demoted_runtime` runtime-demote path).

**Therefore Architect's Scenario F is partly speculative:**
1. The current pytest does NOT depend on packet ordering — it uses presence-only assertion.
2. The current pytest exercises the *standalone* binary (`spe_standalone` per fixture `warning_capture`), NOT the VST3 path where `binaural_diag` would emit. The `ambivs_demoted_runtime` path the plan modifies is in `vst3/SpatialEngineProcessor.cpp:1303-1306`, drained 10 Hz, *not* exercised by this pytest.
3. The new pytest the plan introduces (`test_binaural_diag_emitted_on_demote`) is in a *different* fixture that doesn't share the standalone harness's assumptions.

**However**, the Architect's underlying concern is partially valid in a different form: the **heartbeat drain at `SpatialEngineProcessor.cpp:1293-1350`** has 5 sequential `sendReply()` calls per tick (xfade_truncated, ambivs_demoted, rt_timing, no_sofa, state). All 5 push into the same SPSC outbound ring at `OSCBackend.cpp`. The IO drain thread reads in FIFO order. If `binaural_diag` is inserted **immediately after `ambivs_demoted_runtime`** (per plan §2 Item #3 wording), the wire ordering is *deterministic at the source* — warning packet enqueued first, diag second, drain consumes in order. **There is no real ordering ambiguity for the new test.**

**Confirm severity downgrade to Should-have.** The plan should still document the expected ordering for the new `test_binaural_diag_emitted_on_demote` test, but the existing `test_osc_warning_channel.py` is not at risk and does not need ordering assertions added.

---

## §B. Architect-missed risks — new MAJOR / MINOR findings

### §B.1 MAJOR — Plan's ctest count baseline is FACTUALLY WRONG

**Plan §3 / §4 evidence:**
> §3: "ctest count: v0.6.x **86 → v0.7 92** (+6 NEW)"
> §3 refined: "Default build (relacy OFF): 86 + 1 = **87**"
> §4: "100% tests passed, 0 tests failed out of 87"

**Verification** (`build_off/` and `build_vst3_on/` — actual current builds):
- `cd build_off && ctest -N | grep "Test #" | wc -l` → **115**
- `cd build_vst3_on && ctest -N | grep "Test #" | wc -l` → **115**

The plan's `87` baseline appears to be either (a) a stale memory of a much earlier sprint's count, or (b) counting only a subset (e.g., "tests added in v0.5+"). Neither is correct for a verification target. The v0.6.x commit message (`c304ec2`) and `RELEASE_NOTES_EN.md` reference 85+ but the actual `ctest -N` shows 115. **This is a verification-target failure.** A target of "≥ 87/87" is trivially passed by *deleting tests* — the gate is unenforceable as written.

**Confidence: HIGH.** **Severity: MAJOR.** **Fix:** Re-pin §3 / §4 against actual `ctest -N` output. The deltas in §3 should be expressed as "+N from current baseline = M total", with M sourced from a verifiable command. Possible explanation: planner counted only `core/tests/core_unit/` tests, excluding `vst3/tests/` — if so, that needs to be explicit.

### §B.2 MAJOR — Plan's pytest file paths are WRONG in 4 places

**Plan §2 Item #3 evidence:**
> "`tests/test_osc_warning_channel.py`: extend filter to capture `/sys/binaural_diag`, log to `soak_reports/binaural_diag_YYYYMMDD.jsonl`"
> §3 table: "`test_osc_warning_channel.py` | REVISED (pytest) | #3 D-S3"

**Verification** (`find . -name "test_osc_warning_channel.py"`):
- Actual location: `tests/soak_harness/test_osc_warning_channel.py`.
- The plan reference `tests/test_osc_warning_channel.py` does not exist (verified `ls tests/` returns only directories — `accuracy_harness/`, `compat_harness/`, `dante_loopback/`, `e2e/`, `fixtures/`, `latency_harness/`, `latency_harness_stage1/`, `perceptual/`, `soak_harness/`).

The plan references this path in 4+ places (§2 Item #3 Files block, §3 test deltas table, §7.2 expanded test plan, §2 Item #3 Test block). An executor following the plan literally would fail to find the file.

**Confidence: HIGH.** **Severity: MAJOR.** **Fix:** Replace all 4 occurrences of `tests/test_osc_warning_channel.py` with `tests/soak_harness/test_osc_warning_channel.py`.

### §B.3 MAJOR — Plan over-claims what v0.6.x already shipped (P1-tag fix already closed)

**Plan §2 Item #7 evidence:**
> "Critic §B.5 MAJOR — `P0-X / P1-X / P2-X` tags appear 5+ times across `RELEASE_NOTES_EN.md`, `macos-arm64-verify.md`, `ADR 0016` but `weekly_progress_report_2026-05-18.md:289-304` §5 has 0 P-tag matches"

**Verification** (`grep -n "P0-\|P1-\|P2-\|P3-" docs/weekly_progress_report_2026-05-18.md`):
- Matches found: `P0-1, P0-2, P0-3, P0-4, P1-1..P1-14, P2-1..P2-6` (~30+ explicit P-tags).
- Line 323: `**P1-7 (HIGH-2)** | weekly_progress_report §5 의 P-tag 명시 + cross-reference 정합성 | ✅ closed | 본 리비전.`

**The Critic-v0.6-retro HIGH-2 issue has ALREADY been fixed in `c304ec2`** (commit subject: `MEDIUM-3 ADR §6 mapping`). The weekly_progress_report DID add P-tags in that revision. The plan's §2 Item #7 *(c)* "Add explicit `P[0-3]-N` tags to `docs/weekly_progress_report_2026-05-18.md` §5.1 entries" is **redundant work** — they're already there.

The actual gap is narrower: §2 Item #7 (a) `scripts/audit_ptags.py` is still useful as a regression gate, and §2 Item #7 (b) "Update `RELEASE_NOTES_EN.md:111` 'P1 process gap' to cite the specific §5.1 entry" may still apply (would need verification). But (c)'s premise is wrong.

**Confidence: HIGH.** **Severity: MAJOR (over-claim).** **Fix:** Re-scope Item #7 — keep the audit script (a) and the targeted release-note precision pass (b), drop the "add P-tags to weekly report" rework. Re-verify (b)'s scope against actual `RELEASE_NOTES_EN.md:111` content before locking the spec.

### §B.4 MAJOR — Plan's ADR 0016 §A.3 work is largely already done (overstates scope)

**Plan §2 Item #6 evidence:**
> "Critic §A.3 MAJOR — ADR 0016 has 0 occurrences of `lawyer`, `attorney`, `legal counsel`, or GPL-3 §6 'written offer for 3 years.' MEDIUM-3 partially landed in `c304ec2`."

**Verification** (`grep -n "Appendix\|legal\|lawyer\|attorney\|§6\|written offer\|Limitations" docs/adr/0016-external-distribution-policy.md`):
- Line 233: `## Appendix A — GPL-3 §6 obligation mapping per band`
- Line 247-263: Full §6.a, §6.b, §6.c, §6.d, §6.e recap
- Line 265-269: Per-band Band 0/1/2/3 election table (this is exactly the table the plan §2 Item #6 (a) proposes to add!)
- Line 272-286: "§6.b written offer for 3 years" decision rationale section
- Line 291-297: `## Limitations & legal review status` section, "without legal counsel", "not a substitute for legal review"

**Plan §2 Item #6 (a) — the Appendix A obligation-mapping table — is ALREADY IN THE ADR.** The plan asks to "add" a table that already exists at line 265-269. Plan §2 Item #6 (c) — the Limitations section — also already exists at line 291-297.

**The remaining gap is genuinely narrower:**
- §2 Item #6 (b) `docs/legal/BAND_1_HANDOFF_TEMPLATE.md` standalone file — NEW (not verified to exist; likely genuinely TODO).
- §2 Item #6 (c)'s "trigger events" list — partially present (line 291-297) but worth verifying the wording matches the plan's 3-event list.

**Confidence: HIGH.** **Severity: MAJOR (overstates scope).** **Fix:** Re-scope §2 Item #6 to focus on (b) Band-1 template *only*, plus a precision-pass on (c) trigger events. The Acceptance criterion `grep -ciE 'lawyer|attorney|legal counsel|written offer' ≥ 4` likely *already passes* at HEAD — re-verify and adjust.

### §B.5 MINOR — D-S2 `block_size <= 0` guard already exists; AS-1 is OBSOLETE

**Architect §D AS-1 evidence:**
> "AS-1: Item #2 D-S2: add `if (block_size <= 0 || sample_rate <= 0.f) return;` guard at the top of `recordB2BlockTiming` *before* the new derivation."

**Verification** (`core/src/output_backend/BinauralMonitor.cpp:461`):
```cpp
void BinauralMonitor::recordB2BlockTiming(int block_size,
                                          float sample_rate,
                                          long long elapsed_ns) noexcept
{
    if (block_size <= 0 || sample_rate <= 0.f) return;
    // Already demoted — no further bookkeeping needed (avoid pointless
    // atomic traffic on the audio thread once the decision is sticky).
    if (runtime_demoted_.load(std::memory_order_acquire)) return;
```

The guard is **already present at line 461**, *before* the `runtime_demoted_` early-return. Architect did not read the actual code closely enough — he missed this and listed AS-1 as a binding Should-have.

**Confidence: HIGH.** **Severity: MINOR (documentation only).** **Fix:** Drop AS-1 from the v0.7 work list; the guard is in production. The D-S2 derivation simply needs to be added *after* line 464 (post early-return), and the existing guard at line 461 protects the new derivation from div-by-zero. Note this finding in §D so Architect's AS-1 isn't silently re-introduced.

### §B.6 MAJOR — Relacy GPL/license verification gap

**Plan §2 Item #4 evidence:**
> "third_party/relacy/ (or core/external/relacy/) — vendored header-only relacy 2.5 release (or pin to a specific GitHub commit hash). License (BSD-2-clause) verified in `LICENSE-relacy`."

**Concern (not Architect-raised):** The plan asserts relacy is BSD-2-clause without citing a verification source. Checking the public relacy repository at github.com/dvyukov/relacy (Dmitry Vyukov's relacy) — the license is BSD-2-clause for code, but the *headers* contain copyright notices that are inheritable. The plan's `LICENSE-relacy` file claim needs an actual upstream URL + commit SHA pin in the planner spec. **Bigger concern:** ADR 0016 §A.3 just spent significant ink hardening the GPL legal surface — vendoring a new third-party dev-dep *without* an ADR 0016 update for it is a regression of the Honesty-first surface principle.

The relacy 2.5 release (last upstream activity ~2014) may also have undocumented header dependencies (e.g., on Boost) that pull additional licenses transitively. Architect did not require a license-audit deliverable.

**Confidence: MEDIUM.** **Severity: MAJOR (process; per Principle 1).** **Fix:** Add to §2 Item #4: "License audit — vendored relacy commit SHA, upstream URL, license text included in `LICENSE-relacy`; transitive dep audit (e.g., relacy headers depend on X, X's license is Y). Update ADR 0016 if relacy expands the dev-dep license surface beyond MIT/BSD/Apache." Make this acceptance-blocking.

### §B.7 MINOR — Item #4 relacy ON CI plan claims "promote to required once stable" but signal-only is the v0.7 ship state

**Plan §2 Item #4 evidence:**
> "`continue-on-error: true` for v0.7 (signal-only); promote in v0.8 after 1 cycle of green."

**Pre-mortem cross-check:** Plan §7.1 Scenario B explicitly considers relacy false positives and proposes cppmem as substitute. This is good. But: the "1 cycle of green = promote" criterion is **the same exact criterion used for `cross-platform.yml` Linux ARM64 in v0.6** which proved insufficient for the v0.6 retro Critic to be confident — that's why the v0.7 plan extended to "5-green soak before promotion" (per pre-mortem Scenario C). The relacy CI should adopt the same 5-green standard, not the weaker 1-green that v0.6 used.

**Confidence: HIGH.** **Severity: MINOR.** **Fix:** Pin the relacy CI promotion criterion at 5 consecutive green runs (same as `linux-arm64`), not 1.

### §B.8 MINOR — Plan §0.3 Decision A rejection of A-2 (D-L1 SPSC) is weaker than Architect §B.1 claims

**Plan §0.3 evidence:** "D-L1 cost-benefit (~300 LOC for a benefit gated on 'future high-frequency outbound channel') is not justified by current outbound load."

**Architect §B.1 reinforcement:** "v0.7 explicitly *creates* one such channel (binaural_diag) but its cadence is **event-driven on demote** — not high-frequency."

**Cold-eye check:** This is consensus and seems fine, but the rejection assumes the SPSC need is purely about outbound bandwidth. The actual v0.5.1 multi-producer hotfix at `OSCBackend.cpp:436-492` was driven by **producer-side contention safety** (control + heartbeat + future audio thread), not cadence. If a future feature (say, per-block telemetry instead of per-demote) pushes the audio thread to start producing, the current ring's "drop-on-full" behaviour becomes a *correctness* issue, not just a latency issue. The plan should surface this trigger condition explicitly so v0.8 doesn't have to re-derive it.

**Confidence: MEDIUM.** **Severity: MINOR.** **Fix:** Add to §5 "Out-of-scope" D-L1 row: "Trigger for v0.8 revival = any audio-thread `sendReply` call site is introduced (not just high cadence; any audio-thread producer)."

---

## §C. Test plan adversarial scrutiny

Plan §3 target: ctest ≥ 87 PASS + pytest ≥ 49 PASS.

### §C.1 Uncovered risk — Existing 1-second `test_osc_warning_channel.py` semantics regression

The plan adds `binaural_diag` emission to the same heartbeat drain that `ambivs_demoted_runtime` rides. The existing pytest exercises a *different* path (probe-CPU-fallback → standalone binary `ambivs_disabled_cpu`), but it asserts a **200 ms per-emission latency** (`PER_EMISSION_LATENCY_BUDGET_MS`) and a **spread budget** between two emissions.

**Risk:** if v0.7 introduces ANY new latency between heartbeat ticks (even non-causally), the existing test's 200 ms budget could become tight on slow CI runners. The plan does not benchmark the heartbeat-loop cost with the new `binaural_diag` path added. The plan §7.2 lists a *new* benchmark for `recordB2BlockTiming` (≤50 ns/call median) but no benchmark for the heartbeat drain loop.

**Cover gap:** add to §7.2 expanded test plan: "Existing `test_osc_warning_channel.py` PER_EMISSION_LATENCY_BUDGET_MS regression check — run before/after on the same CI runner, assert deltas are < 10%."

### §C.2 Uncovered risk — D-S1 reset under audio-thread CONCURRENT demote race

Plan §2 Item #1 says the OSC handler runs on the IO thread. Critic §B.1 retro noted (now closed): clearForTest race was theoretical because production code doesn't call clearForTest. **But D-S1 makes that EXACTLY the production code path** — the user-reset handler IS a production path that race-clears against the audio thread's `runtime_demote_strikes_.fetch_add()`.

Specifically: at the moment of reset:
1. Audio thread: about to call `recordB2BlockTiming` over-budget → strikes 7→8 → CAS demote (line 488) → demote latch.
2. IO thread: `resetRuntimeDemoteFromUser` called → reset strikes to 0 → reset `runtime_demoted_` to false → reset warning latch.

If step 1's CAS lands *after* step 2's reset, the demote latch fires after reset and the user sees an instant re-demote (no hysteresis). If step 2 lands inside step 1's strike-add but before the CAS, the strike count is reset but the next over-budget block re-bumps and re-CAS-demotes.

The plan's `b2_runtime_underrun_user_reset` ctest does NOT exercise concurrent audio-thread + IO-thread reset. It's a single-threaded test. The race window is small but non-zero. This is a `test_osc_outbound_relacy`-shaped concern — relacy is *also* a candidate verifier for this state machine.

**Cover gap:** add to §2 Item #1 ctest spec: at least one scenario with audio thread driving strikes near threshold + concurrent IO-thread reset. Use `std::thread` + `std::barrier` to synchronize the race window. Alternatively, extend the Item #4 relacy model to cover the reset state machine.

### §C.3 Uncovered risk — D-S3 max-ratio atomic + D-S1 user-reset interaction

If the user resets while `runtime_demote_max_ratio_at_event_x1000_` holds a snapshot from the previous demote, the next demote's diag packet would emit the *new* snapshot (correct, per D-S3 design). But if the implementer follows AM-1 strictly and clears `runtime_demote_max_ratio_x1000_` to 0 on reset (the in-progress max), there's a 1-strike window where a new over-budget block bumps strikes to 1 but max-ratio is 0 (cleared just before the strike's elapsed/deadline computation). The diag packet at the next demote would understate the actual ratio.

**Cover gap:** the spec should say the in-progress `runtime_demote_max_ratio_x1000_` should be cleared *before* the strike counter is cleared, and the new strike starts from a fresh `runtime_demote_strikes_ == 0` baseline anyway, so the first over-budget block after reset re-establishes the max. Document this explicitly in §2 Item #1's AM-1 fix.

### §C.4 Uncovered risk — Linux ARM64 promotion masks weak-memory race only IF relacy passes

Plan pre-mortem §7.1 Scenario C accepts that the Linux ARM64 promotion gate could surface a real race. The mitigation is "5-green soak before promotion." But: relacy (Item #4) is in v0.7 specifically to catch these races *before* they hit hardware. If relacy is signal-only (continue-on-error), and Linux ARM64 is required, the order of promotion matters:

- If Linux ARM64 promotes FIRST and relacy stays signal-only, a real race on ARM64 blocks main with no relacy signal to triage.
- If relacy promotes FIRST (against the plan's "v0.8" sequencing), Linux ARM64 promotion is meaningfully de-risked.

**Cover gap:** the plan does not specify which of #4 / #5 promotes first if both reach 5-green in v0.7. Recommend: relacy goes required first (one cycle ahead of Linux ARM64), so ARM64 races have a synthetic verifier upstream.

---

## §D. Pre-mortem adequacy

Plan §7.1 has 3 base scenarios (A: D-S3 atomic overhead, B: relacy false positive, C: Linux ARM64 silent race).
Architect §C.4-C.6 added 3 more (D: D-S1 process-lifetime cooldown bypass, E: audit script divergence, F: pytest packet-ordering).

**My evaluation:**
- A: well-modeled, mitigation ordering is BACKWARDS (Architect's AM-2 catches this; I confirm).
- B: well-modeled, cppmem fallback realistic.
- C: well-modeled, rollback procedure missing (Architect's AS-4 catches; I confirm).
- D: well-modeled, doc-only fix.
- E: well-modeled, snapshot-diff is the real signal (Architect AN-1 catches).
- F: I downgrade — the existing pytest doesn't depend on packet ordering, see §A.4 above.

**My additional pre-mortem (not in plan, not in Architect):**

### §D.7 NEW Scenario G — D-S1 user-reset OSC handler accidentally exposes denial-of-service

The new `/sys/binaural_reset_demote ,i 1` inbound verb is reachable from any peer that has completed the v0.5.1 handshake. There is no authentication or per-peer rate-limiting beyond the 60s cooldown atomic itself. A malicious or buggy peer could spam `/sys/binaural_reset_demote ,i 1` packets at the OSC listen port:
- Each call hits `BinauralMonitor::resetRuntimeDemoteFromUser` → loads atomic → computes elapsed → CAS-update of cooldown atomic → emits `/sys/binaural_warning ,s "reset_demote_cooldown_active"` via SPSC ring.
- The outbound ring has 16 slots (`kOutboundRingCap = 16` per `OSCBackend.h:254`). A flood of N>16 rapid rejected resets backs up the ring → other warnings (e.g., legitimate `ambivs_demoted_runtime`) are dropped via the `outbound_drops_` counter.

**Severity:** Medium-Low. Mitigated by ADR 0016 Band 0 internal-lab assumption (no untrusted peers). But the plan should at least document this: any new inbound verb adds an attack surface.

**Mitigation if it fires:** rate-limit the rejection warnings (only emit cooldown_active warning once per cooldown window, suppress duplicates).

**Recommendation:** add to §2 Item #1 — emit `reset_demote_cooldown_active` warning **at most once per cooldown window** (use a `bool reset_cooldown_warning_emitted_` flag, cleared on accept).

### §D.8 NEW Scenario H — Concurrent OSC reset + initialize() race

If the host calls `prepareToPlay` (which calls `initialize()`) WHILE an OSC `/sys/binaural_reset_demote` is in flight on the IO thread, the two reset paths race. `initialize()` runs on the control thread (audio thread typically stopped, per the existing v0.6 D-M1 commit comment). But the OSC IO thread is NOT stopped — `OSCBackend::outboundDrainLoop` keeps running, and inbound packets keep being processed.

The race could leave the cooldown atomic in an unexpected state (`initialize()` does NOT reset it per the plan's explicit design at §2 Item #1).

**Severity:** Low — both paths converge to "demote cleared, cooldown unchanged or fresh". But the design intent is "cooldown survives initialize() but resets on process restart." This intent should be explicitly tested.

**Recommendation:** Add to §2 Item #1 ctest: a scenario where initialize() runs concurrently with OSC reset. Assert no atomic ends in invalid state.

---

## §E. Verdict + conditions

### Verdict: **ITERATE**

Deliberate-mode policy is explicit: "weak pre-mortem" or "weak expanded test plan" forces ITERATE/REJECT. The plan's pre-mortem itself is OK (Architect added 3 scenarios, I add 2 more) — but the plan has **2 MAJOR factual errors** (§B.1 ctest count, §B.2 pytest path) that an executor would hit on day 1, plus **2 MAJOR over-claims** (§B.3 P-tag, §B.4 ADR Appendix A) where the planner described work that's already done. These together push the plan below ACCEPT-WITH-RESERVATIONS.

The plan's *shape* and *intent* are correct. The work itself is on the right track. But ratifying without a precision pass would set autopilot up for a frustrating first hour ("file not found... wait, that's already in the code... wait, the test count was wrong...").

### Conditions for upgrade to ACCEPT (Planner to address before autopilot):

**Must-fix (block autopilot):**

1. **§B.1** — Re-baseline ctest count against actual `ctest -N` output. Re-pin §3 and §4. If the gate is "core_unit only" (sub-115 count), make the build/test invocation explicit (e.g., `cmake --build core/build --target core_unit_tests` + `cd build/core/tests/core_unit && ctest`).
2. **§B.2** — Fix all 4 occurrences of `tests/test_osc_warning_channel.py` → `tests/soak_harness/test_osc_warning_channel.py`.
3. **§B.3** — Re-scope Item #7: drop the P-tag-add-to-weekly-report rework (already closed). Keep audit script (a) + targeted RELEASE_NOTES_EN.md:111 precision (b).
4. **§B.4** — Re-scope Item #6: drop the Appendix A table claim (already in ADR at line 265-269). Keep Band-1 template (b) + Limitations-trigger-events precision pass. Re-verify the `grep ≥ 4` acceptance criterion.
5. **§A.1** AM-1 — Apply Architect's Must-fix #1, EXTENDED with D-S3 max-ratio atomic clearing per §A.1 above.
6. **§A.2** AM-2 — Apply Architect's Must-fix #2, with the read-then-store correction per §A.2 above.
7. **§A.3** AM-3 — Apply Architect's Must-fix #3 (option b) + add the inline comment block referencing v0.7 D-S3 / ADR 0017 §B per §A.3 above.
8. **§B.6** — License audit deliverable for relacy vendoring + ADR 0016 amendment if dep surface expands.

**Should-have (lock in plan):**

9. **§A.4** (AM-4 downgraded) — Document expected ordering for the new `test_binaural_diag_emitted_on_demote` test only; no changes needed to existing pytest.
10. **§B.5** — Drop Architect AS-1 (block_size guard already in code).
11. **§B.7** — Pin relacy CI promotion at 5-green (not 1-green).
12. **§B.8** — Document D-L1 trigger condition (any audio-thread producer, not just high cadence).
13. **§C.1** — Heartbeat drain regression check in expanded test plan.
14. **§C.2** — At least one concurrent reset ctest scenario for Item #1.
15. **§D.7** — Rate-limit `reset_demote_cooldown_active` warning to once per cooldown window.
16. **§D.8** — Concurrent initialize() + OSC reset ctest scenario.

**Architect's AS-2..AS-6** — accept as-is (sound, no overlap with my findings).

**Architect's AN-1..AN-3** — accept as-is.

### Summary of changes Planner needs to make (~16 items):

- 4 spec-precision fixes from §B (ctest count, pytest path, Item #7 over-claim, Item #6 over-claim).
- 3 Must-fix amendments from §A (AM-1 extended, AM-2 corrected, AM-3 with comment block).
- 1 new license-audit deliverable from §B.6.
- 4 Should-have additions from §B.7/§B.8/§C.1/§C.2.
- 3 Should-have additions from §D.7/§D.8 + AM-4 downgrade.
- 1 obsolete item removed (AS-1).

Net effect: plan grows ~25 lines, but several over-claimed items shrink. The actual *code change* scope is essentially unchanged — most fixes are spec-precision and verification-rigor.

---

## §F. Defer to v0.7.x or v0.8 (out-of-scope for v0.7 ratification)

These are items I considered surfacing as findings but assess as not blocking v0.7 ship:

1. **Relacy version 2.5 vs newer fork (e.g., dvyukov/relacy HEAD).** The plan defers this to V07-Q6. Acceptable — vendor at a pinned commit and decide newer-fork question in v0.8.
2. **`/sys/binaural_reset_demote ,i 0` (off) semantics.** Plan only specifies `enable != 0`. What if a host sends `,i 0`? Likely a no-op per the conditional, but undocumented. Defer to v0.7.x patch — minor docs gap.
3. **Macro for `kRuntimeDemoteStrikesSaturationCeiling = 1000`.** Item #8's saturation guard. Defensive code. Plan is fine; the constant value won't matter in practice because the demote latch fires at strikes ≤ 30.
4. **Per-band designated reviewer for ADR 0016 trigger events** (Architect AS-6, V07-Q5). Defer to v0.8 carry-forward open question. Plan §F can name project lead as default reviewer for v0.7.
5. **B-3 (32-block pre-demote window summary).** Already deferred to v0.8 by plan; reasonable.
6. **macOS-14 leg promotion to required.** Defer per plan. Named owner is sufficient v0.7 commitment.
7. **MUSHRA-style subjective evaluation.** Already explicit out-of-scope. Defer.

---

## Ralplan summary row

- **Principle/Option Consistency**: PARTIAL PASS — Plan §0.1 Principles 1 + 4 are well-honored. Principle 5 (cross-platform gating earns its name) is well-honored. Principle 2 (RT-safety regressions are non-negotiable) is honored *only if* the relaxed-store correction (AM-2) lands as Architect specified.
- **Alternatives Depth**: PASS — Decisions A/B/C all retain ≥2 viable options with explicit rejection rationale. Architect §B confirms.
- **Risk/Verification Rigor**: REVISE — 3 base + 3 Architect-added + 2 Critic-added pre-mortem scenarios is adequate. But verification rigor fails the §B.1 / §B.2 baseline-precision check. Re-baseline before autopilot.
- **Deliberate Additions**: PARTIAL PASS — §7.1 pre-mortem present and structured; §7.2 expanded test plan covers unit/integration/e2e/observability layers as required. §7.3 ADR 0017 placeholder is reasonable. Heartbeat drain regression check missing (§C.1); concurrent reset coverage missing (§C.2, §D.8).

---

**Verdict**: **ITERATE** — Planner addresses §A + §B Must-fix items, then re-submits for Critic ratification. Estimated turnaround: 1-2 hours of Planner pass. The plan's core shape is sound; the precision pass is the blocker.

---

## Open Questions (unscored — low confidence or Architect-could-refute)

1. **§B.6 (relacy license audit)** — I assert this as MAJOR but the actual `LICENSE-relacy` file content matters. If Planner ships the file with full upstream URL + commit SHA + transitive dep audit, severity drops to MINOR. Architect may legitimately downgrade if my license-surface concern proves overblown after planner verification.
2. **§D.7 (reset DOS surface)** — Mitigated by ADR 0016 Band 0 assumption. May be over-cautious for v0.7. If autopilot ships without the rate-limit and v0.7.x adds it later, no real-world impact (Band 1 not yet active).
3. **§C.4 (relacy promotes before ARM64)** — Sequencing recommendation; if Architect disagrees with the order, default plan's "both signal-only in v0.7" is fine.
4. **Plan §2 Item #2 `kRuntimeDemoteStrikes = 8` semantics drift after D-S2** — Architect AN-2 catches this. I note it again: with D-S2 making the constant the *floor* rather than the *threshold*, the comment block at `BinauralMonitor.h:243-245` becomes actively misleading. Architect AN-2 (Nice-to-have) is correct; I bump it to a Should-have but not in the Must-fix tier.
