# spatial-engine WebGUI v1 — Round-2 Plan (RALPLAN-DR Final)

**Mode**: SHORT + DELIBERATE annex (high-risk, 2-sprint)
**Round**: 2 (revise after Architect REVISE-AND-RESUBMIT 5 + Critic ITERATE 53%, 8 MUST + 4 SHOULD)
**Date**: 2026-05-12
**Owner**: Planner (RALPLAN-DR consensus loop)
**Successors (review)**: Architect → Critic → user APPROVE → autopilot

---

## §0 Round-2 Changelog & Out-of-Scope

### §0.1 Round-1 → Round-2 변경 요약

R1 본문 13개 섹션을 유지하면서 Architect 5개 권고 + Critic 8개 신규 발견 (N1~N8) + Critic Loop Closure 12개 anchor (8 MUST + 4 SHOULD)를 모두 반영했다.

| ID | 출처 | R1 상태 | R2 반영 |
|---|---|---|---|
| Arch-1 | Architect | Q-1 미해소 | §0.2 Out-of-Scope, ADR-2 deferred 명문화 |
| Arch-2 | Architect | S0 없음 (1.0d) | §3 S0 신규 (verify byte baseline + osc_bridge shutdown + 9100 producer 통합 + Q-2 archaeology) |
| Arch-3 | Architect | G3 dual path | §2 G3 triple path (G3a mock/G3b live UDP/G3c e2e wire bytes hash) |
| Arch-4 | Architect | Q-3 50fps p10 N=20 | §2 G2 min-of-5-windows-p10, N=100 (5×5s @250ms) |
| Arch-5 | Architect | sentinels 빈 칸 | §7 sentinels invocation 명령어 표 완성 |
| N1 (MAJOR) | Critic | G7 governance 부재 | §2 G7 pilot day-1 → median×3 fixed day-2 |
| N2 (MAJOR) | Critic | G2 N=20 무의미 | (Arch-4와 동일, N=100) |
| N3 (MINOR) | Critic | /tmp 등재 누락 | §6 risk 5.4 + ADR-4 deferred |
| N4 (MAJOR) | Critic | G4 단일 client | §2 G4 2 concurrent clients × 30s + S4 +0.3d |
| N5 (MAJOR) | Critic | sentinels 빈 칸 | (Arch-5와 동일) |
| N6 (CRITICAL) | Critic | OSCBackend in-process | §2 G3c mandatory wire bytes hash |
| N7 (MAJOR) | Critic | Q-2 권한 모호 | §3 S0 archaeology + ADR-3 결정 명시 |
| N8 (MINOR) | Critic | pre-mortem 정량 부재 | §5 P×I 정량 + 5 시나리오로 확장 |
| MUST-1 | Critic | Out-of-Scope 섹션 부재 | §0.2 신설 |
| MUST-2~8 | Critic | (위와 동일) | §2/§3/§5/§7 반영 |
| SHOULD-9 | Critic | G4 ±2° derivation | §2 G4 ADM-OSC azim 16-bit quant derivation |
| SHOULD-10 | Critic | Option B 거부 사유 | §1.4 Option B/B' 비교 표 |
| SHOULD-11 | Critic | regression→gate 매핑 부재 | §7 매핑 표 |
| SHOULD-12 | Critic | pre-mortem 시나리오 부족 | §5 5 시나리오 (multi-WS race + deleted test) |

R1 ETA 12.5d + 1.0 slack = 13.5d → **R2 ETA 13.8d + 1.5 slack = 15.3d** (Critic 14.3 권고 대비 +1.0d 보수적 마진).

### §0.2 Out-of-Scope (명시적 비-범위)

다음 항목은 본 v1 sprint에서 다루지 않으며, 각각 별도 plan 또는 deferred ADR로 처리한다.

| 항목 | 사유 | 후속 |
|---|---|---|
| `/scene/list` reply 양방향 통신 (현재 `SceneController.cpp:26-28` 메모리만, `CommandDecoder.cpp:371-373` decode-only) | 코어 측 신규 OSC 송신 코드 + dialect 정의 + JUCE 측 응답 라우팅 필요. 본 sprint는 TX 정합에 집중. | ADR-2 (별도 plan, ETA 미정) |
| `ui/tests/` 76 vs 63 결정 (만약 S0 archaeology 결과 의도적 deletion으로 판명) | 본 sprint는 회귀 방지가 우선, 신규 추가는 별도 결정 | ADR-3 (S0 결과 기반 plan 또는 G6 본문 반영) |
| `/tmp/.spe_bridge_mode` → `$XDG_RUNTIME_DIR` 마이그레이션 (`server.py:86`, `vid2spatial_osc.py:302`) | 보안/멀티유저 위험은 있으나 본 sprint 신규 기능 게이트와 무관 | ADR-4 (별도 plan) |
| v0.3 S8 DAW VST3 hands-on (Reaper/Ableton/Cubase 4 host 실측) | 사용자 실측 필요, 본 plan은 자동화 가능 범위만 | 사용자 큐 (별도 작업) |
| vid2spatial Phase 2 production (`tests/full_eval/`) | 별도 repo 워크플로 | 사용자 큐 |
| Phase 3 출시 작업 (KCC/CE, AppImage, FDA) | 비-기술 의존 | Phase 3 plan (별도) |

