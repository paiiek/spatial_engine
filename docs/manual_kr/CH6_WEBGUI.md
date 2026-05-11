# Ch.6 — WebGUI: 브라우저 기반 공간음향 제어

**버전:** v0.3.0 (WebGUI Phase 1 MVP)  
**대상 독자:** 라이브 공연장 음향 엔지니어, 시스템 운용자  
**최종 수정:** 2026-05-12

---

## 목차

- [6.1 개요](#61-개요)
- [6.2 빠른 시작 (Quick Start)](#62-빠른-시작-quick-start)
- [6.3 4-Layer 토폴로지](#63-4-layer-토폴로지)
- [6.4 핵심 기능](#64-핵심-기능)
- [6.5 정량 게이트 (Phase 1 MVP 통과)](#65-정량-게이트-phase-1-mvp-통과)
- [6.6 한계 및 알려진 이슈](#66-한계-및-알려진-이슈)
- [6.7 트러블슈팅](#67-트러블슈팅)
- [6.8 운영 가이드 (라이브 공연)](#68-운영-가이드-라이브-공연)
- [6.9 참조](#69-참조)

---

## 6.1 개요

WebGUI는 별도 소프트웨어 설치 없이 브라우저만으로 Spatial Engine의 64개 오디오 오브젝트를 실시간으로 제어하는 인터페이스다. 공연장 내 어디서든 같은 LAN에 연결된 iPad, 태블릿, 노트북으로 접속해 사용할 수 있다.

### 시스템 토폴로지

```
브라우저 (HTML5 Canvas)
    ↕  WebSocket  /ws
uvicorn FastAPI 서버 (포트 8000)
    ↕  UDP OSC
osc_bridge (TX 포트 9100 / RX 포트 9101)
    ↕  UDP OSC
spatial_engine_core standalone (포트 9100/9101)
    [선택]
vid2spatial_osc 브리지 (AI 모드: 포트 9000 → 9100)
```

### 사용 시나리오

| 시나리오 | 설명 |
|----------|------|
| 데스크탑 mixer + iPad 태블릿 동시 제어 | 메인 콘솔 엔지니어와 스테이지 어시스턴트가 각자 다른 기기에서 동시 조작 |
| 콘솔 페어링 없는 경량 데모 | 노트북 한 대로 WebGUI + Core 를 실행하여 공간음향 시연 |
| 라이브 공연 — 무대 위 obj 위치 시각화 | top-down 캔버스에서 오브젝트 이동을 실시간으로 확인하며 운용 |

---

## 6.2 빠른 시작 (Quick Start)

처음 5분 안에 WebGUI를 띄우는 최단 경로다.

### 전제 조건

- Python 패키지가 이미 설치된 환경 (`uv sync` 또는 `pip install -r requirements-dev.txt` 완료)
- `spatial_engine_core` 바이너리 빌드 완료
- FastAPI, pythonosc 패키지는 `requirements-dev.txt` 에 포함되어 있어 별도 설치 불필요

### 실행 순서

**Terminal 1 — Core 시작:**

```bash
./build/core/spatial_engine_core
```

정상 기동 시 출력 예시:

```
spatial_engine_core v0.2.0 (full render chain)
  MAX_OBJECTS=64  MAX_BLOCK=512
  OSC listener: 0.0.0.0:9100 (ADM-OSC /adm/obj/N/aed)
  running — Ctrl-C to stop...
```

**Terminal 2 — WebGUI 서버 시작:**

```bash
python3 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000
```

정상 기동 시 출력 예시:

```
INFO:     Uvicorn running on http://0.0.0.0:8000 (Press CTRL+C to quit)
INFO:     osc_bridge ready — tx=9100 rx=9101
```

**브라우저 접속:**

- 로컬: `http://localhost:8000`
- 공연장 내 다른 기기: `http://<호스트 IP>:8000`
- iPad Safari: 동일 LAN 접속 후 위 URL 입력

접속 직후 top-down 캔버스와 elevation 캔버스가 표시되고 오브젝트 목록이 보이면 정상이다.

---

## 6.3 4-Layer 토폴로지

WebGUI 시스템은 네 개의 계층으로 구성된다.

```
┌────────────────────────────────────────────────────────────────┐
│  Layer 1  브라우저 (HTML5 Canvas)                               │
│  · top-down 캔버스 + elevation 캔버스 — 오브젝트 드래그        │
│  · WebSocket /ws 을 통해 서버와 양방향 통신                     │
└──────────────────────┬─────────────────────────────────────────┘
                       │  WebSocket  ws://<host>:8000/ws
┌──────────────────────▼─────────────────────────────────────────┐
│  Layer 2  FastAPI 서버 (uvicorn, 포트 8000)                     │
│  · REST API: /api/mode, /api/vid2spatial/start|stop, /health   │
│  · WebSocket 브로드캐스트 — 멀티 클라이언트 동기화             │
│  · osc_bridge 관리 (lifespan 훅으로 shutdown 보장)             │
└──────────────────────┬─────────────────────────────────────────┘
                       │  UDP OSC  127.0.0.1:9100/9101
┌──────────────────────▼─────────────────────────────────────────┐
│  Layer 3  osc_bridge (ThreadingOSCUDPServer)                   │
│  · TX 포트 9100 (WebGUI → Core 명령 전송)                      │
│  · RX 포트 9101 (Core → WebGUI 상태 수신)                      │
│  · mode toggle (ADR-0013): AI 모드 / 저레이턴시 모드           │
└──────────────────────┬─────────────────────────────────────────┘
                       │  UDP OSC
┌──────────────────────▼─────────────────────────────────────────┐
│  Layer 4  spatial_engine_core standalone                        │
│  · OSC 수신: 0.0.0.0:9100                                      │
│  · OSC 송신 (상태 브로드캐스트): 0.0.0.0:9101                  │
└────────────────────────────────────────────────────────────────┘
          [선택]
┌───────────────────────────────────────────────────────────────┐
│  vid2spatial_osc 브리지 (AI 모드)                              │
│  · 영상 트래킹 결과 → OSC 변환                                 │
│  · 포트 9000 수신 → 포트 9100 송신                             │
│  · AI 모드 활성 시 WebGUI 드래그 입력 차단 (ADR-0013)          │
└───────────────────────────────────────────────────────────────┘
```

---

## 6.4 핵심 기능

### 6.4.1 오브젝트 드래그

- **top-down 캔버스**: 오브젝트를 클릭-드래그하여 방위각(az)과 거리(dist) 조정
- **elevation 캔버스**: 고도각(el) 조정
- 최대 64개 오브젝트 동시 표시 및 조작
- 드래그 즉시 WebSocket → osc_bridge → Core 경로로 `/obj/{id}/pos` OSC 명령 전송

### 6.4.2 모드 전환 (AI / 저레이턴시)

WebGUI 우측 상단 또는 REST API로 모드를 전환한다.

```
POST http://<host>:8000/api/mode?mode=ai
POST http://<host>:8000/api/mode?mode=low_latency
```

| 모드 | 9100 포트 송신 주체 | 설명 |
|------|---------------------|------|
| `ai` | vid2spatial_osc 브리지 전용 | AI 영상 트래킹 기반 자동 위치 제어 |
| `low_latency` | WebGUI osc_bridge 전용 | 수동 드래그 또는 외부 OSC 제어 |

두 모드는 동시에 9100 포트를 사용하지 않는다 (ADR-0013 Single Producer 원칙).  
모드 상태는 `/tmp/.spe_bridge_mode` 파일에 기록된다.

### 6.4.3 씬 저장 및 불러오기

WebSocket 메시지로 씬을 저장하고 불러올 수 있다.

**저장:**
```json
{"type": "scene_save", "name": "act1_opening"}
```

이 메시지는 osc_bridge를 통해 Core에 `/scene/save act1_opening` OSC 명령으로 전달된다.

**불러오기:**
```json
{"type": "scene_load", "name": "act1_opening"}
```

> **주의:** 씬 목록 조회(`scene_list`)는 현재 Core에서 응답을 반환하지 않는다. 6.6절 알려진 이슈 참조.

### 6.4.4 Transport 제어

WebGUI 하단 Transport 바에서 재생/정지를 제어한다.

```json
{"type": "transport", "action": "play"}
{"type": "transport", "action": "stop"}
```

### 6.4.5 Object DSP 및 알고리즘 선택

오브젝트 패널에서 렌더링 알고리즘을 오브젝트별로 선택한다.

| 값 | 알고리즘 | 적합한 음원 |
|----|----------|-------------|
| `vbap` | VBAP | 악기, 보컬, 대사 |
| `wfs` | WFS | 라인 어레이 환경 |
| `dbap` | DBAP | 앰비언트, 효과음 |

알고리즘 전환 시 Core에서 256-샘플 크로스페이드가 자동 적용된다.

### 6.4.6 Trajectory Animation

오브젝트에 미리 정의된 궤적 애니메이션을 적용한다.

```json
{"type": "trajectory", "obj_id": 0, "pattern": "circle", "radius_m": 2.0, "period_s": 4.0}
{"type": "trajectory", "obj_id": 1, "pattern": "line", "from_az": -45, "to_az": 45, "period_s": 3.0}
{"type": "trajectory", "obj_id": 2, "pattern": "lissajous", "a": 1, "b": 2, "period_s": 8.0}
```

지원 패턴: `circle`, `line`, `lissajous`

### 6.4.7 vid2spatial 켜기/끄기

```
POST http://<host>:8000/api/vid2spatial/start
POST http://<host>:8000/api/vid2spatial/stop
```

`start` 호출 시 vid2spatial_osc 브리지 프로세스를 시작하고 모드를 자동으로 `ai` 로 전환한다.  
`stop` 호출 시 브리지를 종료하고 모드를 `low_latency` 로 복원한다.

---

## 6.5 정량 게이트 (Phase 1 MVP 통과)

v0.3.0 WebGUI Phase 1 MVP에서 측정된 실측값이다. 모든 게이트가 임계값 이내로 통과했다.

| 게이트 | 항목 | 임계값 | 실측값 | 결과 |
|--------|------|--------|--------|------|
| G2 | Canvas 렌더링 fps (데스크탑 p10) | 60 fps 이상 | 60 fps | PASS |
| G2 | Canvas 렌더링 fps (모바일 p10) | 50 fps 이상 | 60 fps | PASS |
| G3a | OSC RTT p99 — mock | 5 ms 이하 | 2.12 ms | PASS |
| G3b | OSC RTT p99 — live UDP | 20 ms 이하 | 2.61 ms | PASS |
| G3c | OSC RTT p99 — e2e wire | 30 ms 이하 | 2.77 ms | PASS |
| G4 | 방위각 오차 (2-client 동시) | 2° 이하 | 0.09° | PASS |
| G4 | 멀티 클라이언트 브로드캐스트 drop | 1% 이하 | 0% | PASS |
| G5 | Scene TX SHA256 검증 | 3/3 통과 | 3/3 | PASS |
| G7 | 48h soak RSS slope (day-1 pilot) | pilot × 3 또는 50 MB/h 이하 | 2.25 MB/h (threshold 6.75 MB/h) | PASS |
| G7 | asyncio 태스크 누수 | 0 | 0 | PASS |
| G7 | fd delta (48h 후) | 최소 | 1 | PASS |

> **G2 모바일 참고:** Playwright 소프트웨어 rasterizer 환경에서 측정. 실제 iPad smoke 테스트는 별도 QA 큐.

---

## 6.6 한계 및 알려진 이슈

### Q-1 (Deferred) — scene/list 응답 없음

`scene_list` 요청 시 UI에서 씬 목록이 표시되지 않는다.  
Core의 `SceneController.cpp:26-28` 은 씬을 메모리에 저장만 하며, `CommandDecoder.cpp:371-373` 은 decode-only 상태로 OSC reply를 전송하지 않는다.  
별도 plan에서 양방향 reply 구현 예정 (ADR-2).

### /tmp/.spe_bridge_mode — 멀티 사용자 충돌 가능

같은 머신에서 여러 사용자가 WebGUI를 동시에 실행하면 `/tmp/.spe_bridge_mode` 파일 경쟁 조건이 발생할 수 있다.  
v0.4에서 `$XDG_RUNTIME_DIR` 마이그레이션 예정 (ADR-4).

### 9100 wire 충돌 — mode toggle 강제

WebGUI와 vid2spatial이 동시에 9100 포트로 송신하면 race 조건이 발생한다.  
ADR-0013에 따라 모드 전환 시 이전 producer를 반드시 비활성화해야 한다.

- `ai` 모드: vid2spatial_osc 브리지만 9100 송신, WebGUI osc_bridge 송신 차단
- `low_latency` 모드: WebGUI osc_bridge만 9100 송신, vid2spatial_osc 브리지 차단

### Mobile viewport canvas width=0 collapse

일부 모바일 portrait 환경에서 `#topdown-canvas` 가 width=0 으로 collapse 되어 fps가 0으로 떨어진다.  
**임시 해결책:** 기기를 landscape 방향으로 회전하거나 브라우저의 split-pane 모드 사용.  
정식 수정은 follow-up sprint에서 진행 예정.

---

## 6.7 트러블슈팅

### WebSocket 연결이 안 됨

1. 방화벽에서 포트 8000 (WebSocket/HTTP), 9100, 9101 (UDP OSC) 가 열려 있는지 확인한다.

```bash
sudo ufw status | grep -E '8000|9100|9101'
```

2. uvicorn 서버가 실행 중인지 확인한다.

```bash
curl http://<host>:8000/health
```

정상 응답:
```json
{"status": "ok"}
```

3. iPad 또는 외부 기기에서 접속할 때는 호스트 IP 주소를 정확히 입력한다 (`localhost` 는 본인 기기를 가리키므로 사용 불가).

### 60 fps 미달

1. 브라우저 GPU 가속이 활성화되어 있는지 확인한다 (크롬 계열: `chrome://gpu` → Hardware Accelerated 확인).
2. 다른 무거운 탭을 닫는다.
3. 모바일 기기에서 portrait 방향일 경우 landscape 로 회전한다.

### 씬 저장이 동작하지 않음

Core standalone 이 실행되지 않은 상태에서는 `/scene/save` OSC 명령 수신자가 없어 저장이 이루어지지 않는다.  
Terminal 1 에서 `spatial_engine_core` 가 정상 실행 중인지 확인한다.

### 오브젝트 위치가 반영되지 않음

모드가 `ai` 로 설정된 상태에서 WebGUI 드래그는 무시된다.  
오른쪽 상단의 모드 표시가 `low_latency` 인지 확인하거나 다음 명령으로 직접 전환한다.

```bash
curl -X POST "http://<host>:8000/api/mode?mode=low_latency"
```

### osc_bridge 종료 후 fd 누수 의심

uvicorn을 Ctrl-C 로 종료하면 lifespan `finally` 블록에서 `osc_bridge.shutdown()` 이 자동으로 호출되어 소켓 fd 를 닫는다.  
비정상 종료(SIGKILL 등) 후 남아있는 소켓을 정리하려면 다음 명령을 사용한다.

```bash
ss -ulnp | grep -E '9100|9101'
kill <pid>
```

---

## 6.8 운영 가이드 (라이브 공연)

### 공연 전 체크리스트

| 순서 | 항목 | 명령 / 방법 | 정상 기준 |
|------|------|-------------|-----------|
| 1 | Core 빌드 정합 검증 | `python3 scripts/verify_byte_baseline.py --strict` | exit 0 |
| 2 | Core 기동 | `./build/core/spatial_engine_core` | 포트 9100 바인드 메시지 출력 |
| 3 | WebGUI 서버 기동 | `python3 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000` | `osc_bridge ready` 메시지 출력 |
| 4 | Health 확인 | `curl http://<host>:8000/health` | `{"status":"ok"}` |
| 5 | 브라우저 접속 확인 | `http://<host>:8000` | 캔버스 표시, 오브젝트 목록 출력 |
| 6 | iPad / 태블릿 접속 | Safari → 동일 URL | 캔버스 정상 표시 |
| 7 | 오브젝트 드래그 테스트 | top-down 캔버스에서 오브젝트 이동 | Core 로그에 `/obj/{id}/pos` 수신 확인 |
| 8 | 모드 확인 | UI 상단 모드 표시 | `low_latency` (기본값) |

### 공연 중 운용

- **헬스 모니터링:** 30초마다 `curl http://<host>:8000/health` 응답 확인 (자동화 권장)
- **멀티 클라이언트 동시 접속:** iPad + 데스크탑 최대 2개 클라이언트 동시 접속 검증 완료 (drop 0%)
- **씬 전환:** WebSocket `scene_load` 메시지로 즉각 전환 가능

### 24시간 이상 운영 시

사전에 soak harness 를 병행 실행하여 메모리/fd 누수를 모니터링한다.

```bash
# soak harness 실행 (별도 터미널)
python3 tests/soak_harness/run_soak.py --duration 3600 --interval 60
```

soak harness 설명은 `tests/soak_harness/SOAK_WEBGUI_README.md` 참조.  
Day-1 pilot 결과 RSS slope: 2.25 MB/h → Day-2 임계값: 6.75 MB/h (50 MB/h 상한 이하).

### 공연 종료

1. 브라우저 탭 닫기
2. WebGUI 서버 종료: Ctrl-C (osc_bridge 자동 shutdown)
3. Core 종료: Ctrl-C

---

## 6.9 참조

### ADR (Architecture Decision Records)

| ADR | 제목 | 내용 |
|-----|------|------|
| ADR-0013 | 9100 producer mode toggle | AI / low_latency 모드에서 9100 포트 단일 producer 보장 |
| ADR-0014 | ui/tests N=63 frozen | Q-2 archaeology 결과 — 63개 테스트 기준 동결 |
| ADR-2 (deferred) | scene/list reply 양방향 | Core OSC reply 미구현 — 별도 plan |
| ADR-4 (deferred) | /tmp → $XDG_RUNTIME_DIR | 멀티 사용자 bridge mode 파일 마이그레이션 — v0.4 예정 |

### 측정 보고서 및 관련 문서

| 문서 | 경로 | 내용 |
|------|------|------|
| Phase 1 베이스라인 측정 | `.omc/research/spatial-engine-webgui-v1-baseline.json` | pytest/ctest/soak 실측 결과 전체 |
| Soak harness 가이드 | `tests/soak_harness/SOAK_WEBGUI_README.md` | 48h soak 실행 방법 및 해석 |
| WebGUI v1 플랜 | `.omc/plans/spatial-engine-webgui-v1.md` | G-게이트 정의, ADR 전체, 설계 결정 근거 |
| IPC 스키마 | `docs/ipc_schema.md` | 전체 OSC 메시지 스펙 |
| 좌표 규약 | `docs/coordinate_convention.md` | RIGHT=+az 파이프라인 프레임 정의 |
| vid2spatial OSC 계약 | `docs/adr/vid2spatial_osc_contract.md` | 포트 9000→9100 브리지 통신 규약 |
