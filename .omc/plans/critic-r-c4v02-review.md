# Critic Review — Phase C4 + v0.2.0 Release Plan (Round 1)

**Reviewer**: Critic (RALPLAN consensus, DELIBERATE mode)
**Date**: 2026-05-10
**Plan**: `.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md` (1098 lines)
**Cross-ref**: Architect review at `.omc/plans/architect-r-c4v02-review.md` (ITERATE, 7 blockers)
**Verdict**: **ITERATE** — Concur with Architect on 6/7; downgrade #1; add 5 critic-only blockers; merged total 11.
**Mode**: Started THOROUGH; escalated to ADVERSARIAL after critic-blocker C-1 (state-format ABI lie) surfaced.

---

## 0. Pre-commitment predictions (made before reading)

1. Sidecar architecture would be over-recommended on cert-projection rather than evidence (HIT — A1-δ steelmanned).
2. Semver classification of `kBypass` 7th param (acb8c27) glossed over (HIT — plan calls v0.2 "non-breaking" but state v1→v2 already shipped).
3. The "v0.2 release notes" Korean-manual coupling slips past gate-check (HIT — `docs/manual_kr/install/README.md:3` still says "버전: v0.1.0").
4. Pre-mortem leans on cert-risk vs real-world failure (HIT — Architect raised OOM-killed sidecar; Critic adds GLIBC mismatch).
5. ADR numbering collision between two `0006-*.md` files unaddressed (HIT).

5/5 hit → activated ADVERSARIAL skepticism.

---

## 1. Architect-Critique calibration (per-blocker)

| # | Architect blocker | Critic verdict | Severity match? | Notes |
| - | ----------------- | -------------- | ---------------- | ----- |
| 1 | ADR 0007 must carve UNIX socket out of ADR 0003 OSC-over-UDP scope | **REAL but OVERSTATED** | Severity OK at MAJOR | ADR 0003 §Decision says "v0 uses OSC over UDP loopback" and v1 migration target is "shm + UDS". Single-paragraph fix, not a ratification gate. Downgrade to "single sentence in ADR 0007 §Context". |
| 2 | ADR 0008 must specify locking + GC + EPIPE | **REAL** | Severity match | Concur fully. Add: include `schema_version` field in `instances.json`. |
| 3 | Defer A2.1-β / state v3 to v0.3 | **REAL** | Severity match — highest-leverage fix | Plan is internally inconsistent: §A2.1 line 217 recommends v3, §8 line 807 adopts A2.1-β, §B1-β line 457 says v0.2 = C3-only. v3 schema in v0.2 with no consumer = pure churn. C2B-Q2 already pre-resolved this favorably. |
| 4 | Document thread topology in plugin process | **REAL** | Severity match | Add: clarify SPSC ring per-instance vs shared. With N instances: 2N threads. Some hosts (Pro Tools) cap thread count. Recommend N≤8. |
| 5 | Sidecar binary as B3-β artifact | **REAL** | Severity could downgrade if A1-δ defers to v0.3 | If §6 synthesis (defer A1-δ to v0.3) accepted, dissolves for v0.2. |
| 6 | v0.1.1 rollback procedure when Pre-mortem 3 fires | **REAL** | Severity match | Plan's Pre-mortem 3 line 740-742 says "cut v0.1.1 instead of v0.2.0 with C3 + manual + HOA decoder ONLY" — but C3 contains 4 new CommandTags + new `--osc-dialect` flag. **Not** a patch-bump. Plan acknowledges contradiction at line 463 ("Why not γ: C3 is not a patch") then re-proposes γ in Pre-mortem 3. **Direct internal contradiction.** |
| 7 | Falsifiable success criteria for each of 7/8 params in R3 | **REAL** | Severity match | Architect's table is good. Add: `test_vst3_bypass` already exists; R3 references it for one-step audit trail. |