---

## §1 RALPLAN-DR Summary

### §1.1 Principles (5)

1. **Falsifiable Acceptance**: 모든 G-게이트는 측정 가능한 임계값과 sentinel script로 falsifiable해야 한다.
2. **Byte-Level Wire Compatibility**: WebGUI 도입 후에도 기존 28+ pytest + ctest + `.ci/off_baseline.bytes.sha256` 100% 보존.
3. **Single Producer, Single Wire**: 9100 송신은 단일 producer만 (multiplexer 또는 명시적 mode 토글) — race 금지.
4. **Statistical Validity**: 성능 게이트는 N≥100 또는 min-of-5-windows 같은 통계적으로 유의한 sample 크기.
5. **Sentinel-Gated Verification**: 모든 게이트는 명시적 invocation 명령어로 호출되며 인간 검토 불필요.

### §1.2 Decision Drivers (top 3)

1. **D1 Wire Compatibility (Highest)**: 기존 byte-baseline 깨지면 v0 회귀 → 즉시 fail. (Risk: production POC 신뢰도 손실)
2. **D2 Mobile UX p10 60fps**: sales demo 시연 환경에서 iPad/iPhone p10 frame budget. 단일 frame drop도 시연 가시.
3. **D3 48h Stability**: production POC 의 over-night 시연 가능성 확보 (메모리/fd/asyncio 누수 zero).

### §1.3 Mode: SHORT + DELIBERATE annex

**SHORT** 기본: 게이트 G1~G7 + ETA + Acceptance.
**DELIBERATE annex** (high-risk 사유: 2-sprint scope, byte-baseline 회귀 위험, mobile demo 시연 의존):

- 사유 1: WebGUI 도입은 multi-process 토폴로지를 건드린다 (osc_bridge ThreadingOSCUDPServer + uvicorn + 선택적 vid2spatial_osc) → race 위험.
- 사유 2: 48h soak는 한 번 실패 시 +3d 비용.
- 사유 3: mobile demo 게이트 미달 시 sales 영향.

→ pre-mortem 5 시나리오 (P×I 정량), test plan 6 category (unit/integration/E2E/soak/observability/regression), sentinels invocation 표, regression→gate 매핑 표 모두 의무화.

### §1.4 Options 비교 (Option A 선택, B/B' escape trigger 명시)

| 옵션 | 설명 | 장점 | 단점 | 거부/Escape |
|---|---|---|---|---|
| **A (선택)** | 13.8d 단일 sprint, 8 게이트 (G1~G8 conditional) | 통합 검증 가능 / 컨텍스트 유지 | 게이트 1개 실패 시 sprint 전체 슬립 | 선택 — escape trigger: S5 day-1 pilot에서 RSS slope > 100MB/h 발견 시 B로 분기 |
| **B (sprint 분할)** | 2 sub-sprint (S0~S3 8.5d + S4~S7 6.8d), 게이트 그룹 분리 | 1차 결과로 조기 stop 가능 | 컨텍스트 단절 / handoff 비용 1d 추가 | 거부 사유: 단일 sprint A의 컨텍스트 유지가 +1d 단절보다 효율, escape trigger 발동 시에만 분기 |
| **B' (G-축 분할)** | 운영(G2/G4/G7) sprint + contract(G3/G5) sprint | 운영팀-네트워크팀 분업 가능 | 단일 owner 환경에서 의미 없음 / 의존성 (S5 soak는 G3 wire 정합 후만 의미) | 거부 사유: 본 프로젝트 단일 owner, 의존성 그래프상 S5는 S2/S6 의존 |

**escape trigger** (Option A → B 분기 조건):
- S5 day-1 pilot에서 RSS slope > 100MB/h OR fd leak > 50 → 잔여 S6/S7 차단, S5 재설계로 분기.
- S3에서 p10 < 45fps (50의 90%) → S4 모바일 진입 차단, S3 재설계.

---

## §2 Deliverable Matrix (G1~G8)

