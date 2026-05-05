# Plan: Phase C / C2 — VST3 MVP (fused 6–7d) — **v4**

**Status:** RALPLAN-DR Deliberate — Planner **v4** (v3 → Architect REVISE + Critic ITERATE 반영). Architect/Critic 라운드 4 검토 대기.

> **권위 순서:** §15 (v4) > §14 (v3) > §1~§13 (v2 본문). 충돌 시 더 최신 amendment 가 우선.

**Predecessors:**
- `.omc/plans/spatial-engine-phaseC.md` (Option B Sync-first, ADR `:246`)
- C1.a~d 완료 (HEAD `c2c1a79`, commits `c3edb6a`/`6145a53`/`59df7b7`/`6716cf9`)

**Scope:** Phase C **C2 단일 마일스톤** 의 HOW 합의. C1 완료, C3/C4 별도.

---

## 13. 검증된 사실 인벤토리 (planner v2 dry-run, 2026-05-05)

| 사실 | 위치 / 증거 |
|---|---|
| `GetPluginFactory()` → `nullptr` | `vst3/SpatialEngineVST3.cpp:27` |
| `ModuleEntry/ModuleExit` → `true` (no-op) | `vst3/SpatialEngineVST3.cpp:32, 35` |
| `vst3/CMakeLists.txt` **35 LoC**, `add_library(spatial_engine_vst3 SHARED ...)` `PREFIX ""` `SUFFIX ".vst3"` 박힘 | `vst3/CMakeLists.txt:17, 22-24` |
| `SPATIAL_ENGINE_VST3=OFF` (default) | `core/CMakeLists.txt:33` |
| `SPATIAL_ENGINE_VST3` 의 `add_subdirectory` 는 if-블록 안 → **OFF=byte-identical 가능** | `core/CMakeLists.txt:165-171` |
| `SPATIAL_ENGINE_NO_JUCE=ON` 강제 JUCE-free 가능 | `core/CMakeLists.txt:32, 35-37` |
| **`core/JUCE/` 정정 (v1 사실 오류 수정):** 디스크상 unpacked JUCE 7.x tree (87 MB), `CMakeLists.txt` (7718 B) + `modules/` + `extras/` + `LICENSE.md` + `examples/` + `docs/` 등 **모두 present**. `git ls-files core/JUCE/` = **0** (`.gitignore:13` 의 `core/JUCE/` 라인으로 추적 제외). `juce_audio_plugin_client/` 모듈 부트스트랩됨 (`AAX/`, `AU/`, `detail/`, VST3 source files 포함). | `ls core/JUCE/`, `du -sh core/JUCE`, `git ls-files core/JUCE/`, `.gitignore:12-13` |
| **vst3sdk JUCE 번들 (C-H1 결론):** `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` 디렉토리 존재 — `base/`, `pluginterfaces/`, `helper.manifest`, `JUCE_README.md`, `LICENSE.txt`. **JUCE bootstrap 만으로 vst3sdk 동시 확보**. 별도 FetchContent 불필요. | `ls core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` |
| **JUCE 7 EULA tier 시스템 (H2):** Personal ($0, <50K USD revenue), Indie ($40/mo, <500K), Pro ($130/mo, no limit), Educational ($0, bona fide edu inst), GPLv3 fallback. `juce_audio_basics`/`devices`/`core`/`events` ISC. **`juce_audio_plugin_client` 는 EULA 적용**. | `core/JUCE/LICENSE.md:1-30` |
| **Phase B 가 이미 JUCE 부분 wiring 완료 (C-M1):** `SPE_HAVE_JUCE` 가 `core/JUCE/CMakeLists.txt` 존재 시 자동 ON; `juce_audio_basics/devices/formats/core/data_structures/dsp/events/osc` 이미 link. **C2 는 신규 부트스트랩이 아니라 `juce_audio_plugin_client` 모듈 추가 + `juce_add_plugin` macro**. | `core/CMakeLists.txt:32, 35-43, 137-149` |
| **pluginval 미설치 (H3):** `which pluginval` → not found. Tracktion/pluginval 자체 빌드 step 필수. JUCE 의존이라 JUCE 부트스트랩 후 빌드 가능. | `which pluginval` 실행 결과 없음 |
| `SpatialEngine` ctor: `explicit SpatialEngine(int listen_port = 0)` | `core/src/core/SpatialEngine.h:35` |
| `prepareToPlay(double, int)` 에서 모든 alloc | `core/src/core/SpatialEngine.h:42` |
| **존재하는 public setter (C-H3 검증):** `setLayout`, `setTransportPlay`, `setLtcChaseEnable` **3개만**. `setMasterGain`/`setObj0Az/El`/`setAmbiOrder`/`RoomPreset` 류 setter **부재**. | `grep "void set" core/src/core/SpatialEngine.h` |
| **호스트→코어 control plane 패턴:** `ExternalControl::dispatch(Command const&)` (`core/src/ipc/ExternalControl.h:14`) → `cmd_fifo_` enqueue → audio thread drain → `obj_cache_` / `qc.ambi_order` 업데이트 (`SpatialEngine.cpp:76, 287`). VST3ControlStub 가 이 인터페이스의 no-op 구현. | `core/src/ipc/ExternalControl.h`, `core/src/core/SpatialEngine.cpp:76, 287` |
| `CommandFifo<1024>`, POD QueuedCmd | `core/src/util/CommandFifo.h` (C1.b 확장) |
| 새 opcode `SysLtcChase = 0x14` 안전 등록 | `core/src/ipc/Command.h:38` |
| `ObjDsp = 0x60` 점유 → C2 가 새 opcode 추가 시 0x55–0x5F 가용 | `core/src/ipc/Command.h:65` |

**핵심 결과:**
- (1) OFF-path byte-identical 보장 가능 (`if(SPATIAL_ENGINE_VST3)` 격리)
- (2) JUCE 부트스트랩 = vst3sdk 부트스트랩 (단일 채널)
- (3) `SpatialEngine` 의 6 파라미터 setter **0/6 존재** → 호스트 파라미터 매핑은 **`ExternalControl::dispatch(Command)` 단일 경로**
- (4) JUCE EULA tier 결정 미정 → C2-Q8 신설
- (5) pluginval 미설치 → Step 0 자체 빌드 추가

---

## 1. Principles (Phase C → C2 specialise)

> Phase C 5 Principles (`spatial-engine-phaseC.md:41-46`) 의 C2 specialisation.

