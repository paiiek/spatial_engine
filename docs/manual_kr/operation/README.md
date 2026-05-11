# Spatial Engine 운용 매뉴얼

**버전:** v0.3.0  
**대상 독자:** 음향 엔지니어, 시스템 운용자, 라이브 믹서  
**최종 수정:** 2026-05-10

---

## 목차

1. [운용 개요](#chapter-1-운용-개요)
2. [WebGUI 사용법](#chapter-2-webgui-사용법)
3. [VST3 플러그인 사용법](#chapter-3-vst3-플러그인-사용법)
4. [OSC 프로토콜 레퍼런스](#chapter-4-osc-프로토콜-레퍼런스)
5. [VST3 플러그인 + ADM-OSC 콘솔 (직결 모드)](#chapter-5-vst3-플러그인--adm-osc-콘솔-직결-모드)
6. [시나리오 작성](#chapter-6-시나리오-작성)
7. [알고리즘별 가이드](#chapter-7-알고리즘별-가이드)
8. [멀티존 및 per-zone 리미터 운용](#chapter-8-멀티존-및-per-zone-리미터-운용)
9. [LTC Chase 동기](#chapter-9-ltc-chase-동기)
10. [다중 오브젝트 그룹 자동화](#chapter-10-다중-오브젝트-그룹-자동화)
11. [라이브 공연 권장 워크플로](#chapter-11-라이브-공연-권장-워크플로)
12. [룸 및 스피커 캘리브레이션](#chapter-12-룸-및-스피커-캘리브레이션)
13. [트러블슈팅](#chapter-13-트러블슈팅)
14. [모니터링 및 로그 분석](#chapter-14-모니터링-및-로그-분석)
15. [백업 및 복구](#chapter-15-백업-및-복구)
16. [부록](#chapter-16-부록)

---

## Chapter 1. 운용 개요

### 1.1 Spatial Engine이란

Spatial Engine은 객체 기반 오디오(Object-Based Audio, OBA) 렌더링 엔진이다. 전통적인 채널 기반 오디오와 달리, 각 음원을 독립적인 "오브젝트(object)"로 관리하고 3차원 공간 좌표에 배치한다. 렌더링 엔진은 스피커 배열의 물리적 위치를 기반으로 각 오브젝트의 위치를 실시간으로 스피커 게인 매트릭스로 변환한다.

#### 객체 기반 오디오 vs 채널 기반 오디오

| 구분 | 채널 기반 | 객체 기반 |
|------|-----------|-----------|
| 음원 표현 | 고정 채널 (L/C/R 등) | 3D 좌표 오브젝트 |
| 스피커 레이아웃 종속성 | 강함 (채널 수 고정) | 약함 (런타임 적응) |
| 실시간 위치 변경 | 불가 (팬팟 제한) | 가능 (OSC 명령) |
| 최대 오브젝트 수 | 채널 수 | 64 (v0 기준) |

### 1.2 시스템 아키텍처 개요

Spatial Engine은 두 개의 독립 프로세스로 구성된다.

```
[Process B — UI (Python/PySide6)]
          |  OSC/UDP port 9100 (명령)
          v
[Process A — Core (C++/JUCE)]
          |  OSC/UDP port 9101 (상태 브로드캐스트)
          v
[스피커 출력 — Dante PCIe / PipeWire-JACK]
```

- **Process A (Core):** 실시간 오디오 I/O, DSP 처리, 렌더링 알고리즘, OSC 서버
- **Process B (UI):** 오브젝트 배치 GUI, 시나리오 관리, OSC 클라이언트

두 프로세스는 메모리를 공유하지 않는다. 모든 상태는 OSC/UDP를 통해 교환된다.

### 1.3 렌더링 알고리즘 선택 가이드

Spatial Engine은 세 가지 공간 오디오 렌더링 알고리즘을 지원한다. 각 오브젝트에 독립적으로 할당할 수 있으며, 재생 중에도 실시간으로 전환할 수 있다.

#### VBAP (Vector Base Amplitude Panning)

**원리:** 오브젝트 위치 벡터를 가장 가까운 스피커 삼각형의 세 벡터로 분해하여 진폭 패닝을 계산한다.

**선택 기준:**

- 정밀한 음상 정위(localization)가 중요한 경우
- 스피커가 3개 이상의 비동일 평면 배열인 경우
- 솔로 악기, 보컬, 대사 등 명확한 방향 단서가 필요한 음원

**제약:**

- 스피커 사이 넓은 간격에서 음압 강하(hole-in-the-middle) 발생 가능
- 스피커 최소 3개 비동일 평면 배치 필수 (미충족 시 `layout_incompatible` 경고)

#### DBAP (Distance-Based Amplitude Panning)

**원리:** 오브젝트와 각 스피커 사이의 거리에 반비례하여 진폭을 분배한다. 볼록 다각형 등 비정형 스피커 배열에서도 동작한다.

**선택 기준:**

- 비정형 또는 불규칙 스피커 배열 환경
- 확산적(diffuse) 음장 표현이 목적인 경우
- 앰비언트, 효과음, 공간적 질감

**제약:**

- 스피커 4개 미만 시 경고 (동작은 가능)
- VBAP 대비 음상 집중도 낮음

#### WFS (Wave Field Synthesis)

**원리:** 스피커 배열을 선형/평면 파면 생성기로 사용하여 물리적으로 정확한 음장을 재현한다.

**선택 기준:**

- 선형 또는 평면 스피커 어레이 환경 (예: 대형 라인 어레이)
- "스위트 스팟" 없이 청중 전체에 일관된 음상 제공이 목적인 경우
- 높은 공간 해상도가 필요한 설치 음향

**제약:**

- 반드시 선형/평면 어레이와 알려진 스피커 간격(inter-speaker spacing) 필요
- 잘못된 어레이 구성 시 `layout_incompatible` 경고 후 오디오 시작 차단

#### HOA (Higher Order Ambisonics)

> **v0 상태:** 바이노럴 모니터(BinauralMonitor) 경로에 KEMAR SOFA 기반 HRTF 데이터를 사용하는 부분 구현 포함. 전체 HOA 렌더링 모드는 v1+ 예정.

#### 알고리즘 비교 요약

| 알고리즘 | 음상 집중도 | 스피커 요구사항 | 적합한 음원 | v0 지원 |
|----------|-------------|-----------------|-------------|---------|
| VBAP | 높음 | 3+ 비동일 평면 | 악기, 보컬, 대사 | 완전 지원 |
| DBAP | 중간 | 제약 없음 | 앰비언트, 효과음 | 완전 지원 |
| WFS | 높음 (전 청중) | 선형/평면 어레이 | 설치 음향 | 완전 지원 |
| HOA | — | — | 서라운드 전체 | 바이노럴만 |

### 1.4 좌표계 규약

Spatial Engine은 다음 좌표계를 사용한다.

| 파라미터 | 단위 | 범위 | 양의 방향 |
|----------|------|------|-----------|
| 방위각 (az) | 도 (°) | -180 ~ +180 | 오른쪽 (RIGHT=+az) |
| 고도각 (el) | 도 (°) | -90 ~ +90 | 위쪽 (UP=+el) |
| 거리 (dist) | 미터 (m) | > 0 | 청취자 기준 방사 방향 |

> **주의:** SOFA/AmbiX 규약은 LEFT=+az다. 바이노럴 경로 내부에서 부호 반전이 처리된다. OSC로 위치를 전송할 때는 항상 RIGHT=+az 파이프라인 프레임을 사용한다.

---

## Chapter 2. WebGUI 사용법

### 2.1 WebGUI 시작

Core와 UI를 동시에 시작하는 가장 간단한 방법:

```bash
just run
```

또는 개별 시작:

```bash
# Terminal 1 — Core 시작
./build/core/spatial_engine_core

# Terminal 2 — UI 시작
uv run python -m spatial_engine_ui
```

UI가 성공적으로 Core에 연결되면 상태 표시줄에 "Connected" 가 표시된다.

### 2.2 오브젝트 추가

오브젝트(Object)는 공간에 배치되는 독립적인 음원 단위다.

1. WebGUI 상단 툴바에서 "오브젝트 추가 (Add Object)" 버튼을 클릭한다.
2. 새 오브젝트가 기본 위치 (az=0, el=0, dist=1.0 m)에 생성된다.
3. 오브젝트 패널에서 ID, 레이블, 알고리즘을 설정한다.

오브젝트 수는 최대 64개까지 생성할 수 있다. 64개를 초과하면 Core에서 `object_pool_full` (경고 코드 5) 경고가 발생하고 추가 생성이 차단된다.

### 2.3 오브젝트 위치 조정

#### 탑뷰(Top-Down View) 드래그

- 2D 평면도 뷰에서 오브젝트 아이콘을 드래그하여 방위각(az)과 거리(dist)를 조정한다.
- Shift 키를 누른 채 드래그하면 고도각(el)을 함께 조정한다.

#### 수치 직접 입력

오브젝트 패널 오른쪽의 숫자 필드에 직접 값을 입력한다.

| 필드 | 단위 | 입력 범위 |
|------|------|-----------|
| 방위각 (Az) | ° | -180.0 ~ +180.0 |
| 고도각 (El) | ° | -90.0 ~ +90.0 |
| 거리 (Dist) | m | 0.01 ~ 100.0 |
| 게인 (Gain) | dBFS | -120.0 ~ +6.0 |

변경 시 즉시 OSC 명령이 Core로 전송된다.

### 2.4 알고리즘 선택

오브젝트 패널의 "Algorithm" 드롭다운에서 해당 오브젝트의 렌더링 알고리즘을 선택한다.

- 전환 시 Core에서 256-샘플 크로스페이드가 자동 적용되어 클릭 없이 전환된다.
- 현재 스피커 레이아웃과 호환되지 않는 알고리즘 선택 시 상태 표시줄에 `layout_incompatible` 경고가 표시된다.

### 2.5 시나리오 저장 및 로드

시나리오(Scenario)는 오브젝트의 위치, 알고리즘, 게인 설정의 스냅샷이다.

- **저장:** 상단 메뉴 "파일 > 시나리오 저장" 또는 Ctrl+S. YAML 파일로 저장된다.
- **로드:** "파일 > 시나리오 열기" 또는 Ctrl+O.
- **이름 저장:** 현재 상태를 새 이름으로 저장 (Ctrl+Shift+S).

저장 파일 형식은 YAML이며 사람이 읽을 수 있다. 예시:

```yaml
scenario:
  name: "메인 스테이지 Act 1"
  created: "2026-05-09T10:00:00"
  objects:
    - id: 0
      label: "보컬"
      az_deg: 0.0
      el_deg: 5.0
      dist_m: 2.0
      gain_db: -6.0
      algorithm: vbap
    - id: 1
      label: "기타"
      az_deg: -30.0
      el_deg: 0.0
      dist_m: 1.5
      gain_db: -9.0
      algorithm: vbap
```

### 2.6 OSC 모니터링

WebGUI 하단의 "OSC Monitor" 패널에서 실시간 OSC 트래픽을 확인할 수 있다.

- **수신 (Core → UI):** `/sys/state`, `/sys/metrics`, `/sys/heartbeat_miss`, `/sys/warning`
- **송신 (UI → Core):** `/obj/{id}/pos`, `/obj/{id}/algorithm`, `/noise/{ch}/gain` 등

OSC Monitor는 디버깅 및 외부 컨트롤러 통합 시 유용하다.

---

## Chapter 3. VST3 플러그인 사용법

### 3.1 DAW에서 플러그인 삽입

1. DAW에서 새 인스트루먼트 또는 이펙트 트랙을 생성한다.
2. VST3 플러그인 목록에서 "Spatial Engine" 을 검색하여 삽입한다.
3. 플러그인 창이 열리면 Core와의 연결 상태를 확인한다.

> **사전 조건:** 플러그인 삽입 전에 Core 프로세스(`spatial_engine_core`)가 실행 중이어야 한다.

### 3.2 6+1 파라미터 매핑

VST3 플러그인은 7개의 자동화 가능한 파라미터를 노출한다.

| 파라미터 번호 | 이름 | 단위 | 범위 | 설명 |
|---------------|------|------|------|------|
| 1 | Az (방위각) | ° | -180 ~ +180 | 음원 방위각 |
| 2 | El (고도각) | ° | -90 ~ +90 | 음원 고도각 |
| 3 | Dist (거리) | m | 0.01 ~ 100 | 청취자 기준 거리 |
| 4 | Gain (게인) | dBFS | -120 ~ +6 | 오브젝트 출력 게인 |
| 5 | Algorithm | 정수 | 0~2 | 0=VBAP, 1=WFS, 2=DBAP |
| 6 | Reverb Send | dB | -120 ~ 0 | FDN 리버브 전송량 |
| +1 | Mute | 불리언 | 0/1 | 오브젝트 음소거 |

### 3.3 DAW 자동화 연결

#### Reaper에서 자동화

1. 트랙 패널에서 "Envelopes" 탭을 클릭한다.
2. "VST: Spatial Engine — Az" 등 원하는 파라미터를 추가한다.
3. 타임라인에 오토메이션 포인트를 작성하면 재생 시 Core로 실시간 OSC 명령이 전송된다.

#### Bitwig Studio에서 자동화

1. 디바이스 패널에서 플러그인 파라미터를 우클릭한다.
2. "Add Automation Lane" 을 선택한다.
3. 오토메이션 레인에 값을 작성한다.

### 3.4 복수 오브젝트 관리

각 VST3 인스턴스는 하나의 오브젝트 ID에 대응한다. 복수 오브젝트를 DAW에서 제어하려면 트랙별로 플러그인 인스턴스를 삽입하고, 각 인스턴스의 오브젝트 ID를 고유하게 설정한다.

플러그인 설정 창의 "Object ID" 필드에서 ID (0~63)를 지정한다.

---

## Chapter 4. OSC 프로토콜 레퍼런스

### 4.1 통신 개요

| 방향 | 포트 | 프로토콜 | 용도 |
|------|------|----------|------|
| UI → Core | UDP 9100 | OSC 1.1 | 명령 전송 |
| Core → UI | UDP 9101 | OSC 1.1 | 상태 브로드캐스트 |

모든 패킷의 첫 번째 인자는 `schema_version` (정수, v0에서 항상 1)이다.

### 4.2 주요 명령 메시지 (UI → Core)

#### 오브젝트 위치 설정

```
/obj/{id}/pos  ,iiiff
               schema_version  seq  az_deg  el_deg  dist_m
```

예시 (오브젝트 0을 정면 2m 위치로):

```
/obj/0/pos  ,iiiff  1  42  0.0  0.0  2.0
```

#### 알고리즘 전환

```
/obj/{id}/algorithm  ,iis
                     schema_version  seq  algo
```

`algo` 유효값: `"vbap"`, `"wfs"`, `"dbap"`

#### 노이즈 제너레이터 게인

```
/noise/{channel}/gain  ,if
                       schema_version  gain_dB
```

#### 시스템 핸드셰이크

```
/sys/protocol_version  ,i
                       schema_version
```

UI 시작 시 자동으로 전송된다. Core가 동일 메시지로 응답하면 연결이 확립된다.

### 4.3 주요 상태 메시지 (Core → UI)

#### 시스템 상태 (10 Hz)

```
/sys/state  ,i  schema_version
```

상태 비트마스크: bit 0 = 오디오 실행 중, bit 1 = 리버브 활성, bit 2 = 바이노럴 활성

#### 성능 메트릭 (100 ms 주기)

```
/sys/metrics  ,i...  schema_version  [10 × int32]
```

| 인덱스 | 이름 | 단위 |
|--------|------|------|
| 0 | audio_callbacks_total | 횟수 |
| 1 | audio_xrun_count | 횟수 |
| 2 | cpu_pct_audio_thread | % × 100 |
| 3 | osc_packets_received | 횟수 |
| 4 | osc_packets_dropped | 횟수 |
| 5 | active_object_count | 횟수 |
| 6 | algorithm_swap_count | 횟수 |
| 7 | fdn_denormal_guard_hits | 횟수 |
| 8 | binaural_overrun_count | 횟수 |
| 9 | reserved | 0 |

#### 경고 메시지

```
/sys/warning  ,iis  schema_version  type  details
```

| 코드 | 이름 | 의미 |
|------|------|------|
| 1 | protocol_version_mismatch | UI/Core 스키마 버전 불일치 |
| 2 | layout_incompatible | 레이아웃과 알고리즘 조합 불가 |
| 3 | sofa_load_failure | KEMAR SOFA 파일 누락 또는 손상 |
| 4 | ir_metadata_mismatch | IR 메타데이터 불일치 |
| 5 | object_pool_full | 오브젝트 풀 64개 한도 초과 |

### 4.4 시퀀스 번호 규칙

- 각 오브젝트는 독립적인 단조 증가 시퀀스 카운터를 가진다.
- `seq`는 uint32 (OSC int32로 전송, 232에서 순환).
- Core는 last-write-wins 방식으로 처리한다: 낮은 seq 패킷은 무시된다.

### 4.5 vid2spatial OSC 변환 (선택)

vid2spatial 브리지가 활성화된 경우, 카메라 트래킹 결과가 자동으로 `/obj/{id}/pos` 명령으로 변환된다.

좌표 변환:

- 카메라 프레임 (픽셀 좌표) → 정규화 좌표 → 공간 각도 (az, el) → OSC 명령

vid2spatial OSC 계약 전체: `docs/adr/vid2spatial_osc_contract.md`

---

## Chapter 5. VST3 플러그인 + ADM-OSC 콘솔 (직결 모드) {#chapter-5-vst3-플러그인--adm-osc-콘솔-직결-모드}

> **v0.3.0 신기능.** 이 챕터는 `SPATIAL_ENGINE_VST3_OSC=ON` 빌드를 사용하는 경우에만 해당된다.
> v0.1.0 / v0.2.0 사용자는 기본 OFF 빌드를 그대로 사용할 수 있으며 (바이트 호환),
> 이 챕터를 건너뛰어도 무방하다.

### 5.1 개요 — 3-process 토폴로지

v0.3.0에서 도입된 직결 모드(Direct-Connect Mode)는 **사이드카 없이** 세 개의 독립 프로세스가 직접 통신한다.
ADR 0010 §A1-ε에 따라 사이드카(sidecar) 프로세스는 채택하지 않았다.

```
┌─────────────────────────────────────────────────────────────────┐
│  Process 1  spatial_engine_core  (호스트, 항상 실행)             │
│  · Standalone 오디오 렌더러 + OSC 포워더                         │
│  · UDP 바인드: 127.0.0.1:9100  (외부 ADM-OSC 수신 포트)          │
│  · 레지스트리 읽기: ~/.config/spatial_engine/instances.json      │
└─────────────────┬───────────────────────────────────────────────┘
                  │  /adm/obj/N/*  →  sendto(127.0.0.1:9100+N)
                  ▼
┌─────────────────────────────────────────────────────────────────┐
│  Process 2  DAW + VST3 플러그인 인스턴스                         │
│  · Reaper 7.x / Bitwig 5.x / Ardour 8.x 안에서 동작             │
│  · setActive(true) 시점에 127.0.0.1:9100+N UDP 바인드            │
│  · 레지스트리 쓰기: PID + boot_id + 포트 등록                    │
│  · UDP 스레드 → 오디오 콜백 큐 (S2.6 strategy (a) 마샬링)        │
└─────────────────────────────────────────────────────────────────┘
                  ▲
                  │  /adm/obj/N/aed  /adm/obj/N/gain  등
                  │  (ADM-OSC 표준 패킷)
┌─────────────────┴───────────────────────────────────────────────┐
│  Process 3  외부 ADM-OSC 콘솔  (별도 장비 또는 소프트웨어)       │
│  · DiGiCo Quantum / Avid S6L / Yamaha Rivage                    │
│  · L-ISA Controller / Spat Revolution / d&b Soundscape          │
│  · 전송 대상: 127.0.0.1:9100  (standalone core)                  │
└─────────────────────────────────────────────────────────────────┘
```

**핵심 흐름:**
1. 외부 ADM-OSC 콘솔이 `/adm/obj/N/...` 패킷을 `spatial_engine_core:9100` 으로 전송
2. Core가 레지스트리를 조회하여 오브젝트 N을 담당하는 플러그인 인스턴스의 포트(9100+N)를 확인
3. Core가 해당 포트로 패킷을 전달(forward)
4. 플러그인의 UDP 스레드가 수신하고 오디오 콜백으로 마샬링

### 5.2 빌드 옵션

직결 모드는 별도 빌드 플래그가 필요하다.

```cmake
cmake .. \
  -DSPATIAL_ENGINE_VST3=ON \
  -DSPATIAL_ENGINE_VST3_OSC=ON \
  -DSPATIAL_ENGINE_NO_JUCE=OFF
make -j$(nproc)
```

| 플래그 | 기본값 | 설명 |
|--------|--------|------|
| `SPATIAL_ENGINE_VST3` | OFF | VST3 플러그인 빌드 활성화 |
| `SPATIAL_ENGINE_VST3_OSC` | OFF | ADM-OSC 직결 수신 기능 포함 |

`SPATIAL_ENGINE_VST3_OSC=OFF` (기본) 빌드는 v0.1.0 / v0.2.0 과 바이트 호환된다.
OSC 직결 수신 기능을 사용하려면 반드시 두 플래그 모두 ON 으로 빌드해야 한다.

빌드 산출물 위치:
- 플러그인 번들: `build/vst3/SpatialEngine.vst3`
- 설치: `~/.vst3/SpatialEngine.vst3` (또는 `/usr/lib/vst3/`)

### 5.3 인스턴스 레지스트리

플러그인이 DAW에서 활성화되는 시점에 레지스트리 파일을 갱신한다.

**파일 경로:** `~/.config/spatial_engine/instances.json`

**권한:** `0600` (소유자 읽기/쓰기만 허용)

#### 스키마

```json
{
  "schema_version": 1,
  "instances": [
    {
      "object_id": 0,
      "pid": 12345,
      "boot_id": "abc123def456",
      "port": 9100,
      "daw": "Reaper",
      "registered_at": "2026-05-11T10:00:00Z"
    },
    {
      "object_id": 1,
      "pid": 12345,
      "boot_id": "abc123def456",
      "port": 9101,
      "daw": "Reaper",
      "registered_at": "2026-05-11T10:00:01Z"
    }
  ]
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `object_id` | 정수 | 담당 오디오 오브젝트 ID (0~63) |
| `pid` | 정수 | DAW 프로세스 PID |
| `boot_id` | 문자열 | `/proc/sys/kernel/random/boot_id` (Linux) |
| `port` | 정수 | 플러그인이 바인드한 UDP 포트 |
| `daw` | 문자열 | DAW 식별자 (로깅 전용) |
| `registered_at` | ISO 8601 | 등록 시각 (UTC) |

**쓰기:** 플러그인 인스턴스가 `setActive(true)` 시 항목 추가/갱신
**읽기:** `spatial_engine_core` 포워더가 패킷 라우팅 시 읽기
**GC:** Core 시작 시 및 포워딩 실패 시 stale 항목 자동 삭제
- PID가 더 이상 존재하지 않는 경우 삭제
- `boot_id` 가 현재 부팅과 다른 경우 삭제 (재부팅 후 stale 정리)

### 5.4 포트 할당

플러그인은 `setActive(true)` 시점에 UDP 포트를 직접 바인드한다.

**할당 전략 (ADR 0010 §A1-ε):**
1. 오브젝트 ID N에 대해 `127.0.0.1:9100+N` 시도
2. 실패(EADDRINUSE)이면 `9100` ~ `9115` 범위 순차 탐색(walk)
3. 범위 내 모두 실패이면 OS 할당 임시 포트(ephemeral, 49152+)로 폴백
4. 최종 바인드 포트를 레지스트리에 기록

```
object_id=0  →  시도: 9100  성공 시 → 포트 9100 사용
object_id=1  →  시도: 9101  성공 시 → 포트 9101 사용
object_id=0  →  9100 충돌 → 9101 시도 → ... → 9115 → ephemeral
```

> **참고:** 같은 머신에서 여러 DAW 인스턴스를 동시에 실행하는 경우,
> 혹은 `9100` 포트를 다른 프로세스가 점유한 경우 walk/ephemeral 경로가 활성화된다.

### 5.5 콘솔 → 플러그인 라우팅

`spatial_engine_core` 는 수신된 `/adm/obj/N/*` 패킷을 레지스트리 기반으로 라우팅한다.

**포워딩 로직 요약:**
1. 수신 패킷의 오브젝트 ID N 추출
2. 레지스트리에서 `object_id == N` 인 항목 조회
3. 항목의 `port` 로 `sendto(127.0.0.1:{port}, packet)` 실행
4. 레지스트리 항목 없음 → `warn` 로그 후 드롭

**고정 형식 로그 예시 (`tag=value` 형식):**

```
event=adm_fwd src=9100 obj=0 dst_port=9100 bytes=32 result=ok
event=adm_fwd src=9100 obj=1 dst_port=9101 bytes=28 result=ok
event=adm_fwd src=9100 obj=5 dst_port=0    bytes=28 result=no_registry_entry
event=adm_fwd src=9100 obj=2 dst_port=9102 bytes=32 result=sendto_fail errno=111
```

`result=sendto_fail` 시 Core는 해당 레지스트리 항목을 stale로 간주하고 GC 대상으로 표시한다.

### 5.6 DAW 통합

#### Reaper 7.x

1. Reaper 설정 → "VST" 탭 → VST 플러그인 경로에 `~/.vst3` 또는 설치 경로 추가
2. "Rescan" 실행 → "SpatialEngine" 검색 확인
3. FX 체인에 "SpatialEngine (Spatial Engine)" 삽입
4. 플러그인 창의 "Object ID" 필드에서 오브젝트 ID 설정
5. 플러그인 활성화(bypass 해제) 시 UDP 바인드 수행 및 레지스트리 등록

> setActive(true) 타이밍: Reaper에서는 트랙 FX bypass 해제 또는 프로젝트 로드 시 호출된다.

자동화 파라미터 연결:
1. 트랙의 "Envelopes" 탭 → "Add envelope" → "VST: SpatialEngine — Az" 등 선택
2. 타임라인에 포인트 작성 → 재생 시 ADM-OSC 역경로로 DAW automation lane 기록 가능

#### Bitwig Studio 5.x

1. "Settings → Locations" → "VST3 plug-in locations" 에 경로 추가
2. "Rescan plug-ins" 실행
3. 디바이스 패널에서 "SpatialEngine" 드래그하여 트랙에 삽입
4. 디바이스 파라미터 "Object ID" 설정
5. 트랙 활성화 시 UDP 바인드 수행

자동화:
- 파라미터 우클릭 → "Add Automation Lane"

#### Ardour 8.x

1. "Edit → Preferences → Plugins → VST3" 경로 확인
2. "Window → Plugin Manager" 에서 "SpatialEngine" 검색 후 삽입
3. 플러그인 에디터의 "Object ID" 설정
4. 트랙 armed/active 상태에서 UDP 바인드

> **종료(terminate) 동작:** 플러그인이 `setActive(false)` 호출 시 UDP 소켓을 닫고
> 레지스트리에서 해당 항목을 삭제한다. DAW 종료 또는 FX 제거 시 자동으로 처리된다.

### 5.7 자동화 — kMute (역방향 경로)

ADR 설계 계약 A2.1-β에 따라 kMute 파라미터는 다음 경로로 처리된다.

**수신 경로 (콘솔 → DAW):**
```
콘솔  →  /adm/obj/N/mute 1  →  Core 포워더  →  플러그인 UDP 스레드
       →  메시지 큐 (message-thread 마샬링, S2.6 strategy (a))
       →  오디오 콜백에서 파라미터 큐 소비
       →  performEdit(kMute, 1.0)
       →  DAW automation lane 기록
```

**마샬링 전략 (S2.6 strategy (a)):**
- UDP 스레드에서 락-프리 큐에 메시지 enqueue
- 오디오 콜백이 블록 경계에서 큐 소비
- RT-safe: 오디오 스레드에서 힙 할당 없음

> **현재 상태 (v0.3.0):** kMute 수신 디코딩 및 큐잉 구현 완료.
> DAW automation lane 실제 기록(performEdit) 활성화는 S7 완료 후.
> v0.3.0에서는 kMute 값을 읽고 내부 상태에 반영하지만 DAW lane 기록은 비활성.

### 5.8 트러블슈팅

#### 포트 충돌이 발생하면?

포트 충돌 시 플러그인은 자동으로 walk → ephemeral 순서로 폴백한다.
폴백 발생 여부를 확인하는 방법:

```bash
# Core 로그에서 ephemeral 폴백 확인
grep "port_fallback=ephemeral" logs/spatial_engine_core.log

# 또는 플러그인 측 로그
grep "bind_fallback" ~/.config/spatial_engine/plugin.log
```

ephemeral 포트를 사용하는 경우 Core가 레지스트리를 통해 실제 포트를 파악하므로
**수동으로 포트를 맞출 필요가 없다.**

```bash
# 현재 레지스트리 상태 확인
cat ~/.config/spatial_engine/instances.json
```

#### 레지스트리 파일이 손상된 경우

레지스트리 파일이 JSON 파싱 실패 시 Core는 자동으로 빈 레지스트리로 초기화한다.

```bash
# 수동 초기화
rm ~/.config/spatial_engine/instances.json
# Core 재시작 시 자동 재생성
```

#### schema_version 미스매치

```
[WARN] event=registry_schema_mismatch expected=1 got=2
```

이 에러는 더 새로운 버전의 플러그인이 기록한 레지스트리를 구버전 Core가 읽을 때 발생한다.
Core와 플러그인 버전을 맞추거나 레지스트리 파일을 삭제 후 재등록한다.

#### 플러그인이 레지스트리에 등록되지 않는 경우

- Core가 실행 중인지 확인: 레지스트리 파일은 플러그인이 직접 쓰므로 Core 무관
- 레지스트리 파일 권한 확인: `ls -la ~/.config/spatial_engine/instances.json` → `0600`
- DAW 프로세스가 `~/.config/spatial_engine/` 디렉토리에 쓰기 권한이 있는지 확인

### 5.9 알려진 한계

| 항목 | 상태 | 예정 |
|------|------|------|
| kMute DAW automation lane 기록 | 미활성 (reader만) | S7 완료 후 (v0.3.0 sprint) |
| macOS 지원 | 미지원 | v0.4 이상 |
| Windows 지원 | 미지원 | v0.4 이상 |
| 최대 동시 플러그인 인스턴스 | 8개 (ADR 0010 §A1-ε) | v0.4에서 확장 검토 |
| 외부 네트워크 콘솔 (원격 IP) | 미지원 (loopback 전용) | v0.4 이상 |

---

## Chapter 6. 시나리오 작성

### 6.1 시나리오 구성 요소

- 오브젝트 초기 위치 및 알고리즘 설정
- 트라젝토리 (trajectory) 애니메이션 키프레임
- 스냅샷 (snapshot) 및 크로스페이드 전환
- 스피커 레이아웃 참조

### 6.2 YAML 시나리오 파일 구조

- 파일 위치 및 명명 규칙 (`scenarios/` 디렉토리)
- `scenario`, `objects`, `trajectory`, `snapshots` 섹션 설명
- 레이아웃 파일 참조 방법 (`configs/lab_*.yaml`)

### 6.3 오브젝트 배치

- 정적 배치 (시나리오 로드 시 고정)
- 동적 배치 (타임코드 기반 키프레임)
- 그리드 스냅 및 심메트리 도구

### 6.4 트라젝토리 애니메이션

- 키프레임 방식: 시간, 위치, 보간 방법 (linear, bezier)
- OSC 타임태그 활용
- LTC 타임코드 동기화 (Chapter 9 참조)

### 6.5 스냅샷 크로스페이드

- 스냅샷 A → B 전환 시 오브젝트 위치의 선형 보간
- 크로스페이드 시간 설정 (기본: 2초)
- 즉각 전환 (0 ms) 사용 시 주의사항 (오디오 클릭 가능성)

---

## Chapter 7. 알고리즘별 가이드

### 7.1 VBAP 운용 지침

- 스피커 배열 검증 방법 (삼각형 분해 커버리지 확인)
- "hole-in-the-middle" 현상 발생 시 보완 방법
- 고도각 사용 시 스피커 배열 3D화 요구사항
- 권장 az/el 이동 속도 (청취자 추적 한계)

### 7.2 DBAP 운용 지침

- 거리 감쇠 지수(rolloff exponent) 설정
- 비정형 공간에서 스피커 위치 YAML 입력 방법
- 확산 음장 표현 기법

### 7.3 WFS 운용 지침

- 어레이 간격(inter-speaker spacing) 설정 및 검증
- 앨리어싱 주파수 계산 (f_alias = c / (2 × d_speaker))
- 프리필터(prefilter) 적용 여부 설정

### 7.4 알고리즘 런타임 전환

- 재생 중 전환 가능 여부 (가능, 256-샘플 크로스페이드)
- DAW 자동화로 알고리즘 전환하는 방법
- 전환 불가 조합 (`layout_incompatible`) 사전 확인 방법

### 7.5 바이노럴 모니터 (헤드폰 모니터링)

- 바이노럴 사이드체인 활성화 방법
- KEMAR SOFA 기반 HRTF 특성 (384샘플 IR, 48 kHz)
- 레이턴시 추가분: +1.33 ~ +5 ms (파티션 크기 의존)
- 좌우 반전 발생 시 `docs/coordinate_convention.md` 확인

---

## Chapter 8. 멀티존 및 per-zone 리미터 운용

### 8.1 멀티존 개요

- 존(Zone) 정의 (스피커 그룹 분할)
- 존별 독립 렌더링 설정
- YAML 레이아웃 파일에서 존 정의 방법

### 8.2 Per-zone 리미터 설정

- 존별 최대 출력 레벨 설정 (dBFS)
- 리미터 임계값 및 어택/릴리스 설정
- 멀티존 환경에서 상호 간섭 방지 방법

### 8.3 운용 예시

- 메인홀 + 로비 동시 운용
- 각 존 독립 시나리오 로드 방법
- 존 간 오브젝트 이동 처리

---

## Chapter 9. LTC Chase 동기

### 9.1 LTC (Linear Timecode) 개요

- LTC의 역할 (영상 + 음향 동기)
- SMPTE 타임코드 형식 (30 fps, 25 fps 등)
- Spatial Engine에서의 LTC 활용 방법

### 9.2 LTC 입력 설정

- LTC 오디오 입력 채널 설정 방법
- 프레임 레이트 설정
- 록(lock) 상태 확인 방법

### 9.3 타임라인 동기화

- LTC 기반 트라젝토리 재생 방법
- 드리프트 보정 설정
- 타임코드 손실 시 동작 (자유 실행 또는 일시 정지)

### 9.4 영상 연동 워크플로

- 영상 플레이어 → LTC → Spatial Engine → 오디오 렌더링 연결 예시
- 오프셋(offset) 설정으로 영상-음향 지연 보정

---

## Chapter 10. 다중 오브젝트 그룹 자동화

### 10.1 오브젝트 그룹 정의

- 그룹 생성 및 명명 방법
- 그룹 내 오브젝트 일괄 위치 조정
- 그룹 게인 오프셋

### 10.2 그룹 트라젝토리

- 그룹 전체 이동 (평행 이동 모드)
- 그룹 내 상대 위치 유지
- 중심점 기준 회전 자동화

### 10.3 OSC로 그룹 제어

- 외부 컨트롤러에서 그룹 전체 이동 명령 전송 방법
- 매크로 OSC 명령 묶음 작성 예시

---

## Chapter 11. 라이브 공연 권장 워크플로

### 11.1 공연 전 사전 점검 (Soundcheck)

공연 시작 전 다음 항목을 순서대로 점검한다.

#### 시스템 기동 순서

1. 랩 머신 부팅 후 PREEMPT_RT 커널 선택 확인
2. Dante 드라이버 로드 확인: `lsmod | grep alp`
3. PipeWire-JACK 상태 확인: `systemctl --user status pipewire`
4. JACK 시작:

```bash
jackd -R -P80 -d alsa -d hw:Dante -r 48000 -p 64 -n 2 -o 8 -i 8
```

5. Core 시작: `./build/core/spatial_engine_core`
6. UI 시작: `uv run python -m spatial_engine_ui`
7. 연결 상태 확인 (UI 상태 표시줄 "Connected")
8. 시나리오 로드 및 오브젝트 위치 확인
9. 핑크 노이즈 오브젝트로 스피커 레벨 체크

#### 자가 진단 항목

| 점검 항목 | 방법 | 정상 기준 |
|-----------|------|-----------|
| OSC 연결 | UI 상태 표시줄 | "Connected" |
| xrun 카운트 | `/sys/metrics` index 1 | 0 (또는 증가 없음) |
| 오디오 CPU 점유율 | `/sys/metrics` index 2 | < 70% (× 100) |
| 바이노럴 오버런 | `/sys/metrics` index 8 | 0 |
| 스피커 레이아웃 | `layout_incompatible` 경고 없음 | 경고 없음 |

### 11.2 공연 중 안전망

- 시나리오 스냅샷 사전 준비 (최소 2개 백업 스냅샷)
- 긴급 오브젝트 위치 초기화 단축키 설정
- OSC 재연결 자동화 설정
- 헤드폰 모니터를 통한 바이노럴 실시간 확인

### 11.3 공연 후 점검

- `audio_xrun_count` 기록 및 원인 분석
- 로그 파일 보관: `logs/spatial_engine_core.log`
- 시나리오 저장 확인

### 11.4 긴급 상황 대응

| 상황 | 대응 조치 |
|------|-----------|
| Core 비정상 종료 | UI 자동 재연결 대기 후 Core 재시작 |
| 오디오 전체 무음 | JACK 상태 확인 → Core 재시작 → 오디오 인터페이스 확인 |
| 특정 스피커 무음 | 레이아웃 YAML 채널 매핑 확인 |
| OSC 연결 끊김 | UI 재시작 (Core는 계속 실행) |

---

## Chapter 12. 룸 및 스피커 캘리브레이션

### 12.1 스피커 레이아웃 YAML 작성

스피커 배열은 `configs/` 디렉토리의 YAML 파일로 정의한다.

기본 파일 형식:

```yaml
layout:
  name: "Lab 8ch Ring"
  sample_rate: 48000
  speakers:
    - id: 0
      az_deg: 0.0
      el_deg: 0.0
      dist_m: 3.0
      jack_port: "system:playback_1"
    - id: 1
      az_deg: 45.0
      el_deg: 0.0
      dist_m: 3.0
      jack_port: "system:playback_2"
    # ... 이하 동일 형식으로 추가
```

배열 기동 시 레이아웃 파일 지정:

```bash
./build/core/spatial_engine_core --layout configs/lab_8ch_ring.yaml
```

### 12.2 레이아웃 호환성 검증

Core 기동 시 `LayoutCompatibilityChecker`가 레이아웃과 알고리즘 조합을 자동으로 검사한다.

| 알고리즘 | 최소 요구사항 |
|----------|---------------|
| VBAP | 스피커 3개 이상, 비동일 평면 |
| WFS | 선형/평면 어레이, 알려진 스피커 간격 |
| DBAP | 제약 없음 (스피커 4개 미만 시 경고) |

### 12.3 수동 캘리브레이션 절차

1. 측정용 마이크를 청취 지점에 배치한다.
2. 각 스피커 채널에 핑크 노이즈를 순서대로 재생한다.
3. 측정 소프트웨어(REW, Room EQ Wizard 등)로 SPL과 딜레이를 측정한다.
4. 측정 결과를 YAML 레이아웃 파일의 `dist_m` 및 `delay_ms` 필드에 반영한다.

### 12.4 자동 캘리브레이션 (예정)

- v1+ 예정 기능
- 측정 마이크 입력 자동 처리 및 YAML 자동 생성

---

## Chapter 13. 트러블슈팅

### 13.1 오디오 없음 (No Audio)

| 증상 | 원인 | 조치 |
|------|------|------|
| 모든 스피커 무음 | Core 미시작 또는 비정상 종료 | Core 로그 확인 후 재시작 |
| JACK 포트 없음 | Dante 드라이버 미로드 | `lsmod`, `aplay -l` 확인 |
| 핸드셰이크 실패 | 스키마 버전 불일치 | Core와 UI 버전 맞추기 |
| 레이아웃 검사 실패 | `layout_incompatible` 경고 | YAML 파일 및 알고리즘 확인 |

### 13.2 음 위치 이상

| 증상 | 원인 | 조치 |
|------|------|------|
| 좌우 반전 | 좌표계 혼동 (SOFA vs 파이프라인) | `docs/coordinate_convention.md` 확인 |
| 위치 고정 | OSC 시퀀스 번호 오동작 | UI 재시작으로 seq 리셋 |
| 특정 스피커만 소리 | VBAP 삼각형 커버리지 밖 | 스피커 배열 및 오브젝트 위치 확인 |
| 알고리즘 전환 후 클릭 | 크로스페이드 미적용 | Core 버전 확인 (ADR 0006 구현 여부) |

### 13.3 레이턴시 스파이크

| 증상 | 원인 | 조치 |
|------|------|------|
| 간헐적 레이턴시 증가 | C-state 깊은 슬립 | `intel_idle.max_cstate=1` 설정 |
| xrun 증가 | CPU governor 절전 모드 | `cpupower frequency-set -g performance` |
| 일관된 높은 레이턴시 | 블록 크기 너무 큼 | 64프레임으로 재설정 |
| JACK 버퍼 오버런 | PipeWire-JACK 버전 낮음 | 0.3.65 이상으로 업그레이드 |

### 13.4 OSC 연결 오류

| 증상 | 원인 | 조치 |
|------|------|------|
| `heartbeat_miss` 반복 | Core 응답 없음 | Core 프로세스 상태 확인 |
| 포트 충돌 | 다른 프로세스가 9100/9101 사용 | `ss -ulnp \| grep 9100` |
| 패킷 드롭 | 네트워크 버퍼 부족 | UDP 소켓 버퍼 크기 증가 |

---

## Chapter 14. 모니터링 및 로그 분석

### 14.1 실시간 메트릭 모니터링

Core는 100 ms 주기로 `/sys/metrics` OSC 메시지를 전송한다. WebGUI의 "Metrics" 패널에서 실시간으로 확인할 수 있다.

#### xrun 카운트 모니터링

`audio_xrun_count` (인덱스 1)는 실시간 오디오 스레드가 주어진 블록 시간 내에 처리를 완료하지 못한 횟수다.

- **정상:** 0 (공연 전체 동안 증가 없음)
- **경미한 문제:** 시간당 1~2회 (원인 조사 필요)
- **심각한 문제:** 분당 1회 이상 (즉시 CPU governor, 블록 크기 점검)

#### CPU 점유율 모니터링

`cpu_pct_audio_thread` (인덱스 2)는 오디오 스레드 CPU 사용률 × 100이다.

- **정상:** 5000 미만 (50% 미만)
- **경고:** 7000 이상 (70% 이상) — 여유 마진 부족
- **위험:** 9000 이상 (90% 이상) — xrun 임박

### 14.2 로그 파일

| 파일 | 위치 | 내용 |
|------|------|------|
| Core 로그 | `logs/spatial_engine_core.log` | 시작/종료, 경고, 에러 |
| UI 로그 | `logs/spatial_engine_ui.log` | OSC 이벤트, GUI 상태 |

로그 레벨 설정 (Core):

```bash
./build/core/spatial_engine_core --log-level debug
```

### 14.3 p99 레이턴시 분석

레이턴시 측정은 P12 레이턴시 하네스를 사용한다.

```bash
just latency-test
```

결과는 `docs/latency_budget.md` 형식으로 출력된다. 목표:

- 엔드-투-엔드 p99 레이턴시 5 ms 이하 (PREEMPT_RT 커널 기준)
- 바이노럴 경로 추가 레이턴시 10 ms 이하

### 14.4 denormal guard 히트 모니터링

`fdn_denormal_guard_hits` (인덱스 7)가 증가하는 경우, FDN 리버브의 비정상 denormal float 처리를 의미한다. 정상적인 운용에서는 0이어야 한다.

값이 지속적으로 증가하면 `test_p7_fdn_denormal_guard` 테스트를 재실행하여 진단한다.

---

## Chapter 15. 백업 및 복구

### 15.1 시나리오 백업

시나리오 파일은 YAML 형식으로 `scenarios/` 디렉토리에 저장된다. 공연 전 반드시 백업한다.

```bash
# 시나리오 디렉토리 전체 백업
tar -czf scenarios_backup_$(date +%Y%m%d_%H%M).tar.gz scenarios/
```

### 15.2 프리셋 백업

- 스피커 레이아웃 YAML: `configs/` 디렉토리
- 사용자 설정: `~/.config/spatial_engine/` (구현 시)

```bash
tar -czf configs_backup_$(date +%Y%m%d_%H%M).tar.gz configs/
```

### 15.3 시나리오 복구

```bash
# 백업 압축 해제
tar -xzf scenarios_backup_YYYYMMDD_HHMM.tar.gz

# WebGUI에서 파일 > 시나리오 열기로 로드
```

### 15.4 시스템 설정 복구

Core 바이너리와 Python 환경 재구성:

```bash
git checkout main
just bootstrap
just build
```

설정 파일이 버전 관리에 포함된 경우 `git checkout -- configs/` 로 복구한다.

---

## Chapter 16. 부록

### 16.1 OSC 명령 전체 목록

#### 명령 메시지 (UI → Core, port 9100)

| OSC 주소 | 인자 형식 | 설명 |
|----------|-----------|------|
| `/obj/{id}/pos` | `,iiiff` | 오브젝트 위치 설정 |
| `/obj/{id}/algorithm` | `,iis` | 렌더링 알고리즘 전환 |
| `/noise/{ch}/type` | `,is` | 노이즈 유형 (white/pink) |
| `/noise/{ch}/gain` | `,if` | 노이즈 게인 (dBFS) |
| `/sys/protocol_version` | `,i` | 시작 핸드셰이크 |

#### 상태 메시지 (Core → UI, port 9101)

| OSC 주소 | 인자 형식 | 주기 | 설명 |
|----------|-----------|------|------|
| `/sys/state` | `,i` | 100 ms | 엔진 상태 비트마스크 |
| `/sys/matrix` | `,i...` | 이벤트 기반 | 스피커 라우팅 매트릭스 |
| `/sys/metrics` | `,i...` | 100 ms | 10개 성능 카운터 |
| `/sys/heartbeat_miss` | `,iis` | 이벤트 기반 | 연속 하트비트 누락 |
| `/sys/warning` | `,iis` | 이벤트 기반 | 경고 메시지 |
| `/sys/protocol_version` | `,i` | 핸드셰이크 응답 | 버전 에코 |

### 16.2 WebGUI 단축키

| 단축키 | 기능 |
|--------|------|
| Ctrl+S | 시나리오 저장 |
| Ctrl+O | 시나리오 열기 |
| Ctrl+Shift+S | 다른 이름으로 저장 |
| Ctrl+Z | 최근 동작 취소 |
| Space | 선택 오브젝트 뮤트 토글 |
| Delete | 선택 오브젝트 제거 |
| Ctrl+A | 전체 오브젝트 선택 |
| F5 | OSC 모니터 창 토글 |
| F11 | 전체화면 토글 |

### 16.3 알고리즘 선택 빠른 참고표

| 환경 | 권장 알고리즘 | 이유 |
|------|---------------|------|
| 콘서트홀 (포인트 소스 어레이) | VBAP | 정밀한 음상 정위 |
| 극장 (라인 어레이) | WFS | 전 청중 일관된 음상 |
| 설치 미술 (불규칙 배열) | DBAP | 비정형 배열 대응 |
| 헤드폰 모니터링 | 바이노럴 (자동) | HRTF 기반 가상 음장 |
| 앰비언트 / 효과음 | DBAP | 확산 음장 표현 |
| 솔로 악기 / 대사 | VBAP | 집중 음상 |

### 16.4 관련 문서

| 문서 | 경로 | 내용 |
|------|------|------|
| IPC 스키마 전체 | `docs/ipc_schema.md` | 모든 OSC 메시지 상세 스펙 |
| 아키텍처 | `docs/architecture.md` | 시스템 구조, DSP 체인, ADR |
| 좌표 규약 | `docs/coordinate_convention.md` | 공간 좌표계 정의 및 변환 |
| vid2spatial OSC 계약 | `docs/adr/vid2spatial_osc_contract.md` | 브리지 통신 규약 |
| 레이턴시 예산 | `docs/latency_budget.md` | 단계별 레이턴시 분석 |
| 랩 설정 | `docs/lab_setup.md` | 커널, 드라이버, JACK 설정 상세 |