| ID | 게이트 | R2 임계값 (falsifiable) | 측정 방법 | 의존 S단계 |
|---|---|---|---|---|
| **G1** | 기존 회귀 ZERO | ctest exit==0 / `pytest ui/webgui/tests/` exit==0 ≥**32 tests** (S2 3개 + S6 3개 신규) | `bash scripts/verify_byte_baseline.py --strict && ctest --test-dir core/build && python3 -m pytest ui/webgui/tests/` | S0, S1 |
| **G2** | 60fps WebGUI (canvas drag synth) | **min-of-5-windows-p10 ≥ 60fps (desktop) / ≥ 50fps (mobile)** | playwright headless Chromium, dev-only `?seed=64obj` query → 64 obj concurrent drag 5초 window × 5회, 250ms sampling → N=100 sample → 5개 윈도우 p10 중 최솟값 | S3, S4 |
| **G3a** | RTT mock echo (in-process) | p99 < **5 ms**, N=200 | pytest-asyncio, mock OSC echo sink 직결 | S6 |
| **G3b** | RTT live OSCBackend UDP echo | p99 < **20 ms**, N=200 | live OSCBackend UDP loopback, 200 sample | S6 |
| **G3c** | RTT e2e wire bytes hash (CRITICAL, Critic N6) | p99 < **30 ms** AND SHA256 wire bytes 100% match | WS → osc_bridge → UDP 9100 → pythonosc receiver → hash compare → UDP 9101 → osc_bridge state → broadcast → WS | S6 |
| **G4** | 모바일 emulation + 좌표 정밀도 + 2-client 동시성 | iPhone 13 + Pixel 5 emulation에서 좌표 send/recv 오차 ≤ **±2°** AND **2 concurrent clients × 30s broadcast drop ≤ 1%** | playwright device emulation, drag → /obj/azim/N float 검증; ±2° 도출 = ADM-OSC azim 16-bit float quant (≈ 0.005°) + canvas 1px → 360/canvas_width deg (≈ 0.5° @ 720px) ≈ 1.5° margin → 2° 보수 | S4 |
| **G5** | scene save/load **TX 정합** (RX scope 외) | 송신 OSC packet SHA256 100% match (`/scene/save name`, `/scene/load name`) | mock OSC server fixture 캡처 후 pcap-style hash 비교. `/scene/list` reply는 ADR-2로 deferred | S2 |
| **G6** | `ui/tests/` 회귀 (Q-2 결정 후) | exit==0, N tests pass (S0 archaeology 후 ADR-3로 N 확정) | S0 결과에 따라 N=63 유지(deferral) 또는 N=76 재추가 | S0 |
| **G7** | 48h soak governance | day-1 pilot (24h)에서 median RSS slope 측정 → day-2부터 **threshold = median × 3 (보수 상한 50MB/h)** fixed 적용. AND fd_count leak ≤ 5. AND asyncio.all_tasks() slope ≈ 0 (≤ 1 task/h). AND WS reconnect counter < 1000. | subprocess uvicorn + mock OSC + 60Hz client + psutil + asyncio 측정 + fault injection 5분 간격 | S5 |
| **G8** | 실기 smoke (conditional) | iPad Safari + iPhone Safari 실제 단말 1회 통과 (out-of-scope, Phase 0 MOU 필요) | 사용자 실측 큐 | (out-of-scope) |

---

## §3 Steps (S0~S7, 총 13.8d active)

### S0 — Foundation & Archaeology (1.0d, 신규)

**목표**: R1에서 발견된 6개 risk (5.1~5.6) 중 본 sprint 안에서 fix해야 하는 4개를 사전 해소 + Q-2 archaeology.

**산출물**:
1. `scripts/verify_byte_baseline.py` — `.ci/off_baseline.bytes.sha256` 검증 (`sha256sum -c` 래퍼 + strict mode + CI exit code propagation)
2. `ui/webgui/osc_bridge.py` shutdown 패치 — 모듈 레벨 `_srv_state` 핸들 저장 + `def shutdown()` 함수 + unit test `tests/test_osc_bridge_shutdown.py`
3. `ui/webgui/server.py` lifespan `finally:` 블록에 `osc_bridge.shutdown()` 호출 추가
4. **9100 송신 producer 통합**: `server.py:305` (BridgeServer target_port=9100) ↔ `vid2spatial_osc.py:42, :254-255` 충돌 분석 → 단일 multiplexer 또는 명시적 mode 토글 (`_bridge_mode=='ai'` 만 9100 송신, `'low_latency'` 는 OFF) 결정 문서화 + 코드 반영
5. **Q-2 archaeology**: `pytest --collect-only ui/tests/` + `git log --all --diff-filter=D --summary -- ui/tests/` 결과 보고서 → ADR-3 결정 (재추가 OR 정식 deferral)

**Acceptance (S0)**:
- `bash scripts/verify_byte_baseline.py --strict` exit==0
- `pytest tests/test_osc_bridge_shutdown.py` exit==0 (shutdown 후 fd_count 증가 0)
- 9100 producer 통합 결정 ADR 또는 README 한 문단 작성
- Q-2 archaeology 보고서 + ADR-3 결정

### S1 — Baseline Capture (0.5d, S0 흡수 후 단축)

**목표**: R2 시작 시점의 모든 게이트 baseline 측정.

**산출물**: 28 pytest 통과 결과 + `ui/tests/` 결과 + ctest 결과 + manual browser fps (단순 보조 측정) + R2 baseline JSON 보고서 (`.omc/research/spatial-engine-webgui-v1-baseline.json`).

**Acceptance**: 모든 baseline 측정값 JSON으로 캡처, S0 통합 fix 후 회귀 zero.

### S2 — Scene Save/Load TX E2E (1.5d)

