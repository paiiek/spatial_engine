# Plan — v0.9 Feature Extension (한계 §4.4 자율-가능 항목 클로저)

작성: 2026-05-29
선행: v0.8 audit 패스 완료 (commit `6d9958b` / `f48ef3f`). NO_JUCE 101/101, RT 101/101, pytest 225/4-skip.
기반 문서: [`docs/ENGINE_OVERVIEW_AND_COMPARISON.md`](../../docs/ENGINE_OVERVIEW_AND_COMPARISON.md) §4.4 (한계 목록), §5 (TODO 로드맵).

---

## 0. 스코프 & 원칙

기반 한계 목록은 [`docs/ENGINE_OVERVIEW_AND_COMPARISON.md`](../../docs/ENGINE_OVERVIEW_AND_COMPARISON.md) **§4.4 의 8개 항목**이다. 이를 v0.9 사이클에서 **외부 라이선스·하드웨어 의존 없이 자율 실행 가능한 항목**(레인 A–E)과 **외부 의존 트랙**(사업/법무 선행)으로 분리한다. §4.4 8개 항목이 모두 어딘가로 매핑되도록 한다 — 누락 0.

### §4.4 한계 → v0.9 매핑 (8/8 커버)

| §4.4 # | 한계 | v0.9 처리 |
|---|---|---|
| #1 | AAX 미지원 | **외부 트랙** (Avid AAX SDK + NDA) |
| #2 | MAX_OBJECTS=64 하드캡 | **레인 C** (64→128) |
| #3 | 고차 Ambisonic 5–7차 미지원 | **레인 D** (윤곽만, v0.10 sprint) |
| #4 | GUI 워크플로우 미숙 (씬 라이브러리 / snapshot 자동화) | **레인 E** (신규) |
| #5 | HRTF 데이터셋 KEMAR 단독 | **레인 B** |
| #6 | Atmos 인증 안 됨 | **외부 트랙** (Dolby 인증 프로그램) |
| #7 | MPEG-H 미지원 | **외부 트랙** (Fraunhofer 라이선스) |
| #8 | Real-time monitoring UI 부재 | **레인 A** |

자율 레인 = A · B · C · E (D 는 윤곽만). 외부 의존 = #1 · #6 · #7.

### 원칙
1. **각 레인 = 독립 ralplan + autopilot 1 cycle**. 서로 의존성 없음.
2. **각 milestone = scoped commit on `main`** (v0.7 PR1–PR5 / v0.8 P0–P6 convention).
3. **추가 코드는 RT-asserts + Relacy + pytest 게이트 통과해야 함.** RT 회귀는 즉시 abort.
4. **DSP 동작 변경하는 milestone 은 독립 oracle 테스트 필수** (v0.8 P1.2 SN3D / P2.1 EPAD 패턴).
5. **외부 SOFA / WAV 파일 등 대용량 자산은 fetch 스크립트 + `.gitignore`** — 레포 부풀리지 않음.
6. **MAX_OBJECTS / 메모리 풋프린트가 늘어나는 milestone 은 prepareToPlay 시 한 번만 alloc; audioBlock 은 alloc-free** (v0.6 RT 원칙 유지).

### 우선순위 (실행 권장 순)
| 순서 | 레인 | 이유 |
|---|---|---|
| 1 | **A (Metrics dashboard)** | telemetry 채널 이미 있음 → 순수 FE/BE, 위험 최저, UX 효과 즉시 |
| 2 | **B (HRTF dataset selector)** | SOFA loader 이미 일반화. 카탈로그 + 런타임 전환 wiring 만 |
| 3 | **E (Scene library / 큐 자동화)** | SceneController/SceneCrossfade 이미 존재 → 라이브러리 관리 + 큐 자동화 + WebGUI parity 만. RT 무영향(메시지 스레드) |
| 4 | **C (MAX_OBJECTS 128)** | RT-budget 측정 필요, 메모리 풋프린트 검토 후 진입 |
| 5 | **D (5–7차 Ambisonic)** | 한 사이클 단위 큼. 별도 v0.10 sprint 권장 — 본 plan 은 D 의 plan-only 윤곽만 |

레인 간 의존성: 없음. A/B/C/E 는 순서 무관하나, 권장 순서대로 진행 시 컨텍스트 누적 적음. (E-M4 WebGUI 패널은 A-M4 dashboard shell 을 재사용하면 효율적이므로 A 이후 권장.)

