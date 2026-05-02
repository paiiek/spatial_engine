# Spatial Engine

실시간 객체 기반 몰입형 오디오 렌더링 엔진. 공연·전시 현장용.  
C++ JUCE 코어 (프로세스 A) + PySide6 UI (프로세스 B), OSC/UDP 루프백 IPC.

---

## 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 렌더링 알고리즘 | VBAP (Pulkki 1997), WFS, DBAP (Lossius 2009) |
| 리버브 | 16-line Hadamard FDN, denormal guard (±1e-20 DC offset) |
| 바이노럴 | KEMAR SOFA (RWTH Aachen, IR_len=384, 64800 positions), 분할 컨볼루션 |
| Ambisonics | ACN/SN3D, W=1.0, 1차~3차 (4/9/16채널) |
| OSC 프로토콜 | ADM-OSC `/adm/obj/{n}/azim\|elev\|dist\|gain\|mute` (포트 9100/9101) |
| 엔진 좌표계 | az=0 → +z(전방), az=π/2 → +x(우측), 우수 좌표계 |
| 최대 오브젝트 | 64개 동시 |
| 오디오 I/O | Digigram ALP-Dante PCIe, JACK/PipeWire, 64 frames @ 48 kHz |
| 레이턴시 목표 | OSC 수신 → Dante PCIe 출력 p99 < 5 ms |

---

## 빌드 요구사항

- CMake ≥ 3.20
- C++20 컴파일러: GCC 12+ / Clang 14+
- Python 3.10+, PySide6 (UI 빌드 시)
- JUCE 없이 빌드 가능: `-DSPATIAL_ENGINE_NO_JUCE=ON`

---

## 빠른 시작 (엔지니어용)

```bash
git clone <repo>
cd spatial_engine
mkdir -p core/build && cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON
make -j$(nproc)
ctest --output-on-failure   # 33개 테스트 모두 통과 확인
```

---

## Python UI 테스트

```bash
cd /path/to/spatial_engine
python3 -m pytest tests/ ui/tests/ -q   # 76 passed, 3 skipped
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

### v0 (완료)

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
| ADM-OSC 수신 네임스페이스 | `/adm/obj/n/{azim\|elev\|dist\|gain\|mute\|aed}` OSC 파싱 |
| 스냅샷/씬 시스템 | SceneSnapshot JSON 저장/로드, `/scene/save\|load\|list` OSC |
| MAX_OBJECTS 16→64 | `Constants.h MAX_OBJECTS=64` |
| IR SOFA 로더 확장 | `IRConvolutionStub::loadFromSofa()` 스텁, `ir_sofa_loader.py` |
| Elevation UI 슬라이더 | `ElevationControl` PySide6 위젯 -90..+90°, `/adm/obj/n/elev` OSC 전송 |
| F1 VBAP 3D | 차원성 프로브, C(N,3) 삼각형 열거, Cramer 규칙, 최근접-3 폴백 |
| F2 AmbisonicEncoder 1차 | ACN/SN3D, W=1.0 |
| F3 ElevationView 사이드뷰 위젯 | r vs y 산점도, headless pytest |
| F4 MidiBridge | MIDI PC→`/scene/load` OSC 브리지, worker thread |

### v1b (완료)

| 항목 | 내용 |
|------|------|
| T2 VBAP 3D 폴백 경로 테스트 | degenerate triplet 폴백, 최근접-3 폴백 커버리지 |
| T3 VBAPRenderer 게인 캐시 | 0.5° 빈, 개방-주소 FIFO, 워밍업 후 RT-alloc 없음 |
| T4 AmbisonicEncoder 2차/3차 | ACN/SN3D 2차(9ch)/3차(16ch), W=1.0 일관성 |

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
| C++ 빌드 (NO_JUCE=ON) | ✅ 33/33 ctest |
| Python 테스트 | ✅ 76 passed, 3 skipped |
| RT-alloc 게이트 | ✅ 통과 (SPE_RT_ASSERTS=ON) |

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

---

## 파일 구조

```
core/
  src/
    render/       VBAP, WFS, DBAP, VBAPRenderer (게인 캐시 포함)
    ambi/         AmbisonicEncoder (1차~3차, ACN/SN3D)
    reverb/       FdnReverb (16-line Hadamard), IRConvolutionStub
    core/         SpatialEngine.cpp — 메인 엔진
    input/        FileInput, SynthInput, LockFreeFloatFifo
    backend/      DanteBackend, NullBackend, LiveMicInput
    ipc/          OSC 파싱, Command 스키마, HeartbeatPublisher
  tests/          C++ 유닛 테스트 (33개)
  configs/        스피커 레이아웃 YAML, 리버브/노이즈 설정

ui/
  spatial_engine_ui/   PySide6 UI (탑-다운 뷰, ElevationView, ElevationControl)

configs/          스피커 레이아웃 설정 파일

tests/
  latency_harness/    레이턴시 측정 하네스
  soak_harness/       소크 테스트 하네스
  accuracy/           rE/rV 정확도 하네스
  perceptual/         퍼셉추얼 청취 실험 프레임워크

tools/
  osc_debug_console.py   OSC dry-run/UDP 디버그 콘솔
  sofa_inspector.py      KEMAR SOFA 메타데이터 검사
  midi_bridge.py         MIDI PC → /scene/load OSC 브리지

docs/
  v0.1.0_report.md       개발 완료 보고서
  latency_budget.md      레이턴시 예산 분석
  license_procurement_plan.md  JUCE 상용 라이선스 취득 계획
```

---

## 라이선스

GPL v3 (연구/랩 전용). 외부 배포·상용 배포 시 JUCE Indie/Pro 라이선스 필요.  
상세: `docs/license_procurement_plan.md`