1. **Hardware-free first (specialised: pluginval-on-CI).** Linux 헤드리스 CI 에서 `pluginval --strictness-level 5` 자동. DAW 실행은 개발자 confirm-only. **pluginval 도 자체 빌드 (배포 prebuilt 부재).**
2. **Test gates 정량 + 정직 회계.** 96 + 40 + 6 + 2 = ~145 assertion, tol 1e-6 / 1% / 0 alloc / sha256. **새 pytest skip 도입 금지** (Phase C effective-skip cap 3).
3. **Single-responsibility 의 명시적 예외 — fused C2.a~d.** Phase C ADR 인정 예외 (`spatial-engine-phaseC.md:44`). **Step 0 (bootstrap) 은 분리 commit** `chore(C2-bootstrap): JUCE+pluginval wiring` (M4).
4. **No NDEBUG strip regression.** 신규 5 ctest 모두 `-UNDEBUG` 자동 검증 (`scripts/check_test_ndebug.sh`).
5. **OFF-path byte-identical 불변량.** **byte-identical 정의 (Tension #1):** `libspe_core.a` + `spatial_engine_core` binary 만 비교; `CMakeCache.txt`/build dir state/timestamp 제외.

---

## 2. Decision Drivers (top 3, v2 재서술)

1. **OFF-path byte-identical 불변량 (Driver #1).** `if(SPATIAL_ENGINE_VST3)` 격리 + `vst3/` 서브트리만 변경. `core/src/core/SpatialEngine.{h,cpp}` 와 `core/CMakeLists.txt` 의 `SPE_CORE_SOURCES`/`SPE_HAVE_JUCE` 블록 변경 금지. CI sha256 게이트 강제 (정의: `libspe_core.a` + `spatial_engine_core`).
2. **RT 스레드 안전성 + control plane 단일화 (Driver #2, C-H3 강화).** SpatialEngine setter 부재 인정 → 호스트 파라미터는 `parameterChanged` (control thread) → `Command` 빌드 → cmd_fifo enqueue → audio thread drain. `processBlock` (audio thread) 은 APVTS atomic load 만; `dispatch` 호출 금지.
3. **JUCE 단일 의존성 채널 (Driver #3, C-H1 재서술).** JUCE 7 이 vst3sdk 를 자기 모듈 안에 번들 (`core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/`) → 단일 부트스트랩 step 으로 두 의존성 동시 확보. CI 캐시도 한 디렉토리. EULA tier 결정 (C2-Q8) 별개 트랙.

---

## 3. Viable Options (≥2)

### Option A — JUCE 통합 (`juce_add_plugin` + APVTS) — **권장**

**개요:** `juce_audio_plugin_client` 모듈 + `juce_add_plugin` CMake macro. `SpatialEngineProcessor : juce::AudioProcessor` + APVTS 6 param. 호스트→코어: APVTS atomic store → `parameterChanged` (control thread) → `Command` → cmd_fifo enqueue → audio thread drain.

**Pros:**
- JUCE 가 vst3sdk 번들 → 단일 부트스트랩 (Driver #3)
- APVTS atomic + automation 검증된 코드 → pluginval strictness 5 통과 사례 다수
- Generic editor 무료 (`juce::GenericAudioProcessorEditor`)
- `getStateInformation` ValueTree XML 자동 직렬화 → bit-stable
- `juce_add_plugin` 자동 entry symbol/factory wiring → `vst3/SpatialEngineVST3.cpp` **삭제** (C-H2, C-L1)
- Phase B 가 이미 8 JUCE 모듈 link → 추가 dep 1개만 (C-M1)

**Cons:**
- JUCE 7 EULA tier 결정 필요 (C2-Q8). SNU 이메일 → Educational 자연스러움 — confirm 필요
- VST3=ON 빌드 JUCE 의존 → CI 별도 job (NO_JUCE=ON 메인 영향 없음)
- `juce::AudioBuffer<float>` ↔ `spe::audio_io::AudioBlock` 어댑터 layer 필요
- 빌드 시간 ↑ (캐시로 분할 상환)

**Bounded scope:** Phase D6 (full custom editor) 까지 generic UI; APVTS 유지.

### Option B — vst3sdk 직접 (JUCE 우회) — 보존 (D+2 fallback)

**개요:** `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` 만 직접 include. `IPluginFactory`/`IComponent`/`IAudioProcessor`/`IEditController` hand-roll.

**Pros (v2 재검토):**
- JUCE EULA 회피 가능 (단, VST3 SDK 자체는 GPLv3 또는 Steinberg agreement 별도 — C-M2)
- `spe_core` JUCE link 추가 없음 (Phase B 의 link 는 별도)
- 정밀 제어 (어댑터 layer 불필요)

**Cons (v2 재검토, 강화):**
- **Critic 정정:** vst3sdk 가 이미 JUCE 모듈 안에 번들 → "별도 fetch 절감" 이점 **사라짐**
- 6 param boilerplate ~150 LoC × 6 + GUI binding (VSTGUI 별도) → 6–7d 빠듯
- pluginval strictness 5 hand-roll 디버깅 사이클 ↑ (D+12 위협)
- `getStateInformation` `IBStream` 자체 직렬화
- 프로젝트 컨벤션 (이미 8 JUCE 모듈) 어긋남

**Bounded scope:** Phase D6 시 결국 vst3sdk 직접 다루게 됨 → Option B boilerplate 재사용 가능. C2 시점 시간 비용 압도적.

### Option C — Hybrid (APVTS + 자체 OSC bridge) — 기각 유지

기각 이유: 두 control plane 공존 → bug surface 증가; cmd_fifo 는 sample-accurate 아님. v1 결정 v2 유지.

### **권장: Option A. Option B 재활성 트리거: D+2 까지 JUCE 부트스트랩 CI 통과 실패 또는 JUCE EULA 결정 NO-GO.**

---

## 4. Pre-mortem (3 시나리오, D+12)

### 시나리오 1 — pluginval strictness 5 "Parameter automation timing" fail

**무엇이 잘못됐나:** block-rate cache update 구조라 1 block 안 sample-accurate ramp 무시.

**조기 신호:**
- `test_vst3_automation_offline.cpp` 5-frame VBAP ref 1% 초과
- pluginval strictness 4 통과 / 5 timing fail
- **M2 추가 (v2):** Step 2 (D+5) Exit 의 `--strictness-level 5 --skip-gui-tests` smoke run 에서 timing 섹션 fail 조기 노출

**회피:**
- `parameterChanged` (control thread) → cmd_fifo enqueue → audio thread block-start drain — block-rate parameter 만 지원
- sample-accurate Phase D6 deferral
- **fallback:** strictness 4 클린 시 D+12 통과 인정 합의 시도; 실패 시 C4 → D8 강등 + Phase D6 부채 회계 (C-L2)

### 시나리오 2 — JUCE EULA NO-GO 또는 vst3sdk LICENSE 충돌

**무엇이 잘못됐나:** Educational tier 가 향후 상업화와 충돌 또는 VST3 SDK GPLv3 조항이 spatial_engine 라이선스 (미정) 와 충돌.

**조기 신호:**
- 사용자 EULA confirm D+1 까지 미회수
- `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt` 검토 시 GPLv3 vs Steinberg agreement 선택 강제

**회피:**
- C2 D+0 에 C2-Q8 사용자 의사 확인 — 미회수 시 Educational 가정 진행
- ADR Consequences 에 "VST3 SDK GPLv3 또는 Steinberg agreement" 명시 (C-M2)
- NO-GO → Option B 활성 (JUCE EULA 회피; VST3 SDK 라이선스는 동일하게 GPLv3/Steinberg 택일)
- 상업화: `spatial-engine-commercialization-v1.md` 와 정합 확인

### 시나리오 3 — `SPATIAL_ENGINE_VST3=OFF` byte-identical 깨짐

**무엇이 잘못됐나:** `SpatialEngine.h` friend decl 추가 또는 `SPE_CORE_SOURCES` 에 VST3 파일 추가.

**조기 신호:**
- 매 sub-step CI sha256 mismatch
- `git diff main..HEAD -- core/src/core/SpatialEngine.h core/src/core/SpatialEngine.cpp core/CMakeLists.txt` 비어있지 않음

**회피:**
- C-H3 결론: 호스트→코어 통신은 `ExternalControl::dispatch(Command)` + (Step 2 작업 1 의 OSC loopback 우회) 경유 → 새 setter 추가 0
- M1 sha256 baseline:
  ```bash
  cmake -B build_off_baseline -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON
  make -C build_off_baseline spe_core spatial_engine_core -j$(nproc)
  sha256sum build_off_baseline/libspe_core.a build_off_baseline/spatial_engine_core > .ci/off_baseline.sha256
  ```
- 저장 위치: `.ci/off_baseline.sha256` 체크인 (CI artifact 도 가능; 체크인이 단순; 합의 대상)

---

## 5. 확장 테스트 플랜 (Deliberate)

### Unit (ctest, `-UNDEBUG`)

| 파일 (절대경로) | 검증 케이스 | Assertion |
|---|---|---|
| `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_param_roundtrip.cpp` | 6 param × 16 normalized [0, 1/15, …, 1] → APVTS set/get tol 1e-6 | 96 |
| `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_automation_offline.cpp` | az 0→1 ramp 48000 samples, frames {0, 12000, 24000, 36000, 47999} → VBAP analytic ref ≤ 1% per channel (8ch) | 40 |
| `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_state_persist.cpp` | 6 param × random init → `getStateInformation`/`setStateInformation` bit-identical (APVTS XML schema version=1, L2) | 6 |
| `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_bypass.cpp` | bypass=true input==output / bypass=false 정상 | 2 |
| `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_off_byte_identical.cpp` | CMake fixture: `.ci/off_baseline.sha256` vs branch sha256 | 1 |

**총:** 5 파일 / ~145 assertion / 새 pytest skip 0.

### Integration

- **pluginval Linux:** `scripts/_build/pluginval/build/pluginval --strictness-level 5 --skip-gui-tests --validate-in-process <vst3_path>` clean exit (M3: long form 통일)
- **OFF-build sha256:**
  ```bash
  cmake -B build_off -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON
  make -C build_off spe_core spatial_engine_core -j$(nproc)
  sha256sum build_off/libspe_core.a build_off/spatial_engine_core | diff - .ci/off_baseline.sha256
  ```
- **Latency harness:** `python3 latency_harness/run_dryrun.py` p99 ≤ 4.68 ms.

### E2E (옵션, confirm-only)

- Reaper Linux 7.x: `spatial_engine.vst3` insert → 6 param 노출 → automation lane → 8ch master WAV bounce
- Bitwig Studio Linux: 동일 (Bitwig thread-safety 더 엄격)
- **C2 합격 조건 아님.** Phase D6 정식 게이트.

### Observability

| 신호 | 어디 | 감지 방법 |
|---|---|---|
| Audio-thread alloc | `processBlock` / `engine_->audioBlock` | `RT_ASSERT_NO_ALLOC` (기존). 1000 회 호출 alloc==0 |
| Audio-thread lock | 동일 | C2 lock 미사용; APVTS atomic load 만 |
| Parameter ramp 불연속 | block 경계 | `test_vst3_automation_offline.cpp` 인접 frame Δ ≤ block_rate × max_delta |
| Latency 보고 | host status | `setLatencySamples(N)` → `getLatencySamples()==N`. **Q6 결정 (C-M3, Step 2 entry):** N=0 보고; 동적 propagation 은 Phase D6 (D6.a) |
| OFF-path 오염 | CMake | sha256 mismatch → CI red |

---

## 6. 구현 시퀀스 (5 step, fused C2.a~d + 분리 Step 0)

### Step 0 — Bootstrap (≤ 1d, **분리 commit**, M4)

> M4: Step 0 만 분리 commit `chore(C2-bootstrap): JUCE 7.0.12 wiring + pluginval`. C2.a~d 만 fused. `core/JUCE/` 자체는 gitignored 라 commit 대상 아님 — bootstrap 스크립트 + CI yaml 만.

**Entry:** Phase C C1.d 머지 `main` HEAD `c2c1a79`. C2-Q8 (EULA) 사용자 confirm 회수 진행 중.

**Step 0.1 — `scripts/bootstrap_juce.sh` (idempotent, C-M4 spec):**

```bash
#!/usr/bin/env bash
set -euo pipefail
TARGET=core/JUCE
if test -f "$TARGET/CMakeLists.txt"; then
  echo "JUCE already bootstrapped at $TARGET"
  exit 0
fi
git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git "$TARGET"
```

**Step 0.2 — `scripts/bootstrap_pluginval.sh` (H3, idempotent):**

```bash
#!/usr/bin/env bash
set -euo pipefail
TARGET=scripts/_build/pluginval
if test -x "$TARGET/build/pluginval"; then
  echo "pluginval already built at $TARGET/build/pluginval"
  exit 0
fi
git clone --depth 1 https://github.com/Tracktion/pluginval.git "$TARGET"
cmake -B "$TARGET/build" -S "$TARGET" -DCMAKE_BUILD_TYPE=Release
cmake --build "$TARGET/build" -j"$(nproc)"
test -x "$TARGET/build/pluginval"
```

**Step 0.3 — CI VST3=ON job 추가:**
- 별도 job (`.github/workflows/vst3.yml` 신설 vs `.pre-commit-config.yaml` 로컬 hook — C2-Q5 결정 미정, 기본 가정: 신설)
- 캐시 키: `juce-7.0.12-${{ hashFiles('scripts/bootstrap_juce.sh') }}`, `pluginval-${{ hashFiles('scripts/bootstrap_pluginval.sh') }}`

**Step 0.4 — Sanity build:**
- `cmake -B build_vst3 -DSPATIAL_ENGINE_VST3=ON && make -C build_vst3` 그린. factory 여전히 `nullptr` (C2.a 미완) 이지만 link 통과.

**Step 0.5 — OFF baseline sha256 저장 (M1):**
- `.ci/off_baseline.sha256` 생성 + 체크인

**Exit:**
- 로컬 + CI 양쪽 `cmake -DSPATIAL_ENGINE_VST3=ON` 그린
- `cmake -DSPATIAL_ENGINE_VST3=OFF` sha256 unchanged (baseline 동시 생성)
- `scripts/_build/pluginval/build/pluginval --version` 통과
- **분리 commit `chore(C2-bootstrap)` 작성**

**조기 stop:** D+2 까지 CI 그린 실패 → **Option B 재평가** 트리거.

### Step 1 — C2.a: `juce_add_plugin` + `SpatialEngineProcessor` + APVTS 6 params (1.5d)

**Entry:** Step 0 그린 + commit 머지.

**작업:**
1. **기존 `vst3/CMakeLists.txt` 단일 치환 (C-H2):**
   - 제거: `add_library(spatial_engine_vst3 SHARED SpatialEngineVST3.cpp)` + `set_target_properties(... PREFIX "" SUFFIX ".vst3")` + `VST3_SDK_DIR` if-블록 (`vst3/CMakeLists.txt:17, 20-26, 28-34`)
   - 추가: `juce_add_plugin(spatial_engine_vst3 FORMATS VST3 PRODUCT_NAME "Spatial Engine" PLUGIN_MANUFACTURER_CODE "SpEn" PLUGIN_CODE "Spe1" COMPANY_NAME "spatial_engine" BUNDLE_ID "com.spatial_engine.vst3")`
   - `juce_add_plugin` 자동 entry/factory/bundle 생성 → `SpatialEngineVST3.cpp` **DELETE** (C-L1)
2. 신규 `vst3/SpatialEngineProcessor.hpp/.cpp`:
   - `class SpatialEngineProcessor final : public juce::AudioProcessor`
   - 멤버: `std::unique_ptr<spe::core::SpatialEngine> engine_` (ctor `listen_port=0` MVP, 단 Step 2 작업 1 의 OSC loopback 채택 시 `listen_port=auto` 로 변경)
   - 멤버: `juce::AudioProcessorValueTreeState apvts_`
   - 6 param 등록:
     - `pan_az` (NormalisableRange [-π, π], default 0)
     - `pan_el` (NormalisableRange [-π/2, π/2], default 0)
     - `source_width` (NormalisableRange [0, π], default 0)
     - `master_gain` (NormalisableRange [-60, 6] dB, default 0)
     - `ambi_order` (Choice [1, 2, 3], default 1)
     - `room_preset_idx` (Choice [0..3], default 0 — MVP enum, audio 영향 없음)
3. `prepareToPlay(double, int)` → `engine_->prepareToPlay(sr, block)`
4. `processBlock` Step 1 silence-pass (실 호출 Step 2)
5. `createEditor()` → `juce::GenericAudioProcessorEditor(*this)`

**Exit:**
- `test_vst3_param_roundtrip.cpp` 96 assertion PASS
- `cmake -DSPATIAL_ENGINE_VST3=OFF` sha256 baseline 일치
- pluginval 미실행

**파일 변경:**
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineVST3.cpp` — **DELETE** (`juce_add_plugin` 자동 entry)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.hpp` (NEW)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` (NEW)
- `/home/seung/mmhoa/spatial_engine/vst3/CMakeLists.txt` — REWRITE (35 → ~25 LoC `juce_add_plugin` 기반)
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_param_roundtrip.cpp` (NEW)

### Step 2 — C2.b: `processBlock` thread-safe + control plane 단일화 + `setLatencySamples(0)` (1.5d)

**Entry:** Step 1 그린 + **C2-Q6 결정 완료 (C-M3): N=0 보고**.

**작업:**

1. **호스트→코어 control plane 결정 (C-H3 핵심):**

   - `parameterChanged(parameterID, newValue)` 콜백 (control thread) 에서 6 param 각각 `Command` 빌드 후 엔진으로 dispatch.
   - **문제:** `SpatialEngine` 의 `cmd_fifo_` 는 private; `dispatch(Command)` public method 부재. 새 public method 추가는 **Driver #1 침범 (sha256 변경 위험)**.
   - **두 우회 옵션:**
     - **(a)** Driver #1 완화 합의: `void dispatchCommand(spe::ipc::Command const&)` 추가 (`SpatialEngine.h` 1줄 + `.cpp` 1줄, ~50 byte 추가) → sha256 baseline 갱신 (체크인된 `.ci/off_baseline.sha256` 한 번 업데이트)
     - **(b)** OSC loopback 우회: `SpatialEngineProcessor` 가 자체 `OSCBackend` UDP loopback 클라이언트 구성 (`engine_` 가 `listen_port>0` 로 ctor) → `parameterChanged` 가 OSC 패킷 송신 → `engine_->osc_backend_` 수신 → cmd_fifo enqueue. 추가 코드 ~30 LoC, **`SpatialEngine.h` 미수정 → byte-identical 자연 보존**. UDP loopback 비용 ~5 µs/cmd (control thread, 무시).
   - **권장: (b) OSC loopback** — Driver #1 보호 + sha256 baseline 갱신 회피. UDP localhost 는 kernel buffer 로 처리 → 안정적.

2. `processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)`:
   - APVTS atomic load → 내부 cache (audio thread 안)
   - `juce::AudioBuffer<float>` ↔ `spe::audio_io::AudioBlock` 어댑터 (`vst3/AudioBlockAdapter.hpp`)
   - `engine_->audioBlock(adapted_block)` 호출
   - `engine_->updateLtcChase()` 호출 (control-rate tick — host audio-thread 에서 호출 안전: `updateLtcChase` 가 lock-free)
3. `setLatencySamples(0)` (Q6 결정)
4. `RT_ASSERT_NO_ALLOC` 1000 회 호출 alloc 0
5. **M2: pluginval `--strictness-level 5 --skip-gui-tests` smoke run (failure 허용, 결과 로그)** — sample-accurate 이슈 조기 노출

**Exit:**
- `test_vst3_automation_offline.cpp` 40 assertion PASS
- `RT_ASSERT_NO_ALLOC` 0
- pluginval smoke run 결과 로그 (PASS 또는 timing 섹션 fail 노출)
- `SPATIAL_ENGINE_VST3=OFF` sha256 == baseline (우회 (b) 채택 시 자연 보존)

**파일 변경:**
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` (parameterChanged + processBlock 본문)
- `/home/seung/mmhoa/spatial_engine/vst3/AudioBlockAdapter.hpp` (NEW)
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_automation_offline.cpp` (NEW)

### Step 3 — C2.c: bypass + parameterChanged host notification (1d)

**Entry:** Step 2 그린.

**작업:**
1. `processBlock` 시작에서 `if (isBypassed()) { passthrough; return; }`
2. `parameterChanged` 동작 검증 (APVTS 자동, 테스트 작성)
3. `test_vst3_bypass.cpp` 작성

**Exit:**
- `test_vst3_bypass.cpp` 2 assertion PASS
- **M2 격상:** pluginval `--strictness-level 5 --skip-gui-tests` (Step 3 끝) — strictness 5 클린이면 D+9 통과 안전; 5 fail / 4 클린이면 D+9 fallback 검토.

**파일 변경:**
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` (bypass)
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_bypass.cpp` (NEW)

### Step 4 — C2.d: `getStateInformation`/`setStateInformation` + final pluginval strictness 5 (2d)

**Entry:** Step 3 그린.

**작업:**
1. `getStateInformation(juce::MemoryBlock&)`: `apvts_.copyState().createXml() → writeToStream → MemoryBlock`
2. `setStateInformation(const void*, int)`: 역방향
3. **L2: APVTS state schema versioning** — `<spatial_engine_apvts version="1">` root attribute; v2 마이그레이션 시 lookup
4. Round-trip ctest: `test_vst3_state_persist.cpp` (6 random init → bit-identical)
5. **pluginval `--strictness-level 5 --skip-gui-tests --validate-in-process` clean exit** (final)
6. OFF byte-identical sha256 baseline 일치

**Exit (= C2 합격):**
- 5 ctest PASS
- pluginval --strictness-level 5 clean exit
- `SPATIAL_ENGINE_VST3=OFF` sha256 == baseline
- Latency harness p99 ≤ 4.68 ms

**파일 변경:**
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` (state 직렬화)
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_state_persist.cpp` (NEW)
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_off_byte_identical.cpp` (NEW; CMake fixture)

### Fused commit (C2.a~d 만)

```
feat(C2): VST3 MVP — pluginval strictness 5 클린 + APVTS 6 params + state persist

Phase C ADR (Principle 3 exception) 따라 C2.a~d 4 sub-step 단일 커밋 묶음.
Step 0 (bootstrap) 은 분리 commit `chore(C2-bootstrap)` (M4).

- C2.a: juce_add_plugin + SpatialEngineProcessor + APVTS 6 params
- C2.b: processBlock thread-safe + OSC-loopback control plane (Driver #1 보호)
- C2.c: bypass + parameterChanged
- C2.d: get/setStateInformation (APVTS XML schema v1) + pluginval strictness 5

Test: 5 ctest 145 assertion / pluginval --strictness-level 5 clean / OFF byte-identical sha256.
```

### 조기 정지 + Fallback 게이트

| 시점 | 조건 | 액션 |
|---|---|---|
| D+1 | C2-Q8 EULA confirm 회수 안 됨 | Educational 가정 진행, ADR Consequences 명시 |
| D+2 | Step 0 CI 빨강 | **Option B 재평가** (1d 안 결정) |
| D+5 | `RT_ASSERT_NO_ALLOC` 0 미달성 또는 pluginval smoke run timing fail | Architect 재투입 — block-rate vs sample-accurate 재검토 |
| D+9 | Step 3 끝 pluginval strictness 5 fail | strictness 4 fallback 합의 시도 (Phase D6 부채, C-L2); 실패 시 C4 → D8 (Phase C ADR Option C) |
| D+12 | pluginval still red | **무조건 C4 강등** + Phase C 종료 = C1 + C2(strictness 4) + C3 |

---

## 7. 파일 변경 목록 (절대경로 + 1줄 목적)

### 신규 (NEW)

- `/home/seung/mmhoa/spatial_engine/scripts/bootstrap_juce.sh` — JUCE 7.0.12 idempotent fetch (C-M4)
- `/home/seung/mmhoa/spatial_engine/scripts/bootstrap_pluginval.sh` — Tracktion/pluginval idempotent build (H3)
- `/home/seung/mmhoa/spatial_engine/.ci/off_baseline.sha256` — OFF baseline 체크인 (M1)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.hpp` — `juce::AudioProcessor` 인터페이스
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` — Processor 구현 (APVTS, processBlock, state, OSC-loopback control)
- `/home/seung/mmhoa/spatial_engine/vst3/AudioBlockAdapter.hpp` — `juce::AudioBuffer<float>` ↔ `spe::audio_io::AudioBlock`
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_param_roundtrip.cpp` — 96 normalized roundtrip
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_automation_offline.cpp` — 5×8 frame VBAP ref 1%
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_bypass.cpp` — bypass passthrough
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_state_persist.cpp` — state save/restore
- `/home/seung/mmhoa/spatial_engine/core/tests/test_vst3_off_byte_identical.cpp` — sha256 fixture

### 수정 (MODIFY)

- `/home/seung/mmhoa/spatial_engine/vst3/CMakeLists.txt` — REWRITE: `add_library` 제거 → `juce_add_plugin` (C-H2)
- `/home/seung/mmhoa/spatial_engine/core/tests/CMakeLists.txt` — 5 ctest 등록 + `-UNDEBUG`
- `/home/seung/mmhoa/spatial_engine/.github/workflows/vst3.yml` (NEW or MODIFY; C2-Q5 결정 따라)

### 삭제 (DELETE)

- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineVST3.cpp` — `juce_add_plugin` 자동 entry → 불필요 (C-L1)

### 변경 금지 (Driver #1 보호)

- `/home/seung/mmhoa/spatial_engine/core/src/core/SpatialEngine.h` — **변경 금지**
- `/home/seung/mmhoa/spatial_engine/core/src/core/SpatialEngine.cpp` — **변경 금지**
- `/home/seung/mmhoa/spatial_engine/core/CMakeLists.txt` 의 `SPE_CORE_SOURCES` + `SPE_HAVE_JUCE` 블록 — **변경 금지**

> 호스트→코어 파라미터 전달은 OSC loopback 우회 (b) → `SpatialEngine.h` 미수정 (Step 2 작업 1)

---

## 8. ADR 초안

> 라운드 2 합의 후 ADR 본문 `.omc/plans/spatial-engine-phaseC.md` 의 ADR-C2 sub-section 에 머지.

**Decision:** **Option A (JUCE 통합)** 채택. JUCE 7.0.12 + `juce_audio_plugin_client` + `juce_add_plugin` macro + APVTS 6 파라미터. 호스트→코어 파라미터 전달은 **OSC loopback 우회** (Driver #1 보호). pluginval strictness 5 final gate. fused C2.a~d + 분리 Step 0 commit. fallback D+9/D+12.

**Drivers:**
1. **OFF-path byte-identical 불변량** — `if(SPATIAL_ENGINE_VST3)` 격리. byte-identical 정의: `libspe_core.a` + `spatial_engine_core` 만 (build dir state 제외).
2. **RT 스레드 안전성 + control plane 단일화** — APVTS atomic load (audio cache) + `parameterChanged` (control thread) → OSC loopback → cmd_fifo → audio thread drain. SpatialEngine setter 부재 인정, 새 public API 0 (Driver #1 자연 보호).
3. **JUCE 단일 의존성 채널** — JUCE 7 이 vst3sdk 번들 → 단일 부트스트랩. Phase B 가 이미 8 JUCE 모듈 link → 추가 dep 1개.

**Alternatives considered:**
- **Option B (vst3sdk 직접)** — vst3sdk 가 JUCE 모듈 안 번들 → "별도 fetch 절감" 이점 없음. 6 param boilerplate ~150 LoC × 6 + GUI binding + pluginval 5 hand-roll → 6–7d 빠듯. JUCE EULA 회피 가능하나 VST3 SDK GPLv3/Steinberg 별도 라이선스 필요. D+2 bootstrap 실패 시 재활성.
- **Option C (Hybrid)** — 두 control plane 공존 → bug surface 증가. 기각.

**Why chosen:**
- JUCE 가 vst3sdk 번들 → 단일 부트스트랩 (Driver #3)
- Phase B 가 이미 JUCE 부분 wiring → 추가 dep 1개 (C-M1)
- pluginval strictness 5 통과 사례 압도적 → D+12 통과 가능성 ↑
- OSC loopback 우회 → SpatialEngine.h 변경 0 → byte-identical 자연 보존
- Phase D6 시 APVTS 유지 + editor 만 교체 가능

**Consequences:**
- VST3=ON 빌드 JUCE 의존 → CI 별도 job; NO_JUCE=ON 메인 영향 없음
- pluginval strictness 5 가 game change → Phase D 부담
- APVTS XML state schema (root version="1") 가 schema → 향후 호환성 유지
- C2 fused commit 이 Phase C ADR Principle 3 exception 활용 → 후속 single-responsibility 복귀
- **JUCE 7 EULA tier 결정 필요 (C2-Q8). SNU 이메일 → Educational tier 자연스러움 — confirm 필요. 미정 시 C2 진행 차단.** (H2)
- **VST3 SDK 라이선스: JUCE 번들 SDK 도 GPLv3 또는 Steinberg proprietary agreement. Educational tier 채택 시 GPLv3 fallback 자동 회피 가능.** (C-M2)
- D+9/D+12 fallback strictness 4 = **Phase D6 부채로 회계** (C-L2). C4 → D8 (Option C)
- OSC loopback 우회 → control rate ~5 µs 비용 (control thread)
- pluginval 자체 빌드 step 추가 → CI 캐시로 분할 상환 (H3)

**Follow-ups (Phase D):**
- **D6** — Full custom VST3 editor (JUCE Component or VSTGUI). Generic editor 제거.
- **D6.x** — 다중 obj 매핑 (현재 obj 0 만)
- **D6.y** — sample-accurate parameter automation; pluginval timing 재검증
- **D6.z** — APVTS state schema v2 마이그레이션 (필요 시)
- **D6.a** — Q6 재방문: dynamic propagation latency reporting

---

## 9. 수락 기준 체크리스트 (v2)

### 정량 게이트

- [ ] **Step 0:** `bootstrap_juce.sh` + `bootstrap_pluginval.sh` 로컬 + CI 양쪽 idempotent 통과; `cmake -DSPATIAL_ENGINE_VST3=ON` 그린; `pluginval --version` 통과; `.ci/off_baseline.sha256` 생성 + 체크인
- [ ] **C2.a:** `test_vst3_param_roundtrip.cpp` 96 assertion / tol 1e-6 PASS
- [ ] **C2.b:** `test_vst3_automation_offline.cpp` 5 frame × 8ch = 40 assertion / 1% per channel PASS
- [ ] **C2.b:** `RT_ASSERT_NO_ALLOC` `processBlock` 1000 회 alloc 0
- [ ] **C2.b smoke:** `pluginval --strictness-level 5 --skip-gui-tests` smoke 결과 로그 (M2)
- [ ] **C2.c:** `test_vst3_bypass.cpp` 2 assertion PASS
- [ ] **C2.c:** `pluginval --strictness-level 5 --skip-gui-tests` 클린 (Step 3 끝, M2 격상)
- [ ] **C2.d:** `test_vst3_state_persist.cpp` 6 assertion PASS (APVTS XML schema v1, L2)
- [ ] **C2.d:** `pluginval --strictness-level 5 --skip-gui-tests --validate-in-process` clean exit (final)
- [ ] **OFF-path byte-identical:** `cmake -B build_off -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON && make -C build_off spe_core spatial_engine_core && sha256sum build_off/libspe_core.a build_off/spatial_engine_core | diff - .ci/off_baseline.sha256` exit 0
- [ ] **Latency regression 0:** p99 ≤ 4.68 ms, xruns 0
- [ ] **NDEBUG enforce:** `bash scripts/check_test_ndebug.sh` 신규 5 ctest 모두 `-UNDEBUG`

### 정성 게이트

- [ ] `git diff main..HEAD -- core/src/core/SpatialEngine.h core/src/core/SpatialEngine.cpp` 빈 출력
- [ ] `git diff main..HEAD -- core/CMakeLists.txt` 가 `SPE_CORE_SOURCES` / `SPE_HAVE_JUCE` 블록 미변경
- [ ] **분리 commit 구조:** `chore(C2-bootstrap)` 1개 + `feat(C2)` 1개 (M4)
- [ ] fused commit 메시지가 Principle 3 exception 명시
- [ ] No new pytest skip — effective skip ≤ 3 유지
- [ ] **C2-Q8 (EULA) 결정 commit message 또는 ADR 에 기록** (H2)
- [ ] **C2-Q6 (latency reporting) 결정** Step 2 entry 시점 commit message 명시 (C-M3)
- [ ] D+9 fallback 게이트 컨설트 기록 (commit/PR description)

### CI 워크플로

- [ ] CI 메인 job (`SPATIAL_ENGINE_NO_JUCE=ON`) green 유지
- [ ] CI 별도 job `vst3-build-and-pluginval` 추가 — JUCE 캐시 + pluginval 캐시
- [ ] CI 별도 job `off-byte-identical` 추가 — `.ci/off_baseline.sha256` 비교

---

## 10. Open Questions (라운드 2 검토용)

1. **C2-Q1:** JUCE 7.0.12 vs 7.0.x latest? Phase B 검증 minor 확인.
2. **C2-Q2:** MVP obj 0 만 노출 — Phase D6 와 충돌? (현재 무충돌 가정)
3. **C2-Q3:** Generic editor 라벨 polish C2 포함 vs Phase D6?
4. **C2-Q4:** D+9 strictness 5 fail → strictness 4 vs C4 → D8? (Phase C ADR Option C 와 상호작용)
5. **C2-Q5:** CI VST3=ON job venue — `.github/workflows/vst3.yml` vs `.pre-commit-config.yaml` (현재 `.github/workflows/` 부재; 가정 신설)
6. **C2-Q6 ✓ (v2 결정, C-M3):** `setLatencySamples(0)`, dynamic Phase D6 (D6.a)
7. **C2-Q7:** D+2 bootstrap 실패 시 Option B 재활성화 의사결정자 — Architect 단독 vs Architect+Critic?
8. **C2-Q8 ✓ (2026-05-06 결정, H2):** **JUCE 7 Educational tier 채택** (SNU 학술 사용). VST3 SDK 는 JUCE 번들 그대로 — Educational tier 가 GPLv3 fallback 자동 회피. C2 진행 전제 해소.

---

## 11. L 항목 (선택 반영)

- **L1:** Step 3+4 부분 병렬화 → 1.5d 절감 (멀티 dev 시 활성)
- **L2:** APVTS XML schema versioning `<spatial_engine_apvts version="1">` — **Step 4 작업 3 반영 완료**
- **C-L1:** `nullptr` factory 표현 정정 → §7 `SpatialEngineVST3.cpp` DELETE 로 해소
- **C-L2:** strictness 4 fallback = "Phase D6 부채" — §8 ADR Consequences 명시 완료
- **Tension #1:** byte-identical 정의 → §1 Principle 5 + §5 Test plan 명시 완료

---

## 12. v1 → v2 차이 요약 (라인 100자 이내)

- **Inv 정정 (H1):** `core/JUCE/` "docs 10개" 오류 → 87M unpacked, gitignored, CMake/modules/LICENSE present
- **Driver #3 재서술 (C-H1):** vst3sdk 별도 fetch 불필요 — JUCE 7 모듈 안에 번들
- **Step 0 분리 commit (M4):** `chore(C2-bootstrap)` 별도, `feat(C2)` 만 fused
- **pluginval 자체 빌드 (H3):** `bootstrap_pluginval.sh` 추가 (Tracktion/pluginval clone+cmake build)
- **C2-Q8 신설 (H2):** JUCE 7 EULA tier 결정 (Educational tier 가정, 사용자 confirm 필요)
- **vst3/CMakeLists.txt 단일 치환 (C-H2):** `juce_add_plugin` macro 로 REWRITE; SpatialEngineVST3.cpp DELETE
- **Control plane OSC loopback 우회 (C-H3):** SpatialEngine setter 부재 → `parameterChanged → OSC loopback → cmd_fifo` (Driver #1 보호)
- **OFF baseline 명시 (M1):** `.ci/off_baseline.sha256` 체크인; CMake/sha256 명령 본문 박힘
- **strictness 5 격상 (M2):** Step 2 smoke + Step 3 final 5 + Step 4 final 5 (v1 의 strictness 3 → 5)
- **flag 통일 (M3):** `--strictness 5` → `--strictness-level 5` (long form, JUCEUtils.cmake 표준)
- **Q6 결정 (C-M3):** `setLatencySamples(0)`, dynamic Phase D6 (D6.a)
- **bootstrap shell spec (C-M4):** `bootstrap_juce.sh`/`bootstrap_pluginval.sh` 본문 idempotent 명령 박힘
- **byte-identical 정의 (Tension #1):** `libspe_core.a` + `spatial_engine_core` 만; 1줄 명시
- **APVTS schema v1 (L2):** state XML root attribute `version="1"`
- **VST3 SDK 라이선스 ADR (C-M2):** GPLv3 또는 Steinberg agreement 명시
- **strictness 4 부채 회계 (C-L2):** ADR Consequences 에 Phase D6 부채 명시

---

## 종료 조건

다음 모두 만족 시 C2 마일스톤 완료:

1. § 9 정량 게이트 모두 PASS
2. § 9 정성 게이트 모두 만족
3. CI 새 2 job 그린
4. Phase C ADR (`.omc/plans/spatial-engine-phaseC.md`) 에 ADR-C2 머지
5. § 10 Open Questions 모두 closed (Architect/Critic 라운드 2+ 통과)
6. 분리 commit 구조 — `chore(C2-bootstrap)` + `feat(C2.ab)` + `feat(C2.cd)` 세 개 (v3 §14.H)

---

## 14. v3 amendments (Architect R2 NOTE-1/2/3 + Critic R2 H1/H2/H3 + mid 통합)

> 본문 (§1~§13) 보존. 본 §14 가 v3 의 권위적 정정. 라운드 3 검토 대상.

### 14.A — EULA 정정 (Critic H2: Educational tier 가 GPLv3 회피한다는 표현은 category error)

- §8 Consequences `:464` 의 "Educational tier 채택 시 GPLv3 fallback 자동 회피 가능" 표현 무효화. 두 라이선스 결정은 **독립**.
- 두 결정 분리:
  - **JUCE 7 Educational tier (확정, 2026-05-06)** — paik402@snu.ac.kr SNU 학술 사용. JUCE 공식 가입 + EULA 동의 절차.
  - **VST3 SDK Steinberg dual license (C2-Q9 신설)** — Steinberg Proprietary License Agreement (재배포 자유) vs GPLv3 fallback. **권장: Steinberg agreement** (SNU 학술 비상업 조건에서도 무료 + 향후 상업화 충돌 적음). C2 진행 전제 — 사용자 confirm 필요. 미회수 시 D+1 부터 Steinberg 가정.
- §8 Consequences 업데이트 항목 추가:
  - "JUCE 7 Educational tier 와 VST3 SDK Steinberg agreement 는 독립 라이선스. 두 가입/동의 절차 필요."

### 14.B — 호스트→코어 control plane: OSC loopback → `dispatchCommand` inline (Critic H1, Architect synthesis)

> v2 의 OSC loopback 우회 (§6 Step 2 작업 1, 옵션 b) **폐기**. v3 는 옵션 (a) 의 정제 버전 채택.

- **결정:** `core/src/core/SpatialEngine.h` 에 다음 1줄 inline 메서드 추가:
  ```cpp
  inline void dispatchCommand(spe::ipc::Command const& cmd) noexcept {
      cmd_fifo_.push(cmd);  // 기존 audio thread drain 경로와 동일
  }
  ```
- **byte-identical 영향 분석 (Architect 인용):** inline + ODR + OFF 빌드에서 caller 0 → `libspe_core.a` 의 `.text` 변화 없음. `.symtab` 의 weak symbol 1개 추가는 발생 가능. 따라서 baseline 정의를 §14.C 로 강화.
- §6 Step 2 작업 1 의 OSC loopback 단계 모두 제거. `parameterChanged(parameterID, newValue)` 콜백 (control thread) 에서 6 param 각각 `Command` 빌드 후 `engine_->dispatchCommand(cmd)` 직접 호출.
- §7 "변경 금지" 목록 (`SpatialEngine.h`) 갱신 — **단일 inline forwarder 추가만 허용**, 그 외 변경 금지.
- §6 Step 0.5 baseline 한 번 재생성: `dispatchCommand` 추가 commit 후 `.ci/off_baseline.symbols.sha256` 갱신.
- §4 시나리오 3 "byte-identical 깨짐" 의 회피책에 "OSC loopback" → "inline forwarder + symbol-level baseline" 으로 갱신.

### 14.C — sha256 baseline: byte-level → symbol-level (Critic H3: toolchain 안정성)

- v2 의 `sha256sum libspe_core.a spatial_engine_core` 는 컴파일러/glibc/플래그 변동에 민감.
- **v3 baseline 정의:**
  ```bash
  # symbol-level: toolchain-stable
  nm -D --defined-only build_off/libspe_core.a | awk '{print $2, $3}' | sort -u | sha256sum > .ci/off_baseline.symbols.sha256
  nm -D --defined-only build_off/spatial_engine_core | awk '{print $2, $3}' | sort -u | sha256sum >> .ci/off_baseline.symbols.sha256
  ```
- §5 / §6 Step 0.5 / §6 Step 4 / §9 정량 게이트 의 `sha256sum` 명령 모두 위 패턴으로 교체. 파일명 `.ci/off_baseline.sha256` → `.ci/off_baseline.symbols.sha256`.
- 보너스 검증: 동일 host 에서 `--rebuild-from-clean` 후 byte-level diff 도 supplementary 로 기록 (실패 시 warning, gate 아님).

### 14.D — `juce::createPluginFilter()` 자유함수 명시 (Architect NOTE-2)

- §6 Step 1 작업 2 에 추가:
  ```cpp
  // vst3/SpatialEngineProcessor.cpp
  juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
      return new SpatialEngineProcessor();
  }
  ```
- §7 NEW 항목에서 `SpatialEngineProcessor.cpp` 가 이 자유함수 포함 명시.

### 14.E — `juce_add_plugin` 인자 명시 (Critic mid)

- §6 Step 1 작업 1 의 `juce_add_plugin(...)` 인자 보강:
  ```cmake
  juce_add_plugin(spatial_engine_vst3
      FORMATS VST3
      PRODUCT_NAME "Spatial Engine"
      PLUGIN_MANUFACTURER_CODE "SpEn"
      PLUGIN_CODE "Spe1"
      COMPANY_NAME "spatial_engine"
      BUNDLE_ID "com.spatial_engine.vst3"
      IS_SYNTH FALSE
      NEEDS_MIDI_INPUT FALSE
      NEEDS_MIDI_OUTPUT FALSE
      EDITOR_WANTS_KEYBOARD_FOCUS FALSE
      VST3_CATEGORIES "Fx" "Spatial"
  )
  ```

### 14.F — bootstrap idempotency 강화 (Architect NOTE-3 + Critic mid)

- `scripts/bootstrap_juce.sh`:
  - SHA pinning: `JUCE_VERSION=7.0.12` 변수 + `git rev-parse HEAD` 후 known-good commit hash 와 비교 (mismatch 시 재 clone 또는 fail-fast).
  - Post-clone sanity: `test -f core/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp || (echo "JUCE bootstrap incomplete"; exit 1)`
- CI cache key: `juce-7.0.12-${{ hashFiles('scripts/bootstrap_juce.sh') }}` → `juce-7.0.12-bundled-vst3sdk-${{ hashFiles('scripts/bootstrap_juce.sh') }}` (literal version + bundle marker; 캐시 corruption 격리).

### 14.G — 파라미터 tolerance 분리 (Critic mid: dB skew + Choice quantize)

- §5 `test_vst3_param_roundtrip.cpp` 의 96 assertion 을 다음 그룹별 tolerance 로 정정:
  - `pan_az` / `pan_el` / `source_width`: NormalisableRange 선형 → tol 1e-6
  - `master_gain` (-60..6 dB skew): `getNormalisableRange().convertFrom0to1` 적용 후 tol 1e-5 (dB skew 한 자릿수 완화)
  - `ambi_order` / `room_preset_idx` (Choice): exact equality (정수 인덱스 비교)
- assertion 수 96 = 6 param × 16 grid 그대로 유지.

### 14.H — fused commit 분할 (Critic mid: bisect 회복)

- v2 의 `feat(C2)` fused commit 1개 → **3개 분할**:
  1. `chore(C2-bootstrap)`: Step 0 만 (JUCE/pluginval wiring + baseline)
  2. `feat(C2.ab)`: Step 1 (juce_add_plugin + Processor + APVTS) + Step 2 (processBlock + dispatchCommand)
  3. `feat(C2.cd)`: Step 3 (bypass) + Step 4 (state persist + pluginval strictness 5 final)
- Phase C ADR Principle 3 exception 은 fused **C2 묶음 전체** 의 의미가 아니라 **single-responsibility 의 점진 완화** 로 재해석. 3 commit 으로 bisect 복원.

### 14.I — APVTS v≠1 fallback 명세 (Critic mid)

- §6 Step 4 작업 2 (`setStateInformation`) 명세에 추가:
  ```cpp
  if (xml->getIntAttribute("version", -1) != 1) {
      DBG("APVTS state version mismatch — using defaults");
      // 기본값 유지, 부분 복구 안 함
      return;
  }
  ```

### 14.J — `updateLtcChase` 호출 위치 (Architect NOTE-4)

- §6 Step 2 작업 2 step 4 의 "audio-thread 안 `engine_->updateLtcChase()`" 는 C1.c 설계와 정합 확인 필요.
  - `core/src/core/SpatialEngine.cpp` 의 `updateLtcChase` 가 lock-free + alloc-free 인지 검증 (RT_ASSERT_NO_ALLOC 1000회 통과로 검증). RT-safe 가 아니라면 `juce::Timer` (control thread) 로 이전.
  - 기본 가정: 검증 후 audio-thread 유지. 실패 시 control thread 로 이동.

### 14.K — Open Questions 갱신

- **C2-Q5** (CI venue) 결정: `.github/workflows/vst3.yml` 신설 (`.github/workflows/` 디렉토리 자체도 새로). pre-commit hook 은 로컬 캐시 무용 + CI 재현 어려움.
- **C2-Q7** (D+2 fallback 의사결정자) 결정: **Architect+Critic 라운드 1 합의** (라운드 2 까지 갈 시간 부족). 단독 결정 금지.
- **C2-Q9 (NEW, 14.A)**: VST3 SDK 라이선스 — Steinberg proprietary agreement vs GPLv3. **권장: Steinberg agreement** (학술/비상업도 무료). 사용자 confirm 필요, 미회수 시 D+1 가정.
- **현재 effective skip count 명시:** `python3 -m pytest --collect-only -q 2>&1 | grep -c "skip"` 로 측정 (Step 0 entry 시점 명시 필요). 현재 가정: 2 (mandatory MIDI skips, phaseC `:129`).

### 14.L — 실행 순서 영향 (요약)

| § | v2 표현 | v3 정정 |
|---|---|---|
| §3 Driver #2 | "OSC loopback → cmd_fifo" | "`dispatchCommand` inline → cmd_fifo" |
| §4 시나리오 3 | "OSC loopback 자연 보존" | "inline forwarder + symbol-level baseline" |
| §5 Test plan | byte-level sha256 | symbol-level sha256 (`.symbols`) |
| §6 Step 0.5 | baseline 1회 | baseline 1회 (symbol-level + dispatchCommand 추가 후) |
| §6 Step 1 | 6 param + processor | + `createPluginFilter` 자유함수 + `juce_add_plugin` 인자 보강 |
| §6 Step 2 | OSC loopback | `dispatchCommand` 직접 |
| §7 변경 금지 | SpatialEngine.h 변경 0 | inline forwarder 1줄 추가만 허용 |
| §8 Consequences | EULA = GPLv3 회피 | EULA + VST3 SDK 독립 결정 |
| §10 | Q5/Q7 미정 | Q5 vst3.yml / Q7 1라운드 / Q9 신설 |
| 종료조건 6 | 2 commit | 3 commit |

### 14.M — v3 가 닫는 라운드 2 항목

| Reviewer | 항목 | v3 처리 |
|---|---|---|
| Architect NOTE-1 | OSC loopback CI 가정 | 14.B 로 OSC loopback 자체 제거 → 무효화 |
| Architect NOTE-2 | createPluginFilter 누락 | 14.D |
| Architect NOTE-3 | bootstrap cache key | 14.F |
| Architect NOTE-4 | updateLtcChase 위치 | 14.J |
| Critic H1 | OSC loopback 위험 | 14.B |
| Critic H2 | EULA category error | 14.A |
| Critic H3 | sha256 toolchain 불안정 | 14.C |
| Critic mid (param tol) | dB skew / Choice quantize | 14.G |
| Critic mid (juce_add_plugin args) | IS_SYNTH 등 누락 | 14.E |
| Critic mid (fused 분할) | bisect 회복 | 14.H |
| Critic mid (APVTS v≠1) | fallback 명세 | 14.I |
| Critic mid (skip cap) | 현재 수 미명시 | 14.K |
| Critic mid (bootstrap SHA) | clone integrity | 14.F |

---

## 15. v4 amendments (Architect R3 REVISE + Critic R3 ITERATE 통합)

> §15 가 v4 의 권위. §14 항목 일부 supersede; supersede 시점 본문에 명시.

### 15.A — §14.B 정정: `dispatchCommand` 정확한 시그니처 + `OSCBackend::injectCommand` 도입

> **§14.B supersede.** v3 §14.B 의 `cmd_fifo_.push(cmd)` 는 **컴파일 실패** (cmd_fifo 는 `QueuedCmd` 저장; `Command` 와 시그니처 불일치). Architect 제안 `osc_backend_.dispatch(cmd)` 는 JUCE 빌드에서 v0 no-op (`OSCBackend.cpp:26-30` 의 deferred stub) 이라 broken. **v4 의 안전한 fix:**

- **OSCBackend.h public 영역에 1줄 inline 메서드 추가** (현재 `injectPacket` 와 같은 in-process 채널, 인코더 우회):
  ```cpp
  // core/src/ipc/OSCBackend.h
  void injectCommand(Command const& cmd) noexcept { if (sink_) sink_(cmd); }
  ```
- **SpatialEngine.h public 영역에 1줄 inline forwarder 추가:**
  ```cpp
  // core/src/core/SpatialEngine.h (private 멤버 osc_backend_ 접근 가능)
  inline void dispatchCommand(spe::ipc::Command const& cmd) noexcept {
      osc_backend_.injectCommand(cmd);
  }
  ```
- **검증된 사실 (코드 인용):**
  - `osc_backend_` 는 `SpatialEngine.h:76` 의 private 멤버 (`ipc::OSCBackend osc_backend_`).
  - `OSCBackend::injectPacket` (`OSCBackend.cpp:32-37`) 이 이미 `if (cmd.tag != CommandTag::Unknown && sink_) sink_(cmd)` 패턴 사용 — `injectCommand` 는 이 패턴에서 packet 디코드만 빠진 대응.
  - `sink_` 는 SpatialEngine ctor 에서 `Command → QueuedCmd` 변환 후 `cmd_fifo_.push(qc)` 람다로 wire 됨 (`SpatialEngine.cpp:17-105`).
- **byte-identical 영향:** OFF 빌드 (`SPATIAL_ENGINE_VST3=OFF` + `SPATIAL_ENGINE_NO_JUCE=ON`):
  - `OSCBackend::injectCommand` 는 헤더 inline → caller 0 시 `.text` emission 없음.
  - `SpatialEngine::dispatchCommand` 동일 — caller 0 시 `.text` emission 없음.
  - `.symtab` weak 심볼 추가 가능 → §15.B 의 dual-gate 로 보장.
- **Step 2 작업 1 (§6) 본문 supersede:** `parameterChanged` 콜백에서 `engine_->dispatchCommand(cmd)` 직접 호출. 변환 lambda 중복 없음, OSC loopback UDP 없음.

### 15.B — §14.C 정정: `nm` 명령 + dual-gate

> **§14.C supersede.** v3 §14.C 의 `nm -D` 는 archive 에 부적합 (`-D` 는 dynamic symbols 전용). Critic R3: symbol-level 만으로는 함수 본문 변경 못 잡음 → dual-gate.

- **명령 정정:**
  ```bash
  # Archive (.a) — static symbols + extern only
  nm --defined-only --extern-only build_off/libspe_core.a \
    | awk '{print $2" "$3}' | LC_ALL=C sort -u | sha256sum > .ci/off_baseline.symbols.sha256

  # Executable — defined symbols
  nm --defined-only build_off/spatial_engine_core \
    | awk '{print $2" "$3}' | LC_ALL=C sort -u | sha256sum >> .ci/off_baseline.symbols.sha256
  ```
- **Dual-gate 도입:**
  - **Primary gate (CI 동일 host, byte-level):** `sha256sum build_off/libspe_core.a build_off/spatial_engine_core` — toolchain pinned (CI image 고정) → 결정적. `.ci/off_baseline.bytes.sha256`.
  - **Secondary gate (cross-toolchain, symbol-level):** 위 nm 명령. `.ci/off_baseline.symbols.sha256`.
  - 둘 다 통과 시 OFF byte-identical 합격. 어느 한쪽 실패 시 CI 빨강.
- 파일 2개 모두 체크인. CI 가 baseline 이미지 pinning 확인 (`.github/workflows/vst3.yml` 에 `runs-on: ubuntu-22.04` 명시).

### 15.C — §6 fused commit block ↔ §14.H 정합 (Critic M4)

> §6 의 "Fused commit (C2.a~d 만)" 블록 (line 374-388) **supersede by §14.H + §15.C**. v4 의 권위적 commit 구조:

| commit | 범위 | 메시지 prefix |
|---|---|---|
| 1 | Step 0 (bootstrap + JUCE/pluginval wiring + baseline 생성) | `chore(C2-bootstrap):` |
| 2 | Step 1 (juce_add_plugin + Processor + APVTS) + Step 2 (processBlock + dispatchCommand) | `feat(C2.ab):` |
| 3 | Step 3 (bypass) + Step 4 (state persist + pluginval strictness 5 final) | `feat(C2.cd):` |

각 commit 메시지 본문에 Phase C ADR Principle 3 exception 인정 명시.

### 15.D — §14.J supersede: `updateLtcChase` control-thread 강제 (Critic M2)

> v3 §14.J 의 "audio-thread 유지 (검증 후)" **폐기**. C1.c 설계 의도가 control-thread tick (>= 50 Hz) 임을 존중.

- §6 Step 2 작업 2 step 4 변경: `engine_->updateLtcChase()` 를 audio thread (`processBlock`) 에서 호출하지 않는다.
- VST3 처리: `SpatialEngineProcessor` 가 `juce::Timer` 상속 (또는 멤버) 으로 100 Hz 콜백에서 `engine_->updateLtcChase()` 호출. `Timer` 는 JUCE message thread → control-thread 정의에 부합.
- 코드 골자:
  ```cpp
  class SpatialEngineProcessor final : public juce::AudioProcessor, private juce::Timer {
      void timerCallback() override { engine_->updateLtcChase(); }
      void prepareToPlay(double, int) override { /* ... */ startTimerHz(100); }
      void releaseResources() override { stopTimer(); }
  };
  ```
- §6 Step 2 Exit 의 `RT_ASSERT_NO_ALLOC` 1000회 에서 `updateLtcChase` 호출 제외 (audio thread 에서 안 부르므로 자동 만족).

### 15.E — §14.A 보강: Step 0 entry gate 라이선스 written confirmation (Critic M1)

- v3 의 "D+1 Steinberg 가정 진행" 폐기. C2-Q9 결정 **Step 0 entry 차단** 기준.
- Step 0 entry checklist 추가:
  - [ ] JUCE 7 Educational tier 등록 완료 — JUCE.com 가입 + EULA 동의 스크린샷/URL 을 commit message footer 또는 `.omc/plans/c2-licensing.md` 에 기록.
  - [ ] VST3 SDK Steinberg Licensing Agreement 동의 — Steinberg developer portal 가입 + signed agreement 스크린샷/URL 동일 위치에 기록.
- 둘 중 하나라도 미회수 → Step 0 시작 금지. **D+1 silent default 폐지** (라이선스는 silent default 부적합).

### 15.F — §14.I 보강: APVTS v≠1 fallback 강화 (Critic M3)

- v3 의 "기본값 유지, 부분 복구 안 함" → "host data-loss silent" 위험 → 강화:
  ```cpp
  void setStateInformation(const void* data, int sizeInBytes) override {
      auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes));
      if (!xml || xml->getTagName() != "spatial_engine_apvts") {
          juce::Logger::writeToLog("SpatialEngineProcessor: unknown state schema, defaults retained");
          return;
      }
      const int v = xml->getIntAttribute("version", -1);
      if (v != 1) {
          juce::Logger::writeToLog("SpatialEngineProcessor: state version=" + juce::String(v) + " unsupported, defaults retained");
          return;
      }
      apvts_.replaceState(juce::ValueTree::fromXml(*xml));
  }
  ```
- Phase D6 follow-up `D6.z (APVTS schema v2 migration)` 에 forward-compatible reader 명시.

### 15.G — §14.G NormalisableRange skew 정의 (Critic minor)

- `master_gain` (-60..6 dB) 등록 시 명시적 skew 사용:
  ```cpp
  juce::NormalisableRange<float> gainRange(-60.0f, 6.0f, 0.001f);
  gainRange.setSkewForCentre(0.0f);  // 0 dB at midpoint
  apvts_.createAndAddParameter(std::make_unique<juce::AudioParameterFloat>(
      "master_gain", "Master Gain", gainRange, 0.0f));
  ```
- tolerance 1e-5 는 skew 적용 후 `convertFrom0to1`/`convertTo0to1` 라운드트립 검증 기준.

### 15.H — §14.F bootstrap SHA pinning 강화 (Critic minor)

- `scripts/bootstrap_juce.sh` 코어 추가:
  ```bash
  #!/usr/bin/env bash
  set -euo pipefail
  TARGET=core/JUCE
  JUCE_VERSION=7.0.12
  EXPECTED_SHA=  # JUCE 7.0.12 tag SHA — Step 0.1 작업 시 채움
  if test -f "$TARGET/CMakeLists.txt"; then
    actual=$(git -C "$TARGET" rev-parse HEAD)
    if [[ -n "$EXPECTED_SHA" && "$actual" != "$EXPECTED_SHA" ]]; then
      echo "ERROR: JUCE SHA mismatch (got $actual, expected $EXPECTED_SHA). Re-clone."; exit 1
    fi
    echo "JUCE already bootstrapped at $TARGET ($actual)"; exit 0
  fi
  git clone --depth 1 --branch "$JUCE_VERSION" --single-branch https://github.com/juce-framework/JUCE.git "$TARGET"
  test -f "$TARGET/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp" \
    || (echo "JUCE bootstrap incomplete"; exit 1)
  echo "JUCE bootstrapped: $(git -C "$TARGET" rev-parse HEAD)"
  ```

### 15.I — §14.D `JUCE_CALLTYPE` 검증 (Critic minor)

- `vst3/SpatialEngineProcessor.cpp` 의 자유함수 시그니처는 `JUCE_CALLTYPE` 매크로 그대로 사용 — Linux 에서 빈 매크로지만 plugin client 의 호출 규약 (`juce_CreatePluginFilter.h:35` 의 `::createPluginFilter()`) 와 일치 보장.

### 15.J — 신규 게이트: MAX_BLOCK clamp + parameterChanged 재진입성

- **MAX_BLOCK** (Critic missing): `prepareToPlay(double sr, int maxBlock)` 에서 `if (maxBlock > spe::audio_io::MAX_BLOCK) { jassertfalse; suspendProcessing(true); }` (호스트 변동 차단). §5 ctest 추가는 안 함 (확률 낮음); 런타임 가드만.
- **parameterChanged 재진입성** (Critic missing): `parameterChanged` 가 nested 호출되지 않음을 명시. APVTS 는 lock 으로 보호되며, `engine_->dispatchCommand` 는 lock-free SPSC 큐 → 재진입 안전. §5 에 ctest 추가 안 함; ADR Consequences 에 "APVTS+SPSC 결합으로 재진입 안전" 명시.

### 15.K — Open Questions 갱신 (라운드 3 → 4)

- **C2-Q1** 결정: JUCE **7.0.12** (Phase B 검증된 minor). LICENSE.md 와 일치.
- **C2-Q3** 결정: Generic editor 라벨 polish **Phase D6** 미룸. C2 는 default 라벨 유지 (사용자 혼동은 D6 task `D6.b: editor label polish` 에 회계).
- **C2-Q4** 결정: D+9 strictness 5 fail → **strictness 4 fallback + Phase D6 부채 회계** (Phase C ADR Option C 와 일치). C4 → D8 강등은 D+12 까지 유보 후만.
- **C2-Q9** Step 0 entry 차단 (§15.E 참조).
- **현재 effective skip count = 2** (mandatory MIDI; phaseC `:129` 인용). 5 ctest 도입 후도 cap 3 유지.

### 15.L — v3 → v4 supersede 매핑 (executor 가이드)

| v3 §14 | v4 처리 |
|---|---|
| §14.B (compile bug) | §15.A 로 supersede |
| §14.C (nm -D archive 부적합) | §15.B 로 supersede + dual-gate |
| §14.J (audio-thread 유지) | §15.D 로 supersede (control-thread 강제) |
| §14.A (D+1 default 진행) | §15.E 로 supersede (Step 0 entry block) |
| §14.I (silent default) | §15.F 로 강화 (Logger + 명시 schema check) |
| §14.G (skew tol 1e-5) | §15.G 로 정의 명시 |
| §14.F (SHA pinning) | §15.H 로 강화 (EXPECTED_SHA + sanity check) |
| §14.D (createPluginFilter) | §15.I 로 검증 보강 |
| §14.E/H/K | 변경 없음 (v3 그대로 채택) |
| §14.M (R2 닫기 매핑) | §15.M 로 R3 닫기 추가 |

### 15.M — v4 가 닫는 라운드 3 항목

| Reviewer | 항목 | v4 처리 |
|---|---|---|
| Architect R3 §14.B compile bug | §15.A |
| Architect R3 §14.C nm 명령 | §15.B |
| Architect R3 dual-gate 권고 | §15.B |
| Architect R3 §14.0 cross-ref | §15.L (executor guide) |
| Critic R3 C1 (compile fail) | §15.A |
| Critic R3 C2 (행위 변화 누락) | §15.B (dual-gate primary byte) |
| Critic R3 M1 (Steinberg signing) | §15.E |
| Critic R3 M2 (updateLtcChase) | §15.D |
| Critic R3 M3 (silent state drop) | §15.F |
| Critic R3 M4 (§6 ↔ §14.H 모순) | §15.C |
| Critic R3 minor (skew, SHA pinning) | §15.G, §15.H |
| Critic R3 missing (MAX_BLOCK, 재진입) | §15.J |
| Critic R3 unscored Q5/Q7 → Q9 | §15.K |
