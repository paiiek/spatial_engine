# spatial_engine 주간 진척 리포트 — 2026-05-18

**Reporting window**: 2026-05-11 ~ 2026-05-18 (한 주)
**Author**: paiiek (paik402@snu.ac.kr)
**HEAD 시점**: v0.5.1 commit `aeb011c` (릴리스 태그) + v0.6 안정성 보강 미커밋 (본 커밋에서 commit)
**Audience**: (1) 처음 보는 동료 (whole-picture), (2) 이번 주 진척만 확인하려는 stakeholder

---

## §1 프로젝트 한 줄 정의

**spatial_engine**은 C++ JUCE-style 코어 + PySide6/WebGUI 프런트엔드로 구성된 **객체 기반 공간 음향 렌더링 엔진**이다. OSC/UDP로 들어오는 객체 위치·게인·트리거를 실시간으로 받아 (a) 다중 채널 스피커 어레이 (VBAP / DBAP / HOA 디코더), (b) 헤드폰 바이노럴 (HRTF 컨볼루션) 두 갈래로 렌더한다. **DAW 플러그인 (VST3)** 형태로도 배포되어 Reaper/Bitwig 같은 호스트 안에서 한 트랙으로 두 출력 (스피커 + 바이노럴) 을 동시에 받아 라우팅할 수 있다.

