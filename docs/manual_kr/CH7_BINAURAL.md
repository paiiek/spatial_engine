# Chapter 7 — 바이노럴 모니터링 (Binaural Monitoring)

> **적용 버전**: v0.5.0 부터 (B1 / B2 디코더 + KdTree3D 룩업 + RT 안전 슬롯 스왑).
> v0.5.1 에서 OSC 통보 채널 / 모드 전환 크로스페이드 / SOFA 미로드 강제 뮤트 추가.
> v0.6.0 에서 audio thread OSC 송신 분리 + 런타임 sticky 자동 디모트 추가.

이 챕터는 헤드폰으로 공간 음향을 미리듣기 위한 **바이노럴 모니터링** 기능의
사용법을 다룹니다. v0.4 까지의 `-6 dB` placeholder 다운믹스가 아니라, **실제
HRTF (머리전달함수) 컨볼루션 기반 상용 수준 바이노럴 렌더링**을 다룹니다.

---

## 7.1 바이노럴이 무엇인가, 언제 켤까

### 7.1.1 정의

**바이노럴 (Binaural) 음향**은 마이크가 사람 머리 모양 더미 (KEMAR / HUTUBS
등) 양 귀 위치에 놓였다고 가정해, 각 음원이 머리·귀·어깨에 의해 어떻게 회절·
산란되는지 미리 측정한 **HRTF (Head-Related Transfer Function)** 를 음원
신호에 컨볼루션한 결과입니다. 그 결과를 헤드폰으로 들으면 "스피커 어레이 앞에
앉아있는 것 같은" 공간감을 헤드폰만으로 얻을 수 있습니다.

spatial_engine 에서 바이노럴은 **VST3 Bus 1 (stereo)** 또는 standalone
`OutputBinaural` 백엔드로 출력됩니다. Bus 0 (스피커) 와 동시에 출력 가능 —
DAW 한 트랙에서 두 출력을 동시에 라우팅합니다 (Ch.5 참고).

### 7.1.2 언제 켤까

| 상황 | 바이노럴 ON 이 적절? |
| --- | --- |
| 스피커 어레이가 없는 작업실에서 믹스 미리듣기 | **예** — 메인 용도. |
| 라이브 공연 모니터링 (실 스피커가 있음) | 보조 모니터로만. 메인은 실 스피커. |
| 외부 음악 감상 (Spotify 등) | 아니오 — spatial_engine 의 객체 기반 신호가 아님. |
| 헤드폰 없이 데모/시연 | 아니오 — 스피커 어레이 또는 노트북 내장 스피커. |

### 7.1.3 한계

- HRTF 는 측정한 더미 (KEMAR / HUTUBS) 의 머리 형태 평균. **개인 head-related
  특성은 반영되지 않음** — 음원이 머리 위 ↔ 머리 뒤로 confuse 되는 "cone of
  confusion" 현상이 발생할 수 있습니다.
- 헤드폰 출력에 직접 컨볼루션이므로 **헤드폰 자체의 주파수 응답** 도 함께
  들립니다. 가능하면 평탄한 응답의 모니터링 헤드폰 (HD600, K712 등) 사용 권장.
- 머리 회전 (head-tracking) 은 v0.5/v0.6 에는 미지원. v0.7 candidate.

---

## 7.2 빠른 시작 (Quick Start)

### 7.2.1 5분 체크리스트

1. **SOFA 파일 준비** — `~/spatial_engine_data/hrtf/kemar.sofa` 같은 경로에
   1 개 SOFA 파일이 있어야 합니다. (없으면 §7.6 SOFA 획득 가이드 참고.)
2. **`.speh` 세션 파일 작성** — `synthetic_min.speh` 와 같은 헤더 파일에
   SOFA 경로 + binaural enable bit 를 명시합니다. 빠른 템플릿:
   ```
   layout_path: layouts/lab_8ch.yaml
   sofa_path:   /home/<user>/spatial_engine_data/hrtf/kemar.sofa
   binaural:    enable=1 mode=0   # 0=B1, 1=B2
   ```
