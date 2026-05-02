# 국산 공간음향 엔진 상용화 로드맵 — 최종 구현 플랜

> 기반 스펙: `.omc/specs/deep-interview-spatial-commercialization.md` (모호성 10.7%)
> Architect v1/v2 + Critic REVISE 모두 반영: 최종본
> 작성일: 2026-05-02

---

## RALPLAN-DR Summary

### Principles
1. **블로커 우선** — GPL 법적 검토 완료 전 배포 진행 금지
2. **킬러 피처 조기 증명** — vid2spatial 스파이크를 Phase 0에 포함해 첫 고객 미팅 전 데모 가능
3. **RT 안전성 불가침** — 오디오 스레드 alloc·blocking 금지 (`SPE_RT_ASSERTS=ON` 유지)
4. **AI는 비동기 레이어** — vid2spatial OSC 브리지는 RT 코어와 프로세스 분리
5. **정량 게이트** — 각 Phase 종료는 측정 가능한 수치 기준으로만 판단

### Decision Drivers (Top 3)
1. **6~12개월 내 첫 유료 계약**
2. **NO_JUCE 배포** — `SPATIAL_ENGINE_NO_JUCE=ON`, 라이선스 비용 0원
3. **킬러 피처 조기 가시화** — Phase 0 스파이크로 데모 가능 상태 확보

### Viable Options

#### Option A: Phase 순차 + Phase 0 스파이크 (선택)
Phase 0에 vid2spatial→OSC 번역기 1주 스파이크 → Phase 1 WebGUI 집중 → Phase 2 프로덕션화

#### Option B: WebGUI + vid2spatial 병렬 개발
Phase 1부터 동시 개발 → **기각**: 솔로 개발자 집중도 분산, 양쪽 품질 저하 위험

**선택 근거:** Option A + 스파이크가 킬러 피처 증거를 Phase 0에서 확보하면서 집중도 보존.

---

## Requirements Summary

| 항목 | 내용 |
|------|------|
| 첫 배포 대상 | 한국 공연장 음향엔지니어 |
| MVP 스택 | spatial_engine NO_JUCE + WebGUI + vid2spatial OSC 브리지 |
| 타임라인 | Phase 0: 5주, P1: 9+2주, P2: 13+2주, P3: 23주 |
| 수익 모델 | 일회성 라이선스 + 연간 기술지원 계약 |
| 블로커 | GPL 검토 + HW P10/P11 실측 + vid2spatial 좌표 변환 계약 |
| 포지셔닝 | 국내 유일 국산 실시간 공간음향 엔진 (블루오션) |

---

## Critical Technical Contracts (Architect 발견)

### vid2spatial → spatial_engine ADM-OSC 변환 계약

`vid2spatial_v2/vid2spatial_pkg/osc_sender.py:24-31` 확인 결과:
- **포트**: vid2spatial=9000 → spatial_engine=9100 (변환 필수)
- **prefix**: `/vid2spatial` → `/adm/obj/N/` (변환 필수)

`vid2spatial_v2/vid2spatial_pkg/osc_sender.py:104-108` + `docs/coordinate_convention.md:13,30-31` 확인 결과:

| 항목 | vid2spatial 내부 | ADM-OSC 표준 | 변환 필요 |
|------|-----------------|-------------|----------|
| 방위각 부호 | RIGHT=+az (pipeline frame) | LEFT=+az (AmbiX) | `az_adm = -az_pipeline` |
| 거리 방향 | 1=near, 0=far (역방향) | 0=near, 1=far | `dist_adm = 1 - dist_v2s` |
| 포트 | 9000 | 9100 | 재구성 |
| prefix | `/vid2spatial` | `/adm/obj/N/` | 재구성 |

이 변환 계약은 `docs/adr/vid2spatial_osc_contract.md`에 버전화하여 관리.

### IIR 스무딩 한계 (알려진 제한)
- α=0.3 IIR + 60Hz 레이트 리밋: 빠른 팬 (>180°/s)에서 약 33ms 지연 발생
- 데모 및 공연 환경에서 허용 가능 (배우/악기 이동 속도 < 90°/s 일반적)
- `docs/known_limitations.md`에 문서화

---

## Acceptance Criteria (정량 게이트)

### Phase 0 — 블로커 해소 + 스파이크 (5주)

#### 법적·라이선스
- [ ] NO_JUCE 빌드 바이너리 상업 배포 GPL 준수 법적 의견서 수령
- [ ] 불가 판정 시: GPL 감염 소스 목록 + dual-license 전환 계획 수립

#### 하드웨어 실측
- [ ] `tests/latency_harness/run_latency.py` → OSC→Dante **p99 < 5ms** (`baseline.json`)
- [ ] `tests/soak_harness/run_soak.py --duration=1800` → **xrun=0**, RSS < 1MB/30분

