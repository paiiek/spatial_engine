# Spatial Engine

실시간 객체 기반 몰입형 오디오 렌더링 엔진. 공연·전시 현장용.  
C++ 코어 + FastAPI WebGUI + vid2spatial 브리지, OSC/UDP IPC.

---

## 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 렌더링 알고리즘 | VBAP 2D/3D (Pulkki 1997), WFS, DBAP (Lossius 2009) |
| 리버브 | 16-line Hadamard FDN, denormal guard (±1e-20 DC offset) |
| 바이노럴 | KEMAR SOFA (RWTH Aachen, IR_len=384, 64800 positions), 분할 컨볼루션 |
| Ambisonics | ACN/SN3D, W=1.0, 1차~3차 (4/9/16채널) |
| OSC 프로토콜 | ADM-OSC `/adm/obj/{n}/azim\|elev\|dist\|gain\|mute\|aed` (포트 9100/9101) |
| 엔진 좌표계 | az=0 → +z(전방), az=π/2 → +x(우측), 우수 좌표계 |
| 최대 오브젝트 | 64개 동시 |
| 오디오 I/O | Digigram ALP-Dante PCIe, JACK/PipeWire, 64 frames @ 48 kHz |
| 레이턴시 목표 | OSC 수신 → Dante PCIe 출력 p99 < 5 ms |

---

## 빌드 요구사항

- CMake ≥ 3.20
- C++20 컴파일러: GCC 12+ / Clang 14+
- Python 3.10+ (WebGUI, 브리지, 테스트)
- JUCE 없이 빌드 가능: `-DSPATIAL_ENGINE_NO_JUCE=ON`

---

## 빠른 시작

```bash
git clone <repo>
cd spatial_engine
mkdir -p core/build && cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON
make -j$(nproc)
cd ../..
/home/seung/miniforge3/bin/ctest --test-dir core/build --output-on-failure
# → 34/34 tests passed
```

---

## WebGUI 실행

```bash
# WebGUI 서버 (FastAPI + WebSocket + Canvas)
python3 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000
# 브라우저: http://<server-ip>:8000
```

---

## 엔드투엔드 오디오 렌더 데모

```bash
# Terminal 1: C++ 엔진 — OSC 수신 + WAV 캡처
core/build/spatial_engine_core \
  --layout configs/lab_8ch.yaml \
  --osc-port 9100 \
  --wav /tmp/spatial_demo.wav \
  --seconds 30

# Terminal 2: 5개 오브젝트 궤도 이동 (WebGUI 캔버스 + 엔진 오디오 동시)
python3 tools/demo_e2e.py --n-objs 5 --duration 30

# WAV 확인
python3 -c "
import struct
with open('/tmp/spatial_demo.wav','rb') as f: f.read(44); d=f.read()
s=[struct.unpack_from('<h',d,i+96000*16)[0] for i in range(0,16000,2)]
rms=(sum(x*x for x in s)/len(s))**.5
print(f'WAV RMS={rms:.0f}  (>10 이면 정상)')
"
```

---

## vid2spatial 브리지

```bash
# vid2spatial 출력(포트 9000) → 엔진(포트 9100) 브리지
python3 bridge/vid2spatial_osc.py --input-port 9000 --output-port 9100

# 모드 전환: WebGUI server.py set_mode() 또는 /tmp/.spe_bridge_mode 파일
```

---

## Python 테스트

```bash
python3 -m pytest ui/tests/ -q   # 44 passed, 3 skipped
```

---

## 하드웨어 연결 테스트 (현장)

```bash
# 레이턴시 측정
python3 tests/latency_harness/run_latency.py

# 소크 테스트 (30분 파일럿)
python3 tests/soak_harness/run_soak.py --duration 1800

# 12시간 풀 소크
python3 tests/soak_harness/run_soak.py --duration 43200
```

---

## 구현 현황

### v0 (완료, commit 97056c6 ~ 1704049)