---

## 레인 A — Real-time metrics 대시보드

### A.0 동기

v0.7 까지 telemetry 채널은 풀세트로 깔려있음:
- `/sys/state` 1 Hz (전체 상태 string)
- `/sys/warning` event (one-shot drain latch)
- `/sys/binaural_status` 1 Hz (B1/B2 플래그 10개)
- `/sys/binaural_diag` event (B2 demote: block_size, sample_rate, max_ratio)
- `/sys/binaural_reset_demote` 사용자 복구
- shm telemetry: `/sys/warning shm_full / shm_reset / ring_overrun` (ADR 0019 PR4)

하지만 이걸 **사람이 볼 수 있는 UI** 가 없음. 운영 시 OSC 메시지를 raw 로 읽거나 `socat - UDP-RECV:9101` 로 봐야 함. 상용은 L-ISA Controller / Spat Inspector / Atmos Renderer Monitor 가 이걸 그래프로 보여줌.

### A.1 인수 조건 (acceptance)

브라우저에서 `http://localhost:8000/dashboard` 진입 시:
1. **상단**: 엔진 핵심 상태 (running/muted, sample_rate, block_size, channels, decoder type, layout name) — 1 Hz 갱신
2. **CPU & xrun 패널** (시계열 60초 윈도우):
   - audio thread 평균/피크 CPU% (engine 이 measurement 추가 필요)
   - xrun 누적 count (engine `XrunCounter` 노출)
   - shm xrun count (ADR 0019 PR4 채널)
3. **Binaural 패널**:
   - B1/B2 현재 모드 + 누적 demote count + cooldown remaining
   - last demote diag (block_size, max_ratio) 표시
   - 사용자 reset 버튼 (→ `/sys/binaural_reset_demote ,i 1`)
4. **경고 로그**: `/sys/warning` 최근 50개, 타임스탬프 + 카테고리 + payload
5. **객체 활성도**: 각 object 의 az/el/dist + active 플래그 + (있으면) per-obj gain (`/sys/state` 에 이미 일부 포함)

### A.2 Milestones

| M | 제목 | 파일 | gate |
|---|---|---|---|
| **A-M1** | 엔진 측 telemetry 확장 (필요한 것만) | `core/src/util/CpuMeter.h` (NEW); `core/src/ipc/OSCBackend.cpp` 의 `/sys/state` 페이로드에 `cpu_pct,xrun_count,shm_xrun,demote_count` 추가 (schema_version bump 없이 trailing args extension) | NO_JUCE ctest + 새 `test_p_sys_state_extended` 추가 |
| **A-M2** | osc_bridge 확장 — telemetry 분류 + WebSocket fan-out | `ui/webgui/osc_bridge.py` (`/sys/state`, `/sys/warning`, `/sys/binaural_*` 파싱 + WebSocket subscriber 별 routing) | pytest `tests/test_osc_bridge_dashboard.py` 신규 |
| **A-M3** | FastAPI `/api/metrics` + WebSocket `/ws/metrics` 엔드포인트 | `ui/webgui/server.py` (`MetricsHub` 추가, 600 Hz throughput 게이트 통과) | pytest `tests/test_metrics_ws.py` |
| **A-M4** | Dashboard HTML/JS + Chart.js 시계열 | `ui/webgui/static/dashboard.html`, `ui/webgui/static/js/dashboard.js` | playwright `tests/test_dashboard_smoke.py` — 헤드리스 브라우저로 진입 + CPU% 차트 mount + reset 버튼 클릭 검증 |
| **A-M5** | `/sys/binaural_reset_demote` 버튼 라우팅 | dashboard.js → WS → osc_bridge → 9100 OSC | pytest end-to-end |
| **A-M6** | 문서 갱신 + README pointer | `docs/TESTING.md` §A — dashboard 접근 절차; README 의 WebGUI 섹션 | – |

### A.3 위험 / 가드

- `/sys/state` 페이로드 확장 시 schema_version 호환성 (기존 `legacy` dialect 가 깨지면 안 됨) → trailing-args 만 추가, prefix 보존
- WebSocket fan-out 시 producer (osc_bridge) 와 consumer (browser tab N개) 의 backpressure → 큐 사이즈 + drop-newest 정책 명문화
- CPU% 측정은 `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` 기반; macOS/Linux 동작 검증 (Windows 는 별도 — v1.x)

