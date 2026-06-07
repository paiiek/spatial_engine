# Spatial Engine — v0.9.0 이후 백로그 (ordered, resume-safe)

> **목적**: 세션이 끊겨도 무손실 재개. 이 파일이 활성 백로그의 기준(source of truth).
> **방침**: 각 레인 = `ralplan`(Planner→Architect→Critic 합의) → `autopilot`(자율 실행).
> 재개 시 `autopilot`이 아래 "현재 진행" 포인터를 읽어 이어서 진행. **절대 처음부터 다시 계획하지 않는다.**

## 기준선 (2026-06-02)
- **릴리스 완료**: v0.8.0(`f48ef3f`) + v0.9.0(`8c18369`) 어노테이트 태그 push 완료(origin).
- 레인 A/B/C/E/F5 전부 main 안착. ctest 114/114, pytest 260p/4s green.
- 분석 출처: 2026-06-02 병렬 3-에이전트 감사 + 직접 코드 검증. 상세는 메모리 `project_v09_feature_extension.md` "다음 작업 후보" 참조.

## 현재 진행 포인터
- **ACTIVE**: 레인 2 (ADR 0019 **PR7**) — pending, ralplan 착수 예정 (PR6 완료로 PR7이 다음)
- 완료한 레인: **레인 2 (ADR 0019 PR6) ✅ 2026-06-08 — v0.9.1 릴리스 머지 완료** — merge commit `96ae451` → main, annotated tag **`v0.9.1`** push(origin). 포함 커밋: `30f5ce8`(plan REV4)+`799a485`(impl)+`0a6da21`(REV6)+`7ab732d`(REV7)+`b14ed4a`(JUCE-ON ,iis 링크 fix)+`d49fef2`(close-out)+`3a773a6`(CHANGELOG). 검증: NO_JUCE 빌드 클린, ctest **115/115**(머지 후 재확인), pytest **261p/6s**, shm soak smoke 30p, full 60s soak 1p, **JUCE-ON ,iis 링크 오브젝트레벨 증명**(T↔U; 풀 JUCE-app 빌드는 호스트 X11 dev 부재로 불가). VST3 빌드(Option B=JUCE-free)도 재설정 후 green(`spatial_engine_vst3.so`+vst3 테스트) → **Lane 3 선결조건 충족**. PR6 plan: `.omc/plans/spatial-engine-v0.9-laneG-adr0019-pr6-soak.md`.
- 완료한 레인: **레인 1 (F4) ✅ 2026-06-02** — commits `2392730`(plan REV5) + `c6c8415`(impl), push 완료. 5-iter ralplan consensus → executor → 독립 code-review APPROVE-WITH-NITS(MAJOR=주석 over-claim만, 수정함). 게이트: ctest 115/115 @64+@128, RT-asserts, TSan soak_scene_save_race 0 races/0 tears 양캡, pytest 260p/4s. 구현: F4a 직렬화+emit, F4b 3-buffer reader-claim publish handshake(seqlock optimistic은 그 자체가 formal race라 교체), param-7 decoder reject. **후속(optional)**: AC9 soak에 reader-slow 변종 추가, tearing tolerance absolute화(둘 다 non-blocking, 리뷰어 MINOR).

## 순서별 백로그

### 레인 1 — F4 씬 스냅샷 width_rad/reverb_send 직렬화  [STATUS: ralplan consensus 진행 — REV3]

**Consensus 진행 로그 (resume-safe):**
- 플랜 파일: `.omc/plans/spatial-engine-v0.9-laneF4-scene-snapshot-fields.md`
- **REV1**(Planner) → Architect/Critic 둘 다 ITERATE: 프로덕션 `/scene/save`가 빈 objects 영속 = F4a(직렬화)만으론 사용자 버그 그대로. → F4a+F4b 분리 필요.
- **REV2**(Planner): 권위 소스=`SpatialEngine::obj_cache_`(audio thread, StateModel 아님 — ADR0006 bypass), F4b=read-only `snapshotObjects()`→`ObjectStateProvider` 콜백. param-7=EQ-band-0 silent corruption(decoder fix folded in). → **Architect 재검토: SOUND-with-amendments** — B2 크로스스레드 읽기가 "기존 idiom" 주장 거짓, NullBackend/SharedRingBackend RT worker가 obj_cache_ 동시 write = 실제 레이스(UB, TSan 잡음, lifetime hazard는 없음).
- **REV3**(Planner): 동시성 해법 = **Option 2d publish-on-dirty 더블버퍼**(2a suppression·2b atomicize 기각 — 2b는 렌더러 hot loop SpatialEngine.cpp:702 .load() ripple). audio-thread 단일 post-drain publish, 렌더러 hot path 무변경, no alloc/lock, 신규 TSan 게이트 `soak_scene_save_race`(AC9, ≥150 rounds). muted=touched 휴리스틱, algo static_assert. → **Architect 재검토: SOUND-with-amendments** — 아키텍처 정확, 단 기계적 amendment 3개: (1)2-buffer→**3-buffer rotation**(unbounded writer + slow reader race by-construction 제거), (2)매크로명 `SPE_RT_NO_ALLOC_SCOPE()`, (3)dirty=popped_any coarse 의도 명시+post-drain publish. "F5와 동일 구조" 주장 완화(F5는 one-shot monotonic = 약한 precedent).
- **REV4**(Planner): 3개 amendment 반영(3-buffer rotation, 매크로, coarse dirty). → **Critic 재검토: ITERATE** — C1(CRITICAL): 3-buffer single re-check도 unbounded writer엔 racy(reader 2회 copy 中 첫 copy만 보호) → **retry-until-stable seqlock 루프 + 수렴한계(reader ~8KB copy ≪ 2 block ≈21ms)** 필요. M1: AC9 불변식이 gen tag 없이 검증불가 → correlated pair(dist==f(az))로 재작성. M2: ObjMove active=true로 omission corner 한정. +4 items. Critic 조건부 사전승인("C1·M1 수정시 APPROVE-ready").
- **REV5**(Planner): C1(retry-until-stable seqlock reader+수렴한계)/M1(correlated-pair AC9: width==encode(az)+dist/gain 쌍)/M2(ObjMove·ObjXYZ active=true로 omission corner 한정)/4items 반영. → **Critic 최종: APPROVE — consensus 도달 iter 5/5.** C1/M1/M2 전부 소스검증. 비차단 노트: 3rd 버퍼는 liveness 최적화(2버퍼+retry로도 correct).
- **STATUS: autopilot 실행 중** (2026-06-02). 플랜 `.omc/plans/spatial-engine-v0.9-laneF4-scene-snapshot-fields.md` REV5 APPROVED.
- **실행 범위**: F4a(ObjectSnapshot 2필드+toJson/fromJson 하위호환+CueEngine snapshotToFrames+emitObject ObjWidth/ObjDsp-6) + F4b-T0(param-7 decoder reject) + F4b-T1/T2(snapshotObjects() 3-buffer publish+seqlock retry reader+ObjectStateProvider 데몬 wiring) + 테스트(round-trip/backward-compat/cue-fire/AC4 e2e/AC9 soak_scene_save_race TSan/64+128).
- **게이트**: NO_JUCE ctest 64+128, RT-asserts build, TSan soak_scene_save_race(0 races+correlated-pair ≥150), pytest. 전부 green 시 커밋→레인1 완료→포인터 레인2(PR6)로.

