# Spatial Engine — Testing Guide

작성: 2026-05-29 (v0.7.0+8, HEAD `6d9958b` 기준)

엔진의 **모든 검증 경로**를 한 페이지에 정리합니다. 자동 테스트(ctest + pytest) 와 수동 smoke 둘 다 포함합니다. 각 명령은 그대로 복붙해서 동작합니다.

---

## 1. 자동 테스트 (CI 게이트)

### 1.1 C++ ctest — 기본 (NO_JUCE, 101 tests)

```bash
cd /home/seung/mmhoa/spatial_engine
mkdir -p core/build && cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON
make -j$(nproc)
ctest --output-on-failure
```
**기대치:** `100% tests passed, 0 tests failed out of 101`. 약 60초.

### 1.2 C++ ctest — RT-no-alloc sentinel

오디오 스레드에서 `malloc` 가 호출되면 SIGTRAP 으로 즉시 실패하는 빌드. v0.8 P1.3 VBAP 수정 후 모든 테스트 그린.

```bash
cd /home/seung/mmhoa/spatial_engine
mkdir -p core/build_rton && cd core/build_rton
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON
make -j$(nproc)
ctest --output-on-failure
```
**기대치:** `100% tests passed, 0 tests failed out of 101`.

### 1.3 C++ ctest — Relacy 합성 경합

`AmbiDecoder` 더블 버퍼 swap, OSC outbound ring 등의 멀티스레드 안전성을 합성적으로 검증 (실제 스레드 + 1024 iteration).

```bash
cd /home/seung/mmhoa/spatial_engine
mkdir -p core/build_relacy && cd core/build_relacy
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON
make -j$(nproc)
ctest -R relacy --output-on-failure
```

### 1.4 Python pytest — 225 tests / 4 skip

```bash
cd /home/seung/mmhoa/spatial_engine
pip install -r requirements.txt -r requirements-dev.txt
python3 -m playwright install chromium    # 1.4의 WebGUI 60fps 검증용
python3 -m pytest
```
**기대치:** `225 passed, 4 skipped in ~140s`. 카테고리:
- `tests/e2e/` (1)  — IR loader
- `tests/soak_harness/` (6)  — OSC warning, Phase B handshake, WebGUI schema
- `tests/latency_harness/` (1)  — latency schema
- `tests/accuracy_harness/` (2)  — HRTF ITD, accuracy
- `ui/tests/` (15)  — PySide6 transport, drag coalescer, trajectory
- `ui/webgui/tests/` (204)  — server, dispatch, scene E2E, RTT, throughput

### 1.5 VST3 ctest (선택 — build 22일 stale)

```bash
cd /home/seung/mmhoa/spatial_engine
mkdir -p core/build_vst3 && cd core/build_vst3
cmake ../.. -DSPATIAL_ENGINE_VST3=ON
make -j$(nproc)
ctest --output-on-failure    # 29 state/param tests
```
**현재 상태:** v0.8 audit P3.1/P3.5 미해결. 다음 supervised VST3 sprint 까지 CI 미포함.

---

## 2. 수동 Smoke 테스트 — 외부 wav 입력

엔진 단독으로는 wav 파일 직접 입력 옵션이 없습니다. 정식 경로는 `adm_player` (sidecar producer) 가 wav → shm ring 으로 push, 엔진이 `--input-backend shm:/NAME` 로 consume.

### 2.1 사전 준비

1. **`adm_player`** 가 `/home/seung/mmhoa/adm_player/dreamscape` 에 클론되어 있어야 함 (commit `363298a+` 권장 — `soundfile.SoundFile.frames` 버그 수정 포함).
2. **테스트 wav** — 멀티채널 BWF (axml/chna 포함) 권장. 예: `/home/seung/mmhoa/adm_player/dreamscape/01.wav` (38ch / 48kHz / 162s).
3. **레이아웃 YAML** — `/home/seung/mmhoa/spatial_engine/configs/lab_8ch.yaml` 등.

### 2.2 단일 셸 세션에서 producer + consumer 동시 실행 (권장)

