# Plan: spatial_engine Phase A — 시중 엔진 대비 기능 패리티 (필수 6항목)

## Header
- **Project**: `spatial_engine`
- **Plan ID**: `spatial-engine-phaseA-feature-parity`
- **Date**: 2026-05-02
- **Status**: `READY-FOR-AUTOPILOT`
- **Predecessor**: `.omc/plans/spatial-engine-v0.md` (P0~P12 + v0e 통합 회로 완료, ctest 34/34, pytest 149)
- **Reference research**: `.omc/research/spatial_engine_market_audit.md` (L-ISA / d&b Soundscape / Meyer / Flux / Panoramix 비교)
- **Mode**: SHORT (각 항목 self-contained, 헤드리스 CI 게이트, RT-safe)

---

## 배경

`spatial_engine` v0/v1/v0e 통합 회로 완료 시점 기준으로 시중 spatial audio 엔진(L-ISA, d&b Soundscape, Spat Revolution, Panoramix, Spacemap Go)과 비교했을 때 다음 6개 항목이 **상용 진입 시 즉시 부재가 느껴지는 MUST-HAVE 갭**으로 식별됨:

| # | 갭 | 어느 엔진? | 추정 |
|---|---|---|---|
| M1 | Source WIDTH/Spread 파라미터 | L-ISA 4대 파라미터 | 1주 |
| M2 | HOA Decoder (B-format → 스피커) | Spat Rev (5종 디코더), Panoramix | 1주 |
| M3 | IR Convolution Reverb 완성 | d&b En-Space, Spat Rev | 3-4일 |
| M6 | Speaker Time-Alignment | L-ISA 핵심, Meyer GALAXY | 1주 |
| M8 | Object Trajectory Animation | L-ISA, d&b, Spat Rev | 3-5일 |
| M9 | Per-zone Limiter / Output Trim | L-ISA, d&b | 3-5일 |

(M4 Snapshot Crossfade, M5 VST3 플러그인, M7 MTC/LTC 동기는 Phase B/C 로 이연.)

---

## RALPLAN-DR Summary

### Principles (5)
1. **No regression**: ctest 34/34 + pytest 149 + 3 skipped 기준선 유지.
2. **RT-safe**: audio thread 에 alloc/lock/syscall 없음. 모든 신규 모듈은 `prepareToPlay`에서 사전 할당.
3. **Headless CI**: 모든 테스트는 `SPATIAL_ENGINE_NO_JUCE=ON` + headless pytest 조건에서 통과.
4. **OSC 일관성**: 신규 명령은 `Command.h` 스키마 + `CommandDecoder.cpp` 디코더/인코더 양방향 + `test_p4_command_decode.cpp` round-trip 게이트.
5. **단일 책임 커밋**: 각 M 항목이 독립 커밋. 하나 실패해도 나머지 진행 가능.

### Decision Drivers
1. **차별화·필수**: 모든 6개 항목이 "있는 게 표준, 없으면 회피 불가" 기능.
2. **재사용**: 기존 모듈(`OlaConvolver`, `AmbisonicEncoder`, `LayoutLoader`, `GainRamp`) 최대 재사용.
3. **Per-Phase 평가**: 각 항목 종료 시 ctest+pytest 통과 + 정량 게이트 + 리뷰.

### Execution Order
1. M3 (IR Conv) — Easy, 큰 ROI, 단일 모듈
2. M2 (HOA Decoder) — Medium, 인코더 재사용
3. M1 (Source Width) — Medium, OSC 추가 + per-algo 변경
4. M6 (Time-Alignment) — Medium, LayoutLoader 확장
5. M8 (Trajectory) — Easy, UI 측 자동화
6. M9 (Per-zone Limiter) — Easy, 출력 매트릭스

각 항목 후 build + test + 정량 게이트 → 모두 통과 시 단일 커밋. 6개 모두 완료 후 통합 회귀 + 평가 보고.

---

## Per-Item Specification

### M3 — IR Convolution Reverb 완성 (3-4일)

**현재 상태**: `core/src/reverb/IRConvolutionStub.{h,cpp}` 인터페이스 슬롯만, `ReverbEngine` 구현체 없음. `OlaConvolver` 는 `BinauralMonitor` 에서 사용 중.

**목표**: `IRConvReverb` 클래스를 `ReverbEngine` 인터페이스의 구현체로 추가하여 `SpatialEngine::audioBlock` 의 reverb send 경로에 dropable 하게 만들기.

**Files**:
- `core/src/reverb/IRConvReverb.{h,cpp}` (new) — `ReverbEngine` 상속, `OlaConvolver` 재사용 (mono → stereo or mono mix)
- `core/src/reverb/ReverbEngine.h` — 가상 인터페이스 확인 (process / getLatencySamples / setMix 등)
- `core/src/core/SpatialEngine.{h,cpp}` — FdnReverb / IRConvReverb 선택 로직 (atomic flag), `/reverb/{type}` OSC
- `core/tests/core_unit/test_p_ir_reverb.cpp` (new) — IR 로딩 + process + tail decay 검증
- `core/CMakeLists.txt` — IRConvReverb 등록

