# Spatial Engine — 기능 인벤토리 · 상용 비교 · TODO 로드맵

작성: 2026-05-29 (HEAD `6d9958b`, v0.7.0+8)
대상: 의사결정자 / 리뷰어 / 신규 합류 엔지니어

---

## 0. 한 줄 요약

객체 기반 + 채널 기반 + 앰비소닉 + 바이노럴을 모두 단일 코어에서 처리하는 **헤드리스-우선 (headless-first)** 실시간 공간 오디오 엔진. C++ NO-JUCE 코어 + Python WebGUI + VST3 플러그인 + ADM-OSC 표준 + 공유메모리 IPC sidecar 까지 한 트리에 들어있음. GPL-3.0-or-later (외부 배포 시 JUCE 상용 라이선스 필요 — `LICENSE.md` 참조).

---

## 1. 데이터 플로우 (3가지 production 경로)

```
[A] 헤드리스 standalone  (ADM BWF → engine WAV)
   adm_player <wav> --sink ipc://NAME --osc-port 9100
                          ↓ shm ring (POSIX shared memory)
   spatial_engine_core --input-backend shm:/NAME --backend null --wav out.wav

[B] DAW 내 처리  (VST3 호스트)
   DAW (Reaper / Logic / Bitwig) → SpatialEngine.vst3 plugin
                                    ↓ audioProcessBlock + 6 params
                                    bus 0: multi-ch out  bus 1: binaural stereo

[C] 브라우저 컨트롤  (WebGUI → engine)
   Browser :8000 ─WebSocket→ FastAPI ─OSC :9100→ spatial_engine_core
                                ↑              ←:9101 /sys/state 1Hz
                                └─ trajectory runner (자동 경로 합성)
```

A, B, C 단독 모두 production-ready. B+C 동시는 port 9100/9101 contention 주의.

---

## 2. 구현 인벤토리 (v0.7.0+8 시점)

### 2.1 DSP 렌더링

| 기능 | 상세 | 구현 위치 | 테스트 |
|---|---|---|---|
| **VBAP 2D / 3D** | Pulkki 1997 원형 트라이앵글레이션, RT-safe scratch 버퍼 (v0.8 P1.3 alloc 제거) | `core/src/render/VBAPRenderer.cpp`, `AlgorithmAnalyticReference.cpp` | `test_p_vbap*`, `test_p_vbap3d*` |
| **DBAP** | Lossius distance-based panning, 비방사 가중 | `core/src/render/DBAPRenderer.cpp` | `test_p_dbap*` |
| **WFS** | 1D-array wave field synthesis (실험적) | `core/src/render/WFSRenderer.cpp` | `test_p_wfs*` |
| **Ambisonic encoder** | 1–3차, SN3D 정규화, AmbiX 채널 순서 (ACN) | `core/src/ambi/AmbisonicEncoder.cpp` | `test_p_ambi.cpp:214-241` + v0.8 P1.2 SN3D-상수 oracle + P3.2 abs-gain golden |
| **Ambisonic decoder ×5** | Sampling / MaxRE / EPAD / AllRAD / InPhase, **런타임 lock-free 더블 버퍼 swap** (v0.8 P1.1) | `core/src/ambi/AmbiDecoder.cpp` + `*Decoder.cpp` | 12 ctest, concurrency stress (`test_p_ambi_decoder_double_buffer_race`) |
| **EPAD rank-aware** | `D·Dᵀ` 에너지 보존, K<S 분기에서 `1/√N` 정규화 (v0.8 P2.1) | `EPADDecoder.cpp:226` | `test_epad_rank_aware*` |
| **HRTF Binaural** | 4 SOFA 데이터셋 (KEMAR / CIPIC-003 / SADIE-II-H08 / HUTUBS-pp1) 런타임 hot-swap (v0.9 Lane B), OLA 컨볼브, KD-tree NN lookup, B1/B2 모드 (B2=고품질 / B1=러닝 fallback), demote 디멀티플렉서 + 사용자 reset | `core/src/hrtf/HrtfLookup.cpp`, `OlaConvolver.cpp`, `KdTree3D.cpp`, `SofaBinReader.cpp` | `test_p_binaural*` (11), P3.4 interp oracle |
| **FDN Reverb** | 16-line Hadamard, mutually-prime 딜레이 (1499–3001), 1-pole LP per line, DC 주입, FTZ/DAZ | `core/src/reverb/FdnReverb.cpp` | **DSP-6 fix (v0.8 P2.3, 2026-05-29)**: `readPos = writePos`; T60 oracle |
| **IR Convolution Reverb** | 멀티채널 IR validation + RT-safe FFT 컨볼루션, FDN/IR 런타임 swap (`/reverb/select` ,s `"fdn"`/`"ir"`) | `core/src/reverb/IRConvReverb.cpp`, `ReverbEngine.cpp` (factory) | `test_p7_ir_metadata*` |

