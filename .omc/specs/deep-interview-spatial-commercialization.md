# Deep Interview Spec: 국산 공간음향 엔진 상용화 로드맵

## Metadata
- Interview ID: spatial-commercialization-2026
- Rounds: 12
- Final Ambiguity Score: 10.7%
- Type: brownfield
- Generated: 2026-05-02
- Threshold: 20%
- Initial Context Summarized: no
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.94 | 35% | 0.329 |
| Constraint Clarity | 0.90 | 25% | 0.225 |
| Success Criteria | 0.83 | 25% | 0.208 |
| Context Clarity | 0.88 | 15% | 0.132 |
| **Total Clarity** | | | **0.893** |
| **Ambiguity** | | | **10.7%** |

---

## Goal

**한국 최초 국산 공간음향 엔진을 6~12개월 안에 첫 유료 계약으로 상용화한다.**

현재 한국 공연장 시장은 L-ISA(L-Acoustics)와 d&b Soundscape 등 고가 유럽산 시스템만 존재하며, **국산 대안이 전무한 블루오션**이다.

### 핵심 포지셔닝
- **국산 최초** — 기술 주권 + 국내 서비스 지원 강점
- **AI 자동화** — 카메라로 배우/악기 자동 추적, 수작업 1시간 → 5분
- **가격 경쟁력** — L-ISA 대비 1/3 이하 목표
- **이중 모드 아키텍처** — AI 오프라인 처리 모드 + 저레이턴시 RT 직접 모드

### 제품 로드맵
```
v1 MVP (6~12개월):  spatial_engine NO_JUCE + WebGUI + vid2spatial AI 추적
v2 (1~2년):         text2traj 자연어 궤적 생성 통합
v3 (2년+):          DAW 플러그인(VST/AU) + 글로벌 SDK + JUCE 도입
```

---

## Architecture: 이중 모드

### AI 오프라인 모드
```
카메라 입력 → vid2spatial (SAM2+YOLO) → OSC 브리지 → spatial_engine RT 코어
                                                        ↓
                                          실시간 위치 업데이트 (p99 < 100ms)
```
- vid2spatial 영상 분석이 비동기로 실행
- 분석 결과를 OSC UDP로 실시간 스트리밍
- 사람이 수동으로 position을 override 가능

### 저레이턴시 직접 모드 (AI 미사용)
```
OSC/MIDI 컨트롤러 → spatial_engine RT 코어 (p99 < 5ms)
                    64 오브젝트 동시, 48kHz/64-block
```
- AI 파이프라인 완전 우회
- 하드웨어 P10 실측 목표: OSC→Dante p99 < 5ms

---

## Constraints

| 항목 | 내용 |
|------|------|
| **팀** | 솔로 개발자 + 하드웨어 현장 테스트 협력 업체 |
| **타임라인** | 6~12개월 내 첫 유료 계약 |
| **JUCE 라이선스** | v1 NO_JUCE 배포 → 라이선스 비용 0원. v3 DAW 플러그인 시 도입. |
| **GPL v3** | NO_JUCE 빌드는 독립 실행형 → 상업 배포 가능 (GPL 동적 링크 회피 구조 확인 필요) |
| **첫 시장** | 한국 공연장/전시 → 글로벌은 v3 이후 |
| **예산** | 부트스트랩 — 첫 수익으로 운영 |

---

## Non-Goals (v1 제외)

- text2traj 자연어 궤적 (→ v2)
- DAW 플러그인 VST/AU (→ v3, JUCE 도입 시)
- 글로벌 SDK 배포 (→ v3)
- Dolby Atmos / DTS:X 포맷 인코더 (→ 미정)
- 방송/OTT 실시간 삽입 (→ v3+)
- 완전한 경쟁 기능 동등 (→ 킬러 피처로 차별화가 우선)

---

## Acceptance Criteria

### Phase 0: 블로커 해소 (0~4주)
- [ ] `SPATIAL_ENGINE_NO_JUCE=ON` 빌드로 상업 배포 GPL 준수 여부 법적 검토 완료
- [ ] 하드웨어 P10: OSC→Dante p99 < 5ms 실측 통과
- [ ] 하드웨어 P11: 30분 소크 xrun=0, RSS 증가 없음
- [ ] 협력 업체와 v1 출시 계획 합의

### Phase 1: WebGUI MVP (1~3개월)
- [ ] FastAPI + WebSocket + HTML5 Canvas WebGUI 구현
- [ ] 태블릿(iPad/Android)에서 64 오브젝트 실시간 드래그 조작
- [ ] WebGUI ↔ OSC 왕복 지연 p99 < 20ms
- [ ] 협력 업체 현장 설치 + 무장애 운영 48시간 이상

### Phase 2: vid2spatial 통합 (3~6개월)
- [ ] `vid2spatial_v2` OSC 브리지 구현 (영상 → 실시간 위치 업데이트)
- [ ] 단일 카메라로 최대 8개 오브젝트 동시 추적 (1.36° AzMAE 유지)
- [ ] AI 모드 ↔ 저레이턴시 직접 모드 스위치 < 500ms (무음 전환)
- [ ] 공연장 현장 라이브 데모 1회 성공 (파트너사 참관)

### Phase 3: v1 상용 출시 (6~12개월)
- [ ] 첫 번째 유료 계약 체결 (한국 공연장/전시 현장 1곳)
- [ ] 일회성 라이선스 + 연간 지원계약 계약서 완비
- [ ] 한국어 설치 가이드 + 운용 매뉴얼 완성
- [ ] 고객 현장 1주일 이상 무장애 운영

---

## Assumptions Exposed & Resolved

