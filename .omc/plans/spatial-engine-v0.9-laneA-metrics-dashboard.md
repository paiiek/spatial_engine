# Execution Plan — v0.9 Lane A · Real-time Metrics Dashboard

작성: 2026-05-29 · 개정: 2026-05-30 (Critic ITERATE 7건 + minor 3건 반영; DD-B p99 = 스칼라 추정기, user-approved)
모드: RALPLAN consensus 후보 (SHORT). Architect/Critic 검토용.
기반 윤곽: [`.omc/plans/spatial-engine-v0.9-feature-extension.md`](spatial-engine-v0.9-feature-extension.md) §레인 A (A-M1~A-M6).
선행: v0.8 audit 완료 (`6d9958b`/`f48ef3f`). NO_JUCE 101/101, RT 101/101, pytest 225/4-skip.

---

## 0. Explore 매핑이 윤곽 가정과 다른 핵심 3가지 (반드시 먼저 읽을 것)

기반 윤곽 문서의 A.0/A.2 가정 일부가 **현재 코드와 불일치**한다. 실행 전 결정 필요.

### 불일치 #1 — 엔진 측 full `/sys/state` 1Hz 문자열은 **존재하지 않는다**

- 윤곽 A-M1 은 "`core/src/ipc/OSCBackend.cpp` 의 `/sys/state` 페이로드에 trailing args 추가"로 적혀 있으나, **`OSCBackend.cpp` 에 `/sys/state` sendReply 가 없다.**
- 메인 코어 바이너리(`core/src/bin/spatial_engine_core.cpp:602-680`)의 1Hz 제어 루프는 `/sys/state` 를 **전혀 방출하지 않는다.** 방출하는 것: `servicePlayerStaleWatchdog`, `applyPendingAmbiDecoderChange`, shm 게이트 시 `ShmTelemetryEmitter::tick()`, 그리고 one-shot `/sys/binaural_warning`.
- 코드베이스 전체에서 `/sys/state` 를 보내는 유일한 곳은 **`core/src/bin/ShmTelemetryEmitter.h:175-220 emitState()`** — shm sidecar 가 3개의 개별 `,s` key=value 메시지(`shm_producer_alive=N`, `shm_producer_state=N`, `shm_consumer_locked=N`)를 **on-change(값 변할 때만)** 방출. 이건 풀-상태 스냅샷이 아니라 shm 텔레메트리 전용이며, cpu/xrun 을 여기 붙이는 건 의미 충돌.
- → **Decision Driver A (아래) 의 본질**: A-M1 은 "기존 `/sys/state` 확장"이 아니라 **신규 1Hz 텔레메트리 방출 경로를 만드는 일**이다.

### 불일치 #2 — `ObservabilityCounters` 는 이미 `/sys/metrics` 채널을 *문서상* 예약했다

- `core/src/util/ObservabilityCounters.h:6,16` 주석: *"exposed via /sys/metrics OSC address"*, *"Metric names match the /sys/metrics OSC bundle fields"*. 그러나 **`/sys/metrics` emitter 는 코드에 없다**(grep 결과 주석뿐).
- 더 근본적으로 **`ObservabilityCounters` struct 는 dead code** — 정의만 있고 어디에서도 인스턴스화되지 않는다(외부 grep 히트 0; 유일 언급은 `:13` 의 `static ObservabilityCounters g_counters;` docstring 예시). 즉 "이미 단일 소스 오브 트루스가 있다"는 전제는 틀렸고, **인스턴스·소유자·접근자가 NET-NEW**(Principle 3 참조).
- `cpu_pct_audio_thread`(`:59`), `per_block_time_p99_us`(`:56`) 필드는 선언됨(`:54` 주석은 "lightweight estimator"로 스칼라 추정기 의도). **측정 코드 0, 방출 코드 0.**
- → `/sys/metrics` 신설이 윤곽의 `/sys/state` 확장보다 설계 의도에 부합. Decision Driver A 의 강한 후보.

### 불일치 #3 — `/sys/binaural_status` 1Hz 방출도 현재 없다

- 윤곽 A.0 은 "`/sys/binaural_status` 1Hz (B1/B2 플래그 10개)"를 기성 채널로 전제하나, 메인 루프에서 방출 호출이 없다. 현재 방출되는 binaural 채널은 `/sys/binaural_warning ,sf`(`SpatialEngine.cpp:1022,1037`)와 demote diag `,iif`(forwarder `SpatialEngine.h:151`)뿐.
- demote 카운트/cooldown 상태는 `BinauralMonitor` 내부 atomic(`runtime_demote_strikes_`)으로 존재하지만 **주기적 push 경로가 없다.**
- → A-M1 의 demote_count 노출도 신규 방출 작업이다. 윤곽 A.1 §3(B1/B2 패널)은 A-M1 에서 텔레메트리 소스를 새로 만들어야 충족된다.

