# Plan: spatial_engine Phase B — 디코더 고도화 + 외부 IR + UI 완성 + Snapshot/VST3/타임코드

## Header
- **Project**: `spatial_engine`
- **Plan ID**: `spatial-engine-phaseB`
- **Date**: 2026-05-03
- **Status**: `READY-FOR-AUTOPILOT`
- **Predecessor**: `.omc/plans/spatial-engine-phaseA-feature-parity.md` (M3, M2-1차, M1, M6, M9, M8 완료, ctest 39/39, pytest 160+)

---

## 배경
Phase A 종료 시점의 알려진 한계 + 사용자 우선순위:
1. **HOA decoder 1차만** → 2nd/3rd order + AllRAD 디코더 추가 필요
2. **DBAP width 조잡 근사** → 다중 가상 소스 기반 정밀 spread 모델로 교체
3. **IR Reverb 디폴트 합성 IR** → 외부 무료 IR 셋 다운로드 + 실측 로딩 검증
4. **Trajectory UI 패널 부재** → canvas.js 에 trajectory 컨트롤 추가
5. **pytest 모듈명 충돌** (`tests/accuracy_harness/test_accuracy.py` ↔ `bridge/tests/test_accuracy.py`) → 해결
6. **M4 Snapshot Crossfade** (Phase A 이연)
7. **M5 VST3 플러그인** (Phase A 이연, stub → 기본 동작)
8. **M7 MTC/LTC 동기** (Phase A 이연)

---

## RALPLAN-DR Summary

### Principles (5)
1. **No regression**: ctest 39/39 + pytest 160+ 기준선 절대 유지.
2. **RT-safe**: audio thread 에 alloc/lock/syscall 없음. 신규 모듈 prepareToPlay 사전할당.
3. **Headless CI**: SPATIAL_ENGINE_NO_JUCE=ON + headless pytest 통과.
4. **단일 책임 커밋**: 각 항목 독립 커밋. 종속성 명시.
5. **외부 데이터**: IR / SOFA 등 외부 다운로드는 `assets/` 디렉토리에 캐싱, .gitignore.

### Execution Order (의존성 + ROI 순)

| 순 | ID | 항목 | 추정 | 종속 |
|---|---|---|---|---|
| 1 | B1 | pytest 모듈 충돌 해결 | 30분 | 없음 |
| 2 | B2 | Trajectory UI 패널 (canvas.js) | 1시간 | M8 |
| 3 | B3 | IR 외부 셋 다운로드 + 로딩 검증 | 1시간 | M3 |
| 4 | B4 | DBAP width 정밀화 (다중 가상소스) | 1시간 | M1 |
| 5 | B5 | HOA Decoder 2nd/3rd order + AllRAD | 2시간 | M2 |
| 6 | M4 | Snapshot Crossfade (시간 보간) | 2시간 | 없음 |
| 7 | M5 | VST3 플러그인 (JUCE 빌드) | 3시간 | 없음 |
| 8 | M7 | MTC/LTC 동기 (LTC reader) | 2시간 | 없음 |

각 항목 종료 시: build + test + 단일 커밋. 회귀 없음 검증 후 다음 항목 진행.

---

## Per-Item Specification

### B1 — pytest 모듈명 충돌 해결
**현재 상태**: `tests/accuracy_harness/test_accuracy.py` 와 `bridge/tests/test_accuracy.py` 동일 basename 으로 collection 시 conflict. 영역별 분리 실행만 통과.

**해결**:
- `tests/accuracy_harness/test_accuracy.py` → `tests/accuracy_harness/test_phase2_accuracy.py` 또는 `conftest.py` 에 `__init__.py` 추가 + `--import-mode=importlib` 디폴트 설정.
- `pytest.ini` 또는 `pyproject.toml` 의 `[tool.pytest.ini_options]` 에 `addopts = --import-mode=importlib` 추가가 가장 깔끔.
- 또는 `tests/accuracy_harness/conftest.py` 추가 + `tests/__init__.py` 추가로 패키지 경로 명시.