| Phase | 내용 |
|-------|------|
| P0 | 레포 스캐폴딩, 부트스트랩, 랩 핀닝 |
| P1 | C++ 코어 스켈레톤, NullBackend, AudioDeviceManager |
| P2 | 좌표 변환 모듈, YAML 지오메트리 로더 |
| P3 | Per-object DSP 체인, VBAP/WFS/DBAP, LayoutCompatibilityChecker, GainRamp, 알고리즘 스왑 크로스페이드 |
| P4 | OSC IPC, Command Schema, HeartbeatPublisher (10Hz/300ms), VST3ControlStub, StateModel |
| P5 | FileInput, SynthInput, LockFreeFloatFifo, AudioMatrix |
| P6 | DanteBackend 스텁, LiveMicInput, dante_loopback 테스트 |
| P7 | 16-line Hadamard FDN 리버브, IRConvolutionStub, ReverbEngine, denormal guard |
| P8 | PySide6 UI: OSC IPC, 드래그 코알레서(120Hz), 매트릭스 뷰, 상태 인디케이터 |
| P9 | BinauralMonitor 스텁(KEMAR SOFA), 수치 정확도 하네스(rE/rV), ITD 컨벤션 테스트 |
| P10 | 레이턴시 하네스 (dry-run 스캐폴드, 스키마 테스트, latency_budget.md) |
| P11 | Soak 하네스, ObservabilityCounters, 3U 랙 제약 문서 |
| P12 | 전체 문서, 퍼셉추얼 사전 등록(M2), Stage-1 레이턴시 하네스, license_procurement_plan.md |

### v1 (완료)

| 항목 | 내용 |
|------|------|
| ADM-OSC 수신 | `/adm/obj/n/{azim\|elev\|dist\|gain\|mute\|aed}` 파싱, ObjMute 태그 |
| 스냅샷/씬 시스템 | SceneSnapshot JSON 저장/로드, `/scene/save\|load\|list` OSC |
| MAX_OBJECTS 16→64 | `Constants.h MAX_OBJECTS=64` |
| IR SOFA 로더 | `IRConvolutionStub::loadFromSofa()` 스텁, `ir_sofa_loader.py` |
| F1 VBAP 3D | 차원성 프로브, C(N,3) 삼각형 열거, Cramer 규칙, 최근접-3 폴백 |
| F2 AmbisonicEncoder | ACN/SN3D 1차~3차 (4/9/16ch), W=1.0 |
| F3 ElevationView | PySide6 사이드뷰 위젯 (r vs y 산점도) |
| F4 MidiBridge | MIDI PC → `/scene/load` OSC 브리지, worker thread |

### v1b (완료)

| 항목 | 내용 |
|------|------|
| T2 VBAP 3D 폴백 | degenerate triplet 폴백, 최근접-3 폴백 커버리지 테스트 |
| T3 VBAPRenderer 게인 캐시 | 0.5° 빈, 개방-주소 FIFO, 워밍업 후 RT-alloc 없음 |
| T4 AmbisonicEncoder 2/3차 | ACN/SN3D 2차(9ch)/3차(16ch), W=1.0 일관성 |

### v1c (완료)

| 항목 | 내용 |
|------|------|
| BinauralMonitor C++ | SofaBinReader, HrtfLookup, OlaConvolver 순수 C++ HRTF 파이프라인 |
| 버그 수정 | OlaConvolver alloc-free process(), SofaBinReader 방어적 검증 |

### v1d — WebGUI + 오디오 렌더 체인 (완료)