#### vid2spatial OSC 스파이크 (Week 4, 데모 품질)
- [ ] `bridge/spike_vid2spatial_osc.py` 구현 — 좌표 변환 포함:
  - port 9000 → 9100
  - `/vid2spatial` → `/adm/obj/N/{azim,elev,dist}`
  - `az_adm = -az_pipeline` (RIGHT→LEFT 반전)
  - `dist_adm = 1 - dist_v2s` (near/far 역전)
- [ ] 오프라인 영상 1개(≥30초)로 단일 오브젝트 추적 → spatial_engine ADM-OSC 수신 확인
- [ ] `docs/adr/vid2spatial_osc_contract.md` 작성 (변환 규칙, 레이트 리밋, 알려진 한계)

#### 파트너사
- [ ] v1 로드맵 합의 문서 (MOU or 이메일) 확보

### Phase 1 — WebGUI MVP (9+2주)

- [ ] FastAPI + WebSocket 서버: `python3 -m pytest ui/webgui/tests/ -q` 100% 통과
- [ ] HTML5 Canvas: 64개 오브젝트 드래그, **렌더링 ≥ 60fps** (Chrome DevTools)
- [ ] WebGUI ↔ OSC 왕복 지연: **p99 < 20ms** (`ui/webgui/tests/test_latency.py`)
- [ ] iPad Safari + Android Chrome 터치 이벤트 동작
- [ ] 씬 저장/불러오기 WebGUI 버튼 → `/scene/save|load` OSC 동작
- [ ] `python3 -m pytest ui/tests/ -q` → **76 passed** 리그레션 없음
- [ ] 파트너사 현장 **48시간 무장애 운영** 로그

**탈출 분기점 (Week 10):** 60fps 미달 또는 p99 > 20ms → Canvas 최적화 1주 추가. 여전히 미달 → WebSocket 아키텍처 재검토.

### Phase 2 — vid2spatial 프로덕션화 (13+2주)

- [ ] `bridge/vid2spatial_osc.py` 프로덕션 구현 (스파이크 대체):
  - 좌표 변환 계약 100% 준수 (`docs/adr/vid2spatial_osc_contract.md`)
  - IIR α=0.3, 60Hz 레이트 리밋
  - 최대 8개 오브젝트 동시 추적
- [ ] `python3 bridge/tests/test_accuracy.py` → AzMAE ≤ **2.0°** (테스트 영상 10개+)
- [ ] AI ↔ 저레이턴시 모드 전환: **< 500ms**, 클리핑 없음
- [ ] `ctest --output-on-failure` → **34 tests pass** (리그레션 없음)
- [ ] 공연장 현장 라이브 데모 1회 성공 (파트너사 + 음향엔지니어 참관)

**탈출 분기점 (Week 22):** 현장 AzMAE > 2.0° → 카메라 보정 파라미터 1주 추가.

### Phase 3 — 상용 출시 (23주)

- [ ] 소프트웨어 라이선스 계약서 법적 검토 완료
- [ ] 연간 기술지원 계약서 완성
- [ ] 가격표 확정 (소/중/대 공연장 규모별)
- [ ] 한국어 설치 가이드 PDF (≥ 20페이지)
- [ ] 한국어 운용 매뉴얼 PDF (≥ 30페이지)
- [ ] **첫 유료 계약 체결**
- [ ] 고객 현장 **7일 연속 무장애 운영** 로그

---

## Implementation Steps

### Phase 0 (Week 0~5)

#### P0-1: GPL 법적 검토 (Week 1)
- 검토: `core/CMakeLists.txt` 의존성 전체
- 핵심: NO_JUCE 독립 바이너리 상업 배포 GPL 준수 여부

#### P0-2: 하드웨어 실측 (Week 2~3)
- `tests/latency_harness/run_latency.py` → `baseline.json`
- `tests/soak_harness/run_soak.py --duration=1800`

#### P0-3: vid2spatial OSC 스파이크 (Week 4)
```python
# bridge/spike_vid2spatial_osc.py (데모 전용, 프로덕션 품질 불필요)
# 핵심 변환:
az_adm = -az_pipeline          # RIGHT→LEFT 반전 (coordinate_convention.md:30-31)
dist_adm = 1.0 - dist_v2s     # near/far 역전 (osc_sender.py:107)
# port: 9000 → 9100
# prefix: /vid2spatial → /adm/obj/{N}/{azim,elev,dist}
```
- `docs/adr/vid2spatial_osc_contract.md` 작성

#### P0-4: 파트너사 합의 (Week 5)

---

### Phase 1: WebGUI MVP (Week 5~16)