### 2.2 입력 / 출력 Backend

| Backend | 용도 | 상태 |
|---|---|---|
| `NullBackend` | 헤드리스 테스트 (silence input / discard output) | ✅ v0.0 |
| `DanteBackend` | Audinate Dante AoIP (JUCE 빌드 전용) | ✅ JUCE=ON |
| `SharedRingBackend` (ADR 0019 PR2–3) | POSIX shm ring (Linux/macOS); sidecar producer 와 단일 머신 IPC | ✅ v0.7 |
| `SharedMemoryRegion` (ADR 0019 PR1, P6.1) | shm 매핑; `O_NOFOLLOW` symlink 방어 | ✅ v0.8 |

### 2.3 제어 / IPC

| 인터페이스 | 표준 | 상태 |
|---|---|---|
| **OSC dual dialect** — legacy `/obj/{id}/aed` + **ADM-OSC v1.0** `/adm/obj/{id}/aed` | Immersive-Audio-Live ADM-OSC v1.0 | ✅ v0.2.0 (C3-Q1..Q10 cohort, `CommandDecoder.cpp:179+`) |
| **Heartbeat / `/sys/state`** (1 Hz) | 자체 schema v1 | ✅ ADR 0018 v0.7 |
| **Event `/sys/warning`** (one-shot drain latch) | 자체 (no_sofa_loaded, layout_incompatible, shm_full 등) | ✅ v0.5–v0.7 |
| **`/sys/binaural_diag`** (B2 demote 진단) | 자체 (block_size, sample_rate, max_ratio) | ✅ v0.7 (V07-Q1 deferred) |
| **`/sys/binaural_reset_demote`** | 사용자 주도 복구 | ✅ v0.7 |
| **shm telemetry** (`/sys/warning shm_*`) | ADR 0019 PR4 | ✅ v0.7 |
| **VST3 6 params** + 2-bus output | Steinberg VST3 SDK 3.7 | ✅ v0.2–v0.4 |
| **WebGUI** FastAPI + canvas drag + trajectory synth | 자체 WebSocket → OSC bridge | ✅ v0.1 |

### 2.4 Sidecar / 외부 통합

| 컴포넌트 | 역할 | 상태 |
|---|---|---|
| **adm_player** (sibling repo `paiiek/ADM_player`) | ADM BWF 파일 재생 → `--sink ipc://NAME` 으로 shm ring 에 push + ADM-OSC 객체 위치 emit | ✅ v0.5 (ADR 0019 PR5 dual-repo) |
| **adm_recorder** | BW64 ADM 파일 자체검증 + chna/채널 매핑 | ✅ adm_player Patch 3 |
| **vid2spatial_osc** bridge | 영상 추적 (`vid2spatial_v2`) → OSC `/vid2spatial/spatial` → 엔진 `/adm/obj/N/aed` | ✅ v0.1 |
| **WebGUI osc_bridge** | 웹소켓 ↔ OSC 9100/9101 양방향 브리지 | ✅ v0.1 |

### 2.5 RT 안전성 / 검증 인프라

