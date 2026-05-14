# macOS 셋업 가이드 (tmux + Claude Code + spatial_engine)

Apple Silicon / Intel Mac 에서 처음부터 환경을 잡는 순서. 위에서 아래로 그대로 따라가면 된다.

---

## 1. Homebrew 확인 / 설치

이미 있으면 건너뛴다.

```bash
which brew || /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

설치 후 안내에 나오는 `eval "$(...)"` 한 줄을 `~/.zshrc` 에 추가하고 새 터미널을 연다.

---

## 2. tmux 설치

```bash
brew install tmux
```

기본 사용법 — 이것만 알면 충분하다:

| 동작 | 명령 |
|---|---|
| 새 세션(이름 지정) | `tmux new -s spatial` |
| 세션에서 빠져나오기 (백그라운드 유지) | `Ctrl-b` 누르고 `d` |
| 다시 들어가기 | `tmux attach -t spatial` |
| 세션 목록 | `tmux ls` |

tmux 세션 안에서 작업하면 SSH 연결이 끊기거나 터미널을 닫아도 작업이 죽지 않는다.
긴 작업(빌드, Claude Code 세션)은 항상 tmux 안에서 돌린다.

---

## 3. Python 3.11 설치

이 레포는 Python **3.11 이상**이 필요하다 (`ui/pyproject.toml`). 기본 3.10 이면 설치한다.

```bash
brew install python@3.11
python3.11 --version   # 확인
```

---

## 4. Claude Code 설치

```bash
curl -fsSL https://claude.com/install.sh | bash
```

설치 후 새 터미널을 열거나 `source ~/.zshrc`.
(npm 방식을 쓰려면 `npm install -g @anthropic-ai/claude-code` — Node 18+ 필요)

---

## 5. 레포 받기 + 의존성 설치

```bash
git clone git@github.com:paiiek/spatial_engine.git
cd spatial_engine

# 이미 받아둔 폴더가 있으면 최신화만:
#   cd spatial_engine && git pull

# Python 런타임 의존성 (WebGUI + bridge)
python3.11 -m pip install -r requirements.txt
```

---

## 6. Claude Code 첫 실행 + 인증

```bash
tmux new -s spatial          # tmux 세션 안에서
cd spatial_engine
claude
```

처음 실행하면 브라우저가 열리며 Anthropic 계정 로그인 → 인증.
(Claude 구독 또는 API 키 둘 다 가능)

이후로는 `tmux attach -t spatial` 로 언제든 작업하던 세션에 복귀할 수 있다.

---

## 7. spatial_engine 빌드 / 실행 (macOS 주의사항)

### C++ 코어 빌드

```bash
mkdir -p core/build && cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON
make -j$(sysctl -n hw.ncpu)
cd ../..
ctest --test-dir core/build --output-on-failure
```

macOS 에서는 호스트 아키텍처(arm64/x86_64)로 자동 타겟된다.

### WebGUI 실행

```bash
# 반드시 프로젝트 루트에서, PYTHONPATH 에 ui/ 포함
PYTHONPATH=.:ui python3.11 -m uvicorn ui.webgui.server:app --host 0.0.0.0 --port 8000
```

`PYTHONPATH=.:ui` 를 빠뜨리면 `ModuleNotFoundError: No module named 'spatial_engine_ui'` 가 난다.

브라우저 접속:
- 로컬: `http://localhost:8000`
- 같은 LAN 의 다른 기기: `http://<이 Mac 의 IP>:8000`

### ⚠️ 오디오 출력 — macOS 한계

기본 백엔드는 `null` 이라 렌더는 하지만 소리를 어디로도 내보내지 않는다.
**macOS 에는 현재 실시간 오디오 백엔드(CoreAudio)가 구현돼 있지 않다.**

움직이는 소리를 확인하려면 파일로 캡처해서 재생한다:

```bash
core/build/spatial_engine_core --osc-port 9100 --wav /tmp/out.wav --seconds 10
# 종료 후 /tmp/out.wav 를 재생
```

실시간 모니터링이 꼭 필요하면 Linux + Dante 하드웨어(`--backend dante`)를 쓰거나,
CoreAudio 백엔드를 별도로 구현해야 한다.

### 동작 확인 흐름

1. 터미널 1 (tmux): 코어 실행 — `core/build/spatial_engine_core --osc-port 9100 --wav /tmp/out.wav --seconds 30`
2. 터미널 2 (tmux): WebGUI 실행 (위 명령)
3. 브라우저에서 오브젝트 드래그 → 코어에 위치가 전달됨 (기본 `low_latency` 모드)
4. 코어 종료 후 `/tmp/out.wav` 재생 → 공간 이동이 들리는지 확인

---

## 문제 해결

| 증상 | 원인 / 해결 |
|---|---|
| `ModuleNotFoundError: spatial_engine_ui` | uvicorn 실행 시 `PYTHONPATH=.:ui` 누락 |
| `pip install -r requirements.txt` 실패 | Python 3.10 사용 중 — `python3.11 -m pip ...` 로 실행 |
| 드래그해도 소리가 안 움직임 | (1) 코어가 실행 중인지 (2) `--wav` 로 캡처 중인지 — macOS 는 실시간 출력 불가 (3) `curl http://localhost:8000/health` 의 `osc_ready` 가 `true` 인지 |
| 코어 빌드가 arm64 에서 실패 | 레포 최신화 필요 — `git pull` (arm64 빌드 수정은 커밋 `8c9111f` 이후 반영) |
| tmux 세션을 잃어버림 | `tmux ls` 로 목록 확인 후 `tmux attach -t <이름>` |
