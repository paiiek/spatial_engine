# v0.6 — macOS Apple Silicon 검증 체크리스트

> **목적**: v0.6 의 #9 release-store 강화 (ARM weak-memory-order corner case)
> 와 v0.5 의 SSE 가드가 실제 Apple Silicon (M1/M2/M3) 머신에서 정상 빌드 +
> ctest PASS 하는지 검증.
> **상태**: PENDING — 사용자 Mac 핸즈온 큐. 본 문서의 각 ☐ 박스를 사용자가
> Mac 에서 명령을 돌려 채워주세요.
> **선행 셋업**: `docs/SETUP_MACOS.md` §1~§7 (Homebrew, tmux, Python 3.11,
> 레포 clone) 완료 가정.

---

## A. 사전 확인 (Pre-check)

| # | 단계 | 명령 | 결과 |
|---|------|------|------|
| 1 | 머신 아키텍처 | `uname -m` | ☐ `arm64` 출력 확인 |
| 2 | macOS 버전 | `sw_vers -productVersion` | ☐ 기록: __________ |
| 3 | Xcode CLT | `xcode-select -p` | ☐ 경로 출력 (없으면 `xcode-select --install`) |
| 4 | cmake | `cmake --version` | ☐ 3.20+ |
| 5 | Python | `python3.11 --version` | ☐ 3.11.x |
| 6 | git HEAD | `git -C ~/path/to/spatial_engine log -1 --format='%h %s'` | ☐ `ece6cba feat(rt-safety): v0.6 ...` |

---

## B. 빌드 (Build)

### B.1 Core (NO_JUCE — Linux 와 동일 옵션)

```bash
cd ~/path/to/spatial_engine
rm -rf core/build
mkdir -p core/build
cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON 2>&1 | tee /tmp/v06_mac_cmake.log
make -j$(sysctl -n hw.ncpu) 2>&1 | tee /tmp/v06_mac_make.log
```

| # | 결과 |
|---|------|
| 1 | ☐ cmake 단계 에러 없이 종료 (`/tmp/v06_mac_cmake.log` 에 `-- Configuring done`) |
| 2 | ☐ make 단계 100% (`/tmp/v06_mac_make.log` 마지막 라인) |
| 3 | ☐ `libspe_core.a` 생성 확인 (`ls -la core/build/libspe_core.a`) |
| 4 | ☐ `spatial_engine_core` 바이너리 생성 확인 (`file core/build/spatial_engine_core` → `Mach-O 64-bit executable arm64`) |
| 5 | 컴파일 경고 개수 — 기록: __________ (대략 `grep -c warning: /tmp/v06_mac_make.log`) |

### B.2 SSE 가드 검증

```bash
grep -rn "_mm_" core/src/ | grep -v "//" | head -20
```

| # | 결과 |
|---|------|
| 1 | ☐ 모든 `_mm_*` SIMD 호출이 `#if defined(__x86_64__)` 또는 `SPE_HAS_SSE` 같은 가드 안에 있어야 함. 가드 없는 호출 0 개 확인 |

---

## C. ctest (Core 단위 테스트)

```bash
cd core/build
ctest --output-on-failure 2>&1 | tee /tmp/v06_mac_ctest.log
```

| # | 결과 |
|---|------|
| 1 | ☐ 마지막 라인 `100% tests passed, 0 tests failed out of 85` |
| 2 | ☐ v0.6 신규 ctest 모두 PASS:<br>`b2_runtime_underrun_auto_demote`<br>`b1_b2_mode_transition_smooth`<br>`b1_b2_mode_transition_probe_clamped`<br>`b1_b2_mode_transition_disable_reenable` |
| 3 | ☐ 총 실행 시간 기록 — `Total Test time (real) = ___ sec` |
| 4 | ☐ OSC outbound 관련 ctest PASS:<br>`osc_outbound_multi_producer`<br>`osc_security_peer_validation`<br>`osc_bind_loopback_default` |
| 5 | ☐ binaural 관련 ctest PASS:<br>`binaural_b1_*`<br>`b2_*`<br>`kdtree3d_*` |

**FAIL 발견 시**: 해당 테스트 이름 + 첫 10 줄의 에러 메시지를 본 문서 §F.1
"발견된 이슈" 에 기록해주세요.

---

## D. pytest (Python soak + e2e 하네스)

```bash
cd ~/path/to/spatial_engine
PYTHONPATH=.:ui python3.11 -m pytest tests/ -x --tb=short 2>&1 | tee /tmp/v06_mac_pytest.log
```

| # | 결과 |
|---|------|
| 1 | ☐ 마지막 라인 `47 passed` |
| 2 | ☐ `test_osc_warning_channel.py` PASS (v0.5.1 Q1 outbound) |
| 3 | ☐ `test_soak_webgui_schema.py` PASS (8 초 smoke) |

---

## E. 동작 확인 (Smoke — wav 캡처 모드)

macOS 에는 실시간 CoreAudio backend 가 미구현이므로 wav 캡처 모드만 가능
(`docs/SETUP_MACOS.md` §7 참고). VST3 호스트 (Logic Pro X / Cubase) 안에서의
실시간 모니터링은 호스트가 audio IO 를 담당하므로 들립니다 — 이 경로는 §G
참고.

### E.1 Standalone wav 캡처 smoke (5 초)

