# spatial_engine v1.0 — Full-Coverage Plan (모든 기능 + 모든 성능)

**작성**: 2026-06-06 · **브랜치**: feat/dreamscape-convergence · **선행**: ④WFS ⑤128 ⑥룸(22-param superset) ⑦디코릴 완료, 138/138 양 빌드 green.

## 0. 목표 (사용자 확정)

레퍼런스 계약을 **빠짐없이** 덮는 v1.0:
1. **모든 기능 커버** — xlsx 11시트(00~11) + `immersive-audio-engine` 레퍼런스 엔진의 모든 서브시스템.
2. **모든 성능 커버** — xlsx 10.성능 예산을 **실측으로 충족**하고, 스펙 규모(128ch)에서 실시간 동작.
3. **상용 준비** — 권리 문서화(권리 보유=D3 확정 → clean-room 불필요), 머지, 릴리스 게이트.

권위 계약: xlsx 11시트 + `SpatialSessionState.h`(세션상태 단일 진실원천) + 레퍼런스 Source/. mmhoa는 superset이 목표 — 레퍼런스에 없는 mmhoa 자산(NO_JUCE 헤드리스, 5 Ambisonic 디코더, Dante/LTC/Cue, SHM 텔레메트리, VST3, CI/relacy/no-alloc)은 유지.

## 1. 갭 분석 (3-레인 조사 종합, 2026-06-06)

### 기능 갭 (xlsx 시트 → 상태)
| 시트 | 상태 |
|---|---|
| 00 개요 / 09 좌표계 / 11 클래스맵 | ✅ (좌표 어댑터 골든테스트) |
| 01 상수·한계 | ⚠ 일부 상수 미노출(바이노럴 랭킹, 50 레이아웃슬롯, 캘리브 신호) |
| 02 세션상태 | ⚠ 룸22 done, 바이노럴/헤드트래킹 멤버 미구현 |
| 04 패닝 | ✅ VAP/VBAP3D/MDAP/WFS |
| 05 룸엔진 | ✅ ⑥a~h 완료 (22-param + scene preset) |
| 06 바이노럴 | 🔴 **Phase 2** — 7-stage 체인 일부+헤드트래킹 없음 |
| 07 디코릴 | ✅ ⑦ 완료 |
| 08 ADM-OSC | 🟡 **Phase 3** — x/y/z/xy·이름영속·아웃바운드·−az 반전 없음 |
| 10 성능 | 🟡 **Phase 1** — 예산 미실측(8spk만)·실버그 |

### 레퍼런스 서브시스템 (로드맵 외)
- per-obj 4밴드 EQ/delay/k_hf/width: 구조체 존재, `/obj/dsp` 라우팅 미완 → **Phase 4**.
- 스피커 캘리브레이션/체크(pink Voss-McCartney 7-state, log-sweep 20→20k): `/noise/*`만 있음 → **Phase 4**.
- 레이아웃 라이브러리 50슬롯: SceneController 확장 → **Phase 4**.
- GUI(MainComponent): N/A (mmhoa 의도적 헤드리스; WebGUI/VST3 별도 프론트).
- snapshot/cue 자동화: ✅ 이미 등가(SceneController/CueEngine).

### 성능 (architect B, 근거 file:line)
- 🔴 **실버그**: `enableDenormalFlush()`가 `FdnReverb::prepareToPlay`(control 스레드)에서만 호출. MXCSR=스레드별 → **오디오 스레드 denormal 활성** → 리버브/allpass/FDN 꼬리 denormal stall(10–100×). peak 스파이크 유력 원인. **1줄 수정**.
- 🔴 **구조적**: 5 렌더러가 활성0이어도 매블록 `MAX_OBJECTS` 전체 순회 + 5-way scratch 합산 무조건. 일반 씬 비용 4/5 낭비.
- 🔴 **측정 공백**: 모든 RT 측정이 8스피커. O(spk) 폭발 미측정. 룸-early `O(obj×6×3×spk×frame)` 지배. 128×128 all-active+룸+디코릴 단일코어 실시간 불가.
- 🟡 DBAP gain 매블록 `std::vector` 할당(활성 시 RT 계약 위반). VAP/DBAP gain 캐시 없음.
- 🟡 SIMD 전무, `-march/-O3` arch flag 없음.
- ℹ️ 10.성능은 block=512 기준(예산 10.67ms); perf harness는 block=64(1333µs). VBAP3D O(N³)는 "N<100 실시간" 스펙 명시.