### A.4 예상 effort: **2–3 세션** (M1+M2 / M3+M4 / M5+M6).

---

## 레인 B — HRTF 데이터셋 다양화

### B.0 동기

현재 KEMAR (CIPIC 변형 1인) 만 지원. 개인 ITD/HRTF 가 다른 사용자에게는 외부화 (externalization) 가 잘 안 됨. SADIE-II / CIPIC / HUTUBS / IRCAM Listen 등은 무료 (CC-BY 또는 연구용) 로 공개돼있고 SOFA 표준 포맷. 본 엔진의 `SofaBinReader` 는 이미 일반 SOFA 파일을 읽을 수 있음 → **카탈로그 + 런타임 전환만 추가하면 됨**.

### B.1 인수 조건

1. 사용자가 다음 중 하나로 SOFA 선택 가능:
   - CLI: `--binaural-sofa PATH.sofa` (이미 있음 — 유지)
   - OSC 런타임: `/sys/binaural_sofa_select ,s "<name>"` (신규)
   - WebGUI: dashboard 의 binaural 패널 selector
2. SOFA 전환 시 **lock-free hot-swap** (v0.8 P1.1 decoder-type 패턴):
   - 새 SOFA 를 control thread 에서 load + KdTree3D 빌드 → inactive 슬롯 publish
   - audio thread 는 active 슬롯 index load-acquire once per block
3. 4–5개 데이터셋 (KEMAR, CIPIC subject 003, SADIE-II subject H08, HUTUBS pp1) 카탈로그 JSON
4. **fetch 스크립트** (`scripts/fetch_hrtf_datasets.sh`) — wget/curl 으로 공식 URL 받아서 `assets/hrtf/` 에 저장 (`.gitignore`)
5. 데이터셋 라이선스 / attribution 을 `docs/HRTF_DATASETS.md` 에 명시

### B.2 Milestones

| M | 제목 | 파일 | gate |
|---|---|---|---|
| **B-M1** | HRTF 카탈로그 JSON 스키마 + 로더 | `assets/hrtf/catalog.json` (NEW), `core/src/hrtf/HrtfCatalog.{h,cpp}` (NEW) | NO_JUCE ctest + `test_p_hrtf_catalog` (JSON 파싱 + 라이선스 메타 검증) |
| **B-M2** | 더블 버퍼 SOFA hot-swap | `core/src/hrtf/HrtfLookup.h` 2-slot SOFA + `std::atomic<int> active_sofa_slot_`; `BinauralMonitor::applyPendingSofaChange()` from control tick (v0.8 P1.1 패턴 그대로) | Relacy stress test `test_hrtf_sofa_swap_race` (rapid switch concurrency) |
| **B-M3** | OSC `/sys/binaural_sofa_select ,s` | `core/src/ipc/CommandDecoder.cpp` + `StateModel` 에 pending_sofa_name_ | NO_JUCE ctest `test_p_osc_binaural_sofa_select` |
| **B-M4** | fetch 스크립트 + 라이선스 docs | `scripts/fetch_hrtf_datasets.sh` (NEW), `docs/HRTF_DATASETS.md` (NEW) | 스크립트 dry-run + URL HTTP HEAD 검증 |
| **B-M5** | WebGUI selector + dashboard 연결 | A-M4 dashboard 의 binaural 패널 안에 selector dropdown | playwright `test_dashboard_sofa_select_smoke` |
| **B-M6** | 회귀 가드 — 데이터셋별 ITD oracle | `test_p_binaural_dataset_itd_oracle` — 각 데이터셋에서 az=±90 ITD 가 closed-form (head radius 0.0875 m, 343 m/s) 의 ±20% 이내 | NO_JUCE ctest |

### B.3 위험 / 가드

- **SOFA 파일 라이선스** — 각 데이터셋 라이선스 (CC-BY / 학술 fair use) 명시 + 자동 fetch 는 CC-BY 만 (다른 건 사용자가 수동 동의 후 다운로드)
- KdTree3D 빌드 비용 (control thread, ~수십 ms — 1Hz 사이클 안에 들어옴) — `BindingSpecNote`: 만약 KdTree 빌드가 1 sec 이상 걸리는 데이터셋 발견 시 swap 빈도 제한
- B1/B2 demote 시스템과의 상호작용 — SOFA 전환 직후 demote 카운트 reset 할지 vs 유지할지 결정 (recommend: reset, 새 측정 환경 시작)
- 큰 HRTF (SADIE-II ~500 directions × 2 ears × 256 taps × float = ~500 KB) — 2-slot = 1 MB; 16 ms × 7 데이터셋 = 7 MB 메모리. 허용 범위.