**요약**: Lane A 는 "FE/BE 만, 텔레메트리는 다 있다"는 윤곽 전제보다 엔진 측 작업이 더 크다. 단, audio thread 핫패스는 measurement 삽입(A-M1) 한 곳뿐이고 나머지는 control thread 1Hz 이므로 위험은 여전히 낮음.

---

## 1. RALPLAN-DR 요약 (Architect/Critic 정렬용)

### Principles (4)
1. **RT 핫패스 불가침**: audio thread(`SpatialEngine::audioBlock` `:427`, `SPE_RT_NO_ALLOC_SCOPE` `:428`)에 추가하는 측정은 alloc/lock/syscall 0. audio thread 가 하는 일은 **clock read + O(1) 스칼라 추정기 갱신 + relaxed 스칼라 store** 뿐. sort·배열·reservoir 없음. RT-asserts 빌드(alloc=0) 회귀 0 + RT-latency 마이크로벤치(AC2b) 예산 내.
2. **하위호환 = trailing-args only**: 기존 OSC 주소/타입태그/접두 인자 보존. legacy dialect·기존 osc_bridge default handler 무손상. 새 데이터는 새 주소(`/sys/metrics`) 또는 trailing args 로만.
3. **측정값 단일 소유 인스턴스(NET-NEW)**: `ObservabilityCounters`(`core/src/util/ObservabilityCounters.h`)는 struct 정의만 있고 **코드 어디에서도 인스턴스화되지 않은 dead code** — 외부 grep 히트 0, 유일한 언급은 `:13` 의 `static ObservabilityCounters g_counters;` docstring 예시뿐. 따라서 "이미 단일 소스 오브 트루스가 있다"는 전제는 **틀렸다**. 인스턴스·소유자·수명·접근자는 전부 NET-NEW: `SpatialEngine` 이 `ObservabilityCounters obs_counters_;` 멤버 1개를 소유하고, `oscBackend()`(`SpatialEngine.h:253`)를 미러링하는 `observabilityCounters()` 접근자를 신설한다. 단, 신규 카운터 *구조체*를 또 만들지는 않는다(기존 struct 재사용).
4. **오프라인 우선**: WebGUI 는 외부 JS CDN 의존 0(현재 `static/` 에 vendored lib 없음). 차트도 외부 네트워크 없이 동작해야 함.