⚠️ **중요:** producer 와 consumer 를 별도 `Bash` 호출로 분리하면 producer 의 부모 셸 종료 시 Python `resource_tracker` 가 shm 을 unlink 하고 consumer 가 attach 실패합니다. 같은 셸 세션 안에서 producer 를 `&` 로 백그라운드, consumer 는 포그라운드로 실행하세요.

```bash
# 한 셸 안에서 실행
rm -f /dev/shm/spe-smoke 2>/dev/null   # 이전 잔여 shm 정리

# (1) producer 백그라운드 — adm_player, OSC 활성화 필수
cd /home/seung/mmhoa/adm_player/dreamscape
python3 -u -m adm_player ./01.wav \
    --sink ipc://spe-smoke \
    --block-size 256 --ring-frames 8192 \
    --osc-host 127.0.0.1 --osc-port 9100 > /tmp/adm_player.log 2>&1 &
PROD=$!

# (2) shm region 생성 대기 (보통 1-4초)
for i in 1 2 3 4 5; do
    [ -e /dev/shm/spe-smoke ] && { echo "shm ready at t=${i}s"; break; }
    sleep 1
done

# (3) consumer 포그라운드 — 10초 캡처
cd /home/seung/mmhoa/spatial_engine/core/build
timeout 14 ./spatial_engine_core \
    --input-backend shm:/spe-smoke --block 256 \
    --channels 38 --rate 48000 \
    --layout /home/seung/mmhoa/spatial_engine/configs/lab_8ch.yaml \
    --backend null --wav /tmp/engine_out.wav --seconds 10 \
    --osc-dialect adm > /tmp/engine.log 2>&1

# (4) cleanup
kill $PROD 2>/dev/null; wait $PROD 2>/dev/null
rm -f /dev/shm/spe-smoke 2>/dev/null
```

### 2.3 결과 검증

```bash
python3 -c "
import soundfile as sf, numpy as np
info = sf.info('/tmp/engine_out.wav')
print(f'channels={info.channels} sr={info.samplerate} dur={info.duration:.2f}s')
data, _ = sf.read('/tmp/engine_out.wav')
rms = float(np.sqrt(np.mean(data**2)))
peak = float(np.max(np.abs(data)))
nz = int((data!=0).any(axis=1).sum())
print(f'rms={rms:.6f} peak={peak:.6f} nonzero={nz}/{data.shape[0]} ({100*nz/data.shape[0]:.1f}%)')
"
```
**기대치:** `channels=38 sr=48000 dur=10.00s rms=0.034 peak=0.498 nonzero=479571/480000 (99.9%)`.

⚠️ **만약 `rms=0` 으로 나오면:** producer 에서 OSC 가 비활성화(`--no-osc`) 됐거나 `--osc-port` 가 일치하지 않습니다. 엔진은 OSC 명령으로 만들어진 object 가 없으면 input PCM 을 무시합니다 (object-based 렌더링이라 unattached PCM 은 drop).

### 2.4 자주 보는 항목

```bash
# producer 살아있는지
ps -p $PROD >/dev/null 2>&1 && echo alive || echo dead

# shm region 생겼는지
ls -la /dev/shm/ | grep spe-smoke

# 엔진의 텔레메트리 (1Hz heartbeat)
socat - UDP-RECV:9101 &     # /sys/state, /sys/warning, /sys/binaural_status 등
sleep 3; kill %1

# OSC 포트 listen 확인
ss -unlp | grep 9100
```

---

## 3. WebGUI 통합 테스트

브라우저 + FastAPI + 엔진 3-tier.

```bash
# Terminal 1 — 엔진 (input/output 둘 다 null, OSC 만 사용)
cd /home/seung/mmhoa/spatial_engine/core/build
./spatial_engine_core --backend null --input-backend null

# Terminal 2 — WebGUI 서버
cd /home/seung/mmhoa/spatial_engine
PYTHONPATH=.:ui python3 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000

# 브라우저
xdg-open http://localhost:8000  # 또는 직접 접속
```