| 도구 | 무엇을 잡나 | 상태 |
|---|---|---|
| **RT-asserts 빌드** (`build_rton`) | 오디오 스레드 `malloc/new/free` → SIGTRAP | ✅ v0.6 |
| **Relacy 합성 race detector** | 멀티스레드 invariant (decoder swap, OSC ring) 1024 iter | ✅ v0.6 |
| **TSan / std::thread stress** | concurrency 실측 (decoder double-buffer P1.1) | ✅ v0.8 P1.1 |
| **FTZ/DAZ denormal flush** | SSE MXCSR + ARM FPCR, FDN/IR 경로 모두 | ✅ v0.1 |
| **layout YAML 검증** | 채널 매핑, 토폴로지, 좌표 일관성 | ✅ v0.4 (channel mapping fix) |

---

## 3. 아키텍처 결정 사항 (선별)

- **ADR 0003** OSC schema v1 + heartbeat 의무화
- **ADR 0006a** algorithm 런타임 swap (256-sample crossfade)
- **ADR 0010** VST3 IComponentHandler::performEdit 스레드 안전
- **ADR 0018** Phase B sync handlers (handshake + transport.play + 1Hz heartbeat)
- **ADR 0019** Phase C PCM IPC (PR1 shm region → PR5 adm_player IpcRingSink → **PR6 cross-process soak 미해결**)
- **ADR 0011** VST3 per-instance UDP listener (port reuse 충돌 방지)
- **ADR 0014** WebGUI 60fps methodology (min-of-5-windows-p10, N=100)
- **ADR 0016** binaural license / IR procurement 트랙

ADR 인덱스: `docs/adr/index.md`.

---

## 4. 시중 엔진들과의 기능 비교

> **유의:** 이 표는 공개 자료(공식 사이트 / 매뉴얼 / 학회 논문) 기반의 정리입니다. 상용 제품의 정확한 사양은 라이선스 / 버전 / 옵션에 따라 다를 수 있으니, 의사결정 시 벤더 측 확인을 권장합니다.

### 4.1 상용 제품과 비교