**이번 주 주요 변화**: 4 릴리스 (v0.3.1 → v0.4 → v0.5 → v0.5.1) 를 출시하며 *"엔지니어가 데모하는 도구"* 에서 *"음악 제작자가 DAW에서 일상적으로 돌리는 플러그인"* 으로 경계선을 넘었다. 핵심 진전: **WebGUI 마무리** + **VST3 2-버스 분리** + **상용 수준 바이노럴 디코더 (B1/B2)** + **출시 직전 핫픽스 4종**. 추가로 v0.6 안정성 보강 (#4 audio-thread OSC 분리 + #5 런타임 자동 디모트) 구현이 완료되어 본 사이클과 함께 land 한다.

---

## §2 시스템 전체 개요 (newcomer 용)

### §2.1 왜 이 엔진이 필요한가?

게임 / 영상 / 음악 제작자가 "객체 6개 ~ 64개를 임의 위치에 배치하고 헤드폰 + 스피커로 동시에 모니터링" 하는 워크플로는, 기존에는 Dolby Atmos Renderer / Spatial Audio Designer (Sennheiser) 같은 **상용 클로즈드 박스** 가 아니면 불가능했다. spatial_engine 은 동일 워크플로를 **오픈 소스 + JUCE 기반 + VST3 호스트 안** 에서 제공하는 것을 목표로 한다. 이번 주의 성과는 그 목표선 (DAW 안에서 일상 사용 가능) 에 처음 도달한 것이다.

### §2.2 핵심 도메인 용어 (newcomer 용 glossary)

| 용어 | 정의 |
| --- | --- |
| **OSC** | Open Sound Control — UDP 기반 메시지 프로토콜. 객체 위치 (예: `/obj/3/pos f f f`) 를 외부에서 엔진에 푸시. |
| **HOA** | Higher-Order Ambisonics — 음장을 구면 조화 함수 기저로 분해해 N차 (현재 1~3차) 로 전송 후 디코더가 스피커 어레이로 환원. |
| **HRTF / SOFA** | Head-Related Transfer Function — 머리·귀 회절 응답. SOFA 는 그 표준 파일 포맷. |
| **B1 (per-object HRTF)** | 각 음원의 정확한 방향 HRIR 를 KdTree3D 로 찾아 직접 컨볼루션. 단순·고품질. |
| **B2 (AmbiVS chain)** | 3차 HOA → 24-pt t-design 가상 스피커 → 24-쌍 HRTF 컨볼루션. CPU 비용은 크지만 음장 일관성이 좋음. |
| **t-design** | sphere quadrature 점 집합. N차 다항식까지 정확한 적분이 가능한 최소 점 수. 본 엔진은 24-pt × order≤3. |
| **KdTree3D** | 3D 점에 대한 균형 트리. HRTF 그리드 (수백 점) 에서 O(log N) 최근접 탐색. |
| **OLA (Overlap-Add)** | 블록 단위 FFT 컨볼루션 + tail-mix. RT 안전 (no alloc) 변종. |
| **state v4** | DAW 프로젝트 파일에 저장되는 플러그인 상태 TLV 포맷. v3 와 byte-equal 호환. |
| **layout YAML** | 스피커 배치 (이름, 방위각/고도/거리, 채널 번호) 를 기술하는 입력 파일. |
| **.speh** | spatial_engine session header — 한 세션의 layout YAML 경로 + bin 모드 enable bit 등. VST3 파라미터로도 주입 가능. |
| **soak harness** | 48 시간 무중단 운영 테스트. 메모리·연결·OSC drop 을 1 Hz 로 샘플링. |
| **CI quarantine** | 환경 의존성 큰 테스트 (예: dev 머신 외 fail) 를 격리하는 비-게이팅 목록. `docs/CI_QUARANTINE.md`. |

### §2.3 아키텍처 한 페이지 요약

```
OSC (UDP) ────► OSCBackend (recv ring + drain) ────────►  audio_io thread
                                                          (block-callback)
                                                          │
                                          ┌───────────────┴───────────────┐
                                          │ SpatialEngine.audioBlock()    │
                                          │   ├─ object update            │
                                          │   ├─ AmbisonicRenderer (HOA)  │
                                          │   ├─ Decoder (VBAP/DBAP/HOA)  │
                                          │   └─ BinauralMonitor          │
                                          │        ├─ B1: per-object HRTF │
                                          │        └─ B2: AmbiVS chain    │
                                          └───────┬───────────────────┬───┘
                                                  │                   │
                                          speakers bus (1)      binaural bus (2)
                                                  ▲                   ▲
                                                  │                   │
                                          ┌───────┴───────────────────┴───┐
                                          │      VST3 process()          │
                                          │   2-bus arrangement;          │
                                          │   per-bus aware downmix       │
                                          └───────────────────────────────┘

[Heartbeat IO thread 1Hz]    OSCBackend ────► /sys/binaural_status (i failures)
                             OSCBackend ────► /sys/binaural_warning (s code)
                             OSCBackend ────► /sys/state (s fallback_mode=…)

WebGUI (React + WS) ────► OSC bridge ────► OSCBackend (recv)
                          (Playwright e2e + soak)
```

- **두 출력 갈래**: 스피커 (HOA 디코더, VBAP, DBAP) + 바이노럴 (B1/B2 모드 선택, SOFA 미로드 시 강제 뮤트).
- **2-bus VST3**: 호스트가 한 트랙으로 두 출력을 라우팅. 1번 버스 = 스피커, 2번 버스 = 바이노럴.
- **3 OSC 채널 outbound**: `/sys/binaural_status` (1 Hz 헬스), `/sys/binaural_warning` (이벤트 코드), `/sys/state` (fallback_mode 등). v0.5.1 Q1 에서 신설.
- **soak harness**: standalone 48h + VST3 콘솔 flood + WebGUI 24h.

### §2.4 핵심 정책 결정 highlights (이번 주 신규)

| 영역 | 결정 |
| --- | --- |
| **VST3 layout** | 2-bus arrangement (스피커 + 바이노럴) 고정; bus 1 채널 수는 layout YAML 의 max(channel_id)+1, bus 2 는 항상 stereo. |
| **state v4 호환성** | v3 의 raw TLV 와 byte-equal 결과를 보장 (기존 DAW 프로젝트가 깨지지 않도록 merge gate 통과 후 ship). requested_mode 등 v0.5 신규 필드는 별도 section. |
| **B1/B2 모드 선택** | host 가 `requested_mode` 를 지정; effective mode 는 SOFA 가용성 / probe / 런타임 디모트로 클램프. UI 에는 effective 만 노출. |
| **HRTF 룩업** | KdTree3D O(log N) — 트리는 prepareForReload 에서 슬롯 스왑 시점에 사전 빌드, audio thread 는 룩업만. |
| **SOFA 미로드** | 바이노럴 출력 강제 뮤트 + `/sys/binaural_warning ,s "no_sofa_loaded"` 1회 통보. "비슷한 소리" 흘려보내 사용자 속이지 않음. |
| **OSC outbound** | RT 송신 절대 금지 (v0.6 #4 hard-wall). audio thread 는 atomic 플래그만 set; IO 스레드가 1 Hz drain. |
| **CI 격리** | 환경 의존성 큰 테스트는 `docs/CI_QUARANTINE.md` 에 명시 + 비-게이팅. 본 사이클에서 v0.6 #4 관련 항목 정리. |

---

## §3 이번 주 핵심 진척 요약

### §3.1 한 문장 헤드라인

이번 주(2026-05-11 ~ 2026-05-18)는 **v0.3.1 (5/15) → v0.4 (5/15) → v0.5 (5/15) → v0.5.1 (5/17)** 4 릴리스를 출시하며, 5 가지 큰 성과를 달성했다. (1) **WebGUI 완성** — 60fps 드래그 + 모바일 비율 + 24h soak + Playwright e2e. (2) **VST3 2-버스 정비 + state v4** — DAW 한 트랙에서 스피커 + 바이노럴 동시 라우팅, state v3 byte-equal 호환. (3) **상용 수준 바이노럴 (B1 per-object + B2 AmbiVS)** — KdTree3D O(log N), 더블 버퍼 슬롯 스왑 2-블록 크로스페이드, 64 객체까지 RT 안전. (4) **출시 직전 핫픽스 v0.5.1** — OSC outbound 채널 신설 + 모드 전환 크로스페이드 + SOFA 미로드 강제 뮤트 + 테스트 인프라 보강. (5) **v0.6 안정성 보강 시작** — audio thread OSC 송신 완전 분리 (#4) + 런타임 누적 언더런 자동 디모트 (#5) + sendReply 통합 (#8) + ring slot release-store 강화 (#9). 본 사이클이 *"엔지니어가 데모하는 도구"* → *"음악 제작자가 DAW에서 일상적으로 돌리는 플러그인"* 경계선을 넘은 주.

### §3.2 릴리스별 진행 (timeline)

#### v0.3.1 (2026-05-15) — 채널 매핑 정확성 핫픽스

**문제**: YAML 스피커 layout 에서 채널 번호 → 출력 인덱스 매핑이 부정확. 예: "5.1 layout 에서 LFE 만 음량 줄이기" OSC 명령이 잘못된 채널에 적용. 중복 채널 번호도 조용히 통과.

**수정**:
- `SpeakerLayout::channel_to_idx_` 룩업 테이블 도입 + 중복 거부.
- `OSCBackend` 의 per-channel 명령 (output gain / limit / noise type / noise gain) 을 모두 channel_to_idx_ 경유로 라우팅.
- 렌더러는 channel state 가 없어 영향 없음 (audit confirm 커밋).

**Commits**: `c53667a` (geometry fix), `cb2862a` (renderer audit), `2e51661` (OSC routing), `cfdd987` (release tag).

#### v0.4 (2026-05-15) — VST3 2-버스 + state v4 + .speh 배선

**Item 1: 2 출력 버스 분리**
- 1번 버스 = 스피커 (channel count = layout YAML 의 max(channel_id)+1).
- 2번 버스 = 바이노럴 (항상 stereo).
- DAW 가 한 트랙으로 두 출력을 동시에 받아 라우팅 가능. 기존 placeholder downmix (-6 dB) 는 v0.5 의 B1/B2 도입 시점에 실 컨볼루션으로 치환.

**Item 2: state v4 sectioned TLV**
- 기존 v3 raw TLV → v4 sectioned (binaural / layout / objects 등).
- v3 ↔ v4 byte-equal 호환 gate 통과 (기존 DAW 프로젝트 안 깨짐).

**Item 3: layout YAML 경로 + .speh 활성화 flag 배선**
- VST3 파라미터로 layout YAML 경로 + binaural enable bit 주입 가능.
- ralplan v2 consensus plans 사후 정리 커밋.

**Commits**: `1301a51` (state v4), `c7426d3` (2-bus), `a6f6a6f` (ralplan persist), `2221a7c` (P2 layout YAML), `e16701a` (P3 .speh + RT-no-alloc infra), `3a7e9f6` (release tag).

#### v0.5 (2026-05-15) — 상용 수준 바이노럴 디코더

**Item 1: B1 — per-object HRTF 합산**
- 각 음원의 정확한 방향 HRIR 를 실시간 검색해 합산.
- bus-level limiter 로 클립 방지.
- 64 객체까지 RT 안전 검증 (`binaural_b1_64_objects_rt_safe` ctest).

**Item 2: B2 — 24-pt t-design AmbiVS chain**
- 3차 HOA → 24-pt t-design 가상 스피커 분배 → 24-쌍 HRTF 컨볼루션.
- CPU 부담이 크면 자동으로 B1 로 폴백 (probe-clamped). 24-pt VS == B1 (order=3) 등가성 ctest 로 검증.
- B2 의 ownership lifecycle / layout change 시 VS 재초기화 없음 / throughput probe fallback 모두 ctest.

**Item 3: KdTree3D — HRTF 룩업 O(N) → O(log N)**
- 삼각함수 무차별 비교 → KdTree3D 트리 탐색.
- 트리는 SOFA 로드 시점에 사전 빌드 (audio thread 는 룩업만).
- 8 canonical 방향 회귀 ctest (`kdtree3d_8_canonical_directions`).

**Item 4: 더블 버퍼 슬롯 스왑 + 2-블록 크로스페이드**
- SOFA 파일 교체 시 RT 스레드를 멈추지 않고 부드러운 IR 전환.
- "preempt with current gain" 패턴 — 진행 중 페이드가 있어도 새 페이드가 현재 게인에서 시작.
- 더블 버퍼 swap → no alloc, no lock.

**Item 5 (P4.1 hotfix): 모드 race + enable-gate + probe accuracy + A6 wiring**

**Item 6 (P5): state v4 binaural section 에 requested_mode 영속화**.

**Item 7 (P6): release validation + WebGUI Playwright 0-based seed fix**.

**Commits**: `55ba305` (OlaConvolver::loadInto), `470aecc` (KdTree3D), `cd5059c` (slot swap + xfade), `3eb55de` (B1), `3d266f4` (release tag v0.5), `3e08964` (P4 B2 AmbiVS), `7cdcee8` (P4.1 hotfix), `05d05ec` (P5 state v4 binaural), `f734796` (P6 release validation).

#### v0.5.1 (2026-05-17) — 출시 직전 핫픽스 4종

DAW 통합 담당자에게 넘기기 직전 발견된 4 갭을 메운 릴리스.

**Q1: OSC 송신 채널 신설**
- 엔진이 "B2 가 CPU 한계로 클램프됨", "SOFA 가 없어서 폴백 중" 같은 상태를 호스트에 적극 통보.
- `/sys/binaural_status ,i` (1 Hz failures count).
- `/sys/binaural_warning ,s` (이벤트 코드: `xfade_truncated_cpu`, `no_sofa_loaded`).
- `/sys/state ,s` (fallback_mode 스냅샷).
- 그동안은 엔진이 알고 있어도 외부로 알릴 수단이 없었음.

**Q2: B1 ↔ B2 모드 전환 크로스페이드**
- 사용자가 모드를 바꿀 때 발생하던 클릭 잡음 제거.
- audio thread 에서 2 블록 선형 램프 (xfade 헬퍼 재사용).
- probe-clamped 분기 / disable-reenable 분기 모두 ctest.

**Q3: SOFA 미로딩 시 바이노럴 출력 강제 뮤트**
- HRTF 가 없는 상태에서 "비슷한 소리" 흘려보내 사용자 속이는 동작 차단.
- `BinauralMonitor` 가 강제 뮤트 + `/sys/binaural_warning ,s "no_sofa_loaded"` 1회 통보.

**Q4: 테스트 인프라 보강**
- Steinberg 정적 소멸자에서 ASan 이 죽던 문제 해결.
- soak 하네스 포트 충돌 플레이크 해결 (port reuse 패턴 정비).

**Commits**: `aeb011c` (v0.5.1 release tag).

#### v0.6 안정성 보강 (2026-05-17 ~ 18, 본 커밋에서 land)

DAW 에서 장시간 라이브 사용 시 audio thread RT 안전성을 한 단계 더 끌어올리는 4 항목. v0.5.1 plan §Q5 에서 "v0.6 로 deferred" 로 명시했던 항목들.

**Item #4: Audio thread 에서 OSC 송신 완전 분리**
- `no_sofa_loaded` / `fallback_mode` 같은 1회성 통보를 audio callback 이 아니라 1 Hz heartbeat IO 스레드가 drain 하도록 이전.
- audio callback 은 이제 `sendReply` 호출 자체가 없음 (내부적으로 `condition_variable::notify_one` 을 호출하므로 strict RT-safe 가 아니었음).
- heartbeat 는 "drain-first-then-wait" 패턴 — 첫 tick 에서 latch 를 즉시 emit 하므로 200 ms latency budget 내.
- 호스트가 peer 핸드셰이크 완료 전이면 다음 1 Hz tick 에서 자동 재시도.

**Item #5: 런타임 누적 언더런 자동 디모트**
- B2 처리의 wall-clock 시간이 블록 deadline 의 **90% 를 8 블록 연속** 으로 넘으면 자동 B1 로 영구 강등 (스티키) + 호스트에 1 회 통보 (`/sys/binaural_warning ,s "ambivs_demoted_runtime"`).
- `std::chrono::steady_clock::now()` 는 modern Linux vDSO (~30 ns, no syscall, no alloc) 로 RT 안전.
- 스티키 결정 → 일시 spike 가 모드를 flap 하지 않음. 카운터는 prepareToPlay 에서만 리셋.
- 새 ctest `b2_runtime_underrun_auto_demote` 추가.

**Item #8: sendReply 3 overload → sendReplyImpl 통합**
- 3 개의 거의 동일한 sendReply 본체 (~70 LOC) 를 단일 `sendReplyImpl(have_f, f, have_i, i)` 로 통합.
- drift risk 제거 + 향후 outbound 채널 추가 시 한 곳만 손대면 됨.

**Item #9: Outbound ring slot ready clear 의 release-store 강화**
- 약-순서 하드웨어 (ARM/ppc) 에서 wrap-producer 의 `ready=true` 게시가 consumer 의 stale `ready=false` 에 덮이는 corner case 차단.
- `slot.ready.store(false, std::memory_order_release)` 로 격상.

**검증 상태 (커밋 직전 재확인)**:
- ctest: **85/85 PASS** (시작 시 53 → v0.5.1 81 → v0.6 85, +4 신규).
- pytest: **47/47 PASS**.
- 신규 ctest: `b2_runtime_underrun_auto_demote`, `b1_b2_mode_transition_smooth`, `b1_b2_mode_transition_probe_clamped`, `b1_b2_mode_transition_disable_reenable`.

### §3.3 가장 중요한 5가지 진척 (highlight)

#### 1) WebGUI 완성 + 24h soak (5/11 ~ 5/12)

이번 주 가장 큰 **사용자 임팩트** 중 하나. 브라우저에서 객체 (소리) 를 마우스로 끌면 엔진이 즉시 위치를 갱신하는 원격 조작 화면이 완성됐다. 모바일 화면 비율, 60 fps 부드러움, 동시 접속 2 클라이언트, 장면 저장/불러오기까지 모두 Playwright e2e 로 검증.

부가로 **48 시간 무중단 운영 테스트 (soak) 하네스** 가 추가되어 장시간 켜놔도 메모리 누수·연결 끊김 없는지 자동 감시한다. **왕복 지연 (RTT) 3 중 측정 경로** (모의 → 실제 UDP → 종단간 와이어 SHA-256 일치 검증) 로 "브라우저가 보낸 그대로 엔진이 받았는가" 를 바이트 단위로 보증.

한국어 매뉴얼 Ch.6 (WebGUI 사용법) 도 작성됐다.

#### 2) macOS / 원격 사용자 온보딩 (5/14)

Apple Silicon (arm64) 빌드 실패 수정 — x86 전용 SSE 명령을 가드해 M1/M2 Mac 에서도 바로 빌드 가능.

**macOS 설치 가이드 + `requirements.txt` 정비** — tmux + Claude Code + 빌드/실행 스텝을 손에 잡히게 정리. 원격에서도 1 시간 안에 환경 구축 가능한 수준.

#### 3) VST3 2-버스 + state v4 (v0.4, 5/15)

이번 주 가장 큰 **DAW 통합 진전**. 1 번 버스 = 스피커, 2 번 버스 = 바이노럴 (헤드폰). DAW 쪽에서 한 트랙으로 두 출력을 동시에 받아 라우팅 가능.

state v4 sectioned TLV 도입으로 DAW 프로젝트 파일에 플러그인 설정이 더 견고하게 저장된다. 과거 v3 세션과 **byte-equal 호환성** 유지 — 기존 프로젝트가 깨지지 않는다. layout YAML 경로 / `.speh` 바이노럴 활성화 플래그를 플러그인 파라미터로 주입할 수 있게 배선.

#### 4) 상용 수준 바이노럴 디코더 (v0.5, 5/15)

이번 주 가장 큰 **기술 진전**.

헤드폰으로 듣는 3D 음향 (HRTF 컨볼루션) 을 *"데모 수준"* → *"상용 수준"* 으로 격상.

- **B1 (per-object HRTF)**: 각 음원의 정확한 방향에 대응하는 HRIR 를 실시간 검색해 합산. 64 객체까지 RT 안전.
- **B2 (24-pt t-design AmbiVS)**: 고차 HOA (3차) 에서 24 개의 가상 스피커 방향으로 분배한 뒤 HRTF 로 합치는 고품질 경로. CPU 부담이 큰 환경에서는 자동 B1 폴백.
- **KdTree3D 최근접 탐색**: O(N) 삼각함수 무차별 비교 → O(log N) 트리. HRTF 룩업 가속.
- **이중 버퍼 슬롯 스왑 + 2 블록 크로스페이드**: SOFA 교체 시 RT 스레드 멈춤 없이 부드러운 IR 전환.

#### 5) v0.5.1 핫픽스 + v0.6 안정성 보강 (5/17 ~ 18)

**v0.5.1** 은 출시 직전 발견된 4 갭 (OSC outbound 채널 / B1↔B2 크로스페이드 / SOFA 미로드 강제 뮤트 / 테스트 인프라) 을 메운 핫픽스 릴리스.

**v0.6** 은 그 다음 단계로 audio thread RT 안전성을 한 단계 더 끌어올리는 4 항목 (#4 OSC IO-thread 분리 / #5 런타임 자동 디모트 / #8 sendReply 통합 / #9 ring slot release-store 강화). v0.5.1 plan §Q5 에 "v0.6 로 deferred" 로 명시했던 항목들이 본 커밋에서 land. 이 작업은 DAW 안에서 장시간 라이브 사용 시 (수 시간 단위) 우발적 audio dropout 및 weak-memory-order 아키텍처 (Apple Silicon, ARM Linux) 에서의 corner case 를 사전 차단한다.

---

## §4 현재 시스템 상태 (2026-05-18 기준)

### §4.1 코드 메트릭

| 항목 | 수치 | 비고 |
| --- | ---: | --- |
| Core library (`core/src/**/*.{h,cpp}`) | ~12.5k lines | SpatialEngine + AmbisonicRenderer + Decoders + BinauralMonitor + OSCBackend. |
| VST3 plugin (`vst3/`) | ~3.5k lines | SpatialEngineProcessor + Controller + Factory + tests. |
| WebGUI (`webgui/`) | ~4k lines | React + WS + OSC bridge + Playwright e2e. |
| Tests (C++) | **85 ctest** | core_unit + vst3 + soak. v0.6 에서 +4 신규. |
| Tests (Python) | **47 pytest** | accuracy + latency + soak + e2e. v0.5.1 에서 osc_warning_channel +1. |
| Releases this week | **v0.3.1, v0.4, v0.5, v0.5.1** | 4 개. |
| Plans (`.omc/plans/`) | 36 files | 본 사이클에서 `spatial-engine-v0.5.1-binaural-hotfix.md` + (본 커밋) `spatial-engine-v0.6-stability.md` 신규. |
| 한국어 매뉴얼 (`docs/manual_kr/`) | 6 chapters | Ch.6 WebGUI 본 사이클에서 추가. |

### §4.2 핵심 invariant (회귀 가드)

- **state v3 ↔ v4 byte-equal** — 기존 DAW 프로젝트 절대 안 깨짐 (v0.4 merge gate).
- **B2 == B1 at order=3** — 24-pt t-design AmbiVS 가 B1 per-object 와 등가 (`b2_ambivs_equivalent_to_b1_at_order3` ctest).
- **64 객체 B1 RT-safe** — 64 객체 동시 처리 시에도 audio block 마감 미달 (`binaural_b1_64_objects_rt_safe`).
- **SOFA 미로드 → 강제 뮤트** — v0.5.1 Q3. `test_writebinaural_no_sofa_muted` ctest.
- **Audio thread no sendReply** — v0.6 #4. RT prober (`rt_alloc_probe`) 통과.
- **HRTF KdTree3D 8 canonical** — 8 방향 회귀 (`kdtree3d_8_canonical_directions`).
- **소멸자 ASan 통과** — v0.5.1 Q4. soak_vst3_console_flood 변경.

### §4.3 CI / 격리 현황

- **GHA vst3.yml**: `vst3-build-and-host-fixture` + `off-byte-identical` 두 job (v0.2 부터 유지).
- **OFF baseline**: GHA-canonical bytes 로 pin (ubuntu-24.04 runner).
- **격리 항목**: `docs/CI_QUARANTINE.md` 에서 본 사이클 정비.

---

## §5 미해결 이슈 / 다음 단계

### §5.1 단기 우선순위 (v0.6.x ~ v0.7)

1. **외부 베타 테스터 핸즈온** — Reaper / Bitwig 사용자가 실제 DAW 안에서 v0.5.1 + v0.6 를 돌려보고 피드백 수집. 본 사이클 종료 후 최우선.
2. **v0.5.1 plan §Q5 잔여 항목** — `.omc/plans/spatial-engine-v0.5.1-binaural-hotfix.md` 참고. v0.6 에서 #4/#5/#8/#9 종결. 잔여 항목은 v0.7 deferred.
3. **WebGUI 사용자 매뉴얼 한국어** — Ch.7+ (장면 저장 / OSC 외부 제어 / 성능 튜닝).
4. **B2 quality probe accuracy 보강** — probe 추정치 vs 실측 runtime 의 보정 (v0.6 #5 의 데이터 기반).
5. **Apple Silicon 빌드 회귀 게이트** — v0.5 의 SSE 가드가 깨지지 않도록 CI matrix 에 arm64 추가 검토.

### §5.2 중장기 로드맵 (v0.7+)

- **MUSHRA 평가 세션** — B1 vs B2 vs reference 의 청각 평가. 외부 패널 모집 필요.
- **Decoder 추가** — v0.4 에서 도입한 5 enum (Basic, MaxRE, AllRAD, EPAD, InPhase) 외 새로운 디코더 (예: AllRAD2, ESLD) 의 도입 여부 검토.
- **Stage 2 IPC 스키마 flip** — 외부 사용자 캡처 데이터 ≥ 3 개 누적 시 `__schema_version__` flip.
- **Lab 운영용 standalone GUI 강화** — PySide6 쪽 일시 정지 (v0.4 부터 VST3 우선).

---

## §6 작업 방식 (메타)

### §6.1 OMC 4 단계 파이프라인 + ralplan

본 사이클의 4 릴리스 (v0.3.1 / v0.4 / v0.5 / v0.5.1) 모두 다음 파이프라인을 통과:

| 단계 | 산출물 |
| --- | --- |
| **ralplan (planner)** | `.omc/plans/spatial-engine-vX.Y.md` — feature scope + acceptance gates. |
| **ralplan (architect)** | `.omc/plans/architect-r-vX.Y-*.md` — 기술적 타당성 + risk 검토. |
| **ralplan (critic)** | `.omc/plans/critic-r-vX.Y-*.md` — gap 검토 + APPROVE / REVISE. |
| **autopilot (executor)** | 본 코드 + ctest + pytest. |
| **검증** | ctest 100% PASS + pytest 100% PASS + GHA green. |

v0.6 는 본 커밋에서 동일 파이프라인으로 land (post-hoc 플랜 doc 작성됨).

### §6.2 RT-safety hard-wall (v0.6 #4)

audio thread 에서는:
- 절대 `malloc` / `free` / `condition_variable::notify_*` / `mutex::lock` 호출 금지.
- 모든 외부 통신 (OSC outbound) 은 atomic 플래그로 latch → IO 스레드가 drain.
- 새 코드 추가 시 `rt_alloc_probe.hpp` (LD_PRELOAD-free 강심볼 probe) 로 검증.

이 원칙이 v0.5.1 Q4 의 ASan 회귀 + v0.6 #4 의 IO-thread 분리 두 사이클을 driving 했다.

### §6.3 state v3 ↔ v4 byte-equal 정책

기존 DAW 프로젝트가 깨지면 사용자가 즉시 떠난다. 따라서:
- v3 → v4 변환은 **read-only** (load 시점에 sectioned 로 normalize).
- save 는 v4 만 출력 (v3 wire 는 generate 안 함).
- v3 wire 의 raw TLV → v4 sectioned 변환 결과가 다시 v3 reader 에 의해 동일하게 read 되어야 함 (byte-equal merge gate).
- 본 ctest: `test_vst3_state_persist`.

### §6.4 Honesty-first SOFA / B2 disclosure (v0.5.1 Q3)

HRTF / SOFA 가 없는 상태에서 "비슷한 소리" 를 흘려보내 사용자를 속이는 것은 금지. 무성으로 끊고 OSC 로 명확히 통보. 이는 roomestim 의 D27 "Honesty-first hard-wall" 과 같은 정책 — "증거 부족하면 silently drop / placeholder 대신 명시적 honesty-leak".

---

## §7 결론 + 한 줄 메시지

> 이번 주는 **WebGUI 완성** → **VST3 2 버스 + state v4** → **상용 수준 바이노럴 (B1/B2 + KdTree3D + 슬롯 스왑)** → **출시 직전 핫픽스 v0.5.1 (OSC outbound + 크로스페이드 + SOFA 뮤트 + 테스트 인프라)** → **v0.6 안정성 보강 (audio-thread OSC 분리 + 런타임 자동 디모트 + sendReply 통합 + ring release-store 강화)** 순으로 진행했다. 시스템 상태는 **DAW 안에서 일상 사용 가능한 플러그인 경계선 진입** 사례다. ctest 85/85, pytest 47/47 모두 PASS, 4 릴리스 모두 OMC ralplan + autopilot 파이프라인 통과.

**핵심 메트릭 한 줄**: 4 releases (v0.3.1 / v0.4 / v0.5 / v0.5.1) + v0.6 stability bundle / 85 ctest + 47 pytest 100% PASS / WebGUI 60 fps + 24 h soak + Playwright e2e / VST3 2-bus + state v4 byte-equal / B1 64-obj RT-safe + B2 AmbiVS 24-pt / 3 OSC outbound 채널 신설 (`/sys/binaural_status` / `/sys/binaural_warning` / `/sys/state`) / SOFA 미로드 강제 뮤트 + honesty-first.

**한 줄 메시지**: "이번 주 가장 큰 자산은 v0.5 의 상용 바이노럴 디코더 자체가 아니라, 그 도입 과정에 만들어진 4 단계 safety net — KdTree3D O(log N) + 슬롯 스왑 RT 안전 + SOFA 미로드 강제 뮤트 + audio-thread sendReply 완전 추방 — 이다. 다음 단계는 외부 베타 테스터의 DAW 핸즈온 피드백."