| 항목 | 내용 |
|------|------|
| WebGUI Phase 1 | FastAPI + WebSocket + Three.js 캔버스, 오브젝트 드래그, OSC 전송 |
| WebGUI Phase 2 | vid2spatial 이중 모드 브리지 (WebGUI↔vid2spatial 전환), `/tmp/.spe_bridge_mode` IPC |
| OSCBackend POSIX UDP | `NO_JUCE` 경로에 실제 UDP 소켓 리스너 (포트 9100) |
| CommandFifo (SPSC) | lock-free 링 버퍼, OSC 스레드 → 오디오 스레드 RT-safe 커맨드 전달 |
| SpatialEngine 전체 배선 | obj_cache_ (StateModel seq=0 드롭 우회), 오브젝트별 사인 오실레이터, VBAP processBlock → 디인터리브 → output_channels |
| WavWriter | RIFF/WAV 캡처 (`--wav` 플래그), 8ch 16-bit PCM 실시간 기록 |
| spatial_engine_core 바이너리 | `--osc-port`, `--wav`, `--layout`, `--seconds` 플래그 추가 |
| demo_e2e.py | 5 오브젝트 궤도 데모, vid2spatial traj.json 재생, 독립 OSC 엔코더 |
| 좌표 계약 | `az_adm = -az_pipeline`, `dist_adm = 1 - dist_m/20.0` (ADM-OSC 좌/우, 20m 최대) |
| 버그 수정 | osc_bridge 9100 포트 선점 제거, WebSocket JSON.parse 이중 파싱, scene panel JS |

### 하드웨어 대기

| 항목 | 내용 |
|------|------|
| P10 | OSC→Dante p99 레이턴시 실측 |
| P11 | 12시간 소크 (RSS 슬로프, xrun 카운트) |
| P12 | 퍼셉추얼 N=12 청취 실험 (사전 등록 완료) |

---

## CI 현황

| 항목 | 결과 |
|------|------|
| C++ 빌드 (NO_JUCE=ON) | ✅ 34/34 ctest |
| Python 테스트 (ui/tests/) | ✅ 44 passed, 3 skipped |
| RT-alloc 게이트 | ✅ 통과 (SPE_RT_ASSERTS=ON) |
| 엔드투엔드 WAV RMS | ✅ RMS > 4000 @ t=1s (5 오브젝트, 8ch, 8초) |

---

## 테스트 바이너리 목록 (ctest -N 기준)

| # | 테스트 이름 | 내용 |
|---|------------|------|
| 1 | p0_smoke | 기본 스모크: 엔진 인스턴스 생성/소멸 |
| 2 | p1_null_backend | NullBackend 오디오 루프 기본 동작 |
| 3 | p1_max_block_boundary | 최대 블록 경계(64 frames) 처리 |
| 4 | p1_trace_ring | TraceRing 링 버퍼 오버플로·워크어라운드 |
| 5 | p2_coords | 좌표 변환 (극좌표 ↔ 데카르트) 정확도 |
| 6 | p2_layout_loader | YAML 스피커 레이아웃 로더 |
| 7 | p2_compat_checker | LayoutCompatibilityChecker 규칙 검증 |
| 8 | p1_rt_no_alloc | RT 스레드 내 동적 할당 없음 (SPE_RT_ASSERTS) |
| 9 | p3_vbap | VBAP 2D 게인 합산, 팬닝 정확도 |
| 10 | p3_dbap | DBAP 거리 기반 팬닝 |
| 11 | p3_wfs | WFS 지연·게인 계산 |
| 12 | p3_compat_rules | 알고리즘 호환 규칙 (레이아웃 제약) |
| 13 | p3_distance_hf | 거리 기반 HF 롤오프 |
| 14 | p3_gainramp_click | GainRamp 클릭 방지 (선형 램프) |
| 15 | p3_propdelay_sweep | 전파 지연 스윕 정확도 |
| 16 | p3_algoswap_crossfade | 알고리즘 스왑 크로스페이드 무음 전환 |
| 17 | p4_handshake | OSC 프로토콜 핸드쉐이크 |
| 18 | p4_command_decode | Command 스키마 디코드 |
| 19 | p4_state_model_seq | StateModel 시퀀스 일관성 |
| 20 | p4_reorder_alert | 패킷 재정렬 경보 |
| 21 | p4_heartbeat_miss | 하트비트 미스 게이트 (300ms) |
| 22 | p4_vst3stub | VST3ControlStub 기본 동작 |
| 23 | p4_flood_valid | 유효 OSC 플러드 처리 |
| 24 | p4_flood_malformed | 비정상 OSC 플러드 방어 |
| 25 | p5_decoder_underflow | LockFreeFloatFifo 디코더 언더플로 처리 |
| 26 | p6_dante_loopback | DanteBackend 루프백 스텁 |
| 27 | p7_fdn_reverb | 16-line Hadamard FDN 리버브, denormal guard |
| 28 | p_adm_osc | ADM-OSC 네임스페이스 파싱 (`/adm/obj/n/*`) |
| 29 | p_scene | SceneSnapshot 저장/로드/리스트 OSC |
| 30 | p_max_objects | MAX_OBJECTS=64 CPU 예산 확인 |
| 31 | p_vbap3d | VBAP 3D elevation 수치 (el=0 gain-sum=1, el=±90°) |
| 32 | p_vbap_cache | VBAPRenderer 게인 캐시 RT-alloc 없음 검증 |
| 33 | p_ambi | AmbisonicEncoder 1차/2차/3차 ACN/SN3D 정확도 |
| 34 | p_adm_osc_ext | ADM-OSC 확장 커맨드 (aed 3-float, ObjMute) |