3. **VST3 호스트 또는 standalone 실행**:
   - **VST3**: DAW 에서 spatial_engine_vst3 인스턴스를 트랙에 로드. VST3
     파라미터 패널에서 layout YAML 경로 + `.speh` 파일 경로 + binaural
     enable 체크. Bus 1 (Binaural) 을 헤드폰 트랙에 라우팅.
   - **Standalone**: `spatial_engine_core --layout ... --speh ... --binaural`
     로 띄우고 OS 의 오디오 출력 (Pulse / JACK / CoreAudio) 을 헤드폰으로.
4. **헤드폰 끼고 객체 움직이기** — WebGUI (Ch.6) 또는 OSC `/adm/obj/0/azim`
   같은 명령으로 객체를 좌우/앞뒤로 움직였을 때 헤드폰 안에서 위치가 바뀌어
   들려야 합니다.

### 7.2.2 5분 안에 안 되면

1. **헤드폰에서 무음**:
   - `.speh` 의 `sofa_path:` 가 실제 존재하는지 확인 (`ls` 로).
   - SOFA 가 로드 안 됐으면 v0.5.1 부터 **강제 뮤트** 가 켜집니다. OSC `/sys/binaural_warning ,s "no_sofa_loaded"` 가 1 회 들어왔는지 호스트에서 확인.
   - 자세히는 §7.4 OSC 통보 채널 + §7.5 Troubleshooting.
2. **헤드폰에서 placeholder 같은 다운믹스 소리** (v0.4 흔적):
   - `.speh` 의 `binaural:` 줄이 `enable=0` 또는 누락. v0.5+ 에서도 binaural
     이 꺼져있으면 v0.4 의 `-6 dB` 다운믹스가 fallback 으로 흐릅니다.
3. **DAW 가 Bus 1 을 인식 못함**:
   - 호스트가 multi-bus VST3 를 지원하는지 확인. Reaper 7.x / Bitwig 5.x 는
     기본 지원. 단일 버스 호스트는 Bus 0 만 보입니다.

---

## 7.3 B1 / B2 모드 — 무엇을 고를까

### 7.3.1 두 경로의 차이

| 축 | **B1** (Direct, per-object HRTF) | **B2** (AmbiVS chain) |
| --- | --- | --- |
| 알고리즘 | 각 객체의 정확한 방향 HRIR 를 KdTree3D 로 검색 → 직접 컨볼루션 → 합산 | 3차 HOA 디코드 → 24-pt t-design 가상 스피커 → 각 VS 의 HRTF 로 컨볼루션 → 합산 |
| 음장 일관성 | 객체 위치 잘 표현. 인접 객체 간 위상 간섭은 자연스럽게 나타남. | t-design quadrature 의 균등 분포 → 어떤 방향에서나 일정한 품질. 인접 객체간 회절 정밀도는 B1 보다 낮음. |
| CPU 비용 | 객체 수에 선형 (객체 64 × OLA 컨볼루션). | 고정 (24 × OLA 컨볼루션). 객체 수와 무관. |
| 객체 수가 적을 때 (≤ 24) | **유리** | 동등 또는 약간 무거움 |
| 객체 수가 많을 때 (≥ 32) | 무거워짐 | **유리** |
| HOA 콘텐츠 (3차 이상) | 객체로 풀어야 함 → 손실 | **직접 디코드** → 손실 없음 |
| RT 안정성 (느린 CPU) | 가장 단순 → 안정적 | probe / 런타임 디모트가 자동으로 B1 폴백 가능 |

### 7.3.2 선택 가이드라인

- **잘 모르겠으면 B1 (mode=0)**. v0.5.0 부터 시간이 검증된 단순한 경로.
- 객체 수가 32 개 이상이고 CPU 여유가 있으면 **B2 (mode=1)** 시도. 동등하거나
  더 부드러운 결과.
- **HOA 콘텐츠** (외부에서 3차 HOA wire 신호를 받아 디코드 모니터링) 일 때는
  **B2 권장**.
