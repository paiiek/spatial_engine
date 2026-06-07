# Phase 2 — Binaural / Headtracking Architecture Design

**작성**: 2026-06-07 (architect, read-only 분석) · **브랜치**: feat/dreamscape-convergence
**대상 결정**: xlsx-시트-06 7-stage 바이노럴 체인 + 헤드트래킹을 mmhoa에 어떻게 전달할 것인가.
**입력**: 플랜 §Phase 2 / Phase 2 사전조사 노트 + mmhoa `BinauralMonitor.{h,cpp}` + 레퍼런스 `BinauralMonitorChain` / `KemarHrtfBinauralProcessor` / `BinauralPan` / `AudioRenderQuality.h`.

---

## Summary

**권고: Option A (mmhoa per-object / B2 경로를 7-stage "개념"으로 확장)**, 단 레퍼런스의 speaker-bus
front-end(FIR 에너지 랭킹 top-24)는 **이식하지 않는다**. 근거: 레퍼런스 7-stage 중 4개(prefeed LP·delay
ring·5-band EQ·headtracking)는 **아키텍처-중립 pre/post 스테이지**로 B1/B2 어느 쪽에도 그대로 얹힌다.
나머지 중 FIR 랭킹(2.2)은 **128 물리스피커를 24 HRTF로 줄이는 비용제어 장치**일 뿐인데, mmhoa B1은 이미
활성-객체 수로 비용이 bounded이고 **B2 AmbiVS 경로는 이미 "24 가상스피커 → per-VS HRIR → 합산"으로
레퍼런스 가상스피커 모델과 구조적으로 동일**하다 (`BinauralMonitor.h:479-509`, `processBlockB2`). 즉 레퍼런스가
"필요해서 만든" 랭킹은 mmhoa 비용모델에선 불필요하다. HRTF 합성(2.3)도 두 형태(B1 KdTree + B2 t-design VS)로
이미 존재한다. 따라서 Option B(speaker-bus 체인 병렬 이식)는 ~1870L의 검증된 RT-safe 자산을 우회하고 새 좌표프레임
축매핑 코드(=#1 버그류 L/R)를 도입하는 **순손실**이다.

**첫 증분**: `2.6a 헤드회전 코어`(Coords.h 순수함수 + 골든) — 가장 자립적이고 L/R 리스크가 가장 큰 조각을
오디오스레드 밖 순수함수로 먼저 잠근다.

---

## Analysis — 두 아키텍처의 실제 차이

### mmhoa = per-object (+ 이미 존재하는 가상스피커 B2)
- **B1 Direct**: 오디오스레드가 활성객체마다 `setDirection(i, c.az, c.el)` → `processBlockForObject()`
  (`SpatialEngine.cpp:1712-1734`). 객체별 dual-slot OlaConvolver(L/R), 활성 SOFA 테이블 KdTree3D nearest
  lookup, 2-block crossfade, 런타임 SOFA hot-swap self-heal, 자동 demote — 전부 alloc-free RT-safe
  (`BinauralMonitor.h:1-50, 511-650`). 비용 = O(활성객체).
- **B2 AmbiVS**: 3rd-order SH(K=16) → AmbiDecoder → **24 t-design 가상스피커 → per-VS HRIR 합성 → L/R**
  (`BinauralMonitor.h:479-509`, `kNumVirtualSpeakers=24`). 비용 = 고정 24 convolution.
  **이것이 레퍼런스 BinauralMonitorChain의 "가상스피커 버스 → HRIR" 모델과 구조적 등가물이다.**

### 레퍼런스 = speaker-bus monitor
- `BinauralMonitorChain::render(pull, speakerFeeds, deviceBuffer, ...)`
  (`BinauralMonitorChain.cpp:74-227`): 이미 렌더된 N채널 **물리 스피커 버스**를 입력으로 받아 7-stage:
  1. **prefeed LP 4200Hz** per-spk 1차 one-pole (`:106-125`, `feedSmoothLp[128]`, `kBinauralPrefeedLowPassHz`).
  2. **FIR 에너지 랭킹** (`KemarHrtfBinauralProcessor.cpp:314-346`, `firRankEnergyLp` α=0.19 decay=0.955) →
     RMS 상위 `kBinauralFirMaxSpeakersPerBlock=24`만 HRTF.
  3. **HRTF 합성** mysofa `getfilter` time-domain FIR (`:354-439`); 미선택 스피커 + 비공간 스피커는
     `addSpeakerPanStereoContribution` 등파워 패닝 폴백 (`:79-132`, `BinauralPan.cpp`).
  4. **delay ring 65536**(≈1.36s) tap=`binauralDelayMs` (`BinauralMonitorChain.cpp:132-154`).
  5. **5-band peak EQ** RBJ, L/R 공유 coeff 캐시 (`:156-202`, `makePeakFilter`).
  6. gain + `jlimit(-3,3)` + device L/R write (`:187-225`).
  - **headtracking**: `worldDirToHead(x,y,z, yaw,pitch,roll)`가 **각 스피커 world 방향**을 head 프레임으로 회전 후
    `sofaCartesianFromHeadDir` 축매핑 → HRIR lookup (`KemarHrtfBinauralProcessor.cpp:24-72, 363-375`).
  비용 = O(min(활성스피커, 24)).

### 핵심 통찰 — 7-stage의 스테이지별 아키텍처 의존성
| stage | 성격 | mmhoa 현황 |
|---|---|---|
| 2.1 prefeed LP | **중립** (HRTF 입력 feed에 거는 one-pole; feed가 객체dry든 스피커든 무관) | 없음 → 신규(작음) |
| 2.2 FIR 랭킹 top-24 | **speaker-bus 전용** (128spk→24 비용제어) | 불필요(B1=O(obj) bounded, B2=고정24) |
| 2.3 HRTF + pan 폴백 | 양쪽 | B1 KdTree + B2 t-design VS 둘 다 존재 |
| 2.4 delay ring | **중립** (최종 L/R 버스 stereo delay) | 없음 → 신규(작음) |
| 2.5 5-band EQ | **중립** (최종 L/R 버스 EQ) | RoomBiquad TDF-II 재사용 + peak form 추가 |
| 2.6 headtracking | 양쪽 (lookup 전 방향 회전) | 없음 → 신규(L/R critical) |

즉 **2.1/2.4/2.5/2.6은 HRTF 코어를 감싸는 wrapper**이고 `binaural_l_buf_/r_buf_`에 거는 공유 post-chain으로
들어간다(삽입점 `SpatialEngine.cpp:1786` render_branch 직후, limiter `:1793` 직전). 진짜 speaker-bus-specific은
2.2 하나뿐인데 그건 mmhoa가 풀 필요 없는 문제다.

### 의미론 차이(정직한 antithesis) — Option A가 버리는 것
B1은 **객체-직결** HRTF(VBAP 패닝을 거치지 않음)이고, 레퍼런스는 **물리 스피커 믹스를 헤드폰으로 audition**하는
*모니터링* 경로다. "실제 라우드스피커에서 나올 믹스(=VBAP 패닝 아티팩트 포함)를 헤드폰으로 확인" 이 **명시적 spec
intent**라면 B1은 의미가 다르다. 그러나 **권위 계약은 xlsx-06 + `SpatialSessionState` pull 필드**(아래 §Headtracking
표의 멤버들)이고, 그 필드는 전부 B1/B2 위에서 전달 가능하다. 그리고 "스피커-믹스 모니터링"이 진짜 필요해지면
B2의 `decode→VS→HRIR` 기계를 `anyBus→per-channel-HRIR`로 일반화(=물리 스피커 버스를 VS 대신 입력)하는 적은
확장으로 도달 가능하다 — 새 병렬 그래프가 아니다. 이것이 §Recommendation의 C 탈출구다.

---

## Root Cause — 왜 "근본적으로 다름"이 결정 난제가 아닌가
플랜의 "per-object vs speaker-bus 근본적 차이"는 **HRTF 입력 도메인**의 차이일 뿐(객체 dry vs 스피커 feed)이고,
7-stage의 대부분(LP/delay/EQ/headtracking)은 입력 도메인과 **무관**하다. mmhoa는 이미 두 HRTF 도메인(B1 객체,
B2 가상스피커)을 갖고 있으므로, "없는 것"은 **wrapper 스테이지 4개 + 랭킹 1개**뿐이다. 랭킹은 mmhoa 비용모델에
unmotivated. 따라서 결정은 "어느 그래프를 쓰나"가 아니라 "검증된 자산 위에 중립 스테이지를 얹고, 불필요한
speaker-bus 비용제어는 명시적으로 생략한다"이다.

---

## Recommendation — **Option A** (+ C 탈출구 문서화)

**A 채택**: B1/B2 유지 + 중립 7-stage(2.1/2.4/2.5/2.6)를 `binaural_*_buf_` 공유 post-chain으로 추가.
FIR 랭킹(2.2)은 **구현하지 않고 근거 문서화**(per-object/B2 비용 이미 bounded). HRTF(2.3) 기존 자산 사용,
BinauralPan 등파워 폴백은 선택적 폴리시(미선택 객체가 없으므로 B1에선 불요).

**B 기각**, **C는 미래 탈출구로만 유지**.

| Option | Pros | Cons |
|---|---|---|
| **A 확장(권고)** | 1870L 검증 RT-safe 자산 재사용; JUCE-free 유지; 새 그래프 0; 증분 최소; **L/R 리스크 최저**(B1 좌표관례 이미 골든잠금 `BinauralMonitor.h:40-50`, `Coords.h:82-113`); xlsx-06 *계약*(delay/EQ/headtracking/gain) 전부 전달 | 문자 그대로의 "speaker-bus 모니터" 경로 아님; FIR 랭킹 생략(=spec 항목 2.2 미구현, 근거필요); B2 헤드트래킹은 SH 회전 필요(아래 리스크) |
| **B speaker-bus 병렬 이식** | spec 문자 충실("라우드스피커 믹스 audition") | 거대; mysofa-FIR-랭킹 JUCE-free 재이식; HRTF 인프라(OlaConvolver/KdTree3D/HrtfLookup) 중복 or 어색한 브리지; **새 좌표프레임 축매핑(`sofaCartesianFromHeadDir` ox=ny,oy=nz,oz=nx) = #1 버그류 L/R 신규 도입**; headless/무스피커 시 물리버스 무의미; mmhoa가 안 풀어도 되는 랭킹 재구현 |
| **C 하이브리드** | A를 베이스로, 진짜 speaker-mix 모니터 필요시 B2를 `anyBus→HRIR`로 일반화 | 지금은 YAGNI; A로 충분하면 불요. **미래 탈출구로만 보유** |

### DoD 재정의 (speaker-bus 프레이밍 → mmhoa 비용모델)
플랜 DoD "HRTF 24-spk 예산 8-12%"는 speaker-bus 프레이밍이다. A에서의 등가 게이트:
- B2 24-VS 경로 예산(이미 존재, throughput probe `runThroughputProbe` 기준) ≤ 스펙 봉투.
- B1 활성-객체 HRTF 예산: 32obj 기준 측정(Phase 1.4 harness 패턴 재사용 `perf_speaker_sweep.cpp`).
- 헤드 회전 시 정위 이동 스모크(실바이너리, 아래 골든).

---

## Increment Decomposition (core-first, 룸엔진 분할 패턴: core→wire→golden/smoke)

> 각 증분 = 구현 → 단위 + no-alloc → 실바이너리 스모크 → 리뷰/검증 → 커밋 → 푸시 → durable.
> 순서 근거: L/R-critical & 자립적인 헤드회전을 **순수함수 코어로 먼저 잠그고**, 그 위에 OSC/wiring,
> 그 다음 중립 DSP 스테이지(작은→큰). 랭킹/폴백은 마지막(A에선 대부분 no-op 문서화).

### ✅ FIRST — **2.6a 헤드회전 코어** (Coords.h 순수함수 + 골든) ★다음 세션 실행
- **무엇**: `Coords.h`에 `rotate_engine_dir_by_head(az,el, yawRad,pitchRad,rollRad) -> (az',el')` 추가.
  엔진프레임 단위벡터 `{cosEl·sinAz(우), sinEl(상), cosEl·cosAz(전)}`(=`pipeline_dir_to_ported` *이전* 프레임)
  → head 회전행렬 적용 → `atan2`로 (az',el') 복원.
- **재사용**: `Coords.h:104-113`(방향벡터 생성), `CoordsTests.h`(기존 골든 하니스).
- **RT 제약**: 순수 float, alloc 0, 오디오스레드 안전(단 이 증분은 오디오스레드 미접촉).
- **골든**(L/R 잠금, §Headtracking 참조): yaw+30° + 정면객체(az=0) → az' 부호 = **레퍼런스 net behavior와 일치**
  (head yaw +30 → 정위 **우측**, R-louder). roll/pitch 0일 때 yaw-only는 가법형 `az'=az+yaw`로 환원됨을 단위검증.
- **왜 first**: 가장 자립적(오디오/OSC/세션 무의존) + #1 버그류(L/R)를 격리된 순수함수에서 먼저 확정.

### 2.6b — /ypr OSC + 세션 head 멤버 + B1 wiring (헤드트래킹 end-to-end)
- **무엇**: 신규 `CommandTag::SysHeadYpr = 0x1C`, `PayloadSysHeadYpr{float yaw,pitch,roll}`(`Command.h:52` 뒤),
  `/ypr ,fff`(+ alias `/sys/ypr`) 디코드(`CommandDecoder.cpp` /sys 블록, `aed` 디코드 `:233-237` 패턴 복제),
  엔진 atomic head 멤버 3개, `SpatialEngine.cpp:1718` `setDirection(i, c.az, c.el)` **직전** 회전 적용:
  `auto [az_h, el_h] = rotate_engine_dir_by_head(c.az, c.el, yaw,pitch,roll); setDirection(i, az_h, el_h);`
- **재사용**: `/obj/aed ,fff` 디코드 패턴, Command FIFO, 기존 atomic 멤버 스타일.
- **RT 제약**: head 멤버 = `std::atomic<float>×3` relaxed, audioBlock에서 블록당 1회 read. 3-atomic 미세 tearing은
  지각상 무의미 + 다음 블록 수렴(엔진 기존 관례와 동일); 원하면 generation seqlock으로 강화 가능(YAGNI).
  회전은 setDirection 경로(KdTree lookup 전) — 추가 alloc 0.
- **스모크**(실바이너리): `/ypr 30 0 0` → 정면객체 → R-louder 비율 측정(`smoke_adm_az.py` 패턴 재사용).

### 2.1 — prefeed LP 4200Hz (HRTF 입력 one-pole)
- **무엇**: B1은 객체 dry(`dry_scratch_[i]`)에, B2는 per-VS 입력(`vs_buf_`)에 1차 LP. 계수
  `a = 1 - exp(-2π·4200/sr)` (`BinauralMonitorChain.cpp:107-109`). 상태 = `feedLp[MAX_OBJECTS]`(B1) /
  `feedLp[24]`(B2), `initialize()`에서 0 초기화.
- **재사용**: RoomBiquad는 불필요(1차 one-pole로 충분); `Constants.h` `kBinauralPrefeedLowPassHz=4200` 노출.
- **RT 제약**: 상태배열 prepare에서 할당, processBlock에서 alloc 0. no-alloc 단위테스트.

### 2.4 — delay ring (stereo 모니터 지연)
- **무엇**: `binaural_l_buf_/r_buf_`에 거는 65536-tap stereo ring, tap=`binauralDelayMs`
  (`BinauralMonitorChain.cpp:132-154` 그대로, JUCE 제거). `initialize()`에서 `std::vector` 할당(컨트롤스레드).
- **재사용**: 기존 `binaural_*_buf_` 파이프라인(`SpatialEngine.cpp:1786`); 삽입점 = render_branch 직후/limiter 직전.
- **RT 제약**: ring 사전할당, modulo 인덱싱 alloc 0.

### 2.5 — 5-band peak EQ (L/R 공유 coeff 캐시)
- **무엇**: `kBinauralEqBands=5` peak biquad ×2(L/R), coeff 캐시(freq/gain/Q 변경시만 재계산
  `BinauralMonitorChain.cpp:156-185`). freq/gain/Q 기본값 = 레퍼런스
  `{120,400,1250,4000,12000}` / Q `{1,1,1,1,1}` (`SpatialSessionState.h:559-561`).
- **재사용**: **`RoomBiquad`(`render/ported/RoomBiquad.h`) TDF-II `processSample` 재사용** — 단 RoomBiquad는
  LP/HP만 있으므로 **RBJ peak form `setPeak(sr, f, Q, gainDb)` 추가**가 필요(JUCE `makePeakFilter` 등가식,
  byte-faithful). state(s0/s1)는 그대로. → biquad 코어 1메서드 확장 + 5밴드 인스턴스화.
- **RT 제약**: coeff 재계산은 dirty일 때만(컨트롤/오디오 어디든 alloc 0), processSample alloc 0.

### 2.2 — FIR 에너지 랭킹  →  **Option A에선 미구현(근거 문서화)**
- per-object(B1)는 활성객체로, B2는 고정 24로 이미 bounded → 128→24 랭킹은 unmotivated.
  플랜 항목으로 남기되 "A 비용모델에선 N/A; speaker-bus 모니터(C)가 도입될 때만 필요"로 PROVENANCE/CH7에 기록.
- **(C 도입 시에만)** `KemarHrtfBinauralProcessor.cpp:314-346` 랭킹 로직 이식 + `firRankEnergyLp` 상태.

### 2.3 — BinauralPan 등파워 폴백  →  **선택(폴리시)**
- B1엔 "미선택 객체"가 없어 불요. B2/C 도입 시 미선택 VS/스피커용으로 `BinauralPan.cpp` 등파워식 이식
  (`gL=cos θ, gR=sin θ, θ=π/8+az/2, elevGain`). 저우선.

### 세션상태/상수 (각 증분에 흡수 or 선행 0번)
- mmhoa 세션상태에 레퍼런스 계약 멤버 미러: `binauralEqFreqHz[5]/GainDb[5]/Q[5]`, `binauralDelayMs`,
  `binauralMonitorGainLinear`, `headYaw/Pitch/RollDeg` (`SpatialSessionState.h:540-564`). scene 직렬화 영향 점검.

**증분 순서**: **2.6a → 2.6b → 2.1 → 2.4 → 2.5** → (2.2 문서화) → (2.3 선택). 헤드트래킹을 먼저 끝내는 이유:
가장 자립적 + L/R 골든을 조기 확정 → 이후 DSP 스테이지는 순수 신호처리(L/R 무관)라 저위험.

---

## Headtracking (2.6) — /ypr 정밀 설계

### OSC 명령
- **주소**: `/ypr ,fff`  (yaw_deg, pitch_deg, roll_deg) — 헤드트래커 직결용 최단 경로.
  **alias** `/sys/ypr ,fff` (기존 `/sys/*` 네임스페이스 일관성, peer validation 동일적용).
- **디코드**(`CommandDecoder.cpp`): `aed`(`:233-237`) 패턴. deg→rad는 **세션 저장은 deg**(레퍼런스 멤버가 deg,
  `headYawDeg`), 회전 적용 시 rad 변환. 누락 인자는 0.
- **CommandTag**: `SysHeadYpr = 0x1C` (`Command.h:52` 뒤), `PayloadSysHeadYpr{float yaw,pitch,roll}`.

### 세션상태 head 멤버 (엔진)
- `std::atomic<float> head_yaw_deg_{0}, head_pitch_deg_{0}, head_roll_deg_{0};` (relaxed).
- 명령 핸들러(`SpatialEngine.cpp:234-285` /sys 블록): 3 store(relaxed).
- audioBlock 바이노럴 진입(`:1610` 부근): 블록당 1회 load → rad 변환 → render_branch에 전달.

### 회전 지점 + 관례 (★L/R 비반전 명세)
- **지점**: **B1** = `SpatialEngine.cpp:1718` `setDirection` **직전** (객체별 az/el 회전). KdTree lookup이
  회전된 방향을 받으므로 추가비용 0. **B2 헤드트래킹은 1차 범위서 제외**(아래 리스크 — SH 회전 필요).
- **프레임**: 엔진 az=atan2(x,z) **RIGHT=+az**, el UP=+ (`Coords.h:1-16`, `BinauralMonitor.h:40-50`).
- **수학** (`rotate_engine_dir_by_head`, 엔진 Cartesian x=우 y=상 z=전):
  1. `d = {cosEl·sinAz, sinEl, cosEl·cosAz}` (world 방향).
  2. head 회전행렬 `R(yaw about +Y, pitch about +X, roll about +Z)`의 **역**을 d에 적용:
     `d_head = R_head^{-1} · d` (= world 방향을 head 프레임으로).
  3. `az' = atan2(d_head.x, d_head.z)`, `el' = asin(clamp(d_head.y, -1, 1))`.
  - **yaw-only 환원**(골든 타깃): `az' = az + yaw`, `el'=el`.
- **부호 잠금 (CRITICAL — 맹목 금지)**: 레퍼런스 net behavior 측정 결과 **headYaw +30° → 정위 우측(+az,
  R-louder)** (`KemarHrtfBinauralProcessor.cpp` worldDirToHead(-yaw) → azimuth=+yaw → `BinauralPan.cpp:87`
  θ=π/8+az/2 증가 → gR↑). 외부 헤드트래커/vid2spatial 계약 일치를 위해 **mmhoa도 동일 net**:
  `az' = az + yaw` 채택. → **골든**: `/ypr 30 0 0` + 정면객체(az=0) ⇒ **az'=+30° ⇒ R-louder**
  (head yaw +X → image shifts **RIGHT**). 
  > 골든 실패 시 **yaw 부호만 뒤집고 downstream az는 절대 건드리지 말 것**(2026-03-01 stereo_pan,
  > Phase 3.1 −az 교훈 — 부호 수정은 단일 권위지점에서만).
- **골든 테스트**: (a) Coords 단위(2.6a) `rotate_engine_dir_by_head(0,0, +30°,0,0).az ≈ +30°`;
  (b) 실엔진 in-proc 골든(`test_convergence_adm_az_golden` 패턴) `/ypr 30 0 0` + 정면객체 → 우반구 우세;
  (c) 스모크 실바이너리(`smoke_adm_az.py` 패턴) R/L 비율 > 1.

### RT-safety 요약 (headtracking)
- 회전: 순수 float, alloc 0, 오디오스레드 안전.
- head 멤버 read: 블록당 1회, atomic relaxed. tearing benign(다음 블록 수렴). seqlock은 YAGNI.

---

## Risks

1. **L/R / yaw 부호 (P0, #1 버그류)** — 회전 부호를 레퍼런스 net behavior에 맞춰 골든으로 잠근다.
   2.6a를 first로 두는 이유. 완화: 부호는 `rotate_engine_dir_by_head` 단일지점에만, downstream az 불변.
2. **B2 헤드트래킹** — B2 VS HRIR은 고정 t-design 점에 **사전캐시**(`vs_hrir_L_/R_`)되어 블록당 방향회전 시
   재lookup 불가. 올바른 해법 = decode 전 **SH 소프트필드 회전(Wigner-D)**인데 mmhoa에 미존재.
   완화: **1차 범위는 B1-only 헤드트래킹**(레퍼런스도 per-direction lookup = B1류). B2 SH 회전은 별도 증분/v1.1.
   문서화: "/ypr는 B1(Direct)에서 동작; B2(AmbiVS)는 v1.1 SH-rotation."
3. **prefeed LP 도메인 차이** — 레퍼런스 LP는 *스피커 feed* VBAP 급변 완화용. B1 객체 dry엔 VBAP 급변이 없어
   효과가 다르다(객체dry는 이미 매끈). 완화: B1에선 LP를 **선택적/약하게**(또는 setDirection crossfade가 이미
   급변완화 → LP 생략 정당화 가능). B2 VS feed엔 의미 있음. → 증분 2.1에서 도메인별 적용여부 명시.
4. **5-band EQ peak form 부재** — RoomBiquad는 LP/HP만. peak RBJ 계수식 신규(byte-faithful 검증 필요).
   완화: JUCE `makePeakFilter` 식 정확 이식 + coeff 단위비교.
5. **스펙 의미론(monitor vs object)** — Option A는 "라우드스피커 믹스 audition"이 아님. 사용자/스펙 intent가
   *물리 스피커 모니터링*을 요구하면 C(=B2 일반화 anyBus→HRIR)로 확장. 완화: 결정 전 intent 1줄 확인 권장.
6. **세션 직렬화** — head/binaural EQ 멤버 추가 시 scene save/load 스키마 영향(Phase 3.3b 이름영속과 유사).
   완화: SCHEMA_VERSION 점검, 기존 scene 하위호환(기본값=무회전/플랫EQ).

---

## Recommended FIRST increment (다음 세션)

**2.6a — 헤드회전 코어 (Coords.h 순수함수 + 골든)**
- `Coords.h`에 `rotate_engine_dir_by_head(az,el,yaw,pitch,roll)` 추가(오디오/OSC/세션 무의존).
- 단위 골든: yaw+30° + 정면 → az'=+30°(R 방향); yaw-only=가법형; roll/pitch 0 항등; finite.
- 산출: L/R 부호가 격리된 순수함수에서 확정됨 → 이후 2.6b wiring은 "이미 검증된 회전 호출"만.
- DoD: `test_convergence_head_rotate` green, 양 빌드 회귀 0.

이후: 2.6b(/ypr+wire+실엔진 골든/스모크) → 2.1 → 2.4 → 2.5 → (2.2 문서화) → (2.3 선택).

---

## References
- `core/src/output_backend/BinauralMonitor.h:1-50` — B1 좌표관례(RIGHT=+az) 잠금 + ITD 설명.
- `core/src/output_backend/BinauralMonitor.h:479-509` — B2 processBlockB2 = 24 VS→HRIR(레퍼런스 가상스피커 등가물).
- `core/src/core/SpatialEngine.cpp:1709-1734` — B1 per-object setDirection/processBlockForObject 루프(회전 삽입점 :1718).
- `core/src/core/SpatialEngine.cpp:1786-1798` — render_branch 직후/limiter 직전 = 중립 post-chain(2.1/2.4/2.5) 삽입점.
- `core/src/coords/Coords.h:82-113` — mmhoa↔ported 프레임 어댑터 + pipeline_dir_to_ported(회전벡터 기반).
- `core/src/render/ported/RoomBiquad.h:40-71` — TDF-II processSample 재사용 코어(2.5; peak form 추가 필요).
- `core/src/ipc/CommandDecoder.cpp:226-264` — /obj/aed ,fff 디코드 패턴(/ypr 복제 기준).
- `core/src/ipc/Command.h:41-53,406-433` — CommandTag/Payload 추가 지점(SysHeadYpr=0x1C).
- `dreamscape_references/.../BinauralMonitorChain.cpp:74-227` — 7-stage 레퍼런스 전체.
- `dreamscape_references/.../KemarHrtfBinauralProcessor.cpp:24-72` — worldDirToHead/sofaCartesianFromHeadDir(회전·축매핑).
- `dreamscape_references/.../KemarHrtfBinauralProcessor.cpp:314-346` — FIR 에너지 랭킹(Option A 미구현 근거 대상).
- `dreamscape_references/.../BinauralPan.cpp:48-118` — 등파워 패닝 폴백(2.3 선택).
- `dreamscape_references/.../AudioRenderQuality.h:22-33` — kBinauralFirMaxSpeakersPerBlock=24, prefeed 4200, α/decay.
- `dreamscape_references/.../SpatialSessionState.h:540-564` — binaural/head 멤버 계약(미러 대상).