**검증 체크리스트:**
- [ ] 캔버스에 기본 object 3개 로드됨
- [ ] 마우스 드래그 시 `/adm/obj/N/aed` OSC 송신 (port 9100)
- [ ] 1초 간격으로 `/sys/state` 수신 → 캔버스 좌표 동기화 (±2°)
- [ ] Transport ▶ Play 후 1Hz `/sys/state` 가 정상 갱신

---

## 4. VST3 호스트 (DAW) 테스트

```bash
# 빌드 (build_vst3 가 22일 stale 이면 reconfigure)
cd /home/seung/mmhoa/spatial_engine
rm -rf core/build_vst3 && mkdir core/build_vst3 && cd core/build_vst3
cmake ../.. -DSPATIAL_ENGINE_VST3=ON
make -j$(nproc)
# 산출물: core/build_vst3/vst3/SpatialEngine.vst3/
```

**Reaper (Linux/macOS/Windows) 권장 절차:**
1. Reaper Preferences → Plug-ins → VST → `core/build_vst3/vst3/` 추가 → Re-scan
2. 새 트랙 Insert plugin → SpatialEngine
3. Track output: Multichannel (bus 0 = 8ch, bus 1 = stereo binaural)
4. 자동화: 6 params — `aed_az`, `aed_el`, `aed_dist`, `gain`, `mute`, `reserved`
5. 세션 save → reload → state persist 검증 (v3/v4 format)

---

## 5. 한 줄 헬스 체크

```bash
# ctest summary
ctest --test-dir core/build | tail -1
# → "100% tests passed, 0 tests failed out of 101"

# pytest summary
python3 -m pytest -q | tail -1
# → "225 passed, 4 skipped in ~140s"

# shm liveness (producer 가 떠 있어야 함)
ls /dev/shm/ | grep spe

# OSC 9100 listen 확인
ss -unlp | grep 9100

# 엔진 텔레메트리 2초 캡처
timeout 3 socat - UDP-RECV:9101 | head -5
```

---

## 6. v0.8 audit 게이트 (참고)

v0.8 audit 추적 가능한 게이트 (`.omc/plans/spatial-engine-v0.8-audit-remediation.md`):

| 게이트 | 명령 | 기대치 |
|---|---|---|
| NO_JUCE ctest | `ctest --test-dir core/build` | 101/101 |
| RT-asserts | `ctest --test-dir core/build_rton` | 101/101 |
| pytest | `python3 -m pytest` | 225/4-skip |
| Relacy | `ctest --test-dir core/build_relacy -R relacy` | 그린 (1024 iter) |
| Standalone smoke | 위 §2 | rms≥0.01, ≥95% non-zero |

이 5개가 모두 그린이면 main 브랜치는 ship-ready 상태입니다.

---

## 7. 트러블슈팅

| 증상 | 원인 / 조치 |
|---|---|
| `shm input attach failed: /spe-smoke` | producer 가 죽었거나 shm 이름 불일치. producer 와 consumer 를 같은 셸에서 실행하세요 (§2.2). |
| 엔진 output wav 가 무음 (rms=0) | OSC 가 꺼져있거나 port 불일치 → producer 에 `--osc-port 9100` 명시 필요. |
| `f.frames` AttributeError | adm_player 가 commit `363298a` 이전 버전. `git pull` 또는 `len(f)` 패치 수동 적용. |
| ctest 가 한 두 개만 fail | OSC outbound/binaural 의 wall-clock flake 가능성. `ctest --repeat until-pass:2 -R <name>` 로 재시도. v0.8 P0 후 거의 사라짐. |
| `vst3_bind_collision` 가 `-j` 빌드에서 fail | port 9100 race. v0.8 P3.5 deferred. `ctest -R vst3_bind_collision` 직렬 실행 시 그린. |
| WebGUI 캔버스가 좌표 갱신 안 됨 | osc_bridge.py 가 안 떠있음 또는 9101 포트 점유. `ss -unlp \| grep 9101` 확인. |

---

문서 버전: 2026-05-29 (HEAD `6d9958b`). 변경 시 같이 갱신: `.omc/plans/spatial-engine-v0.8-status-overview.md`.
