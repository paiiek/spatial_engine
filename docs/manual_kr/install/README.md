# Spatial Engine 설치 매뉴얼

**버전:** v0.2.0  
**대상 독자:** 시스템 관리자, 음향 엔지니어, 통합 담당자  
**최종 수정:** 2026-05-10

---

## 목차

1. [시스템 요구사항](#chapter-1-시스템-요구사항)
2. [사전 설치](#chapter-2-사전-설치)
3. [spatial_engine 다운로드 및 빌드](#chapter-3-spatial_engine-다운로드-및-빌드)
4. [VST3 플러그인 설치](#chapter-4-vst3-플러그인-설치)
5. [WebGUI 서버 설치](#chapter-5-webgui-서버-설치)
6. [vid2spatial OSC 브리지 설치](#chapter-6-vid2spatial-osc-브리지-설치)
7. [라이선스 활성화](#chapter-7-라이선스-활성화)
8. [DAW 통합](#chapter-8-daw-통합)
9. [첫 실행 및 자가 진단 테스트](#chapter-9-첫-실행-및-자가-진단-테스트)
10. [일반 문제 해결](#chapter-10-일반-문제-해결)
11. [성능 튜닝](#chapter-11-성능-튜닝)
12. [부록](#chapter-12-부록)

---

## Chapter 1. 시스템 요구사항

### 1.1 운영체제

Spatial Engine은 Linux 기반 운영체제에서 동작하도록 설계되었다. 지원 및 권장 배포판은 다음과 같다.

| 항목 | 권장 사양 | 최소 사양 |
|------|-----------|-----------|
| 배포판 | Ubuntu 22.04 LTS | Ubuntu 22.04 LTS |
| 커널 | PREEMPT_RT (linux-image-rt-amd64) | Linux 6.x generic |
| glibc | 2.35+ | 2.31+ |
| 아키텍처 | x86_64 (amd64) | x86_64 (amd64) |

> **권장 사항:** 실시간 오디오 성능을 위해 PREEMPT_RT 커널 사용을 강력히 권장한다.
> 일반(commodity) 6.x 커널에서도 동작하지만, p99 레이턴시가 5 ms 기준을 초과할 수 있다.

#### Ubuntu 22.04 LTS 커널 확인

```bash
uname -r
```

PREEMPT_RT 커널이 설치된 경우 출력 예시:

```
5.15.0-1042-realtime
```

또는:

```bash
uname -v | grep -i preempt
```

`PREEMPT_RT` 문자열이 포함된 경우 실시간 커널로 동작 중이다.

### 1.2 하드웨어 요구사항

| 항목 | 권장 사양 | 최소 사양 |
|------|-----------|-----------|
| CPU | 4코어 이상, Intel/AMD x86_64 | 2코어 x86_64 |
| RAM | 8 GB 이상 | 4 GB |
| 저장장치 | SSD 20 GB 여유 공간 | HDD 10 GB 여유 공간 |
| PCIe 슬롯 | 1개 (Dante 카드용) | — (소프트웨어 전용 모드) |
| 오디오 인터페이스 | Digigram ALP-Dante PCIe | ALSA 호환 오디오 장치 |

#### CPU 성능 정책

실시간 오디오 스레드의 안정적인 동작을 위해 CPU는 performance governor 모드로 설정되어야 한다. 자세한 설정 방법은 [Chapter 11. 성능 튜닝](#chapter-11-성능-튜닝)을 참조한다.

### 1.3 소프트웨어 의존성

빌드 및 실행에 필요한 소프트웨어 의존성은 다음과 같다.

#### 빌드 도구

| 도구 | 버전 | 용도 |
|------|------|------|
| GCC | 11.x 또는 13.x | C++17/20 컴파일러 |
| CMake | 3.20 이상 | 빌드 시스템 구성 |
| Ninja | 최신 권장 | 빌드 실행 (Make 대체) |
| Git | 2.x 이상 | 소스 코드 및 서브모듈 관리 |

Ubuntu 22.04에서 CMake 버전 확인:

```bash
cmake --version
```

출력 예시:

```
cmake version 3.22.1
```

#### 런타임 의존성

| 도구 | 버전 | 용도 |
|------|------|------|
| Python | 3.11 이상 | WebGUI UI 프로세스 |
| PySide6 | 최신 | Qt6 기반 UI 프레임워크 |
| uv (Astral) | 최신 | Python 패키지 관리 |
| PipeWire-JACK | 0.3.65 이상 | 오디오 I/O 브리지 |

#### JUCE 프레임워크

JUCE 7.0.12가 `core/JUCE/` 서브모듈로 포함되어 있다. 별도 설치 불필요.

### 1.4 네트워크 요구사항

Spatial Engine의 두 프로세스(Core, UI)는 OSC/UDP 루프백 통신을 사용한다.

| 포트 | 방향 | 용도 |
|------|------|------|
| UDP 9100 | UI → Core | 명령 전송 |
| UDP 9101 | Core → UI | 상태 브로드캐스트 |

로컬 방화벽이 UDP 9100-9101 포트를 차단하지 않도록 설정한다. 외부 OSC 클라이언트(예: vid2spatial 브리지)를 연결하는 경우 해당 포트도 추가로 허용한다.

---

## Chapter 2. 사전 설치

### 2.1 개요

Spatial Engine을 빌드하고 실행하기 전에 다음 구성 요소를 순서대로 설치해야 한다.

1. PREEMPT_RT 커널 (실시간 오디오 요구사항)
2. Dante Domain Manager (DDM) — 네트워크 Dante 라우팅 관리
3. Digigram ALP-Dante PCIe 드라이버 (하드웨어 오디오 I/O)
4. PipeWire-JACK 오디오 미들웨어
5. 빌드 도구 및 라이브러리

### 2.2 PREEMPT_RT 커널 설치

#### 2.2.1 Ubuntu 22.04에서 RT 커널 패키지 설치

```bash
sudo apt update
sudo apt install linux-image-lowlatency-hwe-22.04 linux-headers-lowlatency-hwe-22.04
```

또는 배포판 제공 RT 커널이 있는 경우:

```bash
sudo apt search linux-image-rt
sudo apt install linux-image-rt-amd64
```

설치 후 재부팅하여 GRUB에서 RT 커널을 선택한다.

#### 2.2.2 RT 커널 검증

```bash
uname -v | grep -i preempt
```

`PREEMPT_RT` 문자열이 표시되면 성공이다.

실시간 레이턴시 검증 (선택 사항):

```bash
sudo apt install rt-tests
sudo cyclictest -l 10000 -m -S -p 80 -i 200 -h 400 -q
```

p99 레이턴시가 200 µs 미만이면 실시간 오디오 운용에 적합하다.

> **참고:** p99가 200 µs를 초과하는 경우 [Chapter 11. 성능 튜닝](#chapter-11-성능-튜닝)의 C-state 및 CPU governor 설정을 먼저 적용한다.

#### 2.2.3 커널 파라미터 설정

`/etc/default/grub`을 편집하여 다음 파라미터를 추가한다.

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash intel_idle.max_cstate=1 processor.max_cstate=1 nohz_full=1-3 rcu_nocbs=1-3"
```

AMD CPU를 사용하는 경우 `intel_idle.max_cstate=1` 대신 `amd_idle.max_cstate=1`을 사용한다.

변경 후 GRUB을 업데이트한다:

```bash
sudo update-grub
sudo reboot
```

### 2.3 Dante Domain Manager (DDM) 설치

Dante Domain Manager는 네트워크 Dante 장치의 라우팅과 채널 할당을 관리하는 소프트웨어다. ALP-Dante PCIe 카드와 Dante 네트워크 장치를 연동하는 환경에서 필수이다.

#### 2.3.1 DDM 다운로드

Audinate 공식 사이트(https://www.audinate.com)에서 Linux용 DDM을 다운로드한다.

```bash
# Audinate에서 제공하는 설치 스크립트 실행 (버전은 최신 확인)
sudo dpkg -i dante-domain-manager_*.deb
```

#### 2.3.2 DDM 서비스 시작

```bash
sudo systemctl enable dante-domain-manager
sudo systemctl start dante-domain-manager
sudo systemctl status dante-domain-manager
```

#### 2.3.3 DDM 웹 인터페이스 접속

브라우저에서 `http://localhost:8080` 에 접속하여 DDM 관리 인터페이스를 확인한다.

### 2.4 Digigram ALP-Dante PCIe 드라이버 설치

> **중요:** Digigram 드라이버 버전과 커널 버전은 반드시 호환 매트릭스를 확인 후 설치한다.
> 호환되지 않는 조합은 오디오 채널이 노출되지 않는다.

#### 2.4.1 드라이버 다운로드

Digigram 공식 사이트(https://www.digigram.com 또는 https://getdante.com/product/alp-dante/)에서 Linux용 최신 ALSA 드라이버를 다운로드한다.

#### 2.4.2 드라이버 설치

```bash
tar -xzf alp-dante-driver-*.tar.gz
cd alp-dante-driver-*/
sudo make install
sudo modprobe alp_dante
```

#### 2.4.3 드라이버 로드 확인

```bash
lsmod | grep alp
aplay -l | grep -i dante
```

Dante 장치가 표시되면 드라이버 설치가 완료된 것이다.

### 2.5 PipeWire-JACK 설치

Ubuntu 22.04에서 PipeWire-JACK은 기본 설치되어 있을 수 있다. 없는 경우 다음과 같이 설치한다.

```bash
sudo apt install pipewire pipewire-jack libpipewire-0.3-dev
sudo apt install wireplumber
```

버전 확인:

```bash
pw-jack jack_lsp --version
```

PipeWire-JACK이 정상 동작하는지 확인:

```bash
pw-jack jack_lsp
```

Dante 채널이 목록에 표시되어야 한다. 표시되지 않는 경우 드라이버 재설치 또는 `docs/lab_setup.md`의 JACK 구성을 참조한다.

### 2.6 시스템 빌드 의존성 설치

아래 명령으로 빌드에 필요한 모든 시스템 패키지를 설치한다.

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libasound2-dev \
    libfreetype6-dev \
    libx11-dev \
    libxcomposite-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxrender-dev \
    libwebkit2gtk-4.0-dev \
    libglu1-mesa-dev \
    libjack-jackd2-dev \
    clang-format-18 \
    clang-tidy \
    yamllint
```

Python 패키지 관리자 `uv` 설치:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source $HOME/.cargo/env
```

---

## Chapter 3. spatial_engine 다운로드 및 빌드

### 3.1 개요

Spatial Engine의 소스 코드를 클론하고 빌드하는 절차를 설명한다. 빌드는 두 개의 주요 구성 요소를 생성한다.

- **Process A (Core):** `build/core/spatial_engine_core` — C++17/JUCE 실시간 오디오 엔진
- **Process B (UI):** Python/PySide6 WebGUI 프로세스 (`ui/` 디렉토리)

두 프로세스는 OSC/UDP 프로토콜로 통신하며, 메모리를 공유하지 않는다.

### 3.2 소스 코드 클론

```bash
git clone <repository-url> spatial_engine
cd spatial_engine
```

JUCE 서브모듈을 포함하여 클론한다:

```bash
git submodule update --init --recursive
```

완료 후 `core/JUCE/` 디렉토리가 JUCE 7.0.12 소스로 채워진다.

### 3.3 빠른 빌드 (권장)

프로젝트 루트에서 `just` 명령을 사용하면 의존성 설치, 빌드, 테스트를 순서대로 실행한다.

```bash
# just 설치 (없는 경우)
cargo install just
# 또는
sudo snap install just --classic

# 의존성 설치 + JUCE 서브모듈 + Python 패키지 + pre-commit
just bootstrap

# CMake 구성 + Ninja/Make 빌드
just build

# 단위 테스트 + Python 테스트
just test
```

빌드 성공 시 다음 바이너리가 생성된다:

```
build/core/spatial_engine_core
```

### 3.4 수동 빌드

`just`를 사용하지 않는 경우 수동으로 빌드할 수 있다.

#### 3.4.1 하드웨어 없는 CI/개발 빌드 (NO_JUCE 모드)

Dante 하드웨어 없이 개발 및 테스트하는 환경에서는 `SPATIAL_ENGINE_NO_JUCE=ON` 플래그를 사용한다.

```bash
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPATIAL_ENGINE_NO_JUCE=ON
make -j$(nproc)
```

이 모드에서는 오디오 I/O가 스텁(stub)으로 대체되며, 알고리즘 로직과 OSC 통신은 정상 동작한다.

#### 3.4.2 전체 빌드 (JUCE 포함)

Dante 하드웨어가 설치된 랩 환경에서의 전체 빌드:

```bash
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -GNinja
ninja -j$(nproc)
```

#### 3.4.3 빌드 확인

```bash
./build/core/spatial_engine_core --version
```

출력 예시:

```
spatial_engine_core v0.2.0 (full render chain)
  MAX_OBJECTS=64  MAX_BLOCK=512
  JUCE: not linked  OSC-UDP: port 9100
```

### 3.5 Python 환경 설정

UI 프로세스를 위한 Python 환경을 구성한다.

```bash
# uv로 Python 3.11+ 가상 환경 생성 및 의존성 설치
uv sync
```

설치 확인:

```bash
uv run python -c "import PySide6; print(PySide6.__version__)"
```

### 3.6 빌드 검증

전체 테스트 스위트를 실행하여 빌드 정상 여부를 확인한다.

```bash
just test
```

또는 수동으로:

```bash
# C++ 단위 테스트
cd build && ctest --output-on-failure

# Python 테스트
cd /path/to/spatial_engine
uv run python -m pytest
```

모든 테스트가 통과하면 빌드가 완료된 것이다.

### 3.7 빌드 오류 해결

| 오류 메시지 | 원인 | 해결 방법 |
|-------------|------|-----------|
| `JUCE/modules not found` | 서브모듈 미초기화 | `git submodule update --init --recursive` |
| `libasound2-dev not found` | ALSA 개발 패키지 누락 | `sudo apt install libasound2-dev` |
| `CMake 3.20 required` | CMake 버전 부족 | `sudo apt install cmake` 또는 mamba로 최신 설치 |
| `uv: command not found` | uv 미설치 | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| pre-commit clang-format 오류 | clang-format 버전 불일치 | `sudo apt install clang-format-18` |

---

## Chapter 4. VST3 플러그인 설치

### 4.1 개요

- VST3 플러그인 빌드 요구사항
- `/usr/lib/vst3/` 시스템 배포 절차
- `~/.vst3/` 사용자 로컬 배포 절차
- DAW별 VST3 스캔 디렉토리 설정
- 플러그인 로드 확인 방법

### 4.2 빌드

- JUCE projucer 또는 CMake VST3 타겟 빌드 절차
- 빌드 산출물 경로 및 검증

### 4.3 시스템 설치

- 관리자 권한 복사 절차
- 심볼릭 링크 옵션

### 4.4 설치 확인

- `validator` 도구 실행
- DAW 스캔 후 플러그인 목록 확인

---

## Chapter 5. WebGUI 서버 설치

### 5.1 개요

- WebGUI 아키텍처 (Process B — PySide6 기반)
- 독립 실행 vs DAW 연동 모드 선택 기준

### 5.2 설치 방법

- `uv sync` 기반 설치 (권장)
- 시스템 Python 기반 수동 설치
- `pip install -e .` 개발자 설치 모드

### 5.3 서버 시작

- 기본 실행 명령
- OSC 포트 구성 (`--osc-cmd-port`, `--osc-state-port`)
- 복수 인스턴스 실행 시 에페메랄 포트 설정

### 5.4 방화벽 구성

- UDP 9100/9101 허용 절차
- `ufw` 규칙 설정 예시

---

## Chapter 6. vid2spatial OSC 브리지 설치 (선택)

### 6.1 개요

- vid2spatial 브리지의 역할 (AI 영상 트래킹 → 오디오 오브젝트 위치 변환)
- 설치 여부 결정 기준 (라이브 트래킹 연동 필요 시)

### 6.2 의존성

- vid2spatial 저장소 클론 방법
- Python 의존성 (`vid2spatial/requirements.txt`)
- CUDA/GPU 요구사항 (선택)

### 6.3 OSC 계약

- vid2spatial → Core 포트 설정
- `/obj/{id}/pos` OSC 메시지 형식 (`az_deg`, `el_deg`, `dist_m`)
- 좌표 변환 규칙 (RIGHT=+az 파이프라인 프레임)

### 6.4 테스트

- 브리지 연결 확인 방법
- OSC 모니터링 도구 사용법

---

## Chapter 7. 라이선스 활성화

### 7.1 라이선스 종류

| 라이선스 | 적용 대상 | 조건 |
|----------|-----------|------|
| GPL v3 | 오픈소스 프로젝트, 연구용 | 소스 코드 공개 의무 |
| Commercial | 상업적 배포, 독점 플러그인 | 상업 라이선스 구매 필요 |

### 7.2 GPL v3 사용

- 소스 코드 유지 조건 확인
- `LICENSE.md` 파일 배포 포함 의무

### 7.3 상업 라이선스

- 라이선스 구매 절차 (C5 트리거 이벤트)
- 활성화 키 적용 방법
- `docs/license_procurement_plan.md` 참조

---

## Chapter 8. DAW 통합

### 8.1 지원 DAW

| DAW | 지원 상태 | 비고 |
|-----|-----------|------|
| Reaper | 지원 | VST3 플러그인 + OSC 자동화 |
| Bitwig Studio | 지원 | VST3 플러그인 + OSC 자동화 |
| Cubase | 계획 중 | 향후 버전 지원 예정 |

### 8.2 Reaper 설정

- VST3 플러그인 스캔 경로 추가
- ReaScript OSC 수신 설정
- 트랙 자동화 연결

### 8.3 Bitwig Studio 설정

- 플러그인 스캔 및 인식 확인
- OSC 수신기 설정
- Grid 자동화 연동

### 8.4 6+1 파라미터 매핑

- az (방위각), el (고도각), dist (거리), gain, algorithm, reverb_send, mute
- DAW automation lane 연결 방법

---

## Chapter 9. 첫 실행 및 자가 진단 테스트

### 9.1 스모크 테스트

빌드 완료 후 가장 빠른 동작 확인 방법:

```bash
just run
```

이 명령은 Core 스텁과 UI 스텁을 동시에 시작하며, 다음 출력이 표시되어야 한다.

```
[core] schema_version=1 osc_cmd_port=9100 osc_state_port=9101
[ui]   connected to core at 127.0.0.1:9100
```

### 9.2 자가 진단 테스트 항목

- 프로토콜 버전 핸드셰이크 확인
- OSC 포트 바인딩 확인
- 레이아웃 호환성 검사 (알고리즘/스피커 조합)
- KEMAR SOFA 파일 유효성 검사

### 9.3 SOFA 파일 검사

```bash
just sofa-inspect
```

정상 출력 예시:

```
KEMAR SOFA: sample_rate=48000 ir_length=384 receivers=2 measurements=64800
```

### 9.4 자가 진단 실패 시

- 각 검사 항목별 오류 코드와 해결 방법
- 로그 파일 위치: `logs/spatial_engine_core.log`

---

## Chapter 10. 일반 문제 해결

### 10.1 xrun 발생

- **증상:** 오디오 출력에 팝/클릭 노이즈, `audio_xrun_count` 카운터 증가
- **원인:** CPU 스파이크, 블록 크기 설정 오류, IRQ 경쟁
- **해결:** CPU governor 확인, 블록 크기를 64프레임으로 설정, PREEMPT_RT 커널 확인

### 10.2 시스템 의존성 오류 (sysdep 오류)

- **증상:** 빌드 시 라이브러리 찾을 수 없음 오류
- **원인:** apt 패키지 미설치, 버전 불일치
- **해결:** `just bootstrap` 재실행, 패키지 목록 수동 확인

### 10.3 OSC 포트 충돌

- **증상:** Core 시작 시 `bind: Address already in use` 오류
- **원인:** 다른 프로세스가 9100/9101 포트를 점유 중
- **해결:**

```bash
# 포트 점유 프로세스 확인
ss -ulnp | grep -E '9100|9101'
# 자동 포트 할당 사용
./build/core/spatial_engine_core --osc-cmd-port 0
```

### 10.4 UI 연결 실패

- **증상:** UI에 Core 연결 없음, heartbeat miss 경고
- **원인:** Core 프로세스 미시작 또는 비정상 종료
- **해결:** `logs/spatial_engine_core.log` 확인, Core 재시작

### 10.5 Dante 채널 미노출

- **증상:** `jack_lsp`에 Dante 채널이 표시되지 않음
- **원인:** ALP-Dante 드라이버 미로드 또는 커널 버전 불일치
- **해결:** `lsmod | grep alp`, 드라이버 재설치, Digigram 호환 매트릭스 확인

### 10.6 SOFA 파일 오류

- **증상:** Core 시작 시 `sofa_load_failure` 경고
- **원인:** SOFA 파일 경로 오류, 파일 손상, 메타데이터 불일치
- **해결:** 파일 경로 및 권한 확인, `just sofa-inspect` 실행

---

## Chapter 11. 성능 튜닝

### 11.1 CPU Governor 설정

```bash
# performance 모드 설정
sudo cpupower frequency-set -g performance

# 설정 확인
cpupower frequency-info | grep governor
```

부팅 시 자동 적용을 위한 systemd 서비스 생성:

- `/etc/systemd/system/cpugov-performance.service` 파일 생성 방법
- `tuned` 프로필 `latency-performance` 적용

### 11.2 블록 크기 최적화

| 블록 크기 | 레이턴시 (48 kHz) | CPU 부하 | 권장 환경 |
|-----------|-------------------|----------|-----------|
| 32 프레임 | 0.67 ms | 높음 | 최저 레이턴시 요구 |
| 64 프레임 | 1.33 ms | 중간 | 기본 권장 |
| 128 프레임 | 2.67 ms | 낮음 | CPU 여유 부족 시 |

```bash
# JACK 64프레임, 2 period 설정
jackd -R -P80 -d alsa -d hw:Dante -r 48000 -p 64 -n 2 -o 8 -i 8
```

### 11.3 C-state 정책

- `/etc/default/grub` 파라미터 설정
- AMD/Intel별 차이점
- 설정 효과 검증 방법 (`cyclictest`)

### 11.4 IRQ 어피니티 설정

- 오디오 IRQ를 전용 CPU 코어에 할당하는 방법
- `irqbalance` 비활성화 고려 사항

### 11.5 메모리 잠금 (mlockall)

- 오디오 프로세스의 페이지 아웃 방지
- `/etc/security/limits.conf` 설정

---

## Chapter 12. 부록

### 12.1 참고 사양

| 항목 | 값 |
|------|-----|
| OSC 프로토콜 스키마 버전 | 1 (v0) |
| 최대 동시 오디오 오브젝트 수 | 64 |
| 최대 출력 채널 수 | 64 (ALP-Dante PCIe 한계) |
| 알고리즘 스왑 크로스페이드 | 256 샘플 (~5.3 ms @ 48 kHz) |
| FDN 리버브 | 16라인 Hadamard 믹싱 매트릭스 |
| KEMAR SOFA IR 길이 | 384 샘플 (8.0 ms @ 48 kHz) |
| OSC 명령 포트 기본값 | UDP 9100 |
| OSC 상태 포트 기본값 | UDP 9101 |

### 12.2 ADR (Architecture Decision Records) 목록

| ADR 번호 | 제목 |
|----------|------|
| 0001 | 두 프로세스 모델 (JUCE 코어 + PySide6 UI) |
| 0002 | 네이티브 C++ 코어 + JUCE 선택 |
| 0003 | OSC/UDP IPC 채택 |
| 0004 | FDN 토폴로지 (16라인 Hadamard) |
| 0005 | 알고리즘 디스패치 아키텍처 |
| 0006 | 런타임 알고리즘 스왑 + 크로스페이드 |

전체 ADR 문서: `docs/adr/`

### 12.3 외부 도움말 채널

| 채널 | 주소 | 용도 |
|------|------|------|
| Digigram 지원 | https://www.digigram.com/support | ALP-Dante 드라이버 문의 |
| Audinate Dante | https://www.audinate.com/support | Dante 네트워크 문의 |
| JUCE 포럼 | https://forum.juce.com | JUCE 관련 기술 문의 |
| PipeWire | https://gitlab.freedesktop.org/pipewire | PipeWire-JACK 이슈 |

### 12.4 관련 문서

| 문서 | 경로 | 내용 |
|------|------|------|
| 아키텍처 | `docs/architecture.md` | 전체 시스템 구조 |
| IPC 스키마 | `docs/ipc_schema.md` | OSC 명령/상태 메시지 전체 목록 |
| 랩 설정 | `docs/lab_setup.md` | 커널, 드라이버, JACK 상세 핀 |
| 레이턴시 예산 | `docs/latency_budget.md` | 단계별 레이턴시 분석 |
| 라이선스 조달 | `docs/license_procurement_plan.md` | 상업 라이선스 전환 계획 |
| 좌표 규약 | `docs/coordinate_convention.md` | 공간 좌표계 정의 |