| 항목 | **Spatial Engine (본 프로젝트)** | **L-Acoustics L-ISA** | **Flux:: Spat Revolution** | **Dolby Atmos Renderer / Production Suite** | **IRCAM Spat~** | **Sennheiser AMBEO Orbit** |
|---|---|---|---|---|---|---|
| **라이선스** | GPL-3.0-or-later (외부 배포 시 JUCE 상용 라이선스 필요) | 상용 (h/w 동봉) | 상용 (Pro €1499) | 상용 (Pro €299 + h/w opt) | 상용 (€399/€199 EDU) | 무료 (Sennheiser 등록) |
| **타겟** | 공연·전시 + 연구 | 라이브 공연 (대규모 라우드스피커) | 라이브 + 포스트 | 시네마/홈 라우드 + Atmos OTT | 음악 작곡 / 연구 | 바이노럴 믹스 (헤드폰) |
| **객체 수** | MAX_OBJECTS=64 (기본) / 128 opt-in (WFS-inactive 검증, ADR 0021) | ~96 객체 + 멀티존 | ~128 객체 | ~118 객체 (베드+오디오 오브젝트) | 무제한 (CPU 한도) | 16 객체 |
| **VBAP** | ✅ 2D + 3D | ✅ (proprietary "L-ISA Hyperreal Sound") | ✅ | ✅ (Atmos object panning) | ✅ | ✅ |
| **DBAP** | ✅ | – | ✅ | – | ✅ | – |
| **WFS** | ✅ 실험적 (1D array) | – | – | – | ✅ | – |
| **Ambisonic (encode + decode)** | 1–3차 SN3D AmbiX, 5개 decoder | 자체 포맷 (Hyperreal Sound, 비공개) | 1–7차 + AllRAD/MaxRE/InPhase | – | 1–5차 (FuMa + SN3D) | 1–3차 input (encode only) |
| **HRTF Binaural** | ✅ 4 SOFA 데이터셋 (KEMAR/CIPIC/SADIE-II/HUTUBS) 런타임 hot-swap, B1/B2 demote 시스템 | ✅ (오프라인 변환) | ✅ SOFA, 7+ HRTF 데이터셋 | ✅ Dolby 자체 HRTF | ✅ SOFA, IRCAM Listen | ✅ 자체 HRTF + SOFA |
| **Reverb** | FDN 16-line | proprietary "Hyperreal Reverb" | FDN + 컨볼루션 | proprietary Dolby reverb | FDN + 컨볼루션 + EarlyRefl | – |
| **표준화 IO 포맷** | **ADM BWF (BW64 chna/axml), ADM-OSC v1.0** | proprietary L-ISA 포맷 + ADM (BWF) input | OSC, ADM-OSC, AAX/VST3 | ADM (BWF), MP4 (DD+/JOC) | ADM, OSC, OSC-extensions | ADM, ATSC 3.0 |
| **VST3 / AU / AAX** | VST3 only (v0.2–v0.4) | AAX / VST3 / AU (full) | VST3 / AU / AAX | RTAS / AAX (Pro Tools 전용) | VST3 / Max for Live | VST3 / AU |
| **DAW 통합** | Reaper / Logic / Bitwig 호스트 | Pro Tools 권장 | Pro Tools / Reaper / Logic | **Pro Tools 전용 (Renderer 독립 가능)** | Max/MSP + DAW | Reaper / Cubase / Logic 등 |
| **표준 OSC dialect** | **ADM-OSC v1.0** (legacy 와 dual) | proprietary OSC | OSC + ADM-OSC | – (proprietary RMU API) | OSC (자체 vocabulary) | – |
| **헤드리스 / CLI 실행** | ✅ `spatial_engine_core` 단독 | 부분 (Processor h/w 의존) | – (GUI 필수) | ✅ Atmos Renderer 헤드리스 모드 | ✅ Max headless | – |
| **공유 메모리 IPC** | ✅ POSIX shm ring (ADR 0019) | proprietary internal IPC | – | proprietary RMU 채널 | UDP/socket | – |
| **WebGUI 컨트롤** | ✅ FastAPI + 캔버스 + WebSocket | L-ISA Controller (web 옵션 있음) | – (데스크탑 GUI) | Dolby Atmos Cinema Web | – (Max 패치) | – (데스크탑 GUI) |
| **실시간 안전 검증** | RT-asserts + Relacy + TSan | – (블랙박스) | – (블랙박스) | – (블랙박스) | 부분 (Max 의 deterministic mode) | – |
| **Multi-host 네트워크** | 부분 (Dante 출력) | ✅ L-ISA Processor 분산 | OSC + Dante | Dolby RMU + Ravenna | OSC + Jack | – |
| **운영체제** | Linux + macOS + Windows (Linux 우선) | Linux/macOS appliance | macOS + Windows | macOS + Windows | macOS + Windows + Linux | macOS + Windows |
| **가격대** | **무료** | $$$$ (h/w 동봉) | $$$ | $$$ | $$ | 무료 |
| **타사 ADM-OSC 호환** | ✅ (Q2025 검증) | 일부 (vendor map 필요) | ✅ | – | ✅ | – |

### 4.2 오픈소스 프로젝트와 비교