### B.4 예상 effort: **2 세션**.

---

## 레인 C — MAX_OBJECTS 64 → 128 (선택: 256)

### C.0 동기

L-ISA(~96), Atmos(118) 대비 64 는 한계. 라이브 공연 / 시네마 mix 에서 동시 객체 100+ 시 부족. **하지만 RT budget 이 객체 수에 거의 선형 비례** 하므로 무작정 확장은 안 됨.

### C.1 인수 조건

1. `MAX_OBJECTS=128` 으로 컴파일 시 모든 기존 테스트 그린
2. **RT-asserts 빌드에서 audio thread alloc-free 유지** — 모든 per-object buffer 가 `prepareToPlay` 시 한 번에 할당
3. **CPU budget 측정 표** — 8/16/32/64/96/128 객체 시나리오에서 block 처리 시간 (median, p99) 보고서 (`docs/RT_BUDGET_MAX_OBJECTS.md`)
4. 8 채널 출력 + VBAP 알고리즘 + B2 binaural side-output 활성 상태에서 128 객체 동시 사용 시 RT budget 50% 이하 (Linux 8-core 기준)
5. memory footprint < 100 MB (PerObjectChain × 128)

### C.2 Milestones

| M | 제목 | 파일 | gate |
|---|---|---|---|
| **C-M1** | MAX_OBJECTS 상수화 + 컴파일 옵션 | `core/src/util/Constants.h` (`MAX_OBJECTS` from `cmake -DSPATIAL_ENGINE_MAX_OBJECTS=128`) | NO_JUCE ctest with new 정의 (64 와 128 둘 다 빌드) |
| **C-M2** | 고정 배열 → MAX_OBJECTS 의존 | grep `\[64\]` core/src/ — 모든 사용처 (`ramps_[64]`, scratch buffers 등) 를 `[MAX_OBJECTS]` 로 | 컴파일 워닝 0, ctest 그린 |
| **C-M3** | VBAPRenderer 캐시 / coalescer 메모리 풋프린트 검증 | `core/src/render/VBAPRenderer.cpp` 의 0.5° bin 캐시 (객체별이 아님 — 영향 없음) 확인; per-object 캐시 있는 곳 확장 | `core/build_rton` ctest (RT-asserts) — alloc 없으면 그린 |
| **C-M4** | RT budget benchmark 스위트 | `core/tests/perf/test_perf_max_objects.cpp` (NEW) — gtest benchmark style, 8/16/32/64/96/128 객체 시나리오 × VBAP/DBAP/Ambi3 알고리즘, block 처리 시간 측정 | 명시적 fail 없는 benchmark; 보고서 출력 |
| **C-M5** | `docs/RT_BUDGET_MAX_OBJECTS.md` 보고서 | M4 결과 + Linux 8-core 기준 측정값 + 권장 사용 환경 | 문서 추가 |
| **C-M6** | WebGUI / VST3 / adm_player 측 객체 수 한도 갱신 | `ui/webgui/static/index.html` 의 객체 카운트 상수 + VST3 param max (현재 6 params 는 영향 없음) | playwright smoke |

### C.3 위험 / 가드

- **메모리 footprint** — `PerObjectChain` (EQ 4-band + delay + LPF + propagation delay + reverb send) × 128 ≈ 50 MB. Heap 할당이므로 stack overflow 위험 없음 (v0.6 hotfix 와 동일 패턴).
- **VBAP 캐시 cold-miss 가 객체 수에 비례하지 않음** — bin 캐시는 방향별이라 객체 수와 무관. 단, 객체별 위치 캐시는 객체 수 × 1 이라 영향 있음.
- **128 객체 × 8 채널 = 1024 channel pairs** of computation per sample × 48k = 49M operations/s. 현대 CPU 면 한 코어 < 20% 예상. 측정으로 확인.
- **RT-asserts 통과 핵심** — `cmake -DSPATIAL_ENGINE_MAX_OBJECTS=128 -DSPATIAL_ENGINE_RT_ASSERTS=ON` 빌드에서 alloc 0건이어야 함.

### C.4 예상 effort: **2–3 세션** (C-M1+M2+M3 = 1세션, M4+M5 측정 = 1세션, M6 = 0.5세션).