**Acceptance**:
- `python3 -m pytest -q` (프로젝트 루트) → collection error 없음.
- 모든 영역 (bridge / ui / ui/webgui / tests) 동시 실행 시 통과.

---

### B2 — Trajectory UI 패널 (canvas.js)
**현재 상태**: `/api/trajectory/{start,stop,list}` API 만 제공, 정적 UI 없음.

**Files**:
- `ui/webgui/static/index.html`: Trajectory 패널 HTML (Circle/Line/Lissajous preset radio, speed_hz slider, obj_id input, start/stop button).
- `ui/webgui/static/canvas.js` (또는 새 `trajectory.js`): fetch('/api/trajectory/start', ...) / stop. 활성 trajectory 목록 polling 표시.

**Acceptance**:
- WebGUI 에서 obj_id 0, Circle, speed_hz=0.5 시작 → 백엔드에서 60Hz 송신 확인.
- 정지 버튼으로 trajectory 제거.
- pytest `test_trajectory_api.py` 통과 그대로.

---

### B3 — 외부 IR 셋 다운로드 + 로딩
**현재 상태**: IRConvReverb 디폴트 IR 은 합성 (1024 sample 지수감쇠). 실측 IR 로딩 미검증.

**선정 IR 셋**: OpenAIR (https://www.openair.hosted.york.ac.uk/) 의 free CC-BY 라이선스 IR 1개. 또는 EchoThief 라이브러리 (CC-BY) 1개. 권장: OpenAIR 의 small space IR (예: "Stairwell University of York" — short reverb, mono, 48kHz wav).
- 대안: `assets/ir/` 에 합성 검증용 IR 1개 (Schroeder allpass 또는 짧은 noise burst convolved with exp envelope) 직접 생성 — 외부 다운로드 의존성 회피. 둘 다 시도, 외부 다운로드 실패 시 fallback.

**Files**:
- `assets/ir/.gitkeep` + `.gitignore` 에 `assets/ir/*.wav` 추가.
- `scripts/fetch_ir.py` (새 파일): IR 다운로드 스크립트.
- `core/src/reverb/IRConvReverb.{h,cpp}`: `loadIRFromWav(const std::string& path)` 추가 (간단 WAV 파서, mono float32).
- `core/tests/core_unit/test_p_ir_loading.cpp` (새 파일): WAV 로딩 + 컨볼브 검증.

**Acceptance**:
- `python3 scripts/fetch_ir.py` 실행 시 `assets/ir/sample_ir.wav` 생성 (외부 다운로드 또는 합성 fallback).
- ctest 40/40 PASS (loadIRFromWav 검증).

---

### B4 — DBAP width 정밀화
**현재 상태**: `effective_dist = dist * (1 - 0.3 * width / π)` 의 거친 근사.

**개선**: 다중 가상 소스 모델
- width > 0 시, 원래 위치 (az, el) 주변 ±width/2 에 N=3 가상 소스 배치 (왼/중/오 또는 더 정교히 등각 분포).
- 각 가상 소스에 대해 DBAP 계산, 결과 gain 합산 후 정규화 (energy preserving).
- width=0 → N=1 (현재 동작 보존).

**Files**:
- `core/src/render/DBAPRenderer.{h,cpp}`: `processBlock` 의 per-object 처리에 width fan-out 로직 추가.
- 기존 width 처리 (effective dist 축소) 제거.
- `core/tests/core_unit/test_p_dbap_width.cpp` (새 파일): width=0 baseline + width=π/2 → ≥3 nonzero gain + energy 보존 (sum of gains² ≈ 1).

**Acceptance**:
- ctest 41/41 PASS.
- 기존 `test_p_source_width` 의 DBAP 케이스도 통과.

---

### B5 — HOA Decoder 2nd/3rd order + AllRAD
**현재 상태**: 1차 mode-matching transpose 정규화만. 2nd/3rd order 인코더는 있으나 디코더 부재.

**개선**:
- `AmbiDecoder::prepare(layout, order)` 시그니처 확장 — 1/2/3 차 지원.
- 2nd order: 9채널 (ACN 0..8), 3rd order: 16채널.
- 각 차수에 대해 mode-matching pseudo-inverse 디코딩 행렬 계산.
- AllRAD 옵션: 가상 t-design (Lebedev 그리드 등) → 가상 디코딩 → VBAP 로 재패닝. (선택, pseudo-inverse 만으로도 충분 시 생략).
- `AmbisonicRenderer::setOrder(int order)` API.
- OSC `/sys/ambi_order ,i {1|2|3}` 추가.

**Files**:
- `core/src/ambi/AmbiDecoder.{h,cpp}`: 다차수 지원, pseudo-inverse 구현.
- `core/src/ambi/AllRADDecoder.{h,cpp}` (선택, 새 파일): AllRAD 옵션.
- `core/src/render/AmbisonicRenderer.{h,cpp}`: order 선택.
- `core/src/ipc/Command.h` + `CommandDecoder.cpp`: `/sys/ambi_order` 추가.
- `core/tests/core_unit/test_p_ambi_decoder.cpp`: 2nd/3rd order 케이스 추가.

**Acceptance**:
- ctest 42+/42+ PASS.
- 1/2/3 차 각각 dominant speaker 검증.
- pseudo-inverse 안정 (singular value > 1e-6 가드).

---

### M4 — Snapshot Crossfade
**현재 상태**: `/scene/save`, `/scene/load` 만 존재. 즉시 전환 (click 위험).

**개선**:
- Snapshot 구조: `obj_cache_` 전체 + `active_reverb_` + per-spk gain/delay/limit threshold 등.
- `/scene/load` 시 현재 상태 → 로드 상태 까지 N ms (default 500ms) crossfade.
- 구현: 두 상태 보유 (current, target) + 시간 비례 보간 (lerp az/dist, slerp el? 단순 lerp 충분). gain은 dB linear 보간.

**Files**:
- `core/src/scene/SceneManager.{h,cpp}` (확장): crossfade 시간 관리.
- `core/src/core/SpatialEngine.{h,cpp}`: scene transition 상태 + 매 audioBlock 보간 진행.
- `core/tests/core_unit/test_p_scene_crossfade.cpp` (새 파일): crossfade 진행률 검증.

**Acceptance**:
- ctest +1.
- 500ms crossfade 시 250ms 시점에 az 가 시작과 끝의 중간 (±5%).

---

### M5 — VST3 플러그인
**현재 상태**: `test_p4_vst3stub.cpp` 만 존재. 실제 VST3 빌드 X.

**현실 평가**: VST3 빌드는 JUCE projucer 또는 cmake 기반 vst3sdk 가 필요. 현재 빌드 시스템은 SPATIAL_ENGINE_NO_JUCE=ON 모드. 풀 VST3 플러그인은 별도 빌드 타겟 + JUCE 의존성 필요.

**현실적 범위**:
- Phase A 의 stub 을 확장: VST3 SDK 헤더만 포함 + 컴파일 가능한 minimal entry point.
- 별도 cmake 타겟 `spatial_engine_vst3` (선택 빌드, 디폴트 OFF).
- 풀 기능 (파라미터 매핑, 호스트 통신) 은 Phase C 로 이연. 본 Phase 는 빌드 시스템 정비 + 헬로월드 수준.

**Files**:
- `vst3/` (새 디렉토리): cmake + minimal entry.
- 또는 README 에 "VST3 빌드는 별도 cmake 옵션 -DSPATIAL_ENGINE_VST3=ON 필요, vst3sdk 외부 클론" 명시 + 옵션 추가.

**Acceptance**:
- 새 cmake 옵션 `-DSPATIAL_ENGINE_VST3=ON` 정의 (디폴트 OFF, CI 영향 X).
- 옵션 OFF 시 기존 빌드/테스트 그대로 통과.
- 옵션 정의 + README 의 빌드 가이드 갱신.
- 풀 VST3 기능은 Phase C 명시.

---

### M7 — MTC/LTC 동기
**현재 상태**: 미구현. transport play/stop 만 있음.

**현실적 범위**:
- LTC (Linear Timecode): 오디오 신호로 인코딩된 SMPTE timecode. ALSA/PortAudio 입력 디코드 필요.
- MTC (MIDI Timecode): MIDI 입력 quarter-frame 메시지 파싱.
- 가장 단순한 첫 단계: `core/src/sync/LTCDecoder.{h,cpp}` (LTC manchester biphase 디코드 알고리즘만, 실제 ALSA 입력 wiring 은 차후).
- 또는 MTC: `mido` 의존성 (Python 측, 이미 사용 중) + bridge.

**Files**:
- `core/src/sync/LTCDecoder.{h,cpp}` (새 파일): biphase 디코더 (오디오 sample 입력 → timecode).
- `core/tests/core_unit/test_p_ltc_decoder.cpp` (새 파일): 합성 LTC 신호 → 정확한 timecode 디코딩.
- 또는 Python 측 `bridge/midi_sync.py` 추가.

**Acceptance**:
- ctest +1 (또는 pytest +1).
- 합성 LTC 1초 신호 → 25fps timecode 디코딩 (HH:MM:SS:FF 정확히).

---

## Test Gates (after each item)

| Step | ctest | pytest | 정량 게이트 |
|---|---|---|---|
| baseline | 39/39 | 160 passed, 3 skipped | — |
| B1 | unchanged | 동일 (collection error 없음, 통합 실행 가능) | pytest 루트 통과 |
| B2 | unchanged | unchanged | 정적 UI 패널 렌더 |
| B3 | 40/40 | unchanged | WAV 로딩 OK |
| B4 | 41/41 | unchanged | DBAP width=π/2 → ≥3 nonzero, energy 보존 |
| B5 | 42+/42+ | unchanged | 2nd/3rd order dominant 검증 |
| M4 | +1 | unchanged | 500ms crossfade 중간점 |
| M5 | unchanged (옵션 OFF) | unchanged | -DSPATIAL_ENGINE_VST3=ON 정의 |
| M7 | +1 | unchanged | 합성 LTC 25fps 디코드 |

---

## 평가 (Phase B 종료 후)

1. **8개 항목 완료** + 회귀 없음.
2. **시중 엔진 대비 갭 재평가**:
   - HOA Spat Rev 5종 디코더 → AllRAD + mode-matching pseudo-inverse 1/2/3차 (실질 패리티)
   - L-ISA snapshot crossfade → 시간 보간 구현
   - Spat Rev VST3 → 빌드 시스템 정비 (Phase C 풀 기능)
   - MTC/LTC → LTC 디코더 베이스라인
3. **다음 단계**: Phase C — VST3 풀 기능, real-time MIDI/LTC 동기 wiring, multi-room ABC 라우팅.

---

## ADR

**Decision**: B1~B5 + M4 + M5 부분 + M7 부분을 본 Phase 에서 수행. M5/M7 의 풀 기능은 Phase C 이연.

**Drivers**: 사용자 우선순위 (디코더 고도화, IR 검증, UI 완성, 회귀 정리). VST3/타임코드 풀 기능은 별도 인프라 필요.

**Consequences**:
- ctest 39 → 42+ (+3 정도)
- pytest 160 → 161+ (LTC python 측 변경 시)
- 새 디렉토리: `assets/ir/`, `vst3/` (선택), `core/src/sync/`
- 외부 의존성 추가: 없음 (스크립트 다운로드는 fetch 시점만, 빌드 시 X)

---

## Success Criteria

- [ ] 8개 항목 모두 단독 커밋
- [ ] ctest 42/42 PASS (또는 그 이상)
- [ ] pytest 통합 실행 통과 (collection error 0)
- [ ] `test_p1_rt_no_alloc` PASS
- [ ] README v1g 갱신 (Phase B 완료 마킹 + VST3 빌드 가이드)
- [ ] 외부 IR 1개 로딩 검증
- [ ] WebGUI Trajectory 패널 동작
- [ ] LTC 디코더 합성 신호 검증
