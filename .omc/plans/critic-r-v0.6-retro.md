# v0.6 RT-safety Hardening — Critic Retroactive Review

**Date**: 2026-05-18
**Reviewer**: Critic (retroactive, post-commit)
**Verdict (preview, full at end)**: **ACCEPT-WITH-RESERVATIONS** — 본 사이클은 5 개 risk 중 4 개를 부분/완전 해결하고 새 안전망(P1-4 kill-switch, P1-3 통합 ctest)을 추가했으나, (a) `ipc_schema.md` 스테일 (b) Realist 검증 안 된 ADR 0016 의 GPL §6 의무 (c) 사용되지도 않는 P-tag 우선순위 체계 (d) ARM 회귀 게이트 0 — 4 개의 잔존/신규 리스크를 v0.6.x 사이클 안에 처리해야 한다.

**Mode escalation**: 본 review 는 Phase 2 verification 단계에서 1 개 MAJOR 발견 (`ipc_schema.md` 스테일) + 2 개 다른 MAJOR (Realist 검증 안 된 ADR Band 1, P-tag 정합성 깨짐) 이 surface 되어 Phase 3 부터 ADVERSARIAL 모드로 escalate. 인접 영역 (manual_kr cross-reference, USER_GUIDE.md, /sys/state legacy 표기) 까지 확장 점검 수행.

---

## A. 본 사이클이 정말 해결했는가, 시한폭탄을 남겼는가

### A.1 — Risk #1 (post-hoc plan = self-approve 정책 위반)

**상태**: **부분 해결, 근본 해결 불가.**

`.omc/plans/spatial-engine-v0.6-stability.md:§5` 가 "Why post-hoc plan (process note)" 섹션을 *별도로* 두고 self-disclose 한 점, `docs/release/v0.6.0/RELEASE_NOTES_EN.md:102-114` 가 `## Process note — post-hoc plan cadence` 를 *공개 release note* 에 정직하게 surface 한 점, `CHANGELOG.md:77-81` 의 `### Plan` 섹션이 "The post-hoc cadence is itself a process gap and is itemised as a P1 in ..." 라고 명시한 점은 honesty-first 정책에 부합한다. 이전 critic 의 1번 발견에 대해 *제도적으로 처리* 한 것은 인정한다.

그러나 retroactive ralplan 이라는 본 review (Architect + Critic) 자체가 또 다른 형태의 self-approve 다. ralplan 의 의미론은 "사전 합의 → 작업 진행" 이며, "사후 합의 → 사후 ratification" 은 동일한 4-단계 파이프라인을 통과한 것처럼 보이지만 *gating* 기능을 수행하지 않는다. 본 review 가 발견한 MAJOR finding 을 commit 전에 막아낼 수 없다 — 이미 `ece6cba` 와 `bcb2fed` 가 main 에 들어가 있다.

**Confidence**: HIGH. **Severity**: MAJOR (process; 재발 시 cumulative).

**Realist check**: 단발 발생이고, 의도가 정직 disclosure 인 점, 동일 작업 세션 안의 interrupt 가 원인인 점을 고려해 — MAJOR 유지하되 *재발 방지* 가 후속 의무다. 만약 v0.6.x 가 또 post-hoc 으로 commit 되면 다음 critic 은 REJECT.

### A.2 — Risk #2 (steady_clock 영구 오버헤드 → P1-4 kill-switch)

**상태**: **해결.** 다만 Cold-eye 검증 결과 net win 의 크기는 광고된 만큼 크지 않다.

`SpatialEngine.cpp:723-741` 의 kill-switch 는 정확히 구현됐다. 다만 **net loss in non-demoted state** (atomic acquire-load 가 추가됐는데 steady_clock 도 어차피 실행됨) — 의미론 정리 측면에서 정당하지만 commit message 의 *"steady-state cost on a demoted host returns to zero — relevant on macOS"* 주장이 *demoted 호스트* 라는 전제가 99.9% 시간 false 라는 점은 약간의 honesty 결함.

**Confidence**: HIGH. **Severity**: MINOR.

### A.3 — Risk #3 (JUCE GPL viral trigger → ADR 0016)

