# Spatial Engine

실시간 객체 기반 몰입형 오디오 렌더링 엔진. 공연·전시 현장용.

C++ 렌더 코어 + FastAPI WebGUI + vid2spatial 영상 추적 브리지.  
OSC/UDP를 통해 모든 컴포넌트가 연결됩니다.

---

## 목차

1. [시스템 구성](#시스템-구성)
2. [빠른 시작](#빠른-시작)
3. [WebGUI 사용법](#webgui-사용법)
4. [vid2spatial 연동](#vid2spatial-연동)
5. [C++ 엔진 바이너리](#c-엔진-바이너리)
6. [빌드 방법](#빌드-방법)
7. [테스트](#테스트)
8. [구현 현황](#구현-현황)
9. [파일 구조](#파일-구조)
10. [포트 할당](#포트-할당)
11. [라이선스](#라이선스)

---

## 시스템 구성

```
┌────────────────────────────────────────────────────────┐
│                    WebGUI (브라우저)                    │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────────┐ │
│  │ 오브젝트  │  │ AI 추적  │  │  vid2spatial 시작/정지 │ │
│  │ 캔버스   │  │ 모드 전환 │  │  버튼 + 상태 표시     │ │
│  └────┬─────┘  └────┬─────┘  └───────────────────────┘ │
└───────┼─────────────┼────────────────────────────────── ┘
        │ WebSocket   │ HTTP API
        ▼             ▼
┌────────────────────────────────────────────────────────┐
│            FastAPI 서버 (server.py, port 8000)          │
│  ┌──────────────────┐  ┌─────────────────────────────┐  │
│  │  osc_bridge.py   │  │  vid2spatial bridge 관리    │  │
│  │  (port 9101 수신) │  │  /api/vid2spatial/start|stop│  │
│  └────────┬─────────┘  └──────────┬──────────────────┘  │
└───────────┼────────────────────────┼──────────────────── ┘
            │ OSC 9101              │ 관리 (daemon thread)
            ▼                       ▼
┌───────────────────┐   ┌──────────────────────────────┐
│  C++ 엔진         │   │  vid2spatial_osc.py (bridge) │
│  spatial_engine   │◄──│  port 9000 수신              │
│  _core            │   │  → /adm/obj/N/aed            │
│  (port 9100 수신) │   │  → port 9100 송신            │
│  VBAP 렌더 + WAV  │   └──────────────┬───────────────┘
└───────────────────┘                  │ OSC 9000
                                       ▼
                           ┌───────────────────────┐
                           │   vid2spatial_v2       │
                           │   영상 추적 → OSC 출력 │
                           └───────────────────────┘
```

### OSC 데이터 흐름

| 방향 | 포트 | 프로토콜 | 내용 |
|------|------|---------|------|
| vid2spatial → bridge | 9000 | `/vid2spatial/spatial [az, el, dist, vel, t]` | 영상 추적 결과 |
| bridge → 엔진 | 9100 | `/adm/obj/N/aed [az_deg, el_deg, dist_norm]` | ADM-OSC 공간 파라미터 |
| WebGUI → 엔진 | 9100 | `/adm/obj/N/aed [az_deg, el_deg, dist_norm]` | 수동 드래그 |
| 엔진 → osc_bridge | 9101 | `/spe/state/*` | 엔진 상태 발행 |
| osc_bridge → 브라우저 | WebSocket | JSON | 캔버스 업데이트 |

---

## 빠른 시작

### 1. 레포 클론 및 C++ 빌드

```bash
git clone git@github.com:paiiek/spatial_engine.git
cd spatial_engine

# C++ 코어 빌드
mkdir -p core/build && cd core/build
/home/seung/miniforge3/bin/cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON
make -j$(nproc)
cd ../..

# 빌드 확인 (34개 테스트)
/home/seung/miniforge3/bin/ctest --test-dir core/build --output-on-failure
```

### 2. Python 의존성 설치

```bash
pip install fastapi uvicorn websockets python-osc pyyaml
# 또는
pip install -r requirements.txt
```

### 3. WebGUI 실행

```bash
# 서버 시작
python3 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000

# 브라우저로 접속
# http://<서버-IP>:8000
```

### 4. C++ 엔진 실행

```bash
# 별도 터미널에서
core/build/spatial_engine_core \
  --layout configs/lab_8ch.yaml \
  --osc-port 9100 \
  --seconds 3600    # 1시간 실행 (0 = 무한)
```

이것으로 WebGUI + 엔진이 연결되어 오브젝트를 드래그하면 8ch VBAP 렌더가 동작합니다.

---

## WebGUI 사용법

브라우저에서 `http://<서버-IP>:8000` 접속.

### 인터페이스 구성

```
┌──────────────────────────────────────────────────────────────────────┐
│  [AI 추적] [저레이턴시]  모드: ai  │  vid2spatial [시작] [정지] 정지됨  │
│                                                                      │
│  씬 이름: [________]  [저장] [로드]                                   │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│                     탑-다운 스피커 레이아웃                            │
│                   (오브젝트 드래그로 위치 조정)                         │
│                                                                      │
│              ●  ← 드래그 가능한 음원 오브젝트                           │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 오브젝트 제어

- **오브젝트 드래그**: 캔버스에서 음원 오브젝트(번호 표시 원)를 마우스로 드래그
- 드래그 시 실시간으로 `/adm/obj/N/aed` OSC 메시지가 엔진(9100)으로 전송
- 엔진이 VBAP 게인을 실시간으로 업데이트

### 모드 전환

| 버튼 | 모드 | 동작 |
|------|------|------|
| AI 추적 | `ai` | vid2spatial 브리지 활성화 — 영상 추적 위치가 엔진에 전달됨 |
| 저레이턴시 | `low_latency` | 브리지 비활성화 — 수동 드래그만 동작 |

모드는 `/tmp/.spe_bridge_mode` 파일로 브리지 프로세스에 전달됩니다.

### vid2spatial 브리지 제어

헤더 우측의 `vid2spatial [시작] [정지]` 버튼:
- **시작**: WebGUI 서버가 포트 9000에서 vid2spatial OSC를 수신하는 브리지를 백그라운드로 시작
- **정지**: 브리지 중단
- **상태 표시**: `실행 중 (:9000)` (초록) / `정지됨` (회색)

### 씬 저장/로드

```
씬 이름 입력 → [저장] 클릭 → /scene/save "씬이름" OSC 전송
씬 이름 입력 → [로드] 클릭 → /scene/load "씬이름" OSC 전송
```

씬은 엔진에서 JSON 파일로 저장됩니다 (64개 오브젝트 위치/게인 포함).

---

## vid2spatial 연동

### 연동 흐름

```
vid2spatial_v2 실행
  → /vid2spatial/spatial [az_deg, el_deg, dist_m, velocity, timecode] → UDP 9000
  → bridge/vid2spatial_osc.py 수신 및 변환
  → /adm/obj/1/aed [az_adm, el_adm, dist_norm] → UDP 9100
  → C++ 엔진 수신 → VBAP 렌더 → 8ch 오디오 출력
```

### 좌표 변환 계약

| 항목 | 변환 공식 | 이유 |
|------|-----------|------|
| 방위각 | `az_adm = -az_pipeline` | vid2spatial RIGHT=+, ADM-OSC LEFT=+ |
| 거리 | `dist_adm = 1.0 - dist_m / 20.0` | 가까울수록 1, 20m 최대 |
| 앙각 | `el_adm = el_deg` (그대로) | 동일 컨벤션 |

### WebGUI에서 vid2spatial 연동하기

**방법 1: WebGUI 버튼 사용 (권장)**

1. 브라우저에서 WebGUI 열기
2. 헤더의 **[AI 추적]** 버튼 클릭 (모드 전환)
3. **[vid2spatial 시작]** 버튼 클릭
4. vid2spatial_v2 실행 (포트 9000으로 OSC 출력)

**방법 2: 수동으로 브리지 실행**

```bash
# 브리지 직접 실행
python3 bridge/vid2spatial_osc.py \
  --listen-port 9000 \
  --target-port 9100 \
  --mode ai

# vid2spatial_v2 실행 (별도 터미널)
cd /path/to/vid2spatial_v2
python3 -m vid2spatial_pkg.run --video input.mp4 --osc-port 9000
```

**방법 3: 영상 없이 테스트 (시뮬레이션)**

```python
# vid2spatial 신호 시뮬레이션
from pythonosc import udp_client
import time, math

c = udp_client.SimpleUDPClient('127.0.0.1', 9000)
for i in range(300):
    t = i / 30.0
    az   = 60 * math.sin(2 * math.pi * t / 4)    # ±60° 진동
    el   = 20 * math.sin(2 * math.pi * t / 6)    # ±20° 진동
    dist = 3.0 + 2.0 * math.sin(2 * math.pi * t / 3)  # 1~5m
    c.send_message('/vid2spatial/spatial', [az, el, dist, 0.0, t])
    time.sleep(1/30)
```

### OSC 메시지 형식 (vid2spatial_v2 출력)

vid2spatial_v2가 포트 9000으로 보내는 메시지:

| 주소 | 타입 | 내용 |
|------|------|------|
| `/vid2spatial/spatial` | `[f, f, f, f, f]` | az_deg, el_deg, dist_m, velocity, timecode |
| `/vid2spatial/azimuth` | `f` | 방위각(도) |
| `/vid2spatial/elevation` | `f` | 앙각(도) |
| `/vid2spatial/distance` | `f` | 거리(정규화 0-1) |

브리지는 이를 수신해 `/adm/obj/N/aed [az, el, dist]` 단일 패킷으로 변환 후 엔진(9100)에 전송합니다.

---

## C++ 엔진 바이너리

### 기본 실행

```bash
# 기본 (포트 9100에서 OSC 수신, 무한 실행)
core/build/spatial_engine_core --layout configs/lab_8ch.yaml --osc-port 9100

# WAV 파일로 캡처 (10초)
core/build/spatial_engine_core \
  --layout configs/lab_8ch.yaml \
  --osc-port 9100 \
  --wav /tmp/output.wav \
  --seconds 10
```

### 전체 플래그

| 플래그 | 기본값 | 설명 |
|--------|--------|------|
| `--layout PATH` | `../configs/lab_8ch.yaml` | 스피커 레이아웃 YAML 파일 |
| `--osc-port N` | 9100 | ADM-OSC UDP 수신 포트 |
| `--wav PATH` | (없음) | 8ch WAV 파일로 오디오 캡처 |
| `--seconds N` | 10 | 실행 시간(초). 0이면 Ctrl-C까지 |
| `--backend NAME` | `null` | `null` 또는 `dante` |
| `--block N` | 64 | 오디오 블록 크기(샘플) |
| `--channels N` | 8 | 출력 채널 수 |
| `--rate N` | 48000 | 샘플레이트(Hz) |

### demo_e2e.py — 엔드투엔드 데모

```bash
# 5개 오브젝트 궤도 운동 데모 (캔버스 + 오디오 동시)
python3 tools/demo_e2e.py --n-objs 5 --duration 30

# vid2spatial traj.json 재생
python3 tools/demo_e2e.py --traj path/to/traj.json

# 옵션
#   --n-objs N       오브젝트 수 (기본 5)
#   --duration N     실행 시간(초) (기본 60)
#   --host IP        엔진 IP (기본 127.0.0.1)
#   --port N         엔진 포트 (기본 9100)
```

### ADM-OSC 형식

엔진이 수신하는 메시지:

| 주소 | 타입 | 내용 |
|------|------|------|
| `/adm/obj/N/aed` | `fff` | 방위각(도), 앙각(도), 거리(정규화 0-1) |
| `/adm/obj/N/azim` | `f` | 방위각(도)만 업데이트 |
| `/adm/obj/N/elev` | `f` | 앙각(도)만 업데이트 |
| `/adm/obj/N/dist` | `f` | 거리(정규화)만 업데이트 |
| `/adm/obj/N/gain` | `f` | 게인(0-1) |
| `/adm/obj/N/mute` | `i` | 음소거 (1=음소거) |

> **주의**: 개별 axis 메시지(`/azim`, `/elev`, `/dist`)는 나머지를 기본값으로 덮어씁니다. **가능하면 `/aed` 통합 메시지를 사용하세요.**

---

## 빌드 방법

### 요구사항

- CMake ≥ 3.20
- GCC 12+ 또는 Clang 14+ (C++20)
- Python 3.10+
- `pip install fastapi uvicorn websockets python-osc pyyaml`

### C++ 빌드

```bash
mkdir -p core/build && cd core/build

# NO_JUCE 빌드 (권장 — POSIX UDP OSC 수신 포함)
/home/seung/miniforge3/bin/cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON

make -j$(nproc)
```

### 빌드 옵션

| CMake 옵션 | 기본 | 설명 |
|-----------|------|------|
| `SPATIAL_ENGINE_NO_JUCE` | OFF | JUCE 없이 빌드. POSIX UDP로 OSC 수신 |
| `SPATIAL_ENGINE_RT_ASSERTS` | ON | RT 스레드 내 동적 할당 감지 |

---

## 테스트

### C++ 테스트 (34개)

```bash
cd core/build
/home/seung/miniforge3/bin/ctest --output-on-failure
# → 100% tests passed, 0 tests failed out of 34
```

### Python 테스트 (44개)

```bash
python3 -m pytest ui/tests/ -q
# → 44 passed, 3 skipped
```

### 엔드투엔드 오디오 검증

```bash
# 엔진 실행 + 5오브젝트 데모
core/build/spatial_engine_core --layout configs/lab_8ch.yaml \
  --osc-port 9100 --wav /tmp/test.wav --seconds 8 &
python3 tools/demo_e2e.py --n-objs 5 --duration 7

# WAV RMS 확인 (t=1s 이후, 초기 무음 기간 제외)
python3 -c "
import struct
with open('/tmp/test.wav','rb') as f: f.read(44); d=f.read()
s=[struct.unpack_from('<h',d,96000*16+i*2)[0] for i in range(8000)]
print(f'RMS={sum(x*x for x in s)**0.5/len(s)**0.5:.0f}  (>100 = 정상)')
"
```

### vid2spatial 연동 검증

```bash
# 1. bridge 시작
python3 bridge/vid2spatial_osc.py &

# 2. 엔진 시작
core/build/spatial_engine_core --layout configs/lab_8ch.yaml \
  --osc-port 9100 --wav /tmp/v2s.wav --seconds 8 &

# 3. vid2spatial 시뮬레이션
python3 -c "
from pythonosc import udp_client; import time, math
c = udp_client.SimpleUDPClient('127.0.0.1', 9000)
for i in range(180):
    t = i/30.; c.send_message('/vid2spatial/spatial',
    [60*math.sin(t), 20*math.sin(t/2), 3+2*math.sin(t/1.5), 0., t]); time.sleep(1/30)
"
# → WAV RMS > 1000 이면 정상
```

---

## 구현 현황

### v0 완료 (2025-01 ~ 2025-02)

| Phase | 내용 |
|-------|------|
| P0 | 레포 스캐폴딩, 부트스트랩, 랩 핀닝 |
| P1 | C++ 코어, NullBackend, RT 어설션 |
| P2 | 좌표 변환, YAML 스피커 레이아웃 로더 |
| P3 | VBAP/WFS/DBAP, GainRamp, 알고리즘 스왑 크로스페이드 |
| P4 | OSC IPC, Command 스키마, HeartbeatPublisher, StateModel |
| P5 | FileInput, SynthInput, LockFreeFloatFifo |
| P6 | DanteBackend 스텁, LiveMicInput |
| P7 | 16-line Hadamard FDN 리버브, IRConvolutionStub |
| P8 | PySide6 UI (드래그 코알레서, 매트릭스 뷰) |
| P9 | BinauralMonitor (KEMAR SOFA), rE/rV 정확도 하네스 |
| P10~P12 | 레이턴시 하네스, Soak 하네스, 문서, 퍼셉추얼 사전 등록 |

### v1 완료 (2025-02 ~ 2025-03)

| 항목 | 내용 |
|------|------|
| ADM-OSC 수신 | `/adm/obj/n/{azim\|elev\|dist\|gain\|mute\|aed}` 파싱 |
| 씬 시스템 | SceneSnapshot JSON 저장/로드, `/scene/save\|load\|list` |
| MAX_OBJECTS 64 | `Constants.h MAX_OBJECTS=64` |
| VBAP 3D | C(N,3) 삼각형, Cramer 규칙, 최근접-3 폴백 |
| AmbisonicEncoder | ACN/SN3D 1차~3차 (4/9/16ch) |
| ElevationView | PySide6 사이드뷰 위젯 |
| MidiBridge | MIDI PC → `/scene/load` OSC |
| VBAPRenderer 캐시 | 0.5° 빈, RT-alloc 없음 |
| BinauralMonitor C++ | SofaBinReader, HrtfLookup, OlaConvolver |

### v1d 완료 (2025-04 ~ 2025-05)

| 항목 | 내용 |
|------|------|
| **WebGUI** | FastAPI + WebSocket + Three.js 캔버스, 오브젝트 드래그 |
| **vid2spatial 브리지** | 이중 모드 (AI 추적/저레이턴시), 파일 IPC |
| **OSCBackend UDP** | POSIX UDP 소켓 수신 (NO_JUCE 경로) |
| **CommandFifo (SPSC)** | lock-free 링 버퍼, OSC→오디오 스레드 RT-safe |
| **SpatialEngine 완전 배선** | obj_cache_ → 사인 오실레이터 → VBAP → 디인터리브 |
| **WavWriter** | 8ch RIFF/WAV 실시간 캡처 |
| **spatial_engine_core** | `--osc-port`, `--wav`, `--layout`, `--seconds` |
| **demo_e2e.py** | 5 오브젝트 궤도 데모, vid2spatial traj.json 재생 |
| **WebGUI vid2spatial 통합** | 시작/정지 버튼, BridgeServer 관리 API |
| **bridge aed 수정** | 3개 분리 메시지 → `/aed` 통합 (축 덮어쓰기 버그 수정) |

### 하드웨어 대기

| 항목 | 내용 |
|------|------|
| Dante PCIe | OSC→Dante p99 레이턴시 실측 |
| 소크 테스트 | 12시간 (RSS 슬로프, xrun 카운트) |
| 퍼셉추얼 실험 | N=12 청취 실험 (사전 등록 완료) |

---

## CI 현황

| 항목 | 결과 |
|------|------|
| C++ 빌드 (NO_JUCE=ON) | ✅ 34/34 ctest |
| Python 테스트 | ✅ 44 passed, 3 skipped |
| RT-alloc 게이트 | ✅ SPE_RT_ASSERTS=ON 통과 |
| 엔드투엔드 WAV | ✅ RMS > 2000 @ t=1s (5 오브젝트, 8ch) |
| vid2spatial 연동 | ✅ RMS > 2000 @ t=1s (bridge 경유) |

---

## 파일 구조

```
spatial_engine/
├── core/                          # C++ 렌더 코어
│   ├── src/
│   │   ├── core/
│   │   │   ├── SpatialEngine.h    # 메인 엔진 클래스
│   │   │   └── SpatialEngine.cpp  # audioBlock() — SPSC FIFO → VBAP → 출력
│   │   ├── render/
│   │   │   ├── VBAPRenderer.cpp   # VBAP 2D/3D + 게인 캐시
│   │   │   ├── DBAPRenderer.cpp   # DBAP
│   │   │   └── WFSRenderer.cpp    # WFS
│   │   ├── ambi/
│   │   │   └── AmbisonicEncoder.cpp  # ACN/SN3D 1차~3차
│   │   ├── reverb/
│   │   │   └── FdnReverb.cpp      # 16-line Hadamard FDN
│   │   ├── binaural/
│   │   │   ├── SofaBinReader.cpp  # KEMAR SOFA 로더
│   │   │   ├── HrtfLookup.cpp     # HRTF 조회
│   │   │   └── OlaConvolver.cpp   # OLA 분할 컨볼루션
│   │   ├── ipc/
│   │   │   ├── OSCBackend.cpp     # POSIX UDP 수신 (NO_JUCE)
│   │   │   ├── CommandDecoder.cpp # ADM-OSC 파싱
│   │   │   └── StateModel.cpp     # 오브젝트 상태 모델
│   │   ├── util/
│   │   │   ├── CommandFifo.h      # SPSC lock-free 링 버퍼
│   │   │   ├── TraceRing.h        # 블록 트레이스
│   │   │   └── XrunCounter.h      # xrun 카운터
│   │   ├── audio_io/
│   │   │   ├── NullBackend.cpp    # 타이밍-정확 null 오디오 루프
│   │   │   └── DanteBackend.cpp   # Dante PCIe 스텁
│   │   ├── geometry/
│   │   │   └── LayoutLoader.cpp   # YAML 스피커 레이아웃
│   │   └── bin/
│   │       ├── spatial_engine_core.cpp  # 메인 바이너리
│   │       ├── WavWriter.h/cpp    # RIFF/WAV 캡처
│   ├── tests/core_unit/           # C++ 유닛 테스트 (34개)
│   └── configs/                   # 스피커 레이아웃 YAML
│
├── configs/
│   └── lab_8ch.yaml              # 8채널 원형 레이아웃 (기본)
│
├── ui/
│   └── webgui/
│       ├── server.py              # FastAPI 서버 + vid2spatial API
│       ├── osc_bridge.py          # OSC 9101 ↔ WebSocket 브리지
│       └── static/
│           └── index.html         # Three.js 캔버스 + 씬 패널 + vid2spatial 버튼
│
├── bridge/
│   └── vid2spatial_osc.py        # vid2spatial → ADM-OSC 변환 브리지
│
├── tools/
│   ├── demo_e2e.py               # 엔드투엔드 오디오 데모 (5 오브젝트 궤도)
│   ├── osc_debug_console.py      # OSC dry-run/UDP 디버그 콘솔
│   ├── sofa_inspector.py         # KEMAR SOFA 메타데이터 검사
│   └── midi_bridge.py            # MIDI PC → /scene/load OSC
│
├── tests/
│   ├── latency_harness/          # 레이턴시 측정
│   ├── soak_harness/             # 12시간 소크 테스트
│   ├── accuracy/                 # rE/rV 정확도
│   └── perceptual/               # N=12 청취 실험 프레임워크
│
└── docs/
    ├── architecture.md            # 시스템 아키텍처
    ├── coordinate_convention.md   # 좌표계 계약
    ├── ipc_schema.md             # OSC IPC 스키마
    ├── latency_budget.md         # 레이턴시 예산
    └── license_procurement_plan.md  # JUCE 라이선스 계획
```

---

## 포트 할당

| 포트 | 프로토콜 | 사용처 | 방향 |
|------|---------|--------|------|
| **8000** | TCP (HTTP/WS) | WebGUI FastAPI 서버 | 브라우저 ↔ 서버 |
| **9000** | UDP (OSC) | vid2spatial OSC 출력 수신 | vid2spatial → bridge |
| **9100** | UDP (OSC) | C++ 엔진 ADM-OSC 수신 | bridge/WebGUI → 엔진 |
| **9101** | UDP (OSC) | 엔진 상태 발행 | 엔진 → osc_bridge |

---

## 라이선스

GPL v3 (연구/랩 전용). 외부 배포·상용 배포 시 JUCE Indie/Pro 라이선스 필요.  
상세: `docs/license_procurement_plan.md`
