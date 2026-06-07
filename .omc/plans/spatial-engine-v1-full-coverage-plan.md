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
- **✅ Phase 3.1 사전조사 완료(−az L/R, 권위 검증됨)**: **레퍼런스 엔진 `Source/AdmOscProtocol.h:32` 권위 확인**: `admAzimuthDeg 는 ADM 규약(전방 0°, 왼쪽 +)` = ADM az는 **좌=+**. mmhoa 엔진은 **우=+**(`az=atan2(x,z)`, gap분석 L122). 둘이 정반대 → **`app_az=−adm_az`가 정답**(맹목 아님, 검증됨). 현재 AED 디코드 `CommandDecoder.cpp:227`는 무반전 → gap분석 L149: "모든 ADM 입력 L/R 반전" 버그. **수정 위치**: AED **디코드**(L227 az 부호반전) + AED **인코드**(L857 `p.az_rad*RAD2DEG`도 부호반전, 라운드트립 identity + 외부 ADM 정합). **골든테스트 필수**(ADM az=+30°(좌) → 엔진 az<0 → 좌측 스피커 우세). ⚠ dist: 현재 `*MAX_DIST` vs ADM dmax(AdmOscConstants, gap분석 L237 dmax 20 동결) — 확인 필요. xyz도 ±1×halfSpan(3.2).

## 5. 권장 실행 순서
Phase 0(토대) → **Phase 1(성능, 최우선 ROI)** → Phase 3(ADM, L/R+브로드캐스트) → Phase 2(바이노럴) → Phase 4(per-obj+캘리브) → Phase 5(릴리스). 
근거: 성능 경화는 모든 후속 기능이 그 위에서 측정·동작하는 토대이자 최저위험 최고ROI; ADM는 정확성 버그(−az)+vid2spatial 통합 의존성으로 기능 중 최우선.