**목표**: scene save/load 송신 OSC packet의 **wire bytes SHA256 100% match** 검증 (RX는 ADR-2로 deferred).

**산출물**:
- `ui/webgui/tests/test_scene_e2e.py` — mock OSC server fixture (`python-osc` 기반) + WebGUI HTTP POST `/api/scene/save?name=X` → mock 수신 OSC packet 캡처 → SHA256 비교
- 3개 신규 pytest (`test_scene_save_tx_hash`, `test_scene_load_tx_hash`, `test_scene_list_tx_only` — list는 송신만 검증)

**Acceptance**: G5 통과, 3 신규 pytest pass.

### S3 — Playwright 60fps Harness (2.5d)

**목표**: G2 desktop p10 게이트 N=100 sample.

**산출물**:
- `ui/webgui/tests/playwright/playwright.config.ts`
- `ui/webgui/tests/playwright/fps_desktop.spec.ts` — 64-obj seed (`?seed=64obj` dev-only query), 5 windows × 5s × 250ms sampling = 100 sample → min-of-5-windows-p10 계산
- `pyproject.toml` 또는 `requirements-dev.txt`에 playwright 추가 + Chromium install 스크립트

**Acceptance**: G2 desktop ≥ 60fps p10 (min-of-windows).

### S4 — Mobile Emulation + 2-Client Concurrency (1.8d, +0.3d)

**목표**: G4 mobile emulation + ±2° 좌표 정밀도 + 2 concurrent client × 30s broadcast 안정성.

**산출물**:
- `ui/webgui/tests/playwright/fps_mobile_iphone.spec.ts` (iPhone 13 emulation)
- `ui/webgui/tests/playwright/fps_mobile_pixel.spec.ts` (Pixel 5 emulation)
- `ui/webgui/tests/playwright/coord_precision.spec.ts` (drag → /obj/azim 송신값 검증, ±2° margin)
- `ui/webgui/tests/playwright/multi_client_concurrent.spec.ts` (2 client 동시 30s 드래그, broadcast drop ≤ 1% — `manager.broadcast` 의 `dead` list 길이 추적)

**±2° derivation 메모** (G4 acceptance에 docstring 포함):
- ADM-OSC `/adm/obj/N/aed` azim 32-bit float wire encoding (실제 16-bit quant은 일부 receiver의 round, 보수 추정 ≈ 0.005°).
- canvas 1px → `360° / canvas_width` ≈ 0.5° @ 720px (mobile portrait 추정).
- 합산 margin ≈ 1.5° → 보수 상한 2° 적용.

**Acceptance**: G2 mobile ≥ 50fps p10 + G4 좌표 ±2° + 2-client drop ≤ 1%.

### S5 — 48h Soak with Governance (3.0d active, 48h wall clock 포함)

**목표**: G7 governance — day-1 pilot → day-2 fixed threshold.

**산출물**:
- `tests/soak_harness/run_soak_webgui.py` — subprocess `uvicorn ui.webgui.server:app` + mock OSC 9100 sink + 60Hz drag synthesizer client (asyncio task) + psutil RSS sampling 1Hz + `asyncio.all_tasks()` count 1Hz + `/proc/<pid>/fd/` count 1Hz + WS reconnect counter (subprocess kill SIGKILL 5분 간격 fault injection)
- day-1 (0~24h) pilot 보고서 JSON → median RSS slope 계산 → **threshold = max(median × 3, 50MB/h 상한)** fixed
- day-2 (24~48h) fixed threshold 적용 검증
- soak schema test 기존 `test_soak_schema.py` 확장 (asyncio/fd/reconnect 필드)

**Acceptance**: G7 모든 sentinel pass (RSS slope ≤ threshold, fd leak ≤ 5, asyncio slope ≈ 0, reconnect < 1000).

### S6 — RTT Triple Path (2.0d, +0.5d)

**목표**: G3a / G3b / G3c 3 게이트 모두 N=200.

**산출물**:
- `ui/webgui/tests/test_rtt_mock.py` (G3a, mock echo sink in-process)
- `ui/webgui/tests/test_rtt_udp_live.py` (G3b, OSCBackend live UDP echo)
- `ui/webgui/tests/test_rtt_e2e_wire.py` (G3c, **mandatory**: WS → osc_bridge → UDP 9100 echo receiver → SHA256 compare → UDP 9101 → osc_bridge state → broadcast → WS, 200 sample)
- `ui/webgui/tests/wire_hash_compare.py` (SHA256 비교 유틸)

**Acceptance**: G3a p99<5ms / G3b p99<20ms / G3c p99<30ms AND wire bytes hash 100% match.

### S7 — 매뉴얼 Ch.6 (1.5d)

**목표**: WebGUI 챕터 신규 + TOC 갱신.

**산출물**:
- `docs/manual_kr/CH6_WEBGUI.md` — 토폴로지 (uvicorn + osc_bridge + 선택적 vid2spatial_osc), 환경 변수, 게이트 인터프리테이션, 알려진 한계 (Q-1 ADR-2, /tmp ADR-4).
- `docs/manual_kr/README.md` TOC에 Ch.6 추가.