### Decision Drivers (top 3)
1. **DD-A 텔레메트리 방출 경로** — trailing-args 를 `/sys/state` 에 붙일지 vs **신규 `/sys/metrics` 1Hz 채널 신설**. (불일치 #1·#2 때문에 단순 확장이 불가 → 사실상 신설 여부 결정)
2. **DD-B CPU% 측정 방식** — clock 소스(`CLOCK_THREAD_CPUTIME_ID` vs `steady_clock` wall) × 집계(EWMA vs p99 윈도우). RT 비용·정확도·이식성 트레이드오프.
3. **DD-C 대시보드 차트 라이브러리** — vendored Chart.js vs vendored uPlot vs 자체 canvas 미니차트. 번들 크기·오프라인·playwright mount 검증 난이도.

(DD-D WS fan-out backpressure 는 Decision 으로 두되 옵션 비교 없이 정책 명문화: §3 가드.)

### Viable Options (각 bounded pros/cons)

#### DD-A — 텔레메트리 방출 경로

| | **A1: 신규 `/sys/metrics` 1Hz 번들** (권장) | **A2: 기존 `/sys/state` 에 trailing-args** |
|---|---|---|
| 설명 | `spatial_engine_core.cpp` 1Hz 틱에 `engine.oscBackend().sendReply("/sys/metrics", ",s", kv)` 추가(기존 3-arg overload `OSCBackend.h:120` 재사용, `emitState` 의 `/sys/state` 방식과 동일한 `,s` key=value 묶음). `ObservabilityCounters` snapshot. | `ShmTelemetryEmitter::emitState` 또는 신규 풀-state 빌더에 `cpu_pct=`,`xrun_count=` 등 trailing `,s` 추가 |
| Pros | `ObservabilityCounters.h` 주석 의도와 일치(이미 예약된 주소); shm 의존 없이 항상 방출(null/dante 경로도); **신규 C++ 인코더 0**(검증된 `,s` key=value 재사용 — `ShmTelemetryEmitter.h:175-220` 선례); 신규 인코더 ADR 불필요 | 윤곽 문서 표현과 1:1; 채널 1개 |
| Cons | 새 주소 → osc_bridge·dashboard 가 인지해야(A-M2 에 흡수) | **타겟 `/sys/state` 가 shm 전용 on-change 라 의미 충돌**; null/dante 경로엔 `/sys/state` 자체가 없어 cpu/xrun 이 안 나감(치명) |
| 판정 | **채택 권장.** A2 의 "null/dante 경로에서 안 나감"이 acceptance(상시 CPU/xrun 표시)를 깬다. | 사실상 무효 — 단일 `/sys/state` 풀-state 방출 경로가 없어서 "확장"할 대상이 없음. |

> A2 가 살아나려면 *먼저* 풀-state `/sys/state` 1Hz 방출기를 신설해야 하는데, 그건 A1 보다 큰 작업이고 의미도 중복. → A1 채택, A2 는 위 사유로 invalidate.

#### DD-B — CPU% 측정 방식

| | **B1: `steady_clock` per-block wall + EWMA + 스칼라 p99 추정기** (권장) | **B2: `CLOCK_THREAD_CPUTIME_ID` per-block** |
|---|---|---|
| 설명 | block 진입/이탈 `steady_clock::now()` 차 → block wall μs. CPU% = block_wall_us / block_budget_us × 100. 평균은 EWMA(α≈0.1). p99 는 **스칼라 러닝 추정기**(P² / Greenwald-Khanna 류 단일값) — audio thread 가 블록마다 O(1) 갱신 후 단일 `std::atomic<uint32_t>` 에 relaxed store(이미 `per_block_time_p99_us` 필드 존재; `:54` 주석이 "lightweight estimator"라 명시). | `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` 로 audio thread 순수 CPU 시간 측정 |
| Pros | clock read + O(1) 추정기 갱신 + relaxed 스칼라 store/블록, vDSO 경유 alloc-free; **sort·배열·reservoir·double-buffer 없음 → torn-read 윈도우 없음**; xrun 직결(wall>budget=언더런 위험); 이식성(Linux/macOS/Windows 동일 API); p99 필드 재사용; control thread 는 단순 `.load()` | "진짜 CPU 점유율"(스케줄러 프리엠션 제외) — 더 정확한 CPU 의미론 |
| Cons | wall 시간이라 OS 프리엠션 포함 → CPU%가 약간 비관적; p99 가 정확값 아닌 추정값(운영 표시용으론 충분) | `CLOCK_THREAD_CPUTIME_ID` 는 macOS 에서 동작 차이/플랫폼 분기 필요; 윤곽 A.3 가 이미 "Windows 별도"로 못박음 → 이식성 부담; xrun 마진과 직결 안 됨 |
| 판정 | **채택 권장.** "block 이 deadline 안에 끝나는가"가 운영자 관심사 → wall 기반이 더 actionable. p99 필드 이미 있음. | 보조 지표로 v1.x 고려. 지금은 분기 비용 대비 이득 작음. |

> 두 옵션 다 audio thread 비용은 clock read + O(1) 추정기 갱신 + 단일 atomic store 뿐. cross-thread 메커니즘: **audio thread = clock read + O(1) 스칼라 추정기 갱신 + relaxed 스칼라 store; control thread(1Hz) = `.load()` 1회.** 1024-슬롯 reservoir·double-buffer·큐는 쓰지 않는다(사용자 승인 amendment 2026-05-30 — RT coherence 위해 스칼라 추정기로 대체). prepareToPlay 재진입 시 EWMA·추정기 상태를 reset 한다(아래 A-M1 참조).

#### DD-C — 대시보드 차트

| | **C1: 자체 경량 canvas 미니차트** (권장) | **C2: vendored uPlot** | **C3: vendored Chart.js** |
|---|---|---|---|
| 번들 | 0(자체 ~150 LOC) | ~40KB min | ~200KB+ min |
| 오프라인 | 완전 | vendored 면 OK | vendored 면 OK |
| 60초 시계열 적합 | 충분(라인+피크 표시) | 매우 적합(고성능) | 적합 |
| playwright mount 검증 | canvas 존재+draw 호출 spy 로 검증 | DOM/canvas 노드 검증 | `<canvas>` + Chart 인스턴스 검증 |
| Cons | 축/툴팁 직접 구현 | 라이선스(MIT) 벤더링·버전관리 | 무겁고 기능 과잉 |
| 판정 | **채택 권장.** 지표 4종(CPU avg/peak, xrun, shm_xrun)·60s 윈도우는 단순 → 의존성 0 가 원칙 4 부합. | 시계열이 더 늘면 승격 후보 | 과대 |

> A.2 윤곽 표는 "Chart.js"로 적혀있으나 원칙 4(오프라인·의존성 0)와 충돌. C1 채택 권장, C3 는 "vendored 가능하나 과대"로 invalidate. **이 항목은 사용자/Architect 최종 확인 포인트.**

---

## 2. Milestones (파일 anchor + gate + 순서)

> 표기: **NEW** = 신규 파일, **MOD** = 기존 수정. 행번호는 2026-05-29 HEAD 기준 anchor.

### A-M1 — 엔진 측 텔레메트리 측정 + `/sys/metrics` 1Hz 방출

**목표**: audio thread 에서 per-block 시간·CPU%·xrun 을 alloc-free 로 측정 → `ObservabilityCounters` 갱신 → control thread 1Hz 에서 `/sys/metrics` 방출.

| 파일 | 변경 |
|---|---|
| `core/src/util/CpuMeter.h` **NEW** | per-block wall μs 측정 + EWMA(평균) + **스칼라 러닝 p99 추정기**(P²/GK 류 단일값, 배열·sort 없음). `recordBlockStart()/recordBlockEnd(num_frames, sample_rate)` inline, alloc/lock 없음. `cpuPct()/p99Us()` 게터. `reset()` 게터 — prepareToPlay 재진입/sample-rate·block-size 변경 시 EWMA + 추정기 상태 초기화(stale 샘플이 p99 오염 방지). (DD-B B1, 스칼라 추정기) |
| `core/src/core/SpatialEngine.cpp` `:427-432` **MOD** | `audioBlock()` 에서 **`num_frames > MAX_BLOCK` early-return 가드(`:431` `record_overrun()`+`return;`) *이후*** 에 `cpu_meter_.recordBlockStart()` 배치(오버런 블록이 garbage wall 샘플로 들어가지 않게); 정상 종료(`:1014` 폴-스루)에 `recordBlockEnd(num_frames, sample_rate)`. 결과를 `obs_counters_.cpu_pct_audio_thread.store(.., relaxed)`, `obs_counters_.per_block_time_p99_us.store(.., relaxed)`. **`SPE_RT_NO_ALLOC_SCOPE` 안에서 alloc 0 + O(1) 유지.** 오버런 경로(`:431`)는 wall-time 샘플이 **아니라** xrun 으로만 집계(M1 별도 필드). prepareToPlay 경로에서 `cpu_meter_.reset()` 호출. |
| `core/src/core/SpatialEngine.h` `:253`,`:454` **MOD** | NET-NEW: `util::CpuMeter cpu_meter_;` 멤버 + `ObservabilityCounters obs_counters_;` 멤버(단일 소유 인스턴스) + `ObservabilityCounters& observabilityCounters() noexcept { return obs_counters_; }` 접근자 — `oscBackend()`(`:253`) 미러링. (Principle 3: 인스턴스·소유자·접근자 전부 신설.) |
| `core/src/bin/spatial_engine_core.cpp` `:600-624` + `:685` **MOD** | 1Hz 틱(`last_ambi_decoder_apply` 패턴 복제)에 `last_metrics_emit` 추가 → `/sys/metrics` 방출. **기본 인코딩 = `,s` key=value**(`engine.oscBackend().sendReply("/sys/metrics", ",s", kv)` — 기존 3-arg overload `OSCBackend.h:120` 재사용, `ShmTelemetryEmitter::emitState`(`:175-220`)의 `/sys/state` 방출 방식과 동일). 신규 C++ 인코더 0. 필드: `cpu_pct`, `per_block_p99_us`, `xrun_count`(= `driver->xrunCount()`, `:685` 에서 이미 in-scope), `engine_overrun_count`(= `engine.observabilityCounters()` 또는 internal_xruns, **별도 필드**), `shm_xrun_count`, `binaural_demote_count`. shm 무관하게 항상(null/dante 포함) 방출. |

**Gate**:
- `core/tests/test_p_sys_metrics_extended.cpp` **NEW** (NO_JUCE ctest) — 엔진을 short-run 시키고 9101 측에서 `/sys/metrics` 수신, `,s` key=value 페어 파싱 검증(필드명·값 범위). xrun 강제 주입(`MAX_BLOCK` 초과 블록) 시 `xrun_count` 또는 `engine_overrun_count>0` 반영 검증.
- `core/build_rton` (RT-asserts) ctest — `audioBlock` measurement 삽입 후 alloc 0. **이 게이트 red 면 즉시 abort**(원칙 1). (주: `RtAssertNoAllocScope`(`core/src/util/RtAssertNoAlloc.h`)는 `operator new` 만 카운트 → latency 회귀는 못 잡음 → AC2b 별도 게이트 참조.)
- `core/tests/bench_cpumeter_record_latency.cpp` **NEW** (RT-latency 마이크로벤치 ctest, AC2b) — `recordBlockStart()`+`recordBlockEnd()` 합산 median 비용을 N≥50 샘플로 측정해 작은 예산 내 assert. 스칼라 추정기(O(1), 배열 없음)라 trivially pass. median-of-N 마이크로벤치 패턴(repo 내 기존 선례 없음 — `core/tests/CMakeLists.txt` 에 신규 ctest 등록). AC2b 예산은 A-M1 구현 시 O(1) 베이스라인 대비 구체적 µs 값으로 핀하고 기록. 이 게이트 자체는 NET-NEW.
- **TSan/data-race**: 스칼라-atomic 추정기는 배열 race 가 없으므로(단일 `std::atomic` relaxed store/load) AmbiDecoder double-buffer race 테스트류의 무거운 relacy/TSan 게이트는 **불필요** — executor 가 불필요한 테스트 스캐폴딩을 추가하지 않도록 명시.
- 기존 NO_JUCE 101/101 무회귀.

**하위호환**: 신규 주소 `/sys/metrics` 만 추가. 기존 `/sys/state`(shm), `/sys/warning`, `/sys/binaural_*` 바이트 불변. legacy dialect 무영향.

---

### A-M2 — osc_bridge 텔레메트리 분류 + WebSocket 라우팅

**목표**: 9101 수신 OSC 를 주소별 분류해 구조화 payload 로 WS broadcast. 현재 default handler(`osc_bridge.py:134-142`)는 모든 주소를 `{osc_address,args}` raw 로 보냄 — `/sys/metrics`·`/sys/warning`·`/sys/state`(shm) 를 의미 있게 변환.

| 파일 | 변경 |
|---|---|
| `ui/webgui/osc_bridge.py` `:77-78,134-142` **MOD** | default handler 유지(하위호환)하되 `/sys/metrics`→`{type:"metrics", cpu_pct, p99_us, xrun, engine_overrun, shm_xrun, demote}`(`,s` key=value 파싱), `/sys/warning`→`{type:"warning", ts, category, payload}` 분류 매핑 추가. **`/sys/state`(shm on-change key=value)는 분류·MetricsHub 라우팅을 하지 않고 기존 `/ws` raw 경로 유지** — MetricsHub 의 latest-wins 단일 슬롯이 서로 다른 shm 메시지(`shm_producer_alive`/`shm_producer_state`/`shm_consumer_locked`)를 서로 덮어쓰기 때문(데이터 손실). raw fallthrough 보존(미지 주소 + `/sys/state`). |

**상태 라우팅 결정(m1)**: MetricsHub(A-M3)로 들어가는 것은 **`/sys/metrics` 와 `/sys/warning` 만**. `/sys/state` 는 shm 전용 on-change 다중 key=value 스트림이므로 latest-wins 캐시에 넣으면 서로 다른 키가 충돌 → 기존 `/ws` 경로 그대로 둔다.

**Gate**: `ui/webgui/tests/test_osc_bridge_dashboard.py` **NEW** (pytest) — mock 9101 송신 → `_broadcast_fn` 호출 인자가 분류된 dict 인지 검증(metrics/warning 2종 + `/sys/state` 및 미지 주소는 raw fallthrough). 기존 `test_dispatch.py`/`test_osc_bridge_shutdown.py` 무회귀.

---

### A-M3 — FastAPI `/ws/metrics` + `/dashboard` 라우트 + MetricsHub

**목표**: dashboard 전용 WS 채널 분리(기존 `/ws` 는 positional/제어용 그대로). MetricsHub 가 최신 metrics snapshot 보관 + fan-out.

| 파일 | 변경 |
|---|---|
| `ui/webgui/server.py` `:55-79,152-181,468-471,482-484` **MOD** | (a) `MetricsHub` 클래스(최신 snapshot 캐시 + WS 구독자 집합, `ConnectionManager` 패턴 재사용). (b) `@app.websocket("/ws/metrics")` 신규 — 구독 시 즉시 last-snapshot push. (c) `broadcast_state` 가 **`type in {metrics, warning}` 면 MetricsHub 로 라우팅**; **`type=="state"`(shm key=value)는 기존 `/ws` 경로 유지**(latest-wins 단일 슬롯이 distinct shm 메시지를 덮어쓰는 것 방지 — m1 결정). (d) `@app.get("/dashboard")` → `dashboard.html` FileResponse. |

**Gate**: `ui/webgui/tests/test_metrics_ws.py` **NEW** (pytest) — TestClient 로 `/ws/metrics` 연결 → mock metrics broadcast → 클라이언트 수신 검증; 구독 즉시 last-snapshot 수신 검증. **throughput 게이트**: 기존 `test_throughput.py`(60/120Hz)에 `/ws/metrics` fan-out 추가해도 회귀 없음(1Hz 채널이라 부하 미미하나 명시 검증).

---

### A-M4 — Dashboard HTML/JS (자체 canvas 차트)

**목표**: `/dashboard` 진입 시 윤곽 A.1 §1-5 패널 렌더. DD-C C1.

| 파일 | 변경 |
|---|---|
| `ui/webgui/static/dashboard.html` **NEW** | 상단 엔진 상태 / CPU·xrun 패널 / Binaural 패널 / 경고 로그(최근 50) / 객체 활성도. `/static/js/dashboard.js` 로드. 외부 CDN 0. |
| `ui/webgui/static/js/dashboard.js` **NEW** | `/ws/metrics` 연결, 60초 ring buffer, 자체 canvas 라인차트(CPU avg/peak, xrun, shm_xrun). `ws_client.js` 패턴 재사용. |
| `ui/webgui/static/js/minichart.js` **NEW** | ~150 LOC 경량 시계열 canvas 렌더러(축/그리드/라인/피크). |

**Gate**: `ui/webgui/tests/playwright/test_dashboard_smoke.py` **NEW** — headless 진입(`conftest.py` uvicorn fixture 재사용) → `<canvas>` mount 확인 → mock metrics 주입 후 draw 호출 발생 검증 → reset 버튼 DOM 존재 확인.

---

### A-M5 — `/sys/binaural_reset_demote` 버튼 end-to-end

**목표**: dashboard reset 버튼 → `/ws` → osc_bridge `send_osc` → 9100 `/sys/binaural_reset_demote ,i 1`. 엔진 핸들러는 end-to-end 로 이미 존재: OSC 주소 decode 는 `CommandDecoder.cpp:429`(`/sys/binaural_reset_demote` → `CommandTag::SysBinauralResetDemote`), 커맨드-태그 dispatch(`SysBinauralResetDemote` → `binaural_.resetRuntimeDemoteFromUser`)는 `SpatialEngine.cpp:193` 근처. A-M5 는 FE/bridge wiring 만 — 엔진 측 추가 작업 없음.

| 파일 | 변경 |
|---|---|
| `ui/webgui/static/js/dashboard.js` **MOD** | reset 버튼 → `ws.send({type:"binaural_reset_demote"})`. |
| `ui/webgui/server.py` `_dispatch_to_osc` `:201-304` **MOD** | `elif mtype=="binaural_reset_demote": osc_send_fn("/sys/binaural_reset_demote", 1)` 분기 추가. AI-mode position guard 와 무관(제어 평면). |

**Gate**: `ui/webgui/tests/test_metrics_ws.py` 또는 `test_dispatch.py` 확장 — WS `binaural_reset_demote` → `send_osc("/sys/binaural_reset_demote", 1)` 호출 검증(mock). playwright smoke 에 버튼 클릭→WS 송신 spy 추가(선택).

---

### A-M6 — 문서 + README pointer

| 파일 | 변경 |
|---|---|
| `docs/TESTING.md` **MOD** | §A dashboard 접근 절차(`/dashboard`, `/ws/metrics`, 측정 의미). |
| `docs/ENGINE_OVERVIEW_AND_COMPARISON.md` §4.4 #8 **MOD** | "Real-time monitoring UI 부재" → "✅ 자체 dashboard (`/dashboard`)". |
| `README` WebGUI 섹션 **MOD** | dashboard pointer. |
| `docs/adr/ADR-00XX-sys-metrics-channel.md` **NEW** | 얇은 채널-doc: `/sys/metrics` 채널 존재 + 필드 의미(cpu_pct/p99_us/xrun/engine_overrun/shm_xrun/demote)만 기록. **인코더 signature 결정 없음** — `,s` key=value 재사용이라 신규 encoder·ADR 0017 류 signature 결정 불필요. |

**Gate**: 없음(문서). 단 markdown lint 통과.

---

## 3. RT 불변식 가드 + 하위호환 (Critic 체크리스트)

- **RT-asserts 빌드 (alloc=0)**: `cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON` 빌드에서 A-M1 의 `audioBlock` measurement 가 alloc/lock/syscall 0. `CpuMeter` 는 **스칼라 추정기**(고정 멤버 스칼라뿐, 배열/reservoir 없음) → 핫패스는 물론 ctor 도 동적 alloc 없음. `steady_clock::now()` 는 vDSO(alloc 없음). audio thread 는 clock read + O(1) 추정기 갱신 + relaxed 스칼라 store 만.
- **RT-latency 게이트 (AC2b)**: alloc=0 게이트(`RtAssertNoAlloc.h`)는 `operator new` 만 카운트 → latency 회귀는 못 잡으므로, `bench_cpumeter_record_latency` 마이크로벤치(median-of-N, N≥50)로 `recordBlockStart()+recordBlockEnd()` 비용을 작은 예산 내 assert. 스칼라 추정기(O(1))라 trivially pass — 이 게이트가 §6 abort 기준과 연결.
- **`/sys/state` 무손상**: A-M1 은 `/sys/state` 를 건드리지 않음(신규 `/sys/metrics`). `ShmTelemetryEmitter::emitState`(`:175-220`) 바이트 불변 → shm sidecar 하위호환. A-M2/A-M3 도 `/sys/state` 를 MetricsHub 로 라우팅하지 않고 기존 `/ws` 경로 유지(m1 결정).
- **OSC encoder 하위호환**: A-M1 은 **신규 encoder 를 만들지 않는다** — 기존 3-arg `sendReply(addr, ",s", kv)` overload(`OSCBackend.h:120`)만 재사용. `encodeOscReply`/`encodeOscReplyIIF`/`encodeOscReplyIIS`(`OSCBackend.h:311-339`) 전부 무변경.
- **TSan/data-race**: p99 가 스칼라 `std::atomic` 단일값(relaxed store/load)이라 배열 race·torn-read 가 구조적으로 없음 → AmbiDecoder double-buffer race 테스트류의 무거운 relacy/TSan 게이트 불필요.
- **osc_bridge default handler 보존**: 미지 주소는 raw fallthrough → 기존 WS 소비자 무영향.
- **WS fan-out backpressure (DD-D 정책 명문화)**: `/ws/metrics` 는 1Hz 저레이트. MetricsHub 는 per-client send 실패 시 즉시 disconnect(기존 `ConnectionManager.broadcast` `:68-76` 패턴). **drop-newest** 정책: snapshot 캐시는 항상 latest 1개만 보관(큐 없음) → backpressure 시 중간 프레임 손실 허용, 최신값 보장. 큐 사이즈 = 1(latest-wins).

---

## 4. Acceptance Criteria (테스트 가능 형태)

| # | 기준 | 검증 게이트 |
|---|---|---|
| AC1 | 엔진이 `/sys/metrics`(`,s` key=value)를 1Hz±20% 로 방출, cpu_pct∈[0,100]·p99_us≥0·**xrun_count(=backend `driver->xrunCount()`, `spatial_engine_core.cpp:685`) 단조증가**; engine_overrun_count 는 별도 필드(엔진 측 `internal_xruns_`/`util::XrunCounter`) | `test_p_sys_metrics_extended` (NO_JUCE) |
| AC2 | `audioBlock` measurement 삽입 후 RT-asserts 빌드 alloc 0 | `core/build_rton` ctest |
| AC2b | `recordBlockStart()+recordBlockEnd()` median 비용이 작은 예산(small budget) 내 — N≥50 샘플 측정. 스칼라 추정기(O(1))라 여유 통과 | `bench_cpumeter_record_latency` (NEW ctest) |
| AC3 | osc_bridge 가 `/sys/metrics`→metrics dict, `/sys/warning`→warning dict 분류; 미지 주소 raw 보존 | `test_osc_bridge_dashboard.py` |
| AC4 | `/ws/metrics` 구독 시 즉시 last-snapshot 수신 + 후속 broadcast 수신; 기존 throughput 게이트 무회귀 | `test_metrics_ws.py` + `test_throughput.py` |
| AC5 | `/dashboard` headless 진입 시 canvas mount + metrics 주입 시 draw 발생 + reset 버튼 존재 | `test_dashboard_smoke.py` (playwright) |
| AC6 | reset 버튼 → `/sys/binaural_reset_demote ,i 1` 이 9100 으로 송신 | `test_dispatch.py`/`test_metrics_ws.py` 확장 |
| AC7 | 기존 NO_JUCE 101/101, RT 101/101, pytest 225/4-skip, 기존 playwright 무회귀 | 전체 스위트 |

---

## 5. ADR (최종 합의 확정 시 채움)

- **Decision**: (DD-A) `/sys/metrics` 신규 1Hz 채널 신설, **인코딩 = `,s` key=value**(기존 `sendReply(addr,",s",kv)` overload `OSCBackend.h:120` 재사용 — `ShmTelemetryEmitter::emitState` 선례). (DD-B) steady_clock wall + EWMA(평균) + **스칼라 러닝 p99 추정기**(단일 `std::atomic<uint32_t>`, 배열·reservoir 없음). (DD-C) 자체 canvas 미니차트. (배선) `ObservabilityCounters` 단일 인스턴스를 `SpatialEngine` 이 소유(`obs_counters_` 멤버) + `observabilityCounters()` 접근자 신설 — **NET-NEW**(기존엔 dead struct 정의만 존재).
- **Drivers**: `ObservabilityCounters` 가 `/sys/metrics` 를 *문서상* 예약(다만 인스턴스·방출기 부재 → 신설 필요); null/dante 경로에서도 CPU/xrun 상시 필요; 오프라인·의존성 0 원칙; RT 핫패스 coherence(스칼라 추정기로 torn-read 윈도우 제거).
- **Alternatives considered**: (a) `/sys/state` trailing-args — 무효: 풀-state 방출기 부재·shm 의미충돌. (b) **1024-슬롯 ring reservoir p99** — 드롭: cross-thread 시 배열 race/double-buffer/torn-read 부담; 스칼라 P²/GK 추정기가 O(1)·race-free 로 동등 운영가치 제공(**사용자 승인 amendment 2026-05-30**). (c) `CLOCK_THREAD_CPUTIME_ID` — 이식성 부담, 보조지표로 v1.x. (d) 신규 멀티-arg OSC 인코더(`,iiiii`/`encodeOscReplyMetrics`) — 드롭: `,s` key=value 재사용으로 신규 인코더·ADR 0017 류 signature 결정 불필요. (e) Chart.js/uPlot — 과대/의존성.
- **Consequences**: 신규 OSC encoder **0**(`,s` 재사용); osc_bridge·dashboard 가 새 주소 인지(`/sys/metrics`만 MetricsHub 라우팅, `/sys/state` 는 기존 `/ws` 유지); CPU%는 wall 기반(프리엠션 포함, 약간 비관적); p99 는 정확값이 아닌 스칼라 추정값(운영 표시용 충분); `SpatialEngine` 이 `ObservabilityCounters` 단일 인스턴스 소유(신규 멤버+접근자); ADR 은 얇은 채널-doc(필드 의미만).
- **Follow-ups**: (open-questions 참조) Windows clock 분기; CPU% wall vs thread-cpu 보조지표; binaural_status 주기 push 신설 여부; **1024-슬롯 정밀 reservoir 는 스칼라 추정기로 대체됨**(RT coherence, user-approved 2026-05-30) — 정밀 분위수가 추후 필요하면 control-thread 측 오프-핫패스 집계로 재검토.

---

## 6. 실행 순서 / effort

- 세션 1: A-M1(+ADR 초안), A-M2
- 세션 2: A-M3, A-M4
- 세션 3: A-M5, A-M6
- 모드: `ralplan` 합의 → `autopilot`. 게이트 red 2회 retry 후 비-flaky 실패 시 STOP + surface.
- **Abort 기준(원칙 1)**: A-M1 measurement 삽입 후 (a) RT-asserts alloc=0 게이트(AC2)가 red, 또는 (b) RT-latency 마이크로벤치(AC2b, `bench_cpumeter_record_latency`)가 예산 초과로 red 면 즉시 abort. 스칼라 추정기 채택으로 두 게이트 모두 trivially pass 예상이나, red 시 핫패스 회귀로 간주하고 중단.

## Progress tracker
- [x] A-M1 CpuMeter + /sys/metrics 1Hz 방출 (+ADR) — done 2026-05-30. NO_JUCE 103/103, RT-asserts(build_rton) 107/107 (AC2 alloc=0 via rt_alloc_violations()==0 in test_p_sys_metrics_extended), AC2b bench median 0.115 µs vs 5.0 µs budget. binaural_demote_count emitted as sticky runtime-demote flag (0/1) via binauralIsRuntimeDemoted() — cumulative strike count getter not cheaply available; revisit if dashboard needs the count. ADR-text captured inline in plan §5 (thin channel-doc deferred to A-M6).
- [x] A-M2 osc_bridge 텔레메트리 분류 — done 2026-05-30. `/sys/metrics`→typed metrics dict (`,s` key=value 파싱, 미지/junk 키 robust), `/sys/warning`+`/sys/binaural_warning`→warning dict ({type,ts,category,payload}, `,sf` shape), `/sys/state`(shm) + 미지 주소는 raw fallthrough 유지 (m1 결정). NEW test_osc_bridge_dashboard.py (8 tests). webgui pytest 50 passed (기존 42 + 신규 8), 무회귀.
- [x] A-M3 /ws/metrics + /dashboard 라우트 + MetricsHub — done 2026-05-30. server.py: MetricsHub(최신 snapshot 단일 슬롯 latest-wins + 구독자 set, ConnectionManager drop-on-fail 패턴 재사용, connect 시 즉시 last-snapshot push); `@app.websocket("/ws/metrics")`(push-only, inbound drain); `broadcast_state` 라우팅(type∈{metrics,warning}→MetricsHub, 나머지=raw/sys/state→기존 /ws); `@app.get("/dashboard")` FileResponse(파일 없으면 graceful 404). dashboard.html = A-M4 전까지 placeholder stub(200 served, A-M4 가 전면 교체). NEW test_metrics_ws.py (7 tests: metrics 수신·warning 라우팅·fresh-connect snapshot·latest-wins·/sys/state 미유입·역방향 격리·/dashboard 라우트 등록). webgui pytest 57 passed (기존 50 + 신규 7), throughput 60/120Hz 게이트 무회귀.
- [ ] A-M4 dashboard.html/js + 자체 canvas 차트
- [ ] A-M5 reset_demote 버튼 e2e
- [ ] A-M6 문서 + README + ADR 확정