| 항목 | **Spatial Engine** | **SPARTA / COMPASS** (Aalto) | **IEM Plug-in Suite** (KUG) | **Resonance Audio** (Google, deprecated) | **Mach1 Spatial** | **Ambix Suite** (Kronlachner) |
|---|---|---|---|---|---|---|
| **라이선스** | GPL-3.0-or-later | GPLv3 | GPLv3 | Apache 2.0 (deprecated 2022) | proprietary SDK + OSS bindings | GPLv3 |
| **포커스** | 통합 엔진 + 헤드리스 + 표준 IO | Ambisonic + binaural 연구 | Ambisonic + HOA 음악 | VR/AR 모바일 | Multi-format 자체 spec | Ambisonic 도구 |
| **VBAP / DBAP / WFS** | ✅✅✅ | – (Ambisonic-centric) | – | – | – | – |
| **Ambisonic order** | 1–3차 (SN3D / AmbiX) | 1–7차 (SN3D, N3D) | 1–7차 | 1–3차 | – | 1–7차 |
| **HRTF Binaural** | ✅ 4 SOFA 데이터셋 (KEMAR/CIPIC/SADIE-II/HUTUBS) 런타임 hot-swap | ✅ SOFA + magLS / MagLS | ✅ SOFA | ✅ 자체 HRTF | ✅ | ✅ |
| **표준 IO (ADM/OSC)** | ✅ ADM-OSC + BWF | – (Max/JUCE 플러그인 위주) | – | – (mobile API) | proprietary M1 format | ADM 부분 |
| **VST3 플러그인** | ✅ (1 plugin, 6 params) | ✅ 다수 (10+) | ✅ 20+ plugins | – (mobile-only) | ✅ | ✅ |
| **헤드리스 CLI** | ✅ | – | – | – (lib) | – | – |
| **WebGUI** | ✅ | – | – | – | – | – |
| **공연/라이브 톤** | ✅ (low-latency, RT-asserts) | research-grade (Max/JUCE) | research-grade | mobile/runtime | game/VR | offline 도구 |
| **언어 / 빌드** | C++17 + Python + CMake | MATLAB → C/C++ (SPARTA) + JUCE | C++ + JUCE | C++ + Java/Kotlin (Android) | C++ + Unity/Unreal SDK | C++ + JUCE |
| **표준 준수 (ADM/AmbiX)** | ✅ AmbiX 채널/정규화 | ✅ AmbiX | ✅ AmbiX | proprietary | proprietary | ✅ AmbiX |

### 4.3 본 프로젝트의 **차별점**

1. **표준 ADM + ADM-OSC + BWF 까지 한 트리** — 라이브 콘솔 (L-ISA, d&b Soundscape) 의 standard interop 이 OSS 에서 가능.
2. **헤드리스 standalone + DAW 플러그인 + WebGUI** 세 production 경로가 같은 코어 라이브러리 공유. 다른 OSS 들은 보통 한 경로만.
3. **shared-memory IPC sidecar 패턴** — 단일 머신 sample-accurate transport. Dolby RMU 같은 proprietary 솔루션의 OSS 대체.
4. **RT-no-alloc sentinel + Relacy** — 오디오 스레드 안전성을 CI 에서 강제. SPARTA / IEM 같은 연구용 빌드는 보통 안 함.
5. **5개 Ambisonic decoder + 5개 panner + binaural + reverb** 풀스택. SPARTA/IEM 의 Ambisonic + Mach1 의 multi-format 을 합친 모양.

### 4.4 본 프로젝트의 **한계 / 약점**