**Acceptance**: 챕터 1500자 이상, TOC 정합.

---

## §4 Coupling Graph (S0~S7)

```
S0 (foundation) ──┬──> S1 (baseline)
                  ├──> S2 (scene TX, osc_bridge.shutdown 의존)
                  ├──> S5 (soak, 9100 producer 통합 의존)
                  └──> S6 (RTT, osc_bridge.shutdown 의존)

S1 ──> S2, S3, S5, S6 (baseline 측정값 비교용)

S2 (scene TX) ──> S5 (soak는 scene API 호출 패턴 포함)
S2 ──> S6 (e2e RTT는 scene 정합 확립 후)

S3 (desktop fps) ──> S4 (mobile emulation, harness 재사용)

S4 (mobile + 2-client) ──> S5 (soak는 multi-client race 가정)

S6 (RTT) ──> S7 (매뉴얼 측정값 인용)
S5 (soak) ──> S7 (매뉴얼 측정값 인용)
```

**Critical chain**: S0 → S2 → S6 → S5 → S7 (직렬 ≈ 10.0d) + S3/S4 (병렬 가능 4.3d) → 단일 owner 환경에서 직렬 추정 13.8d active.

---

## §5 Pre-mortem (5 시나리오, P×I 정량, DELIBERATE annex)

| ID | 시나리오 | P (확률) | I (영향) | R (P×I) | Mitigation (어느 게이트가 잡는가) |
|---|---|---|---|---|---|
| **A** | 60fps 미달 (sales demo 시연 가시 frame drop) | 40% | High (8) | **24** | G2 min-of-5-windows-p10 (S3/S4). 발견 시: fillText 캐싱, OffscreenCanvas, dirty rect 도입 |
| **B** | 48h WS leak (ConnectionManager.disconnect 누락 또는 `run_coroutine_threadsafe` future GC 미동작) | 30% | Med (5) | **15** | G7 fd leak ≤ 5 + asyncio slope ≈ 0 (S5). 발견 시: explicit future.add_done_callback 정리 |
| **C** | iPad touch 좌표 어긋남 (devicePixelRatio, touch-action: none, orientationchange) | 20% | Med (5) | **10** | G4 ±2° 정밀도 (S4). 발견 시: touch-action CSS + viewport meta + orientationchange listener |
| **D (신규)** | multi-WS broadcast race (데스크탑 + iPad 동시 드래그, `manager.broadcast` 의 in-flight send_text race) | 50% | High (7) | **35** | G4 2 concurrent client × 30s drop ≤ 1% (S4). 발견 시: per-client state isolation 또는 explicit lock |
| **E (신규)** | deleted test 회귀 (Q-2 archaeology에서 의도적으로 삭제된 sales-demo gate test 발견) | 20% | High (7) | **14** | S0 archaeology → ADR-3 재추가 또는 명시적 deferral. 미반영 시 G6 회귀 검출 누락 |

**가장 높은 R-score**: D (multi-WS race, 35). 본 sprint S4의 G4 2-client 게이트가 mandatory인 이유.

---

## §6 Risks (R1 7개 유지 + Architect 5.1~5.6 통합)

R1 기존 7 (R1, R2, ..., R7 — 코드 변경 회귀, 모바일 시연 환경, 측정 도구 misconfiguration, scene 양방향 한계, soak 인프라 비용, playwright Chromium 의존, 매뉴얼 챕터 갱신 누락) 유지.

**신규 / 강화**:

| ID | 설명 | Mitigation 위치 |
|---|---|---|
| 5.1 | `osc_bridge.py:70-74` ThreadingOSCUDPServer 시작 후 shutdown 미등록 → fd leak / asyncio task leak | **S0** shutdown 패치 + `test_osc_bridge_shutdown.py` |
| 5.2 | 9100 wire 충돌: `server.py:305` BridgeServer(target=9100) ↔ `vid2spatial_osc.py:254-255` 둘 다 송신자 | **S0** producer 통합 (단일 multiplexer 또는 mode 토글) |
| 5.3 | multi-WS broadcast race (S5 §5-D, R=35) | **S4** G4 2-client 게이트 |
| 5.4 | `/tmp/.spe_bridge_mode` (`server.py:86`, `vid2spatial_osc.py:302`) 멀티유저/security 위험 | **본 sprint scope 외**, ADR-4 deferred |
| 5.5 | byte-baseline 회귀 발견 지연 | **S0** `scripts/verify_byte_baseline.py --strict` 필수 통과 |
| 5.6 | deleted test 회귀 (§5-E) | **S0** archaeology → ADR-3 |

---

## §7 Test Plan (6 categories × G-gates 매핑 표 + sentinels invocation)

### §7.1 Category × Gate 매핑