**상태**: **부분 해결.** ADR 자체는 well-structured 지만 *법률 review 없이 작성된* policy 라는 점이 disclose 안 됐다.

- 증거: ADR 에 `lawyer`, `attorney`, `legal counsel` 단어 0 회.
- 증거: GPL-3 §6 의 *"written offer for 3 years"* 조항이 ADR 어디에도 언급 안 됨.
- 증거: `Negative` 섹션 (`docs/adr/0016-external-distribution-policy.md:175-181`) 가 "written acknowledgement asks them not to during v0.x but is not legally enforceable" 라고 정직 disclose. 한계 *인식* 은 있으나 *법률 review 부재* 자체는 surface 안 됨.

**Implication**: ADR 0016 은 *internal lab policy* 로는 충분하나 **외부 GPL 컴플라이언스 audit 에서 방어 가능한 문서가 아니다**.

**Confidence**: HIGH (절차) / MEDIUM (법적 분석).
**Severity**: MAJOR. **Realist check**: 잠재적 risk. Band 1 handoff 아직 0 — 첫 handoff 전 mitigate 가능.

### A.4 — Risk #4 (통합 ctest 부재 → P1-3)

**상태**: **부분 해결.** 본 세션의 `test_b2_runtime_underrun_engine_integration.cpp` 가 *진정한 integration* (실제 wall-clock B2 가 demote 까지 가는 경로) 는 *cover 안 됨* 을 self-disclose.

**진정한 integration test 가 검증해야 할 것 vs 실제로 검증하는 것**:

| 검증 대상 | 본 ctest 가 검증? |
| --- | --- |
| Forwarder symbol existence | ✓ |
| Forwarder dispatch | ✓ |
| 실제 audioBlock() 가 AmbiVS dispatch lambda 진입 | ✗ (probe 가 클램프하면 entry 안 됨) |
| 실제 audio-thread weak-memory-order 동시성 | ✗ |
| 실제 measure-then-record race | ✗ |
| AmbiVS *및* Direct 분기 dispatch 변경 시점 | ✗ |
| sendReply integration via heartbeat | ✗ |

**제안 개선**: rename 또는 한 줄 disclaimer.

**Confidence**: HIGH. **Severity**: MINOR. **Mitigated by**: rt_alloc_probe, soak_vst3_console_flood, test_writebinaural_no_sofa_muted 가 인접 coverage 제공.

### A.5 — Risk #5 (ARM 회귀 게이트 0)

**상태**: **미해결, honesty 는 acceptable.**

`docs/release/v0.6.0/macos-arm64-verify.md:209-214` `§H.검증 못 함 (deferred)` + `RELEASE_NOTES_EN.md:65-72` 의 `#9` 섹션 모두 정직 disclose.

다만 `RELEASE_NOTES_EN.md:92-100` `## Release validation` 섹션이 ARM/macOS 검증 PENDING 을 *prominent* 하게 surface 안 함 — `#9` 절 안에 묻혀있다.

**Confidence**: HIGH. **Severity**: MINOR.

---

## B. 본 사이클이 새로 만든 risks

### B.1 — P1-4 kill-switch 부작용: clear ↔ isRuntimeDemoted race

**판단**: 본 패치가 race 를 introduce 하지 *않음*. `clearForTest` 는 production code path 아님. `prepareToPlay` 의 reset 은 audio thread 정지 상태로 실행. 안전.

### B.2 — P1-3 ctest 의 effective_mode 모호성

§A.4 에서 다룸.

### B.3 — `ipc_schema.md` 스테일

**상태**: **MAJOR finding — 본 review 의 핵심 발견 중 하나.**

- 증거: `docs/ipc_schema.md:1-8` schema_version=1 박제.
- 증거: `docs/ipc_schema.md:104-181` `## State Broadcast Table` 가 v0.5.1 의 `/sys/binaural_status`, `/sys/binaural_warning`, `/sys/state ,s "fallback_mode=..."` 0 회 언급.
- 증거: `RELEASE_NOTES_EN.md:88` *"Public OSC schema unchanged"* — 본 주장은 narrow scope 에서 TRUE 지만 *canonical schema 문서 자체에 binaural_warning 이 한 번도 등재된 적 없음* 을 surface 안 함.
- 증거: `docs/manual_kr/operation/README.md:1121` / `CH6_WEBGUI.md:424` / `docs/architecture.md:39` — 3 곳에서 ipc_schema.md 를 *canonical* 으로 cite. 사용자가 본 문서를 열면 v0.5.1+ outbound 채널을 찾을 수 없음.