---

## 레인 E — Scene library / snapshot 자동화 (§4.4 #4)

### E.0 동기

엔진/UI 에 씬 기본기는 이미 있음:
- `core/src/ipc/SceneController.{h,cpp}` — `SceneSave`/`SceneLoad`/`SceneList` (메시지 스레드 전용, RT 무영향)
- `core/src/scene/SceneCrossfade.{h,cpp}` — 씬 전환 시 파라미터 크로스페이드
- `ui/spatial_engine_ui/views/scene_panel.py` — PySide6 패널 (`/scene/save` `/scene/load` `/scene/list`)
- `ui/webgui/tests/test_scene_e2e.py` — 기본 e2e

하지만 상용 L-ISA Controller / Spat 의 워크플로우 대비 **미숙한 지점**:
1. **라이브러리 관리 부재** — 씬 rename / duplicate / delete / 메타(태그·노트) / 정렬이 없음. 디렉토리에 파일만 쌓임.
2. **snapshot 자동화 부재** — 큐 리스트(cue list)로 씬을 순서대로 dwell-time/crossfade-time 지정해 자동 advance 하거나, 외부 트리거(MIDI Program Change / OSC `/cue/go`)로 cue 발사하는 기능이 없음. 공연/시네마 운용의 핵심.
3. **WebGUI parity 부재** — 씬 패널이 PySide6 데스크톱에만 있고 WebGUI dashboard 에는 없음.

### E.1 인수 조건 (acceptance)

1. **라이브러리 관리**: 씬을 rename / duplicate / delete 할 수 있고, 각 씬에 메타(생성시각·태그·노트)를 붙여 인덱스(`scenes/index.json`)로 조회 가능
2. **큐 리스트(snapshot 자동화)**:
   - 큐 = `{scene_name, crossfade_ms, dwell_ms?}` 의 순서 리스트, `cuelist.json` 로 저장/로드
   - `/cue/go ,i <idx>` / `/cue/next` / `/cue/prev` / `/cue/stop` OSC 로 발사
   - dwell_ms 지정 시 메시지/컨트롤 스레드 타이머가 자동 advance (RT 스레드 아님)
   - 발사 시 기존 `SceneCrossfade` 경로 재사용 (신규 DSP 없음)
3. **트리거**: MIDI Program Change → cue index 매핑 (`ui/spatial_engine_ui/midi/midi_bridge.py` 확장)
4. **WebGUI parity**: dashboard 에 scene library + cue list 패널 (A-M4 shell 재사용)
5. **RT 불변식**: 모든 신규 로직은 메시지/컨트롤 스레드. audio thread 코드 경로 무변경 — RT-asserts 빌드 회귀 0

### E.2 Milestones