- 어떤 모드를 선택하든 **자동 폴백** 이 보호합니다:
  - **시작 시 CPU 프로브** (v0.5): `setActive(true)` 시점에 throughput 측정
    → 부족하면 B2 → B1 자동 폴백.
  - **런타임 sticky 자동 디모트** (v0.6 신규): B2 처리가 블록 마감의 90% 를
    8 블록 연속 초과 시 영구 B1 강등 (다음 `prepareToPlay()` 까지). OSC 로
    1 회 통보 (§7.4.2).

### 7.3.3 모드 전환

- VST3 파라미터 또는 OSC `/sys/binaural_mode ,i {0|1}` 으로 런타임 전환.
- v0.5.1 부터 **2 블록 선형 크로스페이드** 자동 적용 → 클릭 잡음 없이 부드럽게
  전환. CPU 한계로 ramp 가 1 블록으로 잘리면 OSC `/sys/binaural_warning ,s
  "xfade_truncated_cpu"` 가 1 회 통보됩니다.

### 7.3.4 상태 영속화 (v0.5 P5)

- **`requested_mode`** (사용자가 원한 모드) 는 state v4 의 binaural section
  에 저장됩니다 → DAW 프로젝트를 다시 열어도 사용자 의도가 보존됩니다.
- **`effective_mode`** (실제로 동작 중인 모드) 는 telemetry — 새로 열 때마다
  CPU probe 가 다시 평가합니다.
- 즉 *"B2 를 원했으나 이전 머신에서 B1 로 클램프됐다"* 는 상황은 **새 머신에서
  B2 로 복구** 시도됩니다.

---

## 7.4 OSC 통보 채널 — 엔진이 보내는 메시지 읽기

v0.5.1 부터 엔진은 호스트 (DAW / WebGUI / OSC 컨트롤러) 에 **3 가지 outbound
OSC 채널** 로 상태를 통보합니다. 모두 **IO 스레드** (audio thread 가 아님) 가
송신하므로 RT 안전합니다. v0.6 부터 audio thread 의 `sendReply` 호출이
완전히 제거되었습니다.

### 7.4.1 `/sys/binaural_status ,i <failures>` — 1 Hz 헬스

- **주기**: 1 초마다 1 회.
- **payload**: `int32` — 누적 `OlaConvolver::loadInto` 실패 카운트.
- **정상 정상 상태값**: `0`.
- **해석**:
  - 값이 0 에서 멈춰있으면 = 정상. SOFA 슬롯 스왑이 모두 RT-no-alloc 경로로
    성공.
  - 값이 단조 증가하면 = **연성 알람**. 제어 스레드 reload 가 capacity 를
    초과해 슬롯 스왑이 실패한 적이 있음. SOFA 파일이 너무 큰 경우 (>
    `MAX_IR_LEN = 1024 floats per slot`) 가 가장 흔한 원인.
- **권장 호스트 동작**: 값이 처음 증가하는 시점에 DAW 로그/콘솔에 표시.

### 7.4.2 `/sys/binaural_warning ,s <code>` — 이벤트 통보

`,s` payload 의 코드 별 의미:

| 코드 | 의미 | 발생 시점 | v0.x |
| --- | --- | --- | --- |
| `no_sofa_loaded` | binaural enable=1 인데 SOFA 가 없어 강제 뮤트 중 | binaural 가 켜진 후 SOFA 가 한 번도 로드된 적 없음 | v0.5.1 |
| `xfade_truncated_cpu` | 모드 전환 / 슬롯 스왑 크로스페이드 ramp 가 CPU probe 의 클램프로 1 블록으로 잘림 | 모드 전환 시 매 회 발생 가능 | v0.5.1 |
| `ambivs_demoted_runtime` | B2 의 wall-clock 처리 시간이 deadline 의 90% 를 8 블록 연속 초과 → B1 으로 영구 강등 (sticky, 다음 `prepareToPlay()` 까지) | 런타임 한 번 fire 후 reset 전까지 침묵 | **v0.6.0** |
| `rt_timing_unavailable` | 이 머신에서 `std::chrono::steady_clock` 가 vDSO 가 아닌 syscall 로 떨어져 (avg/call ≥ 200ns) — runtime 자동 디모트 detector 가 silently 비활성화됨. B2 는 정상 동작하나 `ambivs_demoted_runtime` 가 영원히 fire 하지 않음. 보통 오래된 ARM Linux 커널 또는 일부 Intel macOS 에서 발생. | `prepareToPlay()` 시점의 probe 결과 slow 인 경우. 1 회 통보 후 다음 `prepareToPlay()` 까지 침묵. | **v0.6.1** |