**Confidence**: HIGH. **Severity**: MAJOR. **Realist check**: 현재 Band 0 internal 상태에서 잠재적. 외부 베타 첫 handoff 전 fix 필수.

### B.4 — Release notes / weekly report 의 self-claim 에 critic 의견 반영 안 됨

본 review 자체가 commit 되기 전이라 (post-hoc) — 차후 v0.6.x patch 에서 surface 해야 한다.

**Confidence**: HIGH. **Severity**: MINOR.

### B.5 — 새로 발견: P-tag 우선순위 체계 정의 부재 / cross-reference 깨짐

`RELEASE_NOTES_EN.md:111` *"P1 process gap"*, `macos-arm64-verify.md:212` *"P2 (GHA arm64 matrix)"*, `0016-external-distribution-policy.md:6` *"P0-2"* — P-tag 5+ 회 cross-reference.

그러나 `weekly_progress_report_2026-05-18.md:289-304` `## §5` 본문은 P-tag 없음.

- 증거: `grep -n "P0-\|P1-\|P2-\|P3-" docs/weekly_progress_report_2026-05-18.md` — 매치 0.

**Confidence**: HIGH. **Severity**: MAJOR.

---

## C. CHANGELOG / release notes / ADR / 매뉴얼 의 honesty 측면

### C.1 — CHANGELOG.md 의 "post-hoc plan cadence" self-disclosure

**합격.** 다만 *P1 in §5* cross-reference 가 깨짐 (§B.5).

### C.2 — ADR 0016 의 Band 1 5-tester cap 정당화

**합격, 그러나 §A.3 의 법률 review 부재 한계가 동시에 존재.**

### C.3 — 한국어 CH7 binaural manual

`CH7_BINAURAL.md:218-221` macOS CoreAudio 미구현 정직 disclose. `CH7_BINAURAL.md:275-285` `## 7.8 다음 단계 / 한계` 5 가지 deferred 항목 정직 listing. **합격.**

### C.4 — v0.6.0 release notes 의 release validation 섹션

§A.5 / §B.4. **합격 imperfect.**

---

## D. 다음 사이클이 무시하면 위험한 항목 (우선순위 매김)

### HIGH — 다음 commit 안에 처리

#### HIGH-1: `ipc_schema.md` 업데이트 (B.3)

1. `docs/ipc_schema.md` 에 `### Outbound — Binaural Telemetry (v0.5.1+)` 절 신설.
2. `/sys/binaural_status ,i <failures>`, `/sys/binaural_warning ,s <code>` (전 3 코드), `/sys/state ,s "fallback_mode=..."` 명시.
3. v0 의 `/sys/state ,i` (bitmask) 와 v0.5.1 의 `/sys/state ,s "fallback_mode=..."` 의 dual-tag conflict 명시 (additive vs replacing).

#### HIGH-2: P-tag cross-reference 정합성 fix (B.5)

1. `weekly_progress_report_2026-05-18.md:289-304` `§5.1` 의 5 개 단기 항목에 명시적 `P0-X / P1-X / P2-X` 태깅 추가.
2. ADR 0016 의 `P0-2` 가 어떤 §5.1 항목인지 확인.
3. `RELEASE_NOTES_EN.md:111` 의 "P1 process gap" 이 §5.1 의 어떤 항목인지 명시.

### MEDIUM — v0.6.x patch 내

#### MEDIUM-1: P1-3 integration ctest rename / 한 줄 disclaimer 추가 (A.4)

#### MEDIUM-2: release notes / changelog 의 "Release validation" 섹션 보강 (A.5, B.4)