---

## 파일 구조

```
core/
  src/
    render/       VBAP 2D/3D, WFS, DBAP, VBAPRenderer (게인 캐시)
    ambi/         AmbisonicEncoder (1차~3차, ACN/SN3D)
    reverb/       FdnReverb (16-line Hadamard), IRConvolutionStub
    binaural/     SofaBinReader, HrtfLookup, OlaConvolver (KEMAR SOFA)
    core/         SpatialEngine.cpp — 메인 엔진 (SPSC FIFO, VBAP 배선)
    util/         CommandFifo (SPSC), TraceRing, XrunCounter
    input/        FileInput, SynthInput, LockFreeFloatFifo
    audio_io/     DanteBackend, NullBackend, LiveMicInput
    ipc/          OSCBackend (POSIX UDP), Command 스키마, HeartbeatPublisher
    bin/          spatial_engine_core 바이너리, WavWriter
  tests/          C++ 유닛 테스트 (34개)
  configs/        스피커 레이아웃 YAML

ui/
  webgui/
    server.py        FastAPI + WebSocket 서버
    osc_bridge.py    OSC ↔ WebSocket 브리지 (포트 9101)
    static/          index.html (Three.js 캔버스, 씬 패널)

configs/
  lab_8ch.yaml     8채널 원형 스피커 레이아웃

bridge/
  vid2spatial_osc.py   vid2spatial → 엔진 OSC 브리지 (이중 모드)

tools/
  demo_e2e.py          엔드투엔드 데모 (5 오브젝트 궤도)
  osc_debug_console.py OSC dry-run/UDP 디버그 콘솔
  sofa_inspector.py    KEMAR SOFA 메타데이터 검사
  midi_bridge.py       MIDI PC → /scene/load OSC 브리지

tests/
  latency_harness/    레이턴시 측정 하네스
  soak_harness/       소크 테스트 하네스
  accuracy/           rE/rV 정확도 하네스
  perceptual/         퍼셉추얼 청취 실험 프레임워크

docs/
  architecture.md            시스템 아키텍처
  coordinate_convention.md   좌표계 계약 (ADM-OSC ↔ 엔진)
  ipc_schema.md              OSC IPC 스키마
  latency_budget.md          레이턴시 예산 분석
  license_procurement_plan.md JUCE 상용 라이선스 취득 계획
```

---

## 포트 할당

| 포트 | 용도 |
|------|------|
| 9000 | vid2spatial OSC 출력 (bridge 수신) |
| 9100 | spatial_engine_core ADM-OSC 수신 (C++ 엔진) |
| 9101 | spatial_engine_core 상태 발행 (osc_bridge 수신) |
| 8000 | WebGUI HTTP + WebSocket |

---

## 라이선스

GPL v3 (연구/랩 전용). 외부 배포·상용 배포 시 JUCE Indie/Pro 라이선스 필요.  
상세: `docs/license_procurement_plan.md`