1. **AAX 미지원** — Pro Tools 사용자는 VST3 wrapper (Blue Cat Audio 등) 거쳐야 함. AAX SDK 라이선스 미정.
2. **기본 MAX_OBJECTS=64** — L-ISA(~96) / Atmos(118) / Spat Pro(무제한) 대비 적음. v0.9 Lane C+F5 에서 **128 opt-in 검증 완료**(WFS-inactive 배포; ADR 0021), 다만 기본값은 64 유지 — RT-peak headroom(46.9% > 35% 임계) + WFS-active 시 footprint ≈111MB(>100MB) 때문. 기본 64→128 flip 은 RT-peak 개선 후속 과제.
3. **고차 Ambisonic (5–7차) 미지원** — IEM/SPARTA 는 7차까지. M2HOA-Q10 open (v0.10 Lane D 후보).
4. **씬/큐 워크플로우 — v0.9 Lane E 로 도입** — `/scene/{save,load,list,rename,duplicate,delete}` + `/cue/{go,next,prev,stop}` cue list + crossfade + MIDI Program Change 트리거 (`docs/SCENE_AND_CUE_WORKFLOW.md`). **잔여 한계**: 스냅샷이 `width_rad`/`reverb_send` 미직렬화(cue load 시 0 리셋, F4 — 후속 수정 예정), crossfade 입자도 ≈100ms(F1), 상용 컨트롤러급 자동화는 여전히 미숙.
5. **HRTF 데이터셋 — v0.9 Lane B 로 4종 확장 + 런타임 hot-swap** (KEMAR / CIPIC-003 / SADIE-II-H08 / HUTUBS-pp1; `docs/HRTF_DATASETS.md`). 개별화(individualized) HRTF 측정 파이프라인은 미지원.
6. **Atmos 인증 안 됨** — Dolby Atmos 워크플로우와 직접 호환 안 됨 (BWF ADM 포맷은 공유하나 metadata 매핑 차이).
7. **MPEG-H 미지원** — ATSC 3.0 / 차세대 방송 워크플로우 미진입.
8. **Real-time monitoring UI — 기본 대시보드 제공** (v0.9 Lane A) — WebGUI `/dashboard` 라우트가 self-hosted canvas 미니차트(외부 CDN 0)로 엔진 텔레메트리를 시각화한다. 엔진이 1Hz 로 방출하는 `/sys/metrics`(cpu_pct/cpu_peak_pct/p99_us/xrun_count/engine_overrun_count/binaural_demote_count, `,s` key=value — ADR 0020) 를 `osc_bridge` 가 분류해 `/ws/metrics` 로 push, CPU avg/peak·xrun Δ·p99 차트 + 경고 로그 + binaural demote 리셋 버튼을 렌더. **범위 한정**: 텔레메트리는 1Hz 갱신(서브-초 트레이스 아님), cpu_peak_pct 는 [0,100] 클램프, binaural_demote_count 는 0/1 sticky 플래그(누적 카운트 아님), object-activity 그리드는 정적 스캐폴드(라이브 배선 미완). 상용 컨트롤러급 분석/스냅샷 워크플로우는 여전히 미숙(#4 참조).

---

## 5. TODO 로드맵 — v0.8 audit 잔여 + v0.9–v1.x

### 5.1 즉시 (v0.8.x patch — supervised sprint)

| 항목 | 우선 | 이유 | 출처 |
|---|---|---|---|
| **P3.1** VST3 state-contract test → NO_JUCE CI | HIGH | 29개 state/param 회귀를 CI 가 감지 못함 | v0.8 audit |
| **P3.5** `vst3_bind_collision` race fix | MED | `-j` 빌드에서 port 9100 race; 현재 RUN_SERIAL 임시 가드 | v0.8 audit |
| **build_vst3 reconfigure** (22일 stale) | MED | P3.1/P3.5 선결 조건 | v0.8 audit |
| **ADR 0006a → 0007 rename** | MED | 번호 갭 (0006 중복, 0007–0009 빈자리) | v0.8 audit P4.1 |
| **open-questions reconcile (88 잔여)** | LOW | v07-Q*, v03-Q4/Q7, M2HOA-Q12/13/15, WGUI-Q11+ | v0.8 audit P4.2 |
| **Python dep upgrade** (starlette via fastapi 0.115+, urllib3, idna, pytest) | LOW | PIN-DEFER per plan, 봉제 후 WebGUI 재검증 필요 | v0.8 audit P6.2 |

### 5.2 v0.9 — 진행/완료 (Lane A/B/C/E/F5 SHIPPED, untagged)

| 항목 | 상태 |
|---|---|
| **Real-time metrics dashboard** (WebGUI `/dashboard`) | ✅ Lane A 완료 (self-hosted canvas, `/sys/metrics` 1Hz) |
| **HRTF dataset 다양화** (KEMAR/CIPIC/SADIE-II/HUTUBS) | ✅ Lane B 완료 (4종 + 런타임 hot-swap) |
| **MAX_OBJECTS 64 → 128** | ✅ Lane C+F5 — 128 opt-in 검증(WFS-inactive); 기본 64 유지(RT-peak/WFS-active) |
| **씬/큐 워크플로우** (`/scene/*`, `/cue/*`, crossfade, MIDI PC) | ✅ Lane E 완료 (잔여: F4 width/reverb_send 직렬화) |
| **WFS/DelayLine 메모리 개선** | ✅ Lane F5 — lazy WFS alloc + capacity 템플릿화 (ADR 0021) |
| **ADR 0019 PR6** — 60s cross-process shm soak | ⏳ 미완 — shm IPC 트랙 종결, 플랜 없음(ralplan 필요) |
| **ADR 0019 PR7** — cross-platform CI (Windows `CreateFileMappingW` / macOS POSIX) | ⏳ 미완 — PR6 후속 |
| **WAV 파일 직접 입력 backend** (`--input-backend file:<wav>`) | ⏳ `core/src/input/FileInput.cpp` 코드 존재, CLI 미연결 |
| **씬 스냅샷 width_rad/reverb_send 직렬화** (F4) | ⏳ 다음 레인 (포맷 계약 변경, ralplan 예정) |
| **`SpatialEngine` god-object refactor** (P7.1) | ⏳ supervised VST3 sprint, BinauralTelemetry facade 추출 |
| **vid2spatial_v3 → engine 통합** | ⏳ 새 비전 모델 결과 받기 |
| **EQ / Limiter / DRC** | ⏳ 출력 체인 마무리 (플랜 없음) |

### 5.3 v1.0 (정식 1년 안) — production ready

| 항목 | 비고 |
|---|---|
| **고차 Ambisonic (5–7차)** | encoder + decoder 일반화, IEM/SPARTA 정도 수준 |
| **Per-bus / per-object decoder selection** | M2HOA-Q12 (Spat 의 per-bus 패턴) |
| **AAX 플러그인** | Pro Tools 시장 진입 (Avid 라이선스 필요) |
| **Multi-host distributed render** | 여러 머신에 split, 동기는 LTC + PTP |
| **MPEG-H Production Audio** | ATSC 3.0 / 차세대 방송 워크플로우 |
| **Sample-accurate transport** | PR6 결과 기반, 외부 timecode chase 확장 |
| **씬 라이브러리 / snapshot 자동화** | L-ISA Controller 수준의 워크플로우 UI |

### 5.4 v1.x+ (장기) — 차별화

| 항목 | 비고 |
|---|---|
| **Atmos 인증 워크플로우** | Dolby 인증 — h/w 의존 큼, ROI 분석 필요 |
| **DTS:X / Auro-3D 호환** | encoder/decoder 추가 |
| **Room acoustics simulation** (MultiVerse style) | 임펄스 측정 + Geometric Acoustics 결합 |
| **Headtracking integration** (IMU / OptiTrack) | 바이노럴 모니터링 라이브 적응 |
| **AI 객체 추적 강화** | 본 프로젝트의 `vid2spatial_*` 라인업 통합 |
| **모바일 SDK (Android/iOS)** | Resonance Audio 빈자리 채우기 |
| **CRT (cross-talk cancellation) for stereo dipole** | Audio Booth / 데스크탑 모니터링 |

---

## 6. 참고 자료

- 본 프로젝트 ADR 인덱스: `docs/adr/index.md`
- 테스트 가이드: `docs/TESTING.md`
- v0.8 audit 트래커: `.omc/plans/spatial-engine-v0.8-audit-remediation.md`
- 엔진 status overview: `.omc/plans/spatial-engine-v0.8-status-overview.md`
- ADM-OSC v1.0 사양: https://immersive-audio-live.github.io/ADM-OSC/
- SOFA 표준: https://www.sofaconventions.org/
- AmbiX 표준: Nachbar, Zotter, Deleflie, Sontacchi (2011), "AmbiX — A Suggested Ambisonics Format"

---

문서 버전: 2026-05-29 (HEAD `6d9958b`). 다음 갱신: v0.8.0 release 또는 v0.9 plan 확정 시.