| 가정 | 검증 질문 | 결정 |
|------|----------|------|
| "모든 경쟁 엔진 기능 필요" | Contrarian — 킬러 피처 하나만으로 계약 가능? | **AI 자동 추적이 킬러. 기능 동등은 v3+** |
| "vid2spatial + text2traj 둘 다 v1에" | Simplifier — 최소 MVP? | **vid2spatial만 v1. text2traj는 v2** |
| "JUCE 라이선스 필수" | 코드베이스에 NO_JUCE 플래그 확인 | **v1 NO_JUCE 무료 배포. v3 DAW 시 JUCE 도입** |
| "글로벌 첫 출시" | 한국 시장 레퍼런스 가치 | **국내 레퍼런스 먼저 → 글로벌** |
| "국내 경쟁자 있음" | 한국 시장 조사 | **국산 엔진 전무 — 블루오션** |

---

## Technical Context (Brownfield)

### spatial_engine 현황 (v0.1.0)
```
core/
├── 렌더러: VBAP 3D / WFS / DBAP / Ambisonics 1~3차
├── 바이노럴: KEMAR HRTF OLA 컨볼버 (RT-safe, alloc-free)
├── 리버브: 16-line Hadamard FDN + IRConvolution 스텁
├── I/O: Dante PCIe 스텁, JACK 백엔드, NullBackend
├── IPC: OSC 9100(cmd)/9101(state), HeartbeatPublisher 10Hz
├── 테스트: 34 ctest 통과 (NO_JUCE+RT_ASSERTS)
└── NO_JUCE 빌드: 완전 지원, JUCE 의존성 제로
```

### vid2spatial_v2 통합 방안
- 출력: OSC UDP → `spatial_engine` 포트 9100 직접 전송 가능
- 형식: `/adm/obj/N/azim`, `/adm/obj/N/elev`, `/adm/obj/N/dist` (ADM-OSC 이미 구현됨)
- 지연: vid2spatial 처리 지연 ~50ms + OSC 전송 < 1ms → 총 < 100ms

### text2traj 통합 방안 (v2)
- 출력: 3D 궤적 → OSC 브리지 → spatial_engine
- 방식: 궤적 파일 로드 후 타임라인 재생 또는 실시간 생성

### 즉시 블로커
1. GPL v3 상업 배포 법적 검토 (NO_JUCE 독립형 배포 가능 여부)
2. P10/P11 하드웨어 실측 (레이턴시·소크)

---

## Competitive Positioning

| | spatial_engine (국산) | L-ISA | d&b Soundscape |
|--|----------------------|-------|----------------|
| **가격** | (미정, 목표 1/3) | $$$ | $$$ |
| **AI 자동 추적** | ✅ vid2spatial | ❌ | ❌ |
| **자연어 조작** | ✅ v2 text2traj | ❌ | ❌ |
| **국내 지원** | ✅ 직접 | 대리점 | 대리점 |
| **오픈 I/O** | OSC/Dante | 독점 | 독점 |
| **WebGUI** | ✅ | 별도 앱 | 별도 앱 |

---

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| SpatialEngine | core domain | version, objects, SR, NO_JUCE | 렌더러·I/O 포함 |
| AudioEngineer | customer | venue_type, workflow | LiveVenueSystem 운용 |
| LiveVenueSystem | core domain | hw_partner, mode | Engine + WebGUI + AITracking |
| WebGUI | supporting | latency_p99, tablet_support | OSC ↔ Engine |
| AIAutoTracking | supporting | AzMAE, max_objects, mode | vid2spatial → OSC → Engine |
| LowLatencyMode | supporting | p99_ms, bypass_ai | Engine 직접 모드 |
| HardwarePartner | external | dante_hw, field_test | 설치 + 유통 협력 |
| NLTrajectory | supporting | model, MAE | text2traj, v2 |
| KoreanVenueMarket | external | competitors(L-ISA/d&b), size | 블루오션, 첫 시장 |
| DAWPlugin | supporting | format(VST/AU) | v3, JUCE 도입 시 |

## Ontology Convergence

| Round | Entities | New | Stable | Ratio |
|-------|----------|-----|--------|-------|
| 1 | 4 | 4 | - | N/A |
| 2 | 5 | 1 | 3 | 80% |
| 3 | 6 | 1 | 5 | 83% |
| 4 | 6 | 0 | 6 | 100% |
| 5 | 8 | 2 | 6 | 75% |
| 6 | 9 | 1 | 8 | 89% |
| 7 | 9 | 0 | 9 | 100% |
| 8 | 9 | 0 | 9 | 100% |
| 9 | 10 | 1 | 9 | 90% |
| 10 | 11 | 1 | 10 | 91% |
| 11 | 11 | 0 | 11 | 100% |
| 12 | 11 | 0 | 11 | 100% ✅ |

---

## Interview Transcript

<details>
<summary>Full Q&A (12 rounds)</summary>

### R1: 제품 형태 → DAW/SDK/HW 모두 고려
### R2: 첫 고객 → 공연장 음향엔지니어
### R3: 팀/예산 → 솔로 + HW 파트너
### R4 [Contrarian]: 킬러 피처 → AI 추적 + 자연어 조작
### R5: v1 성공 기준 → 첫 유료 계약
### R6 [Simplifier]: MVP → spatial_engine + WebGUI + vid2spatial
### R7: 수익 모델 → 일회성 라이선스 + 지원 계약
### R8: 첫 시장 → 한국 먼저
### R9: [임계값 16.9% 달성]
### R10: 추가 scope — JUCE 대안, 오프라인 AI 모드 + 저레이턴시 모드
### R11: JUCE 결정 → NO_JUCE v1 (라이선스 비용 0원)
### R12: 국내 경쟁 → L-ISA/d&b만 있음, 국산 전무 (블루오션)

</details>