| M | 제목 | 파일 | gate |
|---|---|---|---|
| **E-M1** | 씬 라이브러리 인덱스 + 관리 op (rename/duplicate/delete/meta) | `core/src/ipc/SceneController.{h,cpp}` 확장 + `scenes/index.json`; OSC `/scene/rename ,ss` `/scene/duplicate ,ss` `/scene/delete ,s` `/scene/meta ,ss` | NO_JUCE ctest `test_p_scene_library_ops` (rename/dup/delete round-trip + index 무결성) |
| **E-M2** | 큐 리스트 모델 + 직렬화 | `core/src/scene/CueList.{h,cpp}` (NEW) — `{scene, crossfade_ms, dwell_ms}` 벡터 + `cuelist.json` load/save | NO_JUCE ctest `test_p_cuelist_serialize` (JSON round-trip + 경계값) |
| **E-M3** | 큐 발사 엔진 — `/cue/go` `/cue/next` `/cue/prev` `/cue/stop` + dwell 자동 advance | `CueEngine` (제어 스레드 타이머; `SceneController` + `SceneCrossfade` 재사용); `CommandDecoder.cpp` 라우팅 | NO_JUCE ctest `test_p_cue_go_advance` (go→crossfade 트리거, dwell→auto-next 순서 검증) + RT-asserts 빌드 alloc 0 |
| **E-M4** | WebGUI scene library + cue list 패널 | `ui/webgui/static/dashboard.html`/`js` (A-M4 shell 재사용), `ui/webgui/server.py` `/api/scenes` `/api/cues` | playwright `test_dashboard_scene_cue_smoke` (씬 저장→큐 추가→go 발사) |
| **E-M5** | MIDI Program Change → cue 매핑 | `ui/spatial_engine_ui/midi/midi_bridge.py` 확장 + 매핑 테이블 | pytest `test_midi_cue_trigger` (PC# → `/cue/go` 디스패치) |
| **E-M6** | 회귀 + 문서 | `test_scene_cue_e2e` (라이브러리→큐리스트→자동 advance 전체); `docs/SCENE_AND_CUE_WORKFLOW.md` (NEW) + README pointer | NO_JUCE ctest + pytest |

### E.3 위험 / 가드

- **RT 격리 절대 원칙** — cue dwell 타이머·crossfade 트리거는 전부 메시지/컨트롤 스레드. audio thread 는 기존 `SceneCrossfade` 의 lock-free 보간만 수행 (신규 동기화 추가 금지). RT-asserts 빌드에서 신규 alloc 0 확인.
- **dwell 자동 advance 와 수동 `/cue/go` 경합** — 수동 go 가 들어오면 진행 중인 dwell 타이머 취소 후 재시작 (latch + generation counter 패턴)
- **씬 파일 I/O 는 메시지 스레드 only** — `SceneController` 주석의 RT 금지 규약 유지. 대용량 씬도 audio 콜백과 무관.
- **index.json 손상 복구** — 인덱스가 디스크 실제 파일과 어긋날 때 재스캔 fallback (씬 파일이 ground truth, index 는 캐시)

### E.4 예상 effort: **2 세션** (E-M1+M2+M3 엔진 = 1세션, E-M4+M5+M6 UI/회귀 = 1세션).

---

## 레인 D — 5–7차 Ambisonic (별도 sprint, 본 plan 은 윤곽만)

### D.0 동기

IEM / SPARTA 는 7차 (64 channels). 본 엔진은 3차 (16 channels) 까지만. 고차 = 공간 해상도 향상 + 더 큰 sweet spot. 단, 채널 수 폭증 (4차=25ch, 5차=36ch, 6차=49ch, 7차=64ch) → 메모리/CPU 비례 증가.

### D.1 인수 조건 (수준만 — 별도 sprint 에서 상세화)

1. encoder/decoder 가 1–7차 임의 차수 지원
2. SH 정규화 SN3D + AmbiX 채널 순서 유지
3. 모든 decoder type (Sampling / MaxRE / EPAD / AllRAD / InPhase) 가 1–7차 동작
4. 차수별 closed-form SN3D oracle (v0.8 P1.2 패턴) 가 1–7차 전부 존재

### D.2 Milestones (윤곽, 별도 ralplan 으로 정식화)

| M | 제목 | 영향 영역 |
|---|---|---|
| D-M1 | `kMaxOrder = 7` 으로 상수화 | `core/src/util/Constants.h`, kK[7] = {4,9,16,25,36,49,64} |
| D-M2 | SH 정규화 LUT 6+7차 확장 + closed-form oracle | `core/src/ambi/AmbisonicEncoder.cpp`; `test_p_ambi_sn3d_constants_4_5_6_7` |
| D-M3 | `decode_matrices_` 2-슬롯 × MAX_ORDER 일반화 (현재 3 hardcoded) | `core/src/ambi/AmbiDecoder.h:74` v0.8 P1.1 패턴 연장 |
| D-M4 | EPAD / AllRAD / MaxRE / InPhase 각각 4–7차 빌드 + 행렬 정규화 검증 | `core/src/ambi/*Decoder.cpp`; rank-aware energy 가드 |
| D-M5 | `/sys/ambi_order ,i {1..7}` 런타임 전환 | `CommandDecoder.cpp`, `StateModel` |
| D-M6 | 메모리 풋프린트 측정 + RT budget — 7차 = 64 ch × 64 spk × 2-slot = 32 KB matrix per decoder type × 5 types = 160 KB. 허용. | docs |
| D-M7 | 회귀: HRTF 바이노럴 경로가 임의 차수 처리하는지 (BinauralMonitor 가 차수별 행렬 사용하는지 확인) | NO_JUCE ctest |

### D.3 예상 effort: **본 사이클 외 — 별도 v0.10 sprint 권장.** 한 세션에 다 안 끝남.

---

## 외부 의존 트랙 (별도 분리)

| 한계 | 외부 의존 | 진입 조건 |
|---|---|---|
| **#3 AAX** | Avid AAX SDK + NDA + 라이선스 | Pro Tools 시장 진입이 사업적 우선순위가 될 때 |
| **#5a Atmos 인증** | Dolby Atmos Renderer 인증 프로그램 + h/w + 비용 | 상용 라이선스 트랙 (`docs/license_procurement_plan.md`) 확장 |
| **#5b MPEG-H** | Fraunhofer MPEG-H Audio 라이선스 | ATSC 3.0 / 차세대 방송 워크플로우 사업 결정 시 |

이 3개는 **autopilot 으로 손댈 수 없음** — 사업/법무 결정 + 비용 확정이 선행돼야 함.

---

## 실행 모드

- v0.8 audit 와 동일 워크플로우: 각 레인 = `/oh-my-claudecode:ralplan` → consensus → `/oh-my-claudecode:autopilot`
- 각 milestone = scoped commit on `main` + tracker tick (이 파일의 §Progress tracker)
- 게이트 red 2회 retry 후 비-flaky 실패 시 STOP + 사용자 surface (v0.8 패턴 유지)
- 외부 fetch 가 필요한 milestone (B-M4, C-M5 등) 은 사용자 승인 필요 → 자율 진입 금지

---

## Progress tracker (실행 시 채워나감)

### 레인 A — Real-time metrics 대시보드
- [ ] A-M1 엔진 측 telemetry 확장
- [ ] A-M2 osc_bridge 확장
- [ ] A-M3 FastAPI `/api/metrics` + `/ws/metrics`
- [ ] A-M4 Dashboard HTML/JS + Chart.js
- [ ] A-M5 reset_demote 버튼 라우팅
- [ ] A-M6 문서 갱신 + README pointer

### 레인 B — HRTF 데이터셋 다양화
- [ ] B-M1 카탈로그 JSON + HrtfCatalog 로더
- [ ] B-M2 더블 버퍼 SOFA hot-swap
- [ ] B-M3 OSC `/sys/binaural_sofa_select`
- [ ] B-M4 fetch 스크립트 + 라이선스 docs
- [ ] B-M5 WebGUI selector
- [ ] B-M6 데이터셋별 ITD oracle

### 레인 C — MAX_OBJECTS 64 → 128
- [ ] C-M1 cmake 컴파일 옵션 + Constants.h
- [ ] C-M2 고정 [64] 배열 일반화
- [ ] C-M3 RT-asserts 그린
- [ ] C-M4 RT budget benchmark 스위트
- [ ] C-M5 `RT_BUDGET_MAX_OBJECTS.md` 보고서
- [ ] C-M6 WebGUI / sidecar 한도 갱신

### 레인 E — Scene library / snapshot 자동화
- [ ] E-M1 씬 라이브러리 인덱스 + 관리 op
- [ ] E-M2 큐 리스트 모델 + 직렬화
- [ ] E-M3 큐 발사 엔진 + dwell 자동 advance
- [ ] E-M4 WebGUI scene/cue 패널
- [ ] E-M5 MIDI Program Change → cue 매핑
- [ ] E-M6 회귀 e2e + 문서

### 레인 D — 5–7차 Ambisonic (별도 sprint)
- 본 plan 에서는 윤곽만; 정식 ralplan 은 별도

### 외부 의존 트랙
- 사업 결정 필요 — 본 plan 스코프 밖

---

## 최종 인수 (v0.9 release)

A + B + C + E 가 모두 그린이면 v0.9.0 태깅 후보. D 는 v0.10 별도.

상용 비교 표 (`docs/ENGINE_OVERVIEW_AND_COMPARISON.md` §4.4) 갱신 필요 (8개 한계 중 자율 레인 4개 클로저):
- #8 "Real-time monitoring UI 부재" → "✅ 자체 dashboard (`/dashboard`)" (레인 A)
- #5 "HRTF 데이터셋이 KEMAR 단독" → "✅ 4+ 데이터셋 런타임 전환" (레인 B)
- #2 "MAX_OBJECTS=64 하드캡" → "MAX_OBJECTS=128" (레인 C)
- #4 "GUI 워크플로우 미숙 (씬 라이브러리/snapshot)" → "✅ 씬 라이브러리 + 큐 자동화 (`/cue/go`, MIDI PC)" (레인 E)

잔여 (v0.9 스코프 밖): #3 고차 Ambisonic (→ v0.10 레인 D), #1 AAX · #6 Atmos · #7 MPEG-H (→ 외부 의존 트랙).