**Acceptance**:
- ctest 35/35 PASS (+1 = `p_ir_reverb`)
- IR 파일(.wav 단순 mono IR 또는 .speh) 로드 → process → output non-zero
- FdnReverb ↔ IRConvReverb 런타임 전환 시 click 없음
- `/reverb/select ,s "fdn"|"ir"` OSC 명령

---

### M2 — HOA Decoder (1주)

**현재 상태**: `AmbisonicEncoder` 1차/2차/3차만, B-format 시그널을 스피커로 디코드하는 클래스 없음.

**목표**: `AllRADDecoder` (or projection-based) 클래스 추가. 임의 layout 에 대해 디코더 행렬 생성 → audio thread 에서 행렬 곱.

**Files**:
- `core/src/ambi/AmbiDecoder.{h,cpp}` (new) — Mode-matching projection decoder (간단·정확), AllRAD 옵션
- `core/src/render/RenderingAlgorithm.h` — Ambisonic algorithm 추가 (Algorithm::Ambisonic)
- `core/src/render/AmbisonicRenderer.{h,cpp}` (new) — 인코더+디코더 chain, RenderingAlgorithm 인터페이스
- `core/src/core/SpatialEngine.cpp` — Ambisonic algorithm dispatch 추가
- `core/src/ipc/Command.h` — `Algorithm::Ambisonic = 3`
- `core/tests/core_unit/test_p_ambi_decoder.cpp` (new) — 단일 위치 인코딩 → 디코딩 → 해당 위치 스피커가 dominant 검증
- `ui/spatial_engine_ui/views/object_panels.py` — algorithm 콤보에 "Ambisonic" 추가

**Acceptance**:
- ctest 36/36 PASS
- Mode-matching: az=0, el=0 → +z 방향 스피커가 max gain (해당 layout 기준)
- 1차 디코더 (W,X,Y,Z 4ch input) → max layout speaker count 출력

---

### M1 — Source WIDTH/Spread (1주)

**현재 상태**: 점음원 가정만, 음원의 가상 폭(angular spread) 없음.

**목표**: per-object `width_deg` 파라미터 (0..180°). 각 알고리즘에서:
- VBAP/DBAP: 인접 스피커 fan-out (width 비례 가중)
- WFS: 인접 secondary source 활성화
- HOA: max-rE 가중 변환

**Files**:
- `core/src/render/RenderingAlgorithm.h` — `ObjectState.width_rad` 필드 추가
- `core/src/core/SpatialEngine.{h,cpp}` — `ObjCache.width = 0.f`, `/obj/dsp param=7` width 처리
- `core/src/render/VBAPRenderer.cpp` — width > 0 시 인접 스피커 추가 가중
- `core/src/render/DBAPRenderer.cpp` — width 비례 distance shift
- `core/src/render/WFSRenderer.cpp` — width 비례 secondary source 활성화 폭
- `core/src/ipc/Command.h` — `PayloadObjDsp::Param::Width = 7`
- `ui/spatial_engine_ui/views/object_panels.py` — Width 슬라이더 추가
- `ui/spatial_engine_ui/ipc/protocol.py` — `DSP_PARAM_WIDTH = 7`
- `core/tests/core_unit/test_p_source_width.cpp` (new) — width=0 (점음원) vs width=90 (분산) 게인 분포 차이 검증

**Acceptance**:
- ctest 37/37 PASS
- width=0: 기존 동작 그대로
- width=90: 인접 ≥3 스피커에 게인 분배

---

### M6 — Speaker Time-Alignment (1주)

**현재 상태**: `LayoutLoader` 가 (x,y,z) 좌표만 로드. per-speaker delay/level 필드 없음.

**목표**: YAML 에 `delay_ms` / `gain_db` 필드 추가, 출력 매트릭스에서 per-channel 적용.

**Files**:
- `configs/lab_8ch.yaml` — 각 speaker 에 `delay_ms` / `gain_db` 옵션 필드 추가 (default 0)
- `core/src/geometry/SpeakerLayout.{h,cpp}` — Speaker struct 확장
- `core/src/geometry/LayoutLoader.cpp` — YAML 파싱 확장
- `core/src/core/SpatialEngine.{h,cpp}` — 출력 단계에서 per-speaker DelayLine + 게인 적용
- `core/tests/core_unit/test_p_speaker_alignment.cpp` (new) — delay 적용 시 출력 시간 차 검증

**Acceptance**:
- ctest 38/38 PASS
- delay_ms=10 설정한 채널 → 입력 대비 480 sample (48kHz) 지연 출력

---

### M8 — Object Trajectory Animation (3-5일)

**현재 상태**: UI 드래그/외부 OSC 만 위치 변경. 자동 궤적 없음.

**목표**: UI 측 LFO + 경로 보간 → /obj/move 자동 송신. 엔진 변경 없음 (UI/server 작업).