**Score**: Concur 6/7 (Architect overstates blocker #1 by one severity step).

### Blockers Architect missed
- C-1 state-format-ABI semver lie
- C-2 OFF-baseline filename mismatch
- C-3 ADR-0006 numbering collision
- C-4 Korean manual stale "v0.1.0"
- C-5 autopilot-readiness gap

---

## 2. Quality criteria enforcement

| Item | Score | One-line rationale |
| ---- | ----- | ------------------ |
| Principle/Option consistency | **WEAK** | Principles 1, 2, 3 honored. Principle 5 ("no v0.1.0 ABI break") is **violated**: state v1→v2 shipped post-v0.1.0 at acb8c27. See C-1. |
| Fair alternatives (A1-α / A1-ε steelman) | **FAIL** | A1-α dismissed via projection ("Pro Tools / Logic forbid bind"). Linux-only B3-β makes cert-risk argument moot for v0.2. Architect §1 steelman correct. |
| Risk mitigation clarity | **WEAK** | Risk table 8 rows with concrete mitigations — good. But "Architect rejects A1-δ" mitigation = "Backstop is B1-β v0.2 ships C3-only; sidecar revisited Phase D" — exactly the deferral Architect now recommends, yet plan presents it as fallback rather than primary. |
| Testable acceptance criteria (≥90% runnable) | **PASS** | Appendix A indexes A.1..A.12 + B.1..B.8 (~85% testable). |
| Concrete verification steps | **WEAK** | R3 (line 547-571) does NOT cite measurable threshold for "automatable" — Architect blocker #7. R6 testable; R4 (line 591) "both files render" needs `markdown-link-check` command. |

---

## 3. Scope creep check

| Item | Verdict | Rationale |
| ---- | ------- | --------- |
| New ADRs 0007 / 0008 / 0009 | **In scope but numbering collision** | Existing `docs/adr/0006-adm-osc-v1-spec-freeze.md` AND `docs/adr/0006-algorithm-runtime-swap.md` both exist. Plan ignores. New ADRs should start at 0010. |
| macOS / Windows port mentions | **Borderline scope creep** | A1-δ justified at line 256 (A5-β) by "single code path across Linux + macOS + Windows" — yet B3-β (line 485) is Linux-only. Future-platform commitment used as justification without commitment on record. |
| Korean docs reference | **In scope but incomplete** | S5 covers manual Ch.5 (conditional). R4 covers release notes. **Missing**: `docs/manual_kr/install/README.md:3` literally says "버전: v0.1.0" today; `:397` shows "schema_version=1". User-visible version strings wrong on v0.2 install. R2 silent. |

---

## 4. Deferral consistency

C3 plan §S4 promised "Phase C4 issue: VST3 plugin ADM-OSC strategy" with C-α/C-γ trade-off (`spatial-engine-phaseC3-adm-osc-compat.md:99`).

- **YES** at ADR level (S1 drafts 0007 + 0008).
- **PARTIALLY** at ship level — under Architect §6 synthesis, v0.2 ships only ADRs.
- **The user does NOT get a clear "v0.2 vs v0.3 deliverable matrix"** — §4.1 says "C4 lands in v0.3" but Track A's S1..S8 timeline at §9 line 887-895 implies all happen in this sprint.

**Critic position**: Plan must include explicit "v0.2 ships X / v0.3 ships Y / v1.0 ships Z" deliverable table in §4 or §1.4.

---

## 5. Semver / changelog standards

### v0.1.0 → v0.2 classification audit

| Change since v0.1.0 | Type | Plan addresses? |
| -------------------- | ---- | --------------- |
| 4 new HOA decoders | MINOR | Yes |
| 4 new ADM-OSC CommandTags (0x06–0x09) | MINOR | Yes |
| `--osc-dialect` CLI flag | MINOR | Yes |
| **`kBypass` 7th VST3 param + state v1→v2** | MINOR with state-file-format break | **NO — line 96 calls preserved** |
| OFF byte-baseline gate | CI-only | Yes |
| Korean manual | Doc-only | Yes |

**C-1**: Plan §1.1 Principle 5 (line 94-99) states *"VST3 state v2 format (36 bytes, 'SPE1' magic, 7 floats), Component / Controller IIDs ... remain unchanged."* Verified WRONG: v0.1.0 shipped state v1 (28 bytes, 6 floats). State v2 added in C2B postmortem (`acb8c27`, 2026-05-09 — after v0.1.0). Plan's principle statement misstates v0.1.0 invariant.

**Functional safety**: multi-version reader exists at `vst3/SpatialEngineProcessor.cpp:267-289` — v0.1.0 .vstpreset files load via v1 reader path. So functionally non-blocker. But wording-vs-reality drift fails ABI audits.

**Fix**: Plan §1.1 Principle 5 must read:
> "v0.2 preserves the v0.1.0 wire ABI for `--osc-port`, `--osc-dialect`, Component/Controller IIDs, and CLI surface. State format bumped v1→v2 in C2B postmortem (acb8c27) with multi-version reader at `Processor.cpp:267-289`; no further state bump in v0.2.0. v0.1.0 `.vstpreset` files load cleanly via the v1 reader path."

### `kBypass` 7th param semver
C2B-Q1, C2B-Q2 (`open-questions.md:50, :51`) discuss bypass + state-version reader but do NOT classify as v0.1.x compat add or v0.2 minor bump trigger. Plan ignores.

### Release notes format
R1 line 518 says "keep-a-changelog format" but R1 produces `CHANGELOG.md`, not release notes. R4 §3 lists 6 sections (변경 요약 / 신규 기능 / 호환성 / 알려진 제약 / 설치 / 다음) — freeform. **Inconsistency**.

### Tag operation
R6 (line 604-617) gated on user approval per `.claude/CLAUDE.md` "v* 태그는 사용자 명시 요청 시만". OK. But GHA workflow `tags: [v*]` triggered → manual user tag → automated release upload. Plan should say this explicitly.

---

## 6. Pre-mortem adequacy

The 3 scenarios:
1. Pro Tools / Logic / Ableton sandbox blocks `bind()` (line 667-693)
2. Console vendor reports ADM-OSC v1.0 incompat (line 695-716)
3. DAW hands-on gate (R3) fails: Reaper rejects 7-param state (line 718-745)

- **Sufficiently distinct?** Yes — cert / wire-spec / host-format = three layers.
- **Recovery paths?** Yes for all three.
- **Highest-impact failure modes?** Partial — missing:
  - **Architect's Scenario 4** (sidecar OOM-killed mid-session) — concur strongly.
  - **Critic Scenario 5: GLIBC mismatch on customer machine.** B3-β ships prebuilt `.so` built on ubuntu-24.04 (GLIBC 2.39). Korean live-venue mixing PCs frequently run ubuntu-22.04 (GLIBC 2.35) or Debian 11 (GLIBC 2.31). User downloads .so → Reaper plugin scan reports "incompatible binary". HIGH probability, HIGH severity (first-customer first-impression). Plan §B3 line 482-487 lists "single platform" as CON without acknowledging GLIBC subset.

---

## 7. Process gates (autopilot readiness)

Per `.claude/CLAUDE.md` "ralplan→autopilot 필수, 세션 중단 시 플랜 기반 재개":

**DONE = X conditions**: Most steps OK. But:
- §S2 line 285: "Only required if A1-δ chosen" — autopilot cannot self-judge.
- §A2.1 recommends A2.1-β; §8 adopts; §B1-β makes A2.1-β v0.3 work. Autopilot does not know which sprint.
- §B1 line 457 = β; §B2 line 478 pairs with β; §S5 line 363 conditional on B1-α (manual Ch.5) — but plan also lists S5 as 0.5d in §9 ETA line 891 unconditionally.

**Build/test commands spelled out?** Mostly. R2: `cmake --build` + `--version` — yes. R7: `cmake -DSPATIAL_ENGINE_NO_JUCE=ON .. && make && ctest` — yes. **A.4 acceptance: WRONG FILENAME** (see C-2).

**Critic position**: WEAK. Plan needs:
1. §1.5 "Decision freeze" listing A1, A2, A2.1, A3, A4, A5, B1, B2, B3, B4, B5 final picks.
2. Per-step "blocked on / unblocks" graph for autopilot ordering.
3. Concrete file-path references (not wrong `.ci/off-baseline-pins.txt`).

---

## 8. Critic-only blockers

### C-1 (CRITICAL) — Principle 5 misstates v0.1.0 ABI invariant

**Evidence**: Plan §1.1 line 94-99 reads "VST3 state v2 format (36 bytes, 'SPE1' magic, 7 floats)... remain unchanged." Verified at `vst3/SpatialEngineProcessor.cpp:267-289` — state v2 added in C2B postmortem (`acb8c27`), AFTER v0.1.0. v0.1.0 shipped state v1 (28 bytes, 6 floats, no kBypass).

**Impact**: Future plans propagate the lie. Release notes claiming "no breaking change since v0.1.0" technically false (state-format break is reader-mitigated, not absent).

**Fix**: rewrite Principle 5 (see §5 above).

### C-2 (MAJOR) — OFF baseline filename wrong throughout plan

**Evidence**: Plan cites `.ci/off-baseline-pins.txt` at lines 87, 919, 933 (Principles, A.4 acceptance, B.3 acceptance). Actual filenames: `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` (verified). CI workflow at `.github/workflows/vst3.yml:111` uses correct names.

**Impact**: Autopilot will execute "diff against `.ci/off-baseline-pins.txt`" and fail. Acceptance gates A.4 + B.3 unrunnable as written.

**Fix**: Replace all `.ci/off-baseline-pins.txt` with the actual dual-gate filenames.

**Mitigated by**: CI uses correct filename, so first-run failure is detected immediately; downgrade CRITICAL→MAJOR.

### C-3 (MAJOR) — ADR 0006 numbering collision

**Evidence**: `ls docs/adr/`:
- `0006-adm-osc-v1-spec-freeze.md` (latest)
- `0006-algorithm-runtime-swap.md` (older)

Plan adds 0007/0008/0009 without resolving.

**Impact**: "see ADR 0006" — which? Tooling break.

**Fix**: New ADRs start at 0010 OR renumber `0006-algorithm-runtime-swap.md` → `0006a` separately. Recommend latter.

### C-4 (MAJOR) — Korean manual still says "버전: v0.1.0"

**Evidence**:
- `docs/manual_kr/install/README.md:3` — "**버전:** v0.1.0"
- `docs/manual_kr/install/README.md:397` — "spatial_engine_core v0.1.0 (schema_version=1)"
- `docs/manual_kr/operation/README.md:3` — "**버전:** v0.1.0"

R2 (version bump, line 528-545) handles `CMakeLists.txt` and `core/CMakeLists.txt` but does not list manual files. R2 says `grep -rn '0\.1\.0' --include='*.md' ... update as appropriate` — ambiguous.

**Impact**: First-customer (Korean live-venue) opens manual → sees "v0.1.0" → confusion / brand impact.

**Fix**: R2 acceptance must explicitly list:
- `docs/manual_kr/install/README.md:3` AND `:397`
- `docs/manual_kr/operation/README.md:3`
- Update schema_version=1 → 2 in install README:397 (verify against `core/src/ipc/ProtocolVersion.cpp` runtime print).

### C-5 (MAJOR) — Plan is not autopilot-ready

**Evidence**: 4+ unresolved decision forks:
- §S2 line 285: "Only required if A1-δ chosen"
- §S5 line 363: Manual Ch.5 conditional on B1-α
- §A2.1 + §B1-β interaction
- §S7 line 401-410: "if any C4 work *did* leak into core/"
- §6 risk row "Architect rejects A1-δ"

**Impact**: `/oh-my-claudecode:autopilot` does not make consensus-pending architectural decisions; it executes frozen plans. Either pauses (violates autonomy policy) or guesses.

**Fix**: Round-2 patch must add §1.5 "Final decision freeze" — one chosen value per A1..A5, A2.1, B1..B5, no "primary/escape" pairs post-Round-2.

---

## 9. Self-Audit (Phase 4.5)

| Finding | Confidence | Refutable? | Flaw / Preference |
| ------- | ---------- | ---------- | ----------------- |
| C-1 (state-format ABI lie) | HIGH | NO | FLAW |
| C-2 (OFF filename wrong) | HIGH | NO | FLAW |
| C-3 (ADR 0006 collision) | HIGH | NO | FLAW |
| C-4 (Korean manual stale) | HIGH | NO | FLAW |
| C-5 (autopilot-readiness) | HIGH | could refute if "autopilot can branch on A1 verdict" | FLAW (project policy = frozen plan) |
| Concur Architect #1 (ADR 0003 carve-out) | MEDIUM | YES — author could argue | FLAW but downgraded |
| Concur Architect #2-7 | HIGH | NO | FLAW |
| Pre-mortem Scenario 5 (GLIBC) | HIGH | NO | FLAW |

---

## 10. Realist Check (Phase 4.75)

- **C-1**: future plans propagate the principle, v0.3 author claims "never broken state ABI", second break thinking safe. Severity stays CRITICAL.
- **C-2**: autopilot fails diff at R2. Easy rollback. CI uses correct filename → caught at first run, not silent. **MAJOR**.
- **C-3**: reader confusion, no build break. **MAJOR**.
- **C-4**: user opens manual on v0.2 install, sees v0.1.0. Easy fix, fast detection, brand-impact. **MAJOR**.
- **C-5**: autopilot runs S2..S6 sidecar code into v0.2. Detection same day. ralplan loop won't exit ITERATE without Round-2 patch. **MAJOR**.

Only C-1 remains CRITICAL post-recalibration.

---

## 11. Verdict — ITERATE

### Final consolidated revision list (Architect + Critic merged, deduplicated, prioritized)

1. **[C-1, CRITICAL]** Rewrite §1.1 Principle 5 to state v1→v2 break-with-mitigation truthfully.
2. **[C-5, MAJOR]** Add §1.5 "Final decision freeze" — one value per A1..A5, A2.1, B1..B5 axis, no primary/escape pairs.
3. **[Architect #3 + plan §A2.1 internal contradiction, MAJOR]** Defer A2.1-β / state v3 to v0.3. Update §A2.1, §8, §6 risk row, Appendix A. Cite C2B-Q2 as supporting.
4. **[Architect #2, MAJOR]** ADR 0008: add file-locking (atomic rename), reader-tolerance for stale PIDs, EPIPE handling, schema_version field.
5. **[Architect §6 synthesis, MAJOR]** Choose: (a) defer A1-δ S2..S8 to v0.3, S1 (ADRs only) in v0.2; OR (b) explicit "Linux v0.2 + cross-platform v0.3" with cert evidence cited. Plan currently picks neither cleanly.
6. **[Architect #6, MAJOR]** Resolve Pre-mortem 3 ↔ §B1-β contradiction.
7. **[Architect #1 + carve-out, MAJOR]** ADR 0007 §Context: one paragraph clarifying UDS plugin↔sidecar control channel orthogonal to ADR 0003.
8. **[C-2, MAJOR]** Replace `.ci/off-baseline-pins.txt` with `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` (Principle 3, A.4, B.3).
9. **[C-4, MAJOR]** R2 acceptance must explicitly list manual file:line targets.
10. **[C-3, MAJOR]** Resolve ADR 0006 numbering: new ADRs start at 0010 OR renumber `0006-algorithm-runtime-swap.md`.
11. **[Architect #4, #5, #7 + Critic Pre-mortem 5]** Thread topology contract; sidecar artifact decision; falsifiable param tests; GLIBC fallback in release notes.
12. **[MINOR]** Release notes (R4) format align with Keep-a-Changelog.
13. **[MINOR]** Document tag-vs-GHA authority split.

### Cannot APPROVE
- Two CRITICAL/HIGH severity findings (C-1 + Architect #3).
- Plan not autopilot-ready (C-5).
- Multiple MAJOR file:line citations wrong (C-2, C-4).

### Cannot REJECT
- Track B structure sound (B1-β + B2-β correct).
- §0 inventory accurate.
- Pre-mortem effort real.
- Acceptance index well-structured.

### Recommendation
ITERATE with Round-2 patch addressing 13-item merged list. Estimated effort: 1.5–2d plan-edit. After Round-2, re-submit to Architect (lighter pass) + Critic (verification of 13 items closed). Then APPROVE → autopilot.