| Category | G1 | G2 | G3a/b/c | G4 | G5 | G6 | G7 |
|---|---|---|---|---|---|---|---|
| Unit (pytest) | ✓ (S0/S1) | – | ✓ (S6 mock) | – | – | ✓ (S0) | – |
| Integration | ✓ (S2) | – | ✓ (S6 UDP live) | – | ✓ (S2) | – | – |
| E2E (playwright) | – | ✓ (S3/S4) | ✓ (S6 wire hash) | ✓ (S4) | – | – | – |
| Soak | – | – | – | ✓ (S5 multi-client) | – | – | ✓ (S5) |
| Observability | – | – | – | – | – | – | ✓ (S5 RSS/fd/asyncio) |
| Regression (sentinel) | ✓ (S0 byte-baseline) | – | – | – | – | ✓ (S0 archaeology) | ✓ (S5 day-1→day-2) |

### §7.2 Sentinels Invocation 표 (Critic MUST #7 / Architect #5)

| Sentinel | 게이트 | Invocation |
|---|---|---|
| byte-baseline strict | G1 | `bash scripts/verify_byte_baseline.py --strict` (내부에서 `sha256sum -c .ci/off_baseline.bytes.sha256`, exit !=0 시 FAIL) |
| existing ctest | G1 | `ctest --test-dir core/build --output-on-failure` |
| existing pytest | G1 | `python3 -m pytest ui/webgui/tests/ -v --tb=short` |
| fps desktop min-of-windows | G2 | `npx playwright test ui/webgui/tests/playwright/fps_desktop.spec.ts --reporter=json` → `python ui/webgui/tests/playwright/parse_fps.py --min-of-windows --threshold 60` |
| fps mobile min-of-windows | G2 | `npx playwright test ui/webgui/tests/playwright/fps_mobile_*.spec.ts --reporter=json` → `python ui/webgui/tests/playwright/parse_fps.py --min-of-windows --threshold 50` |
| RTT mock (G3a) | G3a | `python3 -m pytest ui/webgui/tests/test_rtt_mock.py -v --rtt-threshold-ms 5` |
| RTT UDP live (G3b) | G3b | `python3 -m pytest ui/webgui/tests/test_rtt_udp_live.py -v --rtt-threshold-ms 20` |
| RTT e2e wire hash (G3c) | G3c | `python3 -m pytest ui/webgui/tests/test_rtt_e2e_wire.py -v --rtt-threshold-ms 30 --hash-strict` |
| OSC wire bytes hash | G3c / G5 | `python ui/webgui/tests/wire_hash_compare.py --expected-sha <hex> --actual-sha <hex> --strict` |
| coord precision | G4 | `npx playwright test ui/webgui/tests/playwright/coord_precision.spec.ts --reporter=json` → `python ui/webgui/tests/playwright/parse_coord.py --tolerance-deg 2` |
| multi-client drop | G4 | `npx playwright test ui/webgui/tests/playwright/multi_client_concurrent.spec.ts --reporter=json` → `python ui/webgui/tests/playwright/parse_drop.py --threshold-percent 1` |
| RSS slope | G7 | `python tests/soak_harness/rss_slope.py --report soak_report.json --threshold-mb-h <day1_median_x3>` (보수 상한 50MB/h) |
| asyncio tasks slope | G7 | `python tests/soak_harness/extract_asyncio_slope.py --report soak_report.json --threshold-tasks-h 1` |
| fd_count post-shutdown | G7 | `python tests/soak_harness/check_fd_leak.py --start-pid <pid> --threshold 5` |
| WS reconnect counter | G7 | `python tests/soak_harness/extract_reconnect.py --report soak_report.json --threshold 1000` |
| osc_bridge shutdown | S0 fix | `python3 -m pytest tests/test_osc_bridge_shutdown.py -v` |

### §7.3 Regression Category → Gate 매핑 (Critic SHOULD #11)

| Regression sentinel | 보호 게이트 | 실패 시 영향 |
|---|---|---|
| `scripts/verify_byte_baseline.py` | G1 (전체 wire) | v0 회귀 — 즉시 sprint abort |
| `ctest` | G1 (core) | C++ 회귀 — 즉시 fix |
| `test_osc_bridge_shutdown.py` | G7 (fd leak) | S5 day-1 무효화 |
| `ui/tests/` (G6) | G6 (legacy UI) | ADR-3 결정 후 명시 |
| wire bytes hash (S6/S2) | G3c, G5 | dialect drift 발견 |

---

## §8 ADRs (1 final + 4 deferred)

### ADR-1 (final): SceneSnapshot fixed-name buffer (R1 유지)

기존 `core/src/ipc/SceneController.cpp:14-17` 의 `snap.name = p.name` null-terminated fixed-size buffer 접근 유지. 동적 할당 없음.

### ADR-2 (deferred): `/scene/list` reply 양방향 통신

**Status**: deferred (별도 plan 큐)
**현재 상태**: `SceneController.cpp:26-28` 메모리만 저장, `CommandDecoder.cpp:371-373` decode-only — 코어 측 응답 송신 미구현.
**필요 작업**: 신규 OSC 송신 dialect 정의 (`/scene/list/reply`), JUCE 메시지-스레드 응답 라우팅, osc_bridge 9101 수신 핸들러 확장.
**현재 plan과의 관계**: G5는 송신 정합만 검증, list 응답은 본 sprint 게이트에서 제외.