**Files**:
- `ui/spatial_engine_ui/state/trajectory.py` (new) — Circle/Line/Custom path 데이터 클래스
- `ui/spatial_engine_ui/controllers/trajectory_runner.py` (new) — QTimer 60Hz 보간 → osc_client.send_object_pos
- `ui/webgui/server.py` — `/api/trajectory/{start|stop}` 엔드포인트 (서버 측 trajectory runner)
- `ui/webgui/static/index.html` + `canvas.js` — Trajectory 패널 (Circle/Line preset + speed)
- `ui/tests/test_trajectory.py` (new) — 보간 함수 unit test
- `ui/webgui/tests/test_trajectory_api.py` (new) — start/stop 엔드포인트

**Acceptance**:
- pytest +N tests PASS
- Circle trajectory speed=0.5Hz → 2초 주기로 az 한바퀴

---

### M9 — Per-zone Limiter / Output Trim (3-5일)

**현재 상태**: 채널별 noise gain 외 출력 단 처리 없음.

**목표**: per-channel `gain_db` + soft limiter (-3 dB threshold, 100ms release) OSC 제어.

**Files**:
- `core/src/dsp/ChannelLimiter.h` (new) — 1-pole envelope follower + soft knee limiter
- `core/src/core/SpatialEngine.{h,cpp}` — 출력 단계에서 per-channel limiter 적용
- `core/src/ipc/Command.h` — `OutputGain` / `OutputLimit` 새 태그
- `core/src/ipc/CommandDecoder.cpp` — `/output/{ch}/{gain|limit}` OSC
- `core/tests/core_unit/test_p_channel_limiter.cpp` (new) — 게인 step + clipping 방지 검증
- `ui/spatial_engine_ui/views/output_panel.py` (new) — per-channel gain 슬라이더
- `ui/webgui/static/index.html` — Output 패널 minimal

**Acceptance**:
- ctest 39/39 PASS
- /output/0/gain -6dB → 채널 0 출력 -6 dB linear scaled
- /output/0/limit -3dB → > 0.708 lin 입력 시 압축

---

## Test Gates (after each M)

| Step | ctest | pytest | 정량 게이트 |
|---|---|---|---|
| baseline | 34/34 | 149 passed, 3 skipped | — |
| M3 | 35/35 | unchanged or +N | IR convolve output non-zero, click-free FDN↔IR transition |
| M2 | 36/36 | +N | encode→decode max-gain speaker direction match (az=0 → +z spk) |
| M1 | 37/37 | +N | width=90 → ≥3 nonzero gains; width=0 baseline preserved |
| M6 | 38/38 | +N | delay_ms=10 → 480-sample latency on that channel |
| M8 | 34/34 | +N (UI) | trajectory speed → period 정확 |
| M9 | 39/39 | +N | gain -6dB linear, limiter compression > threshold |

---

## RT 안전성 게이트
- `test_p1_rt_no_alloc` 모든 단계 후 통과
- 새 클래스의 `prepareToPlay` 에서 모든 버퍼 사전 할당
- audio thread 의 setParam 은 atomic store 만, no allocator interaction

---

## 평가 (Phase A 종료 후)

1. **6개 항목 전부 완료**: ctest 39/39 + pytest +N
2. **시중 엔진 대비 갭 재평가**: 
   - L-ISA WIDTH/PAN/DISTANCE/ELEVATION 4대 파라미터 → 우리 EQ4밴드 + Width + Distance + Elevation 으로 비교 우위
   - d&b En-Space → IR convolution 기능 패리티 (실측 IR 미보유는 별개)
   - Spat Rev 5종 디코더 → AllRAD 1종 + 인코더 4종 (1-3차) 보유
3. **다음 단계 식별**: Phase B 항목 우선순위 재확정 (M4/M5/M7 + S1/S2)

---

## ADR

**Decision**: 6개 항목을 위 순서로 단일 Phase 에서 모두 진행. 각 항목은 독립 커밋, 통합 회귀 게이트.

**Drivers**: 시중 엔진과의 정량적 패리티 확보 (8주 안), 단일 plan-state 로 progress 추적, RT 안전성 불가침.

**Alternatives**:
- (A) 항목별 분할 Phase: 가시성 향상하나 세션 컨텍스트 손실 비용 큼 → 기각
- (B) M3/M2 만 우선 + 나머지 보류: 차별화 격차 잔존 → 기각
- (C) 본 통합 진행: 채택

**Consequences**:
- ctest 34 → 39 (+5), 새 OSC 태그 5개, 새 UI 패널 2개
- IR Conv 기능으로 v1+ ADR 의 ReverbEngine 슬롯 약속 이행
- HOA Decoder 로 Ambisonic full chain (encoder+decoder) 가용

---

## Success Criteria

- [ ] 6개 항목 모두 단독 커밋
- [ ] ctest 39/39 PASS
- [ ] pytest 149 + 신규 테스트 PASS
- [ ] `test_p1_rt_no_alloc` PASS
- [ ] README v1f 갱신 (Phase A 완료 마킹)
- [ ] `.omc/research/spatial_engine_market_audit.md` 갱신 (격차 표 업데이트)
- [ ] 평가 리포트 `.omc/plans/spatial-engine-phaseA-evaluation.md` 작성