- 모든 코드는 **edge-trigger** — 같은 코드가 1 회 통보 후 같은 latch 가 다시
  arm 되기 전까지 침묵합니다.
- 호스트는 사용자 가시 UI 에 코드별로 사람이 읽을 수 있는 한국어 문구를
  매핑해서 표시하는 것을 권장합니다 (예: `ambivs_demoted_runtime` →
  *"고품질 바이노럴 (B2) 가 이 머신의 CPU 한계를 초과 — 안정성을 위해 B1 로
  영구 강등됨. 프로젝트를 다시 열면 다시 시도합니다."*).

### 7.4.3 `/sys/state ,s "fallback_mode=..."` — prepareToPlay 스냅샷

- **주기**: `prepareToPlay()` 마다 1 회 (= DAW 프로젝트 열기, 샘플 레이트
  변경, 트랙 활성화 시점).
- **payload**:
  - `"fallback_mode=normal"` — SOFA 정상, binaural 정상 출력 중.
  - `"fallback_mode=muted"` — SOFA 미로드로 강제 뮤트 (= §7.4.2 의
    `no_sofa_loaded` 와 동일 원인).
- **호스트 활용**: 프로젝트 로드 시점에 상태 indicator 를 그리는 데 유용.

---

## 7.5 Troubleshooting

### 7.5.1 헤드폰에서 아무 소리가 안 나요

| 의심되는 원인 | 진단 / 해결 |
| --- | --- |
| SOFA 가 로드 안 됨 | `/sys/binaural_warning ,s "no_sofa_loaded"` 통보 확인. `.speh` 의 `sofa_path:` 가 실제 존재하는지 `ls` 로 확인. 권한 (`chmod +r`) 도 확인. |
| binaural enable bit = 0 | `.speh` 의 `binaural: enable=1 ...` 확인. VST3 파라미터에서 binaural enable 체크박스도 확인. |
| Bus 1 이 호스트 사이드에 라우팅 안 됨 | DAW 의 트랙 라우팅 패널에서 Bus 1 을 헤드폰 트랙으로 보냈는지 확인. |
| 객체 데이터가 안 들어옴 | WebGUI (Ch.6) 또는 OSC `/adm/obj/N/azim` 으로 객체를 움직여보세요. Bus 0 (스피커) 도 무음이면 객체 데이터 자체가 안 오는 것. |

### 7.5.2 헤드폰에서 dropout / click 잡음이 들려요

| 의심되는 원인 | 진단 / 해결 |
| --- | --- |
| 일시적 CPU 스파이크 (페이지 폴트, 디스크 IO) | 1-2 회는 정상. 반복되면 다른 무거운 플러그인 / 백그라운드 프로세스 의심. |
| B2 가 이 머신에 너무 무거움 | v0.6 의 sticky 자동 디모트가 fire 했는지 `/sys/binaural_warning ,s "ambivs_demoted_runtime"` 확인. 통보가 왔다면 사용자 의도와 무관하게 B1 로 강등된 상태. 다음 `prepareToPlay()` (프로젝트 reopen, sample rate 변경) 까지 sticky. |
| 모드 전환 직후의 click | v0.5.1 부터 2 블록 크로스페이드 자동 적용. 그래도 click 이 들리면 `xfade_truncated_cpu` 통보 확인 — CPU probe 가 ramp 를 1 블록으로 잘랐다는 뜻. |
| SOFA 슬롯 스왑 시점 | `/sys/binaural_status ,i` 값이 증가했는지 확인. 증가했으면 slot 의 IR length 가 1024 floats 를 초과했을 가능성. |

### 7.5.3 음원 위치가 머리 위 ↔ 머리 뒤로 confuse 돼요

- 정상입니다 — HRTF 의 cone-of-confusion 한계. 머리 회전 (head-tracking) 가
  미지원이라 frontal/dorsal 모호성을 헤드폰만으로 해결할 수 없습니다. v0.7
  candidate.
- 부분 우회: 음원에 작은 **조기 반사 (early reflection)** 신호를 더하면 모호성이
  줄어듭니다 (Ch.9 알고리즘 가이드의 ER 섹션 참고). spatial_engine 의 룸
  reverb (FDN) 가 그 역할을 할 수 있습니다.

### 7.5.4 B1 ↔ B2 자동 전환이 너무 빈번해요

- v0.5.0 시점에는 시작 probe 가 매 `prepareToPlay()` 마다 재평가 → 머신 부하
  변화에 따라 B2 ↔ B1 이 flap 할 수 있었습니다.
- v0.6 부터 런타임 디모트가 **sticky** 입니다 → 한 번 B1 로 떨어진 후 다음
  `prepareToPlay()` 까지 B2 로 자동 복귀하지 않습니다. 사용자가 명시적으로
  B2 로 다시 보내려면 프로젝트를 닫고 다시 열거나 `/sys/binaural_mode ,i 1`
  을 다시 보내야 합니다.

### 7.5.5 "디모트 된 상태로 프로젝트를 저장했는데, 다시 열면 B2 가 다시 동작해요"

**예상된 동작입니다.** v0.6 의 sticky 자동 디모트는 **`prepareToPlay()` lifetime
범위 내에서만 sticky** 입니다 — 프로젝트를 저장/종료/재오픈 하면 새 `prepareToPlay`
가 호출되고, 그 시점에 `BinauralMonitor::initialize()` 가 디모트 상태를 reset
합니다 (v0.6.1 D-M1 fix).

순서:

1. 디모트 발생 → `/sys/binaural_warning ,s "ambivs_demoted_runtime"` 통보 → 효과
   모드 B1 클램프.
2. 프로젝트 저장 → state v4 에 `requested_mode = AmbiVS (B2)` 가 저장됨 (사용자
   의도 보존). `effective_mode` byte 는 telemetry 라 reload 시 무시.
3. 프로젝트 재오픈 → `prepareToPlay` → `initialize()` → 디모트 atomics reset → B2 재시도.
4. CPU 환경이 동일하다면 → 다시 디모트 가능 (또 다른 `ambivs_demoted_runtime`
   통보). 환경이 개선됐다면 → B2 정상 동작.

**즉 v0.6 의 디모트는 사용자 의도를 "잊지" 않습니다.** 매번 새 세션마다 *"이번엔
B2 로 갈 수 있는지"* 평가합니다. 한 머신에서 디모트가 계속 발생한다면 그것이
실제 그 머신의 한계 신호 — B1 을 명시적으로 `requested_mode` 로 저장 (`/sys/binaural_mode ,i 0`) 하는 것이 안정적입니다.

### 7.5.6 macOS 에서 들리지 않아요

- v0.6 시점에는 macOS 에 **CoreAudio 백엔드가 미구현** 입니다 (`docs/SETUP_MACOS.md` §빌드 후 노트 참고). 빌드 자체는 통과해도 standalone 의 실시간 출력은 무음입니다.
- VST3 로 macOS DAW (Logic, Cubase) 에서 사용하는 경로는 호스트가 오디오 IO 를 담당하므로 들립니다 — 이 경로의 macOS 검증은 P2 작업으로 진행 예정 (`docs/weekly_progress_report_2026-05-18.md` §5.1 P2-1).

---

## 7.6 SOFA 획득 가이드

### 7.6.1 기본 권장: MIT KEMAR

- **라이선스**: 공개 (학술 사용 자유).
- **출처**: <http://sound.media.mit.edu/resources/KEMAR.html>
- **포맷**: spatial_engine 은 SOFA 1.0 의 `SimpleFreeFieldHRIR` convention 을
  요구합니다. KEMAR 의 원본은 wave-pair (.wav) 라 SOFA 로 변환된 mirror
  사용 권장 (예: pyfar 의 KEMAR distribution).
- **추천 사양**: 44.1/48 kHz, IR length ≤ 1024 samples (= 21.3 ms @ 48 kHz).
  더 긴 IR 도 로드되지만 슬롯 capacity 초과로 `/sys/binaural_status` 의
  실패 카운트가 증가할 수 있습니다.

### 7.6.2 고품질 옵션: HUTUBS (TU Berlin)

- **라이선스**: 비상업 학술 사용 가능 — 상업 사용 시 별도 확인 필요.
- **출처**: <https://depositonce.tu-berlin.de/handle/11303/11340>
- **포맷**: SOFA 1.1. 96 명의 측정 대상 (subject) 별 HRTF — 헤드 사이즈가
  사용자 본인과 가까운 subject 를 선택하면 cone-of-confusion 이 다소 줄어듭니다.

### 7.6.3 본인 측정 (in-situ)

- 본 매뉴얼 범위 밖. 본인 머리 측정 → SOFA 변환 → `.speh` 에 명시.
  pyfar / sofar 라이브러리가 변환 helper 를 제공합니다.

---

## 7.7 OSC 빠른 레퍼런스 (바이노럴 관련)

### Inbound (호스트 → 엔진)

| 주소 | payload | 의미 |
| --- | --- | --- |
| `/sys/load_layout` | `,s <path>` | 스피커 layout YAML 교체. |
| `/sys/binaural_sofa` | `,s <path>` | SOFA 파일 경로 설정 / 교체. |
| `/sys/binaural_enable` | `,i {0\|1}` | binaural 출력 enable bit. |
| `/sys/binaural_mode` | `,i {0\|1}` | 0=B1, 1=B2 (사용자 의도; effective 는 probe 가 클램프 가능). |

### Outbound (엔진 → 호스트)

§7.4 참고. 요약:

| 주소 | payload | 의미 |
| --- | --- | --- |
| `/sys/binaural_status` | `,i <failures>` | 1 Hz, 누적 실패 카운트 (정상 0). |
| `/sys/binaural_warning` | `,s "no_sofa_loaded" \| "xfade_truncated_cpu" \| "ambivs_demoted_runtime"` | edge-trigger 이벤트. |
| `/sys/state` | `,s "fallback_mode=normal\|muted"` | `prepareToPlay` 스냅샷. |

---

## 7.8 다음 단계 / 한계

- **머리 회전 (head-tracking) 미지원** — v0.7 candidate. `/sys/headtrack ,fff`
  주소만 예약돼 있고 receiver 가 미구현.
- **CoreAudio backend 미지원** — macOS standalone 무음 (`docs/SETUP_MACOS.md`).
- **개인화 HRTF 미지원** — KEMAR / HUTUBS subject 선택 외 본인 측정 hook
  부재. v1+ candidate.
- **MUSHRA / ABX 청취 평가** — 외부 패널 모집 후 진행. P3-1
  (`docs/weekly_progress_report_2026-05-18.md` §5.2).
- **HUTUBS 외 다른 SOFA 패밀리 (SADIE, ARI 등) 회귀 게이트** — KEMAR 단일
  검증 상태. v0.7+ candidate.

---

## 7.9 관련 문서

- **챕터 6** [WebGUI 사용법](CH6_WEBGUI.md) — 객체 위치 컨트롤 UI.
- **운용 매뉴얼 Ch.4** OSC 프로토콜 레퍼런스 — 전체 OSC 주소 목록.
- **운용 매뉴얼 Ch.5** VST3 + ADM-OSC 콘솔 직결 모드 — 2-bus 라우팅 상세.
- **CHANGELOG.md** v0.5.0 / v0.5.1 / v0.6.0 — 본 챕터가 다루는 기능의 lineage.
- **설치 매뉴얼 §SOFA** — SOFA 다운로드 + 디렉터리 배치.
- **`docs/release/v0.5.1/RELEASE_NOTES_EN.md`** — v0.5.1 Q1-Q4 hotfix 상세.
- **`docs/release/v0.6.0/RELEASE_NOTES_EN.md`** — v0.6 RT-safety hardening
  (#4 audio-thread OSC 분리, #5 런타임 sticky 자동 디모트 포함).