### ADR-3 (deferred): `ui/tests/` 76 vs 63 결정

**Status**: S0 archaeology 결과 후 결정
**필요 정보**: `git log --all --diff-filter=D --summary -- ui/tests/` 결과 + `pytest --collect-only` 출력
**결정 옵션**: (a) 재추가 (정상 회귀 보호 복구) (b) 명시적 deferral (의도적 deletion 확인 후 deferred)
**G6 acceptance N**: ADR-3 결정 후 확정.

### ADR-4 (deferred): `/tmp/.spe_bridge_mode` → `$XDG_RUNTIME_DIR`

**Status**: deferred (별도 plan)
**현재 경로**: `server.py:86`, `vid2spatial_osc.py:302` 둘 다 `/tmp/.spe_bridge_mode` hard-coded.
**위험**: 멀티유저 환경에서 동일 host 다른 user race; predictable path race(symlink attack).
**필요 작업**: `os.environ.get('XDG_RUNTIME_DIR', tempfile.gettempdir())` fallback, server/bridge 양쪽 동기 수정.

### ADR-5 (final): 본 sprint Option A 채택

**Decision**: Single sprint (Option A, 13.8d active + 1.5d slack = 15.3d total)
**Drivers**: §1.2 D1 wire compat / D2 mobile p10 / D3 48h stability
**Alternatives considered**:
- Option B (sprint 분할): 거부 사유 — 단일 owner 환경에서 +1d handoff cost, 컨텍스트 단절 비용
- Option B' (G-축 분할): 거부 사유 — 의존성 그래프(S5는 S2/S6 의존)와 단일 owner 환경
**Why chosen**: 단일 owner, 명확한 의존성 그래프 (S0 fan-out → S2/S6 → S5), escape trigger로 risk fallback 가능
**Consequences**:
- 게이트 1개 실패 시 sprint 전체 슬립 (slack 1.5d 흡수)
- escape trigger 발동 시 잔여 작업 B로 분기 (S5 pilot 기준)
**Follow-ups**:
- ADR-2/3/4 별도 plan 큐
- v0.3 S8 DAW VST3 hands-on 사용자 큐
- Phase 3 출시 작업 별도 plan

---

## §9 ETA (총 15.3d)

| 단계 | active days | 누적 |
|---|---|---|
| S0 Foundation & Archaeology | 1.0 | 1.0 |
| S1 Baseline Capture | 0.5 | 1.5 |
| S2 Scene TX E2E | 1.5 | 3.0 |
| S3 Playwright Desktop 60fps | 2.5 | 5.5 |
| S4 Mobile + 2-Client | 1.8 | 7.3 |
| S5 48h Soak (active days, 48h wall 포함) | 3.0 | 10.3 |
| S6 RTT Triple Path | 2.0 | 12.3 |
| S7 매뉴얼 Ch.6 | 1.5 | 13.8 |
| **active 합계** | **13.8** | |
| Slack (10%) | 1.5 | |
| **총 ETA** | **15.3** | |

Critic 권고 14.3d (12% slack) 대비 +1.0d 보수적 마진 — DELIBERATE 모드 high-risk sprint에 적합.

Phase 1 (9+2주 ≈ 11주 ≈ 55영업일) 안에 충분히 들어맞음.

---

## §10 Acceptance Index (falsifiable 항목만)

| G | Acceptance |
|---|---|
| G1 | `bash scripts/verify_byte_baseline.py --strict` exit==0 / `ctest` exit==0 / `pytest ui/webgui/tests/` exit==0 ≥ **32 tests** (S2 3 + S6 3 신규) |
| G2 | desktop **min-of-5-windows-p10 ≥ 60fps** / mobile (iPhone 13 + Pixel 5) **min-of-5-windows-p10 ≥ 50fps** / N=100 sample / 64-obj concurrent drag |
| G3a | RTT mock p99 < 5 ms / N=200 |
| G3b | RTT UDP live p99 < 20 ms / N=200 |
| G3c | RTT e2e p99 < 30 ms / N=200 AND wire bytes SHA256 100% match |
| G4 | iPhone 13 + Pixel 5 emulation 좌표 send/recv 오차 ≤ ±2° AND 2 concurrent clients × 30s broadcast drop ≤ 1% |
| G5 | `/scene/save` + `/scene/load` TX OSC packet SHA256 100% match (RX는 ADR-2 deferred) |
| G6 | `pytest ui/tests/` exit==0 / N pass (ADR-3 결정 후 N 확정) |
| G7 | day-1 pilot 후 fixed threshold (median × 3, 상한 50MB/h) 적용 / fd leak ≤ 5 / asyncio slope ≤ 1 task/h / WS reconnect < 1000 |
| Regression | sentinel 표 §7.2 모든 항목 PASS |
| S7 | `docs/manual_kr/CH6_WEBGUI.md` ≥ 1500자 / TOC 정합 |

---

## §11 Files