### 법무 (architect A, D3로 해소)
- 포팅 권리=**보유**(D3 확정). clean-room 불필요. 단 c2-licensing.md/LICENSE.md/docs/legal/* 가 JUCE만 다룸 → ported/ 권리근거 **미문서화**. 상용 실사 갭.
- `CMakeLists VERSION 0.2.0` ↔ v0.9.0 태그 불일치(stale).
- 컨버전스 브랜치 main 대비 +55커밋 미머지.

## 2. 플랜 (Phase 0→5, 각 증분=구현→단위+no-alloc→실바이너리 스모크→리뷰→커밋→푸시→durable)

### Phase 0 — 토대 정리 (1–2일)
- **0.1** `docs/legal/PROVENANCE.md` — ported/ 출처(immersive-audio-engine, 권리 보유 근거=D3), 시트↔구현 매핑, JUCE-free 격리 정책. c2-licensing.md에 ported 섹션 추가.
- **0.2** `CMakeLists VERSION` → v0.9.x 정합. `SCHEMA_VERSION` 영향 점검.
- **0.3** 머지 전략 결정: 컨버전스→main 머지 시점(Phase 5 권장) + 중간 동기화 정책.
- **DoD**: 상용 실사 시 "권리·출처" 질문에 레포 문서로 답 가능.

### Phase 1 — 성능 경화 (모든 성능 커버, ≈1.5주) ★최우선 ROI
- **1.1 denormal 수정** (1줄, High): 오디오 콜백 진입부에서 FTZ/DAZ 설정(JUCE-free ScopedNoDenormals 등가). 단위: 무음 꼬리 블록 타이밍 회귀, no-alloc 유지.
- **1.2 active-object/algo 컴팩션**: 캐시-필 루프에서 알고리즘별 활성 인덱스 컴팩트 리스트 구성 → 활성0 렌더러 `processBlock`·scratch 합산 스킵, `0..MAX_OBJECTS` 대신 컴팩트 순회. 오디오 동등성 테스트로 비트동등 검증.
- **1.3 DBAP/VAP no-alloc**: VBAP의 `_into`+per-block 캐시 패턴 이식 → `dbap_gain` std::vector 제거(RT 계약). 
- **1.4 단계별 타이밍 + 스피커 스윕**: render/room/decorr/binaural 구간 프로브(vDSO steady_clock, ObservabilityCounters 패턴) → `/sys/metrics` 확장. perf harness 8→128 스피커 스윕 추가 → **실제 RT 천장 문서화**(`docs/RT_BUDGET_SPEAKERS.md`). 게이트 예산을 live block size 파생으로(하드코딩 933µs 제거).
- **1.5 (선택, sustained) SIMD**: `core/CMakeLists.txt` `-O3` + 런타임 디스패치/`-march`, per-spk mix를 axpy로 재구조화(GainRamp 선형화). 오디오 동등성 검증.
- **DoD (10.성능 실측 충족)**: ① 32obj/8spk+룸+바이노럴 ≤40% (스펙). ② 128spk 봉투 측정·문서화. ③ denormal stall 제거(peak 안정). ④ 룸+디코릴 켠 채 실용 씬에서 xrun=0 30분 soak.

> **📋 Phase 2 사전조사(2026-06-07, autopilot 세션) — 아키텍처 결정 선행 필요**: mmhoa 바이노럴=**per-object** 모델(BinauralMonitor 1870L: B1 Direct setDirection/processBlockForObject 오디오스레드 HRTF + B2 AmbiVS). 레퍼런스 7-stage=`BinauralMonitorChain`=**스피커-버스 모니터**(prefeed LP 4200Hz/per-spk → FIR 에너지랭킹 top-24 → HRTF → 딜레이 → 5밴드 EQ). **두 아키텍처가 근본적으로 다름**(per-object vs speaker-bus). mmhoa엔 `/ypr` 헤드트래킹·prefeed LP·FIR랭킹·5밴드EQ 모두 없음. **결정 필요**: (A) mmhoa per-object 경로 확장(2.6 헤드트래킹=각 객체 az/el을 head yaw/pitch/roll로 회전 후 setDirection — ⚠회전 부호=L/R류 리스크, 골든 필수; 5밴드EQ=RoomBiquad 재사용 per-object) vs (B) 레퍼런스 speaker-bus 7-stage 체인 병렬 이식. **architect 검토 권장**. core-first 분할 가능하나 결정 먼저. 가장 자립적 첫 슬라이스=2.6 /ypr 헤드트래킹(신규 OSC 태그+세션상태 head 멤버+회전, 골든=head 회전→정위 이동).

> **✅ Phase 2 아키텍처 결정(architect, 2026-06-07)**: **Option A 권고 = mmhoa per-object/B2 경로를 7-stage "개념"으로 확장**, 레퍼런스 speaker-bus FIR 랭킹(2.2)은 **이식 안 함**(mmhoa B2 AmbiVS가 이미 24 가상스피커→per-VS HRIR=레퍼 가상스피커 모델 등가, `BinauralMonitor.h:479-509`). 7-stage 중 4개(prefeed LP·delay ring·5밴드EQ·headtracking)는 아키텍처-중립 pre/post로 B1/B2에 그대로 적층; HRTF 합성은 이미 존재. Option B(speaker-bus 병렬이식)=검증된 1870L RT자산 우회 + 새 축매핑(L/R 리스크) 순손실로 기각. **첫 증분=2.6a 헤드회전 코어**(Coords.h 순수함수 yaw/pitch/roll 회전 + 골든, 오디오스레드 밖에서 L/R 리스크 먼저 잠금) → 2.6b /ypr OSC+세션상태 결선 → 2.5 5밴드EQ(RoomBiquad 재사용) → 2.1 prefeed LP → 2.4 delay ring → (2.2 FIR랭킹 생략, 2.3 HRTF 기존). **상세 설계=`.omc/research/PHASE2_BINAURAL_DESIGN.md`(21KB, gitignored — 다음 세션 첫 read 대상)**.

### Phase 2 — 바이노럴/헤드트래킹 완성 (xlsx 06, ≈2주)
레퍼런스 BinauralMonitorChain 7-stage 정밀 이식(mmhoa B1/B2·.speh·catalog 기반 확장):
- **2.1** prefeed LP 4200Hz(per-spk feedSmoothLp[128], kBinauralPrefeedLowPassHz).
- **2.2** FIR 에너지 랭킹: firRankEnergyLp[128] (α=0.19, decay=0.955) → 상위 24 스피커만 HRTF(kBinauralFirMaxSpeakersPerBlock=24).
- **2.3** HRTF 합성(KEMAR 256–512탭) / 미선택 스피커는 BinauralPan 폴백(panL=cos((az+1)π/4)/panR=sin, elevGain=1−clamp(|el|/90,0,0.3)).
- **2.4** 딜레이 링 65536(≈1.36s) tap=binauralDelayMs.
- **2.5** 5밴드 피크 EQ(kBinauralEqBands=5, RBJ, L/R 공유 캐시) — RoomBiquad 코어 재사용.
- **2.6** 헤드트래킹: headYaw/Pitch/RollDeg 멤버 + worldDirToHead 회전행렬 → HRTF 방향 보정. `/ypr ,fff` 리스너(포트 9001 또는 `/sys/ypr`).
- **OSC**: `/binaural/{eq,delay,gain,...}` + `/ypr`. **DoD**: HRTF 24-spk 예산 8–12%(스펙), 헤드 회전 시 정위 이동 스모크.

### Phase 3 — ADM-OSC 완성 (xlsx 08, ≈1.5주)
- **3.1 ⚠ −az 반전 수정** (L/R 정확성 최우선, memory L/R 전력): `app_az = −adm_az`, `app_dist_m = adm_dist×dmax`. 기존 수신 경로 점검.
- **3.2** `/adm/obj/N/{x,y,z,xy}` 파서 추가(±1→×halfSpanMeters 5.0).
- **3.3** 이름 영속(128-char; 현 31-char stub 확장), gain clamp≤8.
- **3.4 아웃바운드 브로드캐스트** (포트 9003): `/adm/obj/N/aed` @ admOscSendFps(1–60, 기본30), activeObjectCount까지, 번들+OSCTimeTag. **vid2spatial 브리지 필수**(상용화 플랜 Phase2 의존).
- **DoD**: ADM 왕복(수신 정위 + 송신 브로드캐스트) 스모크, L/R 비반전 검증.

### Phase 4 — per-obj DSP + 캘리브레이션 (⑩ + 레퍼런스 미커버, ≈1주)
- **4.1** `/obj/dsp` 라우팅 완성: param 0–7(4밴드 EQ dB / delay ms / k_hf / reverb_send / width) → PerObjectChain 적용(구조체 이미 존재).
- **4.2** 캘리브/체크 신호: pink Voss-McCartney 7-state + log-sweep(20→20k, 1s) + speaker-check 모드. `/noise/*` 확장 또는 `/check/*`.
- **4.3** 레이아웃 라이브러리 50슬롯(kSpeakerLayoutLibrarySlotCount): SceneController 확장 또는 `/layout/slot/*`.
- **DoD**: per-obj EQ/width 스모크, 캘리브 신호 출력 확인.

### Phase 5 — 릴리스 준비
- **5.1** 컨버전스→main 머지(+55커밋), v1.0-rc 태그.
- **5.2** flake 제거: `ambi_decoder_type_swap_concurrent`(2-slot quiescence→3-slot 또는 swap rate bound), OSC/binaural wall-clock flake.
- **5.3** 릴리스 게이트 체크리스트(전 테스트 green + N회 flake-free + 권리 클리어 + 10.성능 DoD).
- **5.4** 패키징(installer: .deb/AppImage), macOS 승격, Windows 검토.

## 3. 전체 DoD (v1.0)
- **기능**: xlsx 06/08 100% + per-obj EQ + 캘리브 + 50슬롯. 67→~80 OSC 태그.
- **성능**: 10.성능 예산 실측 충족 + 128spk 봉투 문서화 + denormal 제거 + 30분 soak xrun=0.
- **품질**: 양 빌드 green(64/128), flake 0(N회), no-alloc 게이트, code-reviewer APPROVE/증분.
- **상용**: 권리 문서화, main 머지, v1.0 태그, 패키징.

## 4. 리스크
- **성능 천장**: 128×128 all-active는 컴팩션+SIMD 후에도 단일코어 한계 가능 → 실용 봉투를 명시(스펙 vs 런타임 구분)하고 문서화. 멀티코어/부분렌더는 v1.1.
- **ADM L/R**: memory 전력(2026-02~03 다수) → 반전 수정은 골든테스트 필수.
- **바이노럴 이식량**: 7-stage가 ⑦보다 큼(2주) — core-first 분할(코어→결선) 권장.
- **머지 충돌**: +55커밋 장기 브랜치 → Phase 5 전 main 동기화 점검.

## 6. 진행 로그 (resume 포인터)
- ✅ **Phase 0 완료 (2026-06-07, 1600d76, 푸시됨)** — 0.1 `docs/legal/PROVENANCE.md`(ported/ 출처 f2cb796=v0.2.1, D3 권리, 파일↔소스↔시트 맵, JUCE-free 격리/re-sync) + c2-licensing Strand 4. 0.2 CMakeLists VERSION 0.2.0→0.9.0(선언적 전용, 소비처 0 확인). 0.3 `convergence-merge-strategy.md`(머지=Phase5, main 0-behind/56-ahead=clean FF, main frozen→중간 rebase 불필요). ctest 138/138 green WERROR+RT_ASSERTS, 회귀0.
- ✅ **Phase 1.1 denormal 수정 완료 (2026-06-07, a026d01, 푸시됨)** — 공유 헤더 `core/src/util/DenormalGuard.h`(inline `spe::util::enableDenormalFlush()`, x86 MXCSR FTZ/DAZ + aarch64 FPCR FZ, JUCE-free, arch 매크로 헤더끝 #undef) → `SpatialEngine::audioBlock()` 최상단(DSP·오버사이즈 early-return 전)에서 호출=오디오 스레드 FTZ/DAZ 보장. FdnReverb.cpp 익명 namespace 복제 제거→공유 헤더 재사용(control-thread prepare 호출 유지). 단위 `test_convergence_denormal_guard`(volatile 1e-40 flush + x86 MXCSR 비트 + #if-guard된 no-alloc). 양 빌드 139/139 green WERROR+RT_ASSERTS, smoke_room_reverb xruns=0, code-reviewer APPROVE(MEDIUM+LOW1 반영). **peak 안정 perf 효과는 Phase 1.4 실측.**
- ✅ **Phase 1.2 active-object/algo 컴팩션 완료 (2026-06-07, a361c53, 푸시됨)** — audioBlock이 알고리즘별 활성 객체수 카운트 → 활성0 렌더러는 fill/processBlock/scratch-sum 스킵, ran 렌더러만 원래 VBAP→DBAP→WFS→Ambisonic→VAP 순서로 합산. **비트동등 검증완**(전 렌더러 0-active=전무음 출력 + 비활성 객체 상태 미진행: VBAP/DBAP/VAP/WFS memset+skip, Ambisonic=zeroed SH→stateless matmul; dropped term=+0.0f, a+0.0f==a; -0.0f 불가). static_assert로 n_act[5]/ran[5] enum범위 가드. 단위 `test_convergence_render_compaction`(transient WFS+Ambisonic 비활성화→never-had-it 엔진과 **byte-identical**, 0 mismatch + all-5-algo finite). 양 빌드 140/140 green, smoke vbap/wfs xruns=0, code-reviewer APPROVE(비트동등 검증 완, LOW2 반영). scratch/objs는 prepareToPlay+이 블록 외 미사용.
- ✅ **Phase 1.3 DBAP no-alloc 완료 (2026-06-07, e2d55a5, 푸시됨)** — `AlgorithmAnalyticReference::dbap_gain_into`(가중치를 out 버퍼에 폴드+제자리 정규화, zero temp, noexcept, bit-identical) 추가, `dbap_gain`은 thin wrapper화(vbap 패턴), `DBAPRenderer::dbapForPosition`이 into() 사용(cap=MAX_SPEAKERS, tail-zero). **VAP는 이미 no-alloc 확인**(computeVolumetricAmplitudePanning=stack float[MAX_SPEAKERS], reviewer 검증). 단위 `test_convergence_dbap_no_alloc`(live DBAP width0+width>0→audioBlock, RT_ASSERTS 센티넬로 8블록 0할당, warmup-then-measure, 비vacuous). 스모크 `smoke_dbap.py`(**올바른 4-int /obj/algo [seq,id,obj,algo] 헤더** — smoke_vap/mdap의 잠복 VBAP-폴백 버그 회피; 8스피커 분산, R/L 24.51x, xruns=0). 양 빌드 141/141 green, code-reviewer APPROVE(bit-identical+no-alloc+capacity+VAP claim 검증, LOW2 무액션).
- ✅ **Phase 1.4a 스피커 스윕 + RT_BUDGET_SPEAKERS.md 완료 (2026-06-07, 4b6723e, 푸시됨)** — `core/tests/perf/perf_speaker_sweep.cpp`(Release-gated ctest, 32 VBAP obj 고정, 스피커 {8..128} 스윕, render-only vs render+room, exact median/p99/peak). **하드코딩 933 우려는 비해당**(CpuMeter·harness 모두 budget=block/sr 파생). 정직한 게이트(render-only <50%@128, render+room 실시간<100%@16spk) → VERDICT PASS. 엔진 코드 무변경(측정툴+doc만, Release-gated라 141 ctest 무영향), 검증=Release-128 실행 PASS.
  - 🔴 **핵심 실측 발견(중요, Phases 2-5에 영향)**: **코어 패너(VBAP/DBAP/VAP/WFS)는 128ch까지 정상**(peak ≤30.6% 예산). 그러나 **룸 리버브 = O(spk²)**: 8spk=32.6% / 16=53.6% / 24=99.1% / 32=247% / 128=**7994%** 예산. 룸은 ~16-24스피커까지만 실시간. **DoD ① 스펙(8spk+룸=32.6%, <40%) 통과**, **DoD ② 128 봉투 측정·문서화 완료**. 근본원인: 룸 early/late/cluster가 **매블록 VBAP 게인을 캐시없이 동적방향으로 재계산**(obj×6img×3width + 8 late lines + cluster; 방향이 모션+opp-bias로 매블록 변해 기존 위치캐시 무효). **수정=sub-O(spk²) 게인경로(kdtree/precomputed triangulation) — 별도 perf 증분/v1.1로 플래그**. 대형어레이 룸리버브는 그전까지 실시간 불가(패닝은 OK).
- ✅ **Phase 1.4b 단계별 타이밍 프로브 → /sys/metrics 완료 (2026-06-07, 401f2ad, 푸시됨)** — ObservabilityCounters에 atomic 4개(stage_render/room/decorr/binaural_us), audioBlock이 steady_clock(vDSO)로 각 구간 측정→블록끝 obs 저장, emitSysMetrics에 4필드 APPEND(,s key=value, 와이어 하위호환), 1Hz 틱이 전달. 단위 `test_p_sys_metrics_extended`(10필드 drain+stage 값 라운드트립). 스모크 `smoke_sys_metrics_stages.py`(실바이너리: render=35µs room=790µs decorr=25µs binaural=0µs 실측, xruns=0 — **room이 지배적, O(spk²) 발견과 일치**). 양 빌드 141/141 green.
- ✅ **Phase 1.4c soak 하니스 완료 (2026-06-07, 6b71bc1, 푸시됨)** — `scripts/soak_room_decorr.py`(실바이너리 launch+handshake+room+decorr+이동객체, 1Hz /sys/metrics 수동수집, xrun=0 + block파생 p99 게이트). run_soak.py의 `poll_metrics_real`은 현 엔진 1Hz emit과 불일치 stub였음(별도 functional 스크립트로 해결). run_soak.py 933 하드코딩→`p99_threshold_us(block,sr)` 파생 수정. **30s 검증 PASS**(12spk/8obj, p99_max=2951µs<3733µs, xruns=0, cpu_peak 59%, 6008블록). **30분 풀 soak 백그라운드 실행 중**(결과 완료 시 기록).
- ⏭️ **Phase 1.5 SIMD = SKIP (정당화됨)**: 플랜서 "선택". 패닝 이미 ≤30.6% 예산@128spk(DoD 충족), 실 병목=룸 O(spk²)=알고리즘적(SIMD로 점근 해결 불가, kdtree 게인 경로 필요=v1.1). `-march=native`는 byte-baseline 이식성 훼손. → 미적용, 근거 문서화.
- **✅ Phase 1 (성능) 실질 완료** (1.1~1.4 done, 1.5 skip). DoD: ①8spk+룸=32.6%<40% ✅ ②128 봉투 문서화 ✅ ③denormal 제거 ✅ ④soak 진행중(30s PASS, 30분 실행중). **다음 = Phase 3 (ADM −az + 브로드캐스트)** — 3.1 −az 사전조사 완료(권위 검증). 그 후 Phase 2(바이노럴)→4→5. ⚠ 룸 O(spk²) 최적화=v1.1.
- ✅ **Phase 3.1 −az L/R 수정 완료 (2026-06-07, bcde841, 푸시됨, code-reviewer APPROVE)** — 3 사이트 부호반전: 디코드(CommandDecoder azim+aed), AdmV1 인코드(aed), 에코 markAed(SpatialEngine). el/dist 불변(dist=ADM_OSC_MAX_DIST 20.0 ADR0006 정확). 테스트 업데이트(test_p_adm_osc, _v1_compat; roundtrip 불변=이중반전 identity; compliance.csv az_rad 부호플립=doc, 테스트 미참조). **신규 골든 `test_convergence_adm_az_golden`**(in-proc: ADM+30좌→left=6.047/right≈0, ADM−30우→right=6.047/left≈0). **스모크 `smoke_adm_az.py`**(실바이너리 wire: +30→L 25.5x, −30→R 25.5x, xruns=0). 양 빌드 142/142 green. **L/R 정확성 경험적 확정.**
- ⚠ **Phase 1.4c 30분 soak 정직한 결과**: 12spk/8obj room+decorr, 337884블록, **xruns=1**(빌드+리뷰어 동시부하 中, NullBackend RT우선순위 없음, cpu_peak 73%@12spk=room O(spk²) 여파로 헤드룸 부족). p99_max=3010µs<3733µs 게이트 통과. **DoD ④ xrun=0 미달(1 xrun)** → **조용한/RT-우선 호스트 재실행을 Phase 5 릴리스 게이트로 플래그**(엔진 결함 아닐 가능성 높음=경합 아티팩트). 30s soak은 PASS.
- ✅ **Phase 3.2a /adm xyz 좌표 수정 완료 (2026-06-07, 6da46a9, 푸시됨, code-reviewer APPROVE)** — 권위 핀다운(레퍼런스 metersFromAppPolarDegrees `{r·ch·sin(az),r·ch·cos(az),r·sin(el)}` + azApp=−admAz): **ADM Cartesian=(x우,y전,z상)×halfSpan, 엔진=(x우,y상,z전) → Y↔Z 스왑**. ObjXYZ 핸들러 `az=atan2(x,z)/el=asin(y/r)/dist=r`(버그) → `az=atan2(x,y)/el=asin(z/r)/dist=r×ADM_OSC_CARTESIAN_HALF_SPAN(5.0)`. 디코드는 raw 저장 불변(CSV ObjXYZ 행 유효, 변경불필요). 골든테스트 확장: xyz x=−1→좌, x=+1→우, **xyz(−0.5,0.866,0)=aed+30 일치(좌)** ⇒ xyz/aed 일관성 확정. 양 빌드 142/142. **3.2b 보류**: 부분축 /adm/obj/N/{x,y,z,xy}(엔진 현재 Cartesian 상태 read-modify-write 필요, 신규 태그) — 별도 증분.
- ✅ **Phase 3.3a ADM gain clamp 완료 (2026-06-07, 빌드 후 커밋)** — `/adm/obj/N/gain` 디코드에 `[0,8]` 클램프(레퍼런스 AdmOscProtocol.cpp:265 `jmin(g,8)` = 선형 max +18dB; 하한 0=음수 위상반전 방지). 단위 test_p_adm_osc PASS 4b(10→8, −1→0). 양 빌드 ctest. (3-line decode clamp + 단위테스트 = reviewer 생략 합리적.)
- ✅ **Phase 3.4a 아웃바운드 ADM 브로드캐스트 완료 (2026-06-07, fdb8d5a, 푸시됨, code-reviewer APPROVE)** — vid2spatial 브리지 스트림. bin tick(컨트롤 스레드)이 `--adm-send-port`>0일 때 `--adm-send-fps`(기본30, clamp1-60) 주기로 `snapshotObjects`(RT-safe reader-claim) → active(!muted) 객체마다 `/adm/obj/N/aed` 를 127.0.0.1:port로 송신(fwd_fd 재사용). **az 반전**(engine 우+ → ADM 좌+, 3.1 관례) + el 통과 + dist/ADM_OSC_MAX_DIST. 신규 `build_adm_aed` OSC 인코더(addr 4-pad + ,fff 8byte + BE floats). 스모크 `smoke_adm_broadcast.py`: **라운드트립 identity**(ADM+30 in→broadcast+30 out, el10/dist0.25 정확) ~25-30fps. ⚠ 스모크 파서 4byte 오프셋 버그(typetag 8byte) 발견·수정(엔진 정상). 양 빌드 컴파일+ctest. **3.4b 보류**: OSC 번들+OSCTimeTag 래핑(현재 per-message; vid2spatial엔 per-message 충분), /adm/send/* OSC 런타임 제어(현재 CLI 플래그).
- **📋 Phase 3.3b 보류(이름 영속 128-char)**: PayloadObjName.name[32]→[128] + ObjCache name 필드 추가(현재 없음=stub) + SceneSnapshot 통합(영속). EchoSubscriber.name[32]도 정합. 별도 증분(scene 직렬화 영향).
- **📋 (구) Phase 3.2 사전조사**: 레퍼런스 `AdmOscProtocol.cpp:205-250`: ADM x,y,z∈[-1,1]×halfSpan(=cartesianHalfSpanMeters, 기본 5.0m) → app 직접매핑, app=+Y전방 → **ADM Cartesian=(x우,y전,z상)**. mmhoa 엔진=(x우,y상,z전) → **ADM↔mmhoa = Y↔Z 스왑** 필요. 현 mmhoa `ObjXYZ`(SpatialEngine: `az=atan2(x,z),el=asin(y/r)`)는 stored xyz를 mmhoa프레임 가정 → **ADM xyz는 현재 front↔up 오매핑**. 추가: ① `x`/`y`/`z`/`xy`=부분축 갱신(나머지축 보존; 레퍼 getObjectPositionMeters→수정→applyPos), 현 단일 ObjXYZ는 전체 대체. ② **aed 경로(−az 반전)와 일관성 필수**(같은 ADM 소스를 xyz vs aed로 줘도 같은 엔진 위치). **권장 구현**: 디코드 경계에서 ADM xyz→엔진 az/el/dist 변환(aed와 동일 관례, Y↔Z 스왑 + 우=+ 정합), x/y/z/xy 부분갱신은 엔진측 현재 Cartesian 상태 필요. **골든테스트**: ADM xyz 좌측 → 좌스피커 + xyz/aed 위치 일치. ⚠ 맹목 금지 — 관례 핀다운 + 골든 필수.
- **📋 (구) Phase 3.1 구현 스펙**: −az는 **engine↔ADM 경계 3 사이트 모두 부호반전**(일관성): ① 디코드 `CommandDecoder.cpp:219`(azim) + `:227`(aed) adm→engine. ② AdmV1 인코드 `:857`(aed) engine→adm. ③ 에코 `SpatialEngine.cpp:38` markAed(현재 `p->az_rad*kRad2Deg` = 디코드된 engine az를 echo → 부호반전 필요 engine→adm). **3중 반전이라 인코드/디코드 라운드트립·에코 라운드트립 = identity 유지**(기존 roundtrip 테스트 green). **업데이트 필요 테스트**: `adm_osc_v1_compliance.csv` row2-5(azim az 부호반전), `test_p_adm_osc.cpp:56`(azim45→-), `:86`(aed90→-), `test_p_adm_osc_v1_compat.cpp:265`(aed1.5→-) — roundtrip(:328)은 불변. `test_echo_plane.cpp:50` 확인. **신규 골든테스트**: 실엔진 /adm/obj/0/aed az=+30°(좌) → 좌반구 스피커 우세(현재는 우측=버그). **dist는 이미 정확**(ADM_OSC_MAX_DIST=20.0 ADR0006 동결 = dmax). **⚠ 계약 변경**: compliance.csv는 ADM-OSC v1 계약 — az 관례 플립은 vid2spatial/외부 ADM 클라이언트에 영향. 사용자 확인 권장(L/R 이력 다수).
- **✅ Phase 3.1 사전조사 완료(−az L/R, 권위 검증됨)**: **레퍼런스 엔진 `Source/AdmOscProtocol.h:32` 권위 확인**: `admAzimuthDeg 는 ADM 규약(전방 0°, 왼쪽 +)` = ADM az는 **좌=+**. mmhoa 엔진은 **우=+**(`az=atan2(x,z)`, gap분석 L122). 둘이 정반대 → **`app_az=−adm_az`가 정답**(맹목 아님, 검증됨). 현재 AED 디코드 `CommandDecoder.cpp:227`는 무반전 → gap분석 L149: "모든 ADM 입력 L/R 반전" 버그. **수정 위치**: AED **디코드**(L227 az 부호반전) + AED **인코드**(L857 `p.az_rad*RAD2DEG`도 부호반전, 라운드트립 identity + 외부 ADM 정합). **골든테스트 필수**(ADM az=+30°(좌) → 엔진 az<0 → 좌측 스피커 우세). ⚠ dist: 현재 `*MAX_DIST` vs ADM dmax(AdmOscConstants, gap분석 L237 dmax 20 동결) — 확인 필요. xyz도 ±1×halfSpan(3.2).
- **✅ Phase 3 (ADM-OSC 코어) 완료 (2026-06-07)** — 3.1 −az(bcde841) + 3.2a xyz Y↔Z(6da46a9) + 3.3a gain clamp[0,8] + 3.4a 아웃바운드 브로드캐스트(fdb8d5a) 전부 푸시·APPROVE. **보류(별도 증분)**: 3.2b 부분축 /x/y/z/xy · 3.3b 이름영속 128-char · 3.4b OSC 번들/런타임 송신제어. ADM 정확성·vid2spatial 브리지 코어 완성.
- **✅ Phase 2 (바이노럴) 아키텍처 결정 + 설계 완료 (2026-06-07, 46ad603→4304eb4, 푸시됨)** — architect read-only 분석으로 **Option A 채택**(B1/B2 검증 자산 위에 중립 7-stage wrapper; speaker-bus FIR 랭킹 2.2는 mmhoa 비용모델서 unmotivated → 미구현+근거문서화; C=B2 일반화는 미래 탈출구). 설계 문서 `docs/design/PHASE2_BINAURAL_DESIGN.md`(증분분해·/ypr 정밀설계·L/R 부호잠금·6 리스크). 증분 순서 **2.6a → 2.6b → 2.1 → 2.4 → 2.5 → (2.2 문서화) → (2.3 선택)**.
- **✅ Phase 2 증분 2.6a 헤드회전 코어 완료 (2026-06-07, 푸시됨, code-reviewer APPROVE)** — `coords/Coords.h`에 순수함수 `rotate_engine_dir_by_head(az,el,yaw,pitch,roll)->pair`(엔진프레임 x=우/y=상/z=전 단위벡터 → Rz(roll)·Rx(pitch)·**Ry(yaw, 표준 RH)** → atan2/asin 복원). **P0 L/R 잠금**: yaw-only=가법형 `az'=az+yaw`(cosEl가 atan2서 상쇄 → 전 고도 성립), yaw+30°+정면(az=0)→az'=+30°=RIGHT=R-louder(`stereo_pan_from_pipeline_az(az')>0`로 교차검증). 순수 스택·alloc 0·오디오/OSC/세션 무접촉. 골든 `test_convergence_head_rotate`(가법성·P0락·zero=항등·pitch/roll 고도·finite·el∈[-π/2,π/2]). **양 빌드 143/143 green WERROR+RT_ASSERTS**(142→143, 회귀0). 스모크 불요(오디오 경로 무접촉). 커밋=구현+테스트+CMake+이 로그.
- **✅ Phase 2 증분 2.6b /ypr 헤드트래킹 end-to-end 완료 (2026-06-07, 576b9ce, 푸시됨, code-reviewer APPROVE)** — OSC→B1 HRTF lookup까지 헤드트래킹 결선(2.6a 회전코어 위에). `CommandTag::SysHeadYpr=0x1C`+`PayloadSysHeadYpr{yaw,pitch,roll deg}`+variant. `/ypr ,fff`(헤드트래커 직결)+`/sys/ypr` alias 디코드(누락인자→0). 엔진 `std::atomic<float>×3` head 멤버(relaxed)+`setHeadYpr()` 제어API+getters. **SysHeadYpr 디스패치=control-thread relaxed store+early-return(FIFO 없음, float-only, sibling /sys verb 패턴)**. B1 분기가 **블록당 1회** 포즈 read(deg→rad)→객체마다 `coords::rotate_engine_dir_by_head`로 회전 후 setDirection. 순수 스택·alloc 0·zero포즈=항등. **B2(AmbiVS)는 미회전=v1.1 SH-rotation(문서화된 한계, 잠복버그 아님)**. bin `--wav-binaural`(WavCapture가 binauralL/R 2ch WAV 캡처 — 헤드트래킹은 B1 버스에서만 가청). 테스트: `test_osc_ypr_roundtrip`(decode+alias+누락기본값) + `test_convergence_head_ypr_golden`(**풀엔진 바이노럴 ITD 골든**: 정면객체+/ypr+90 → 물리 az+90 앵커와 **동일 ear lead**(lag −27, 부호를 관례 아닌 실측 앵커로 잠금), ±90=반대 ITD). 스모크 `smoke_head_ypr.py`(실바이너리 와이어: +90 ITD −30, −90 +30, 반대부호, xruns=0). **양 빌드 145/145 green WERROR+RT_ASSERTS**(143→145, 회귀0).
- **✅ Phase 2 증분 2.5 바이노럴 5밴드 peak EQ 완료 (2026-06-07, code-reviewer APPROVE)** — 최종 바이노럴 L/R 버스의 5밴드 RBJ peaking EQ(중립 post-chain, B1/B2 공유). `iae::RoomBiquad::setPeak(sr,f,Q,gainDb)` = JUCE `makePeakFilter<float>` byte-faithful 이식(A=√gainFactor, a0 정규화, `decibelsToGain` 내장, A floor 1e-6 가드). 상수 `iae::kBinauralEqBands=5` + 기본 freq{120,400,1250,4000,12000}/Q{1×5}(레퍼 SpatialSessionState.h:559-561). OSC: `CommandTag::SysBinauralEq=0x1D` + `PayloadSysBinauralEq{op,enable,band,freq,gain,q}` + variant; `/sys/binaural_eq/enable ,i` / `/sys/binaural_eq/band ,ifff`(누락 Q→1, 단일 선두 int라 seq/id 트랩 회피). **RoomCtl 패턴**: QueuedCmd POD 필드 → FIFO → 오디오스레드 drain `applyBinauralEq`(밴드별 clamp[10..0.45fs / ±24dB / 0.1..10] + L/R `setPeak` 락스텝, coeff-only 무리셋). 엔진 멤버 = `std::atomic<bool> active`(블록당 1회 read) + `RoomBiquad[5]×2`(L/R, 코프 공유·상태 독립) + param 미러[5×3](전부 오디오스레드 전용). 적용점 = render_branch 직후·**limiter 직전**(레퍼 EQ→gain/limit 순서). prepare서 0dB(=unity)·flat·off 초기화. 순수 float·alloc 0(audioBlock RT no-alloc scope 하에서 골든이 검증). 테스트: `test_convergence_binaural_eq`(① setPeak 계수 — 분석적 |H|: 중심=10^(dB/20), DC/Nyq≈1, 0dB=unity, reset 결정성 ② **풀엔진 in-proc 골든**: obj0=110Hz+obj4=330Hz 톤, `/sys/binaural_eq` −18dB@110 컷 → Goertzel E110 40× 감소·E330 유지·밸런스 이동) + `test_osc_sys_binaural_eq_roundtrip`(decode). 스모크 `smoke_binaural_eq.py`(실바이너리 UDP 와이어: 110/330 밸런스 1.11→0.0186, xruns=0). **양 빌드 147/147 green WERROR+RT_ASSERTS**(145→147, 회귀0).
- **✅ Phase 2 증분 2.1 prefeed LP 완료 (2026-06-07, code-reviewer APPROVE)** — HRTF 입력에 1차 one-pole LP(기본 4200Hz, 레퍼 BinauralMonitorChain.cpp:106-125). 각 활성객체 dry를 **블록당 1회** `bin_prefeed_[i]`로 필터(render_branch가 xfade서 2회 호출돼도 상태 1회만 전진) → **B1·B2 모두 bin_prefeed_ 읽음**(객체dry→SH인코드/HRTF 공통, B1≡B2 등가 유지). 계수 `a=1−exp(−2π·fc/sr)`, fc 블록당 1회 read(relaxed atomic)·clamp(1, 0.499fs). mmhoa superset: 코너 **튜너블** `/sys/binaural_prefeed ,f`(`CommandTag::SysBinauralPrefeed=0x1E`+payload+variant, control-thread atomic store early-return=/ypr 패턴; 코너≥Nyquist⇒a≈1⇒passthrough=bypass). 모노 입력 pre-HRTF 적용=양귀 동일⇒**ITD 불변**(저주파/타이밍 무영향). `bin_prefeed_` = fixed `array<array<float,MAX_BLOCK>,MAX_OBJECTS>`(dry_scratch_와 동일, heap 0). prepare서 state 0. 테스트 `test_convergence_binaural_prefeed`(① OSC decode roundtrip ② **풀엔진 골든**: obj0=110Hz+obj63=3575Hz, 4200 vs bypass(동일 HRTF⇒착색 상쇄) → E3575 0.62× 감소·E110 유지·HF/LF 밸런스 하향). 스모크 `smoke_binaural_prefeed.py`(실바이너리: 3575/110 0.065→0.039, 110 유지, xruns=0). **양 빌드 148/148 green WERROR+RT_ASSERTS**(147→148, 회귀0).
- **▶ 현재 위치 / 다음 (resume 포인터)**: Phase 0 ✅ · Phase 1(성능) ✅(1.5 skip 정당화) · Phase 3(ADM 코어) ✅ · Phase 2 설계 ✅ · **2.6a ✅ · 2.6b ✅ · 2.5 5밴드EQ ✅ · 2.1 prefeed LP ✅ → 다음 = 증분 2.4 (delay ring, 바이노럴 L/R 버스 stereo 모니터 지연)**. 그 후 (2.2 FIR랭킹 생략·문서화, 2.3 HRTF 기존) → Phase 4(per-obj DSP+캘리브+50슬롯) → Phase 5(릴리스: 머지 FF + 30분 soak RT-우선 재실행). 설계=`docs/design/PHASE2_BINAURAL_DESIGN.md` §2.4(65536-tap stereo ring, tap=binauralDelayMs, render_branch 직후/limiter 직전 — EQ 앞 또는 뒤? 설계=L/R 버스 stereo delay, prepare서 vector 할당, modulo 인덱싱 alloc 0). ⚠ 룸 O(spk²) 최적화=v1.1. ⚠ 비차단 후속: B2 헤드트래킹(SH 회전)=v1.1. **Phase 번호는 이 v1.0 플랜이 정준**(구 `dreamscape-convergence-master-plan.md`는 SUPERSEDED, 번호 다름).

## 5. 권장 실행 순서
Phase 0(토대) → **Phase 1(성능, 최우선 ROI)** → Phase 3(ADM, L/R+브로드캐스트) → Phase 2(바이노럴) → Phase 4(per-obj+캘리브) → Phase 5(릴리스). 
근거: 성능 경화는 모든 후속 기능이 그 위에서 측정·동작하는 토대이자 최저위험 최고ROI; ADM는 정확성 버그(−az)+vid2spatial 통합 의존성으로 기능 중 최우선.