- **문제**: `ObjectSnapshot`(core/src/ipc/SceneSnapshot.h:10-18)에 `width_rad`/`reverb_send` 필드 부재 → cue load마다 0 리셋(데이터 손실). `CueEngine.cpp:48-50`이 명시적 0 강제. `SceneCrossfade`(SceneCrossfade.cpp:84-85)는 이미 두 값 lerp 준비됨.
- **확인된 사실**: 두 값 모두 엔진이 per-object 추적·런타임 설정 가능 — `width_rad`=DSP idx 7 / OSC `ObjWidth`(Command.h:36, 0x08); `reverb_send`=DSP idx 6(Command.h:254). 빠진 건 직렬화 경로뿐.
- **범위(~6지점)**: (1) ObjectSnapshot에 2필드 추가 (2) toJson/fromJson + **구 scene JSON 하위호환**(키 부재시 0 default) (3) SceneSave 경로(SceneController.cpp:348~)에서 엔진 상태→snapshot 채움 (4) CueEngine::snapshotToFrames 채움 (5) CueEngine::emitObject에 ObjWidth + reverb_send DSP command emit 추가 (6) 테스트 + 포맷 버전 범프.
- **계약 변경**: scene JSON 포맷 → ralplan 합의 필수. backward-compat가 핵심 리스크.
- **플랜 파일**: `.omc/plans/spatial-engine-v0.9-laneF4-scene-snapshot-fields.md` (ralplan 생성 예정)

### 레인 2 — ADR 0019 PR6 (+PR7)  [STATUS: PR6 ✅ 완료(2026-06-07) / PR7 pending]
- **PR6 ✅**: 60s 크로스프로세스 C++↔Python shm soak 완료. drop-free(read_idx→write_idx)+xrun0+seq no-gap+ramp write-integrity+leak gate, x86-64 한정 실증. branch `feat/adr0019-pr6-soak`(미머지). **주의**: PR6은 x86-64 TSO에서 메모리오더링/consumer torn-read를 증명하지 **않음** — 그것은 PR7로 명시 유예.
- **PR7**: (a) Linux ARM64 CI에서 soak 실행해 weak-memory acquire/release pairing 실제 검증(`os.sched_yield()` ≠ release fence 여부 결론), (b) consumer-side read-integrity exposure(debug checksum emit), (c) 크로스플랫폼 CI (Windows `CreateFileMappingW` / macOS POSIX), (d) Option-B `adm_player_full` 변종(60s ADM BWF fixture + soundfile). 출처: PR6 plan §follow-ups.
- **주의**: PR7 플랜 파일 없음 → ralplan부터.

### 레인 3 — VST3 감독 스프린트 (P3.1 + P3.5 + P7.1)  [STATUS: pending, 감독 필요]
- P3.1 VST3 state-contract test → NO_JUCE CI; P3.5 `vst3_bind_collision` race + RUN_SERIAL; P7.1 SpatialEngine god-object refactor(BinauralTelemetry facade).
- **선결**: `core/build_vst3` 재구성 1회 → 셋 다 unblock. JUCE 경계 리스크 → architect 검토 필수.

### 레인 4 — per-active-object WFS 할당  [STATUS: pending]
- WFS-active 128 footprint ~111MB(>100MB) 해소. 전체 MAX_OBJECTS×spk 아닌 active-WFS-object 단위 할당. F5(ADR 0021) follow-up.

### 레인 5 — v0.8 P3.6 확인/완료  [STATUS: pending, 선확인]
- OSC sleep-barrier→event sync. VST3-gated 아닌데 `[ ]` 남음 → 누락/완료 여부 먼저 확인 후 재계획.

## 변경 이력
- 2026-06-07: 레인 2 PR6 완료(soak green @HEAD 검증) → 포인터 PR7로 이동.
- 2026-06-02: 백로그 생성. 레인 1(F4) ralplan 착수.