```
ui/webgui/
├── server.py          # FastAPI + WebSocket hub
├── osc_bridge.py      # WebSocket ↔ OSC 9100/9101
├── static/
│   ├── index.html
│   ├── canvas.js      # requestAnimationFrame 60fps
│   └── ws_client.js   # 자동 재접속
└── tests/
    ├── test_server.py
    └── test_latency.py
```

참조: `ui/spatial_engine_ui/ipc/protocol.py` ADM-OSC 로직, `core/src/ipc/SceneSnapshot.*` 씬 시스템

---

### Phase 2: vid2spatial 프로덕션화 (Week 16~31)

```
bridge/
├── vid2spatial_osc.py     # 프로덕션 (스파이크 대체)
│   ├── OscTranslator      # 좌표 변환 계약 구현
│   ├── RateLimiter        # 60Hz
│   └── IIRSmoother        # α=0.3
├── config.yaml
└── tests/
    ├── test_bridge.py
    ├── test_accuracy.py   # AzMAE 측정
    └── test_mode_switch.py
```

---

### Phase 3: 상용 출시 (Week 31~54)

계약 문서 → 한국어 문서화 → 영업 → 첫 계약 체결

---

## Risks and Mitigations

| 위험 | 확률 | 영향 | 완화 방안 |
|------|------|------|----------|
| GPL 배포 불가 판정 | 낮음 | 높음 | P0-1 조기 검토. 불가 시 dual-license 재구성. |
| HW p99 > 5ms | 중간 | 중간 | audioBlock() 최적화, 블록 크기 조정 (64→128). |
| vid2spatial 좌표 변환 버그 | 확정 위험 | 높음 | P0-3 스파이크에서 `az_adm=-az_pipeline`, `dist_adm=1-dist_v2s` 명시 검증. |
| 현장 AzMAE > 2.0° | 중간 | 중간 | 조명별 보정 파라미터 config.yaml. 허용치 재협의. |
| 솔로 타임라인 초과 | 높음 | 중간 | Phase 1/2 각 2주 버퍼. 탈출 분기점. Phase 2 후 외주 가능. |
| 첫 계약 지연 | 중간 | 높음 | Phase 2 데모에서 POC 선체결. 가격 유연성 유지. |

---

## Verification Steps

```bash
# Phase 0
python3 tests/latency_harness/run_latency.py      # p99 < 5ms
python3 tests/soak_harness/run_soak.py --duration=1800  # xrun=0

# Phase 1
cd core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON && make -j$(nproc)
ctest --output-on-failure                          # 34 tests pass
python3 -m pytest ui/tests/ ui/webgui/tests/ -q   # 76+ passed
python3 ui/webgui/tests/test_latency.py            # p99 < 20ms

# Phase 2
python3 bridge/tests/test_accuracy.py             # AzMAE ≤ 2.0°
python3 bridge/tests/test_mode_switch.py          # 전환 < 500ms
ctest --output-on-failure                          # 34 tests pass
```

---

## ADR: 핵심 아키텍처 결정

### Decision
Option A (순차) + Phase 0 vid2spatial 스파이크 — 솔로 개발자 집중도 보존 + 킬러 피처 조기 증명

### Drivers
1. 6~12개월 첫 계약 마감
2. 솔로 개발자 집중도
3. vid2spatial 킬러 피처 조기 가시화 (Architect synthesis 반영)

### Alternatives Considered
| 대안 | 기각 이유 |
|------|----------|
| Option B (병렬 개발) | 솔로 개발자 집중도 분산, 품질 저하 |
| Phase 0 스파이크 없는 Option A | 첫 고객 미팅 시 데모 불가 |
| JUCE 라이선스 구매 | 연 $30k~$150k, 부트스트랩 불가 |

### Why Chosen
Architect synthesis: 1주 스파이크(Phase 0)가 킬러 피처 evidence를 확보하면서 Phase 1 WebGUI 집중 유지. 프로덕션화는 Phase 2에서.

### Consequences
- Phase 2 이전 vid2spatial 데모 품질 (프로덕션 불보장) — 허용
- 좌표 변환 계약 문서화 필수 (`vid2spatial_osc_contract.md`)
- 거리/방위각 부호 버그가 P0-3에서 발견 → P0 완료의 필수 조건

### Follow-ups
- GPL 검토 결과에 따라 Phase 1 시작 전 라이선스 재구성 가능
- Phase 3 이후: text2traj v2, DAW 플러그인, 글로벌 SDK

---

## Changelog
- v1 초안: Phase 순차 진행, 기본 수용 기준
- v2: Critic REVISE 반영 — Phase 0 스파이크, 정량 게이트, 2주 버퍼, 탈출 분기점
- 최종: Architect NEEDS_MINOR_FIX 반영 — 좌표 변환 계약 (`az_adm=-az_pipeline`, `dist_adm=1-dist_v2s`) + IIR 한계 명시 + ADR 완성