```bash
cd ~/path/to/spatial_engine
./core/build/spatial_engine_core --osc-port 9100 --wav /tmp/v06_mac_smoke.wav --seconds 5 &
ENGINE_PID=$!
sleep 1
# 객체 1개 위치 OSC 송신 (5초 동안 좌→우 sweep)
python3.11 -c "
import socket, time, struct
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
def osc(addr, fmt, *vals):
    p = addr.encode()+b'\0'; p += b'\0'*(4-len(p)%4)
    f = (','+fmt).encode()+b'\0'; p += f + b'\0'*(4-len(f)%4)
    for v in vals: p += struct.pack('>f', v)
    sock.sendto(p, ('127.0.0.1', 9100))
for t in range(50):
    az = -90 + (t/49)*180
    osc('/adm/obj/0/azim','f', az); time.sleep(0.1)
"
wait $ENGINE_PID
ls -la /tmp/v06_mac_smoke.wav
afplay /tmp/v06_mac_smoke.wav 2>&1 || echo "afplay missing"
```

| # | 결과 |
|---|------|
| 1 | ☐ `/tmp/v06_mac_smoke.wav` 생성 확인 (파일 크기 > 0) |
| 2 | ☐ `afplay /tmp/v06_mac_smoke.wav` 로 재생 시 좌→우 sweep 들림 (헤드폰 권장) |

### E.2 Binaural wav 캡처 smoke (SOFA 필요)

선행: `~/spatial_engine_data/hrtf/kemar.sofa` 같은 SOFA 파일 1 개 보유.
SOFA 가 없으면 본 항목 SKIP — 본 verify 의 critical path 가 아님.

```bash
# 본 명령은 SOFA 경로 + .speh 작성 후 실행 (Ch.7 §7.2 참고)
./core/build/spatial_engine_core --osc-port 9100 \
    --speh ~/path/to/synthetic_min.speh \
    --binaural-wav /tmp/v06_mac_binaural.wav --seconds 5 &
# ... (E.1 과 동일한 OSC sweep) ...
afplay /tmp/v06_mac_binaural.wav 2>&1
```

| # | 결과 |
|---|------|
| 1 | ☐ `/tmp/v06_mac_binaural.wav` 생성 |
| 2 | ☐ 헤드폰으로 재생 시 좌→우 sweep 의 위치감이 헤드 안에서 변하는지 청취 |
| 3 | ☐ SKIP 시 사유 기록: __________ |

---

## F. 결과 요약 및 commit

### F.1 발견된 이슈 (있다면)

| 카테고리 | 설명 | 우선순위 (P0/P1/P2/P3) |
|----------|------|------------------------|
| (예) build | `_mm_xxx` 가드 누락 발견: `core/src/dsp/foo.cpp:42` | P1 |
|          |      |     |

발견 항목 0 개 시 "발견된 이슈 없음" 으로 기록.

### F.2 환경 메타 기록

- 머신: ☐ 기록 (예: `MacBook Pro M2 Max, 32 GB RAM, macOS 14.5`)
- 실행 일시: ☐ YYYY-MM-DD HH:MM
- 검증자: ☐ paiiek

### F.3 commit 절차

본 문서의 ☐ 박스를 모두 ☑ (성공) 또는 ☒ (실패) 로 채운 후:

```bash
git add docs/release/v0.6.0/macos-arm64-verify.md
git commit -m "docs(v0.6.0): macOS arm64 verify PASS — ctest 85/85 + smoke wav OK"
```

실패 항목이 있으면 commit message 를 `docs(v0.6.0): macOS arm64 verify — N failures, see §F.1` 로 변경하고 P0/P1 항목은 후속 패치 큐에 추가
(`docs/weekly_progress_report_2026-05-18.md` §5).

---

## G. VST3 in DAW on macOS (P0-1 의 macOS 갈래)

본 verify 와 별개로 DAW 안에서의 VST3 동작 검증은 `docs/release/v0.3.0/daw-handson-log.md` 의 형식을 따라 별도 핸즈온 로그가 필요합니다. macOS 호스트:

- **Logic Pro X**: AU 만 지원 — VST3 는 wrapper (Plogue Bidule / Blue Cat
  PatchWork) 가 필요. 우선순위 낮음.
- **Cubase 13 / Nuendo**: VST3 네이티브 지원. 권장 macOS DAW 호스트.
- **Reaper 7.x macOS**: VST3 지원. Linux 와 동일한 daw-handson 체크리스트 항목
  적용 가능.

별도 문서: `docs/release/v0.6.0/daw-handson-log-macos.md` (없으면 v0.3.0
형식 그대로 복제 후 macOS-specific 항목 추가) — 본 verify 의 critical path
와는 분리.

---

## H. 본 verify 가 실제로 검증하는 것 / 못 하는 것

### 검증함

- v0.5 SSE 가드 + v0.6 #9 release-store 변경 후의 arm64 빌드가 깨지지 않음.
- 85 ctest + 47 pytest 가 arm64 에서도 PASS (즉, 상호-아키텍처 회귀 0).
- wav 캡처 경로의 audio pipeline 이 arm64 에서 동작.

### 검증 못 함 (deferred)

- 실제 weak-memory-order race 가 #9 변경으로 차단됐는지 — corner case 가 자연
  발생할 확률이 낮아 회귀 게이트가 미구현. P2 (GHA arm64 matrix) 에서
  reproducible stress 추가 검토.
- 실시간 monitoring 의 latency / glitch 측정 — CoreAudio 백엔드 미구현.
- VST3 호스트 안에서의 실제 음악 작업 시나리오 — §G 참고.