ARM/macOS/DAW host 0 검증 명시 (현재는 #9 절 안에 묻힘).

#### MEDIUM-3: ADR 0016 의 GPL §6 explicit 표 (A.3)

`### Appendix A: GPL-3 §6 obligation mapping per band` 추가 + `### Limitations & legal review status` 추가.

#### MEDIUM-4: weekly_progress_report 의 §6.1 "동일 파이프라인" 표현 수정 (A.1)

### LOW — v0.7 deferred 가능

#### LOW-1: ARM CI matrix gate (A.5)

GHA `vst3.yml` 에 `macos-14` job 추가. ARM-specific stress test.

#### LOW-2: `runtime_demote_strikes_` saturate 처리

`BinauralMonitor.cpp:446-448` 의 fetch_add 에 saturate guard 추가.

---

## E. 다중 관점 노트

### E.1 — Executor 관점

§3 의 test delta table well-structured. §5 의 "Why post-hoc plan" honesty disclose 가 reference signal.

### E.2 — Stakeholder 관점

문제 명시적 해결됨. *plan target* + *user-visible impact* 모두 measurable. Vanity metric (ctest 85/85) 만으로는 부족 — §A.5 / §B.4 / §D MEDIUM-2 에서 처리.

### E.3 — Skeptic 관점

본 작업 실패 시나리오 (= future regression prevention) 는 *intentional*. Falsifier = "다음 6 개월 이내에 RT-safety regression 0 건". 현재로서는 falsifiable.

---

## Verdict Justification

THOROUGH → Phase 2 의 `ipc_schema.md` 스테일 발견 + Phase 3 의 P-tag 깨짐 + Phase 4 의 ADR 0016 법률 review 부재 발견으로 **ADVERSARIAL** escalate. 인접 영역 확장 점검 결과 추가 발견 없음 — 본 사이클의 issue 는 *cluster* 가 아닌 *isolated*.

Realist Check:
- 1 개 CRITICAL → 발견 없음.
- §B.3 (ipc_schema) MAJOR 유지. Mitigated by: 현재 Band 0.
- §B.5 (P-tag) MAJOR 유지. Mitigated by: text-level fix.
- §A.3 (ADR 0016 법률) MAJOR 유지. Mitigated by: audit-log discipline.

**최종 verdict**: **ACCEPT-WITH-RESERVATIONS**.

근거:
- 본 사이클의 *core RT-safety work* 는 검증됨.
- *documentation surface* 에서 3 MAJOR + 1 MINOR — 모두 v0.6.x patch 안 fix 가능.
- 이전 critic 의 5 risk 중 4 개 부분/완전 해결, 1 개 (ARM) 미해결 + honest disclose.
- self-approve risk 재발 시 누적 → 다음 cycle 의 stress point.

**Upgrade 조건** (ACCEPT 로 가려면):
- HIGH-1 + HIGH-2 두 항목이 v0.6.1 안 처리.
- MEDIUM-2 (release validation 보강) 최소 처리.

---

## Open Questions (unscored)

1. `/sys/state` 의 dual-tag 충돌 — additive vs replacing.
2. `out_cv_.notify_one()` 의 RT 안전성 — 미래 누군가가 audio thread 에서 sendReply 부르면 #4 의 hard-wall 재붕괴. static enforcement 검토.
3. `runtime_demote_strikes_` saturation — defense in depth.
4. ADR 0016 의 Band 1 5-cap actual workflow size 와의 align.
5. `ipc_schema.md` schema_version=1 박제 — fallback_mode dual-tag 가 backward-compat 인지 implicit breaking 인지.

---

## Ralplan summary row

- **Principle/Option Consistency**: PASS.
- **Alternatives Depth**: PASS — Item #5 의 option (b) rejection 명시. Item #4 의 alternative (audio-thread retry loop) implicit reject — minor gap.
- **Risk/Verification Rigor**: REVISE — P1-3 test 한계 self-disclose 됐으나 plan 이 surface 안 함. §D MEDIUM-1 에서 처리.
- **Deliberate Additions**: N/A (post-hoc).

---

**Verdict**: **ACCEPT-WITH-RESERVATIONS** — v0.6 의 4-item RT-safety bundle 은 정확하고 검증됐으나, documentation surface (ipc_schema 스테일, P-tag 정합성, ADR 0016 법률 review 부재) 의 3 MAJOR 와 release-validation 섹션의 1 MINOR 가 v0.6.x patch 안에 정리되어야 한다.