### Modified (3)
- `ui/webgui/osc_bridge.py` — shutdown 패치 (`_srv_state` 핸들 + `shutdown()` 함수)
- `ui/webgui/server.py` — lifespan `finally:` 블록 osc_bridge.shutdown() 호출 + 9100 producer 통합 (mode 토글 또는 multiplexer)
- `pyproject.toml` 또는 `requirements-dev.txt` — playwright + chromium 의존

### New (12)
1. `scripts/verify_byte_baseline.py`
2. `tests/test_osc_bridge_shutdown.py`
3. `ui/webgui/tests/test_scene_e2e.py`
4. `ui/webgui/tests/test_rtt_mock.py` (G3a)
5. `ui/webgui/tests/test_rtt_udp_live.py` (G3b)
6. `ui/webgui/tests/test_rtt_e2e_wire.py` (G3c)
7. `ui/webgui/tests/wire_hash_compare.py`
8. `ui/webgui/tests/playwright/playwright.config.ts` + `fps_desktop.spec.ts` + `fps_mobile_*.spec.ts` + `coord_precision.spec.ts` + `multi_client_concurrent.spec.ts` + `parse_*.py` helpers
9. `tests/soak_harness/run_soak_webgui.py`
10. `tests/soak_harness/rss_slope.py`, `extract_asyncio_slope.py`, `check_fd_leak.py`, `extract_reconnect.py`
11. `docs/manual_kr/CH6_WEBGUI.md` + TOC patch
12. `.omc/research/spatial-engine-webgui-v1-baseline.json` (S1 산출)

### Read-only (R1과 동일)
`core/src/ipc/SceneController.cpp`, `core/src/ipc/CommandDecoder.cpp`, `core/src/ipc/OSCBackend.cpp`, `.ci/off_baseline.bytes.sha256`, `bridge/vid2spatial_osc.py`.

---

## §12 Open Questions (R2 갱신)

| ID | 상태 | 결정 시점 |
|---|---|---|
| Q-1 (/scene/list reply 양방향) | **RESOLVED**: 미구현 확인, ADR-2 deferred | R2 plan 본문 |
| Q-2 (ui/tests 76 vs 63) | **PENDING**: S0 archaeology 결과 후 ADR-3 결정 | S0 종료 시 |
| Q-3 (50fps p10 N=20 methodology) | **RESOLVED**: desktop 60 / mobile 50 / min-of-5-windows-p10 / N=100 | R2 plan 본문 |
| Q-4 (매뉴얼 챕터 번호) | **RESOLVED**: Ch.6 | R2 plan 본문 |
| Q-5 (실기 smoke) | **RESOLVED**: emulation으로 본 sprint 게이트 충족, 실기는 conditional G8 / out-of-scope | R2 plan 본문 |
| **Q-6 (신규)** (G4 ±2° quant derivation 실측 검증) | **PENDING**: ADM-OSC azim 16-bit float quant 정밀도를 S4에서 측정 후 plan 본문 confirm 또는 5°로 relax | S4 종료 시 |

→ `.omc/plans/open-questions.md` 에 Q-2 / Q-6 동기화 예정.

---

## §13 Changelog (R1 → R2)

- **§0.1 신규**: R1 → R2 anchor 반영 표
- **§0.2 신규**: Out-of-Scope 5 항목 명문화 (Q-1 ADR-2, Q-2 ADR-3, /tmp ADR-4, VST3 hands-on, vid2spatial Phase 2)
- **§1.3**: SHORT + DELIBERATE annex 강화 (pre-mortem P×I, regression→gate 표)
- **§1.4**: Option B + B' 비교 표 추가, escape trigger 명시
- **§2 G2**: methodology 변경 (N=20 → N=100 min-of-5-windows)
- **§2 G3**: dual → triple path (G3a/G3b/G3c, G3c mandatory)
- **§2 G4**: 2 concurrent clients × 30s 추가 + ±2° derivation 명시
- **§2 G7**: governance (pilot day-1 → fixed threshold day-2) + asyncio.all_tasks() slope
- **§3 S0**: 신규 1.0d (verify byte baseline + osc_bridge shutdown + 9100 producer 통합 + Q-2 archaeology)
- **§3 S4**: +0.3d (2-client)
- **§3 S6**: +0.5d (triple path)
- **§5**: pre-mortem 3 → 5 시나리오 (D multi-WS race / E deleted test) + P×I 정량
- **§6**: 5.1~5.6 통합 risk 6개 추가
- **§7**: 6 category × G-gate 매핑 표 + sentinels invocation 16개 명령어 + regression→gate 표
- **§8**: ADR-2/3/4 deferred 명문화, ADR-5 final 형식
- **§9**: ETA 13.5d → 15.3d
- **§10**: falsifiable 항목 강화, regression sentinel 추가
- **§11**: New 12 file 명시
- **§12**: Q-1/3/4/5 RESOLVED, Q-2 PENDING (S0), Q-6 신규 (S4)

---

**End of Round-2 plan. Awaiting Architect/Critic Round-2 review.**
