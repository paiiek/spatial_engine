# spatial-engine — Phase C / C2 (Option B) v4

> **Round 4 draft (planner).** 작성일 2026-05-06. v3 (라운드 3) → Architect R3 (verdict **APPROVE-WITH-MINOR-NOTES**, NOTE-R3-A~E 5 minor) → Critic R3 (verdict **ITERATE**, MA-1~5 CRITICAL 2 / HIGH 3 + R-1~4 reservations) 합산 input 을 amendment AM-R4-1~AM-R4-5 + NOTE-R3-A~E + R-1~4 로 반영해 본 v4 작성. v1→v2 변경은 §12.5 (그대로 보존), v2→v3 변경은 §12.6 (그대로 보존), v3→v4 변경은 §12.7 신설.
>
> **v4 핵심 정정 (R3 fact-error 5건 해소):**
> - **AM-R4-1 (CRITICAL):** AM-R3-6 SHA pin 명령 — outer git repo 가 `core/JUCE/` gitignore (`.gitignore:13`) 로 `git ls-tree` 결과 0 entries → empty SHA `e3b0c44...` trivially pass. **Fix:** inner JUCE repo cd: `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` (122 entries 검증).
> - **AM-R4-2 (CRITICAL):** §6 Step 1.2 의 `Vst::AudioEffect` 클래스 vendored 트리 부재 (`find ... -iname "*audioeffect*"` = 0). 실제 helper = `class Component : public ComponentBase, public IComponent` at `vstcomponent.h:52`. **Fix:** `Vst::AudioEffect` → `Vst::Component` (또는 IComponent + IAudioProcessor 직접 다중 상속).
> - **AM-R4-3 (HIGH):** §6 Step 0.c entry gate `CPluginFactory` grep 이 `wc -l == 0` 기대하나 실측 `== 1` (docstring `ipluginbase.h:201`). Logic flip → 항상 NOTE-escalate. **Fix:** `.cpp` 한정 + comment exclude 또는 file existence check.
> - **AM-R4-4 (HIGH):** Principle 2 화이트리스트가 helper 6개 의 transitive `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 누락 → Step 1 빌드 실패. **Fix:** 화이트리스트 확장 + BSD-3 file-level header grep 추가.
> - **AM-R4-5 (HIGH):** §5 #5 bypass ctest 의 `dispatchCommand` RT-safety 검증 scope 가 `cmd_fifo_.push` thread-id 단일 검증 + `OSCBackend::injectCommand` 직접 호출 누락. **Fix:** 두 assertion 추가.
>
> **상위 플랜:** `.omc/plans/spatial-engine-phaseC-C2.md` §16 (D+2 fallback 트리거 발현 → Option A 폐기 결정 commit `c2a5ad0`/`572cc2d` 합의 위에 작성).
>
> **이 문서의 위치:** Phase C 의 C2 sub-phase 를 **Option B (vst3sdk 직접 핸드롤)** 으로 처음부터 다시 한다. v1 의 fact-error (validator 부재 / pluginfactory.cpp 부재 / cleanup 메커니즘 / 라이선스 dual-strand) 는 v2 에서 정정. v2 의 fact-error (factory 매크로 본체 부재 — `BEGIN_FACTORY_DEF` 한 줄만 정의되고 `END_FACTORY` / `DEF_CLASS2` `#define` 0 hit + `CPluginFactory` `.cpp` 구현 자체 부재) 는 v3 에서 vtable 직접 작성 default 로 정정. C2-Q9~Q16 은 §10 에서 라운드 3 검토 대상으로 격리.

---

## 0. Context (왜 새 플랜인가)

- **이전 결정:** `spatial-engine-phaseC-C2.md` v1→v2→v3→v4 라운드 4 까지 Architect/Critic 합의 완료 (Option A: `juce_add_plugin` + APVTS).
- **D+2 fallback 트리거 발현 (§16.1, 2026-05-06):** 사용자 로컬 환경 sudo 부재 → `libx11-dev` / `libfreetype6-dev` / `libasound2-dev` 등 X11/freetype/alsa 시스템 dev 패키지 영구 미설치 → `juceaide` 호스트툴 컴파일이 `X11/Xlib.h: No such file or directory` 로 실패 → `juce_add_plugin` 사용 불가.
- **EULA 결정 (C2-Q8, 2026-05-06):** JUCE 7 Educational tier (paik402@snu.ac.kr) 채택 완료. 이 결정은 **EULA 차단** 이 아니라 **시스템 dev 패키지** 가 차단 사유였음.
- **§16.2 결정:** Option A 폐기 → Option B 활성. `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` 헤더만 + `public.sdk/source/{vst,common}/` 의 selective `.cpp` 화이트리스트 + `public.sdk/source/vst/hosting/module_linux.cpp` (host fixture 용, BSD-3) 만 사용해 vst3sdk 인터페이스 (`IPluginFactory`/`IComponent`/`IAudioProcessor`/`IEditController`) 직접 hand-roll. JUCE plugin client / APVTS / juceaide 의존 제거 → X11 sysdep 회피.
- **유지되는 자산 (commit `572cc2d` Step 0):**
  - `scripts/bootstrap_juce.sh` — vst3sdk 가 JUCE 트리 안 번들이므로 단일 부트스트랩 채널.
  - `.ci/off_baseline.bytes.sha256` / `.ci/off_baseline.symbols.sha256` — Driver #1 보호 dual-gate.
  - `scripts/check_test_ndebug.sh` — 신규 vst3 ctest 의 `-UNDEBUG` 강제.
  - `core/src/core/SpatialEngine.h:57-58` — `dispatchCommand` inline forwarder + `OSCBackend::injectCommand` (§15.A 산출).
- **폐기 대상 (별도 commit `chore(C2): roll back JUCE Option A WIP`, AM-3 메커니즘 확정 + AM-R3-4 강제 순서):**
  - `vst3/SpatialEngineProcessor.{hpp,cpp}` — **untracked** (git status `??`). `git rm` 불가, plain `rm` 사용.
  - `vst3/AudioBlockAdapter.hpp` — **untracked**. 동일.
  - `vst3/tests/` 디렉토리 — **untracked**. 동일.
  - `vst3/SpatialEngineVST3.cpp` — **staged delete** (git status `D`). `git rm --cached` 으로 staged-delete 확정.
  - `vst3/CMakeLists.txt` — **modified** (Option A 의 `juce_add_plugin` 호출). `git checkout HEAD -- vst3/CMakeLists.txt` 으로 되돌림 후, Step 1 에서 새로 작성.

> **v1→v2 변경:** §0 폐기 대상 cleanup 메커니즘을 untracked 4 + staged-delete 1 + modified 1 로 정확히 분리 (AM-3). v1 의 "별도 revert(C2) commit" 은 사실 오류 (untracked 는 revert 불가) → "신규 chore commit + plain rm + git checkout" 메커니즘으로 정정.
> **v2→v3 변경:** §0 폐기 대상 cleanup 5단계 강제 순서 명시 (AM-R3-4). git status → git rm --cached → git checkout HEAD → plain rm → cleanup commit. 시퀀스 위반 시 작업 트리 inconsistency 위험 (예: Option A 의 modified CMakeLists 가 새 commit 에 포함되거나, untracked 가 commit 에서 누락).
> **v3→v4 변경 (NOTE-R3-B):** Step 1-4 사이 swap 은 결과 동일 (`--cached` 플래그가 working tree 보존). 단 **Step 5 (commit) 가 Step 1-4 보다 먼저 실행되면 untracked 4 + modified 1 이 cleanup commit 에서 누락 → Principle 6 (단일 commit 의 bisect 회복비용) 위반** = 유일한 실질 failure mode. 다른 1-4 swap 은 working tree 일관성 보존.

---

## 1. Principles (6)

> Phase C 5 Principles (`spatial-engine-phaseC.md:41-46`) 의 Option B specialisation. Critic R1 H4 (Principle 2 모순) 와 AM-4 (selective include 화이트리스트 명문화), AM-1 의 §3 B.1 폐기 자동 해소를 위해 Principle 2 재작성. Architect R1 의 Principle violation flag 3건 중 2건 (P2/P3) 은 §3/§6 본문 amendment 로 해소, 1건 (P5: commit fused vs split) 은 P6 신규로 명문화. v3 에서 Principle 2 의 helper 6개 화이트리스트가 BSD-3 헤더 검증 결과로 잠금 (AM-R3-8).

1. **Hardware-free first, sysdep-free.** 빌드/CI 가 X11/freetype/alsa 시스템 dev 패키지 없이 통과해야 한다. juceaide / pluginval / VSTGUI 등 GUI host 의존 stack 진입 금지. DAW 실행은 confirm-only.
2. **Minimal SDK surface — selective include 화이트리스트.** vst3sdk 의 다음 트리만 진입 허용:
   - `pluginterfaces/base/*.h` + `pluginterfaces/vst/*.h` (헤더 only, sysdep-free 검증 완료 — `<dlfcn.h>` 외 시스템 헤더 0).
   - `public.sdk/source/vst/{vstbus,vstcomponentbase,vstcomponent,vsteditcontroller,vstinitiids,vstparameters,vstpresetfile}.cpp` (helper, **6개 헬퍼 모두 file-level header 검증 완료 = BSD-3-Clause "Redistribution and use ... permitted ... above copyright notice ... AS IS" 표준 텍스트 — vst3sdk root LICENSE.txt 의 dual-strand (Steinberg agreement OR GPLv3) 와 별개**, AM-R3-8 grep 결과는 §4 시나리오 3 인용; transitive 가 X11 끌어오면 vtable 직접 작성 fallback).
   - **(v4 AM-R4-4 NEW)** `base/source/{fstring,fobject,fdebug,fbuffer,futils}.{cpp,h}` + `base/source/updatehandler.{cpp,h}` — helper 6개 의 transitive 의존 (실측 grep: `vstcomponentbase.cpp:38: #include "base/source/fstring.h"` + `vsteditcontroller.cpp:38: #include "base/source/updatehandler.h"`; `fstring.h` → `base/source/fobject.h`; `fobject.cpp` → `base/thread/include/flock.h`). Step 0 grep 으로 **file-level BSD-3 header 검증 추가** (helper 6개 와 동일 절차).
   - **(v4 AM-R4-4 NEW)** `base/thread/include/flock.h` (`fobject.cpp` thread 의존) — file-level BSD-3 검증 필요.
   - `public.sdk/source/common/{memorystream,readfile}.cpp` (`pluginview.cpp` **금지** — Plug-in view 는 GUI 의존 가능, Phase D6 정식 editor 까지 deferral).
   - `public.sdk/source/vst/hosting/module.{cpp,h}` + `module_linux.cpp` + `hostclasses.{cpp,h}` (host fixture 용, BSD-3 라이선스 별개 — §4 시나리오 3 분석). **(v4 R-4 정정)** `module_linux.cpp` 의 `<experimental/filesystem>` + `<filesystem>` includes 는 **`USE_EXPERIMENTAL_FS` macro 로 compile-time 1개 선택** (실측 line 46-72 mutex), concurrent 사용 아님.
   - **Factory 진입점 (v3 정정, AM-R3-1):** `pluginterfaces/vst/ivstcomponent.h:36` 에 `BEGIN_FACTORY_DEF` 단독 1건만 `#define` 되어 있고 `END_FACTORY` / `END_FACTORY_DEF` / `DEF_CLASS2` / `EXPORT_FACTORY` 의 `#define` 은 vendored 트리에 **0 hit** (Critic R2 fact-check 재검증, v3 자체 grep 재확인). `CPluginFactory::CPluginFactory` / `CPluginFactory::registerClass` `.cpp` 구현도 `pluginterfaces/base/ipluginbase.h:201` 의 SDK 자체 주석 ("An implementation is provided in public.sdk/source/common/pluginfactory.cpp") 이 가리키는 위치에 **부재** — JUCE 7.0.12 가 번들 시 제거. 따라서 **v3 default = `IPluginFactory` 4 virtual method (`pluginterfaces/base/ipluginbase.h:192-214` (v4 R-3 boundary refinement, 클래스 본체 L192, virtuals L197/202/205/208, IID L211/`DECLARE_CLASS_IID` L214) = `getFactoryInfo` / `countClasses` / `getClassInfo` / `createInstance`) 직접 vtable 작성**. 매크로 시퀀스 (`BEGIN_FACTORY_DEF` / `DEF_CLASS2` / `END_FACTORY`) **완전 폐기**, `CPluginFactory` helper **미사용**.
   - **GUI/host helper 절대 금지:** `pluginview.cpp`, VSTGUI 트리 (`core/JUCE/modules/juce_gui_basics/...` 포함), `samples/` (validator 부재, moduleinfotool 만 존재).
3. **Test gates 정량 + 정직 회계.** 7 ctest (host-fixture / param-layout / param-roundtrip / audio-smoke / state-persist / bypass / ndebug-enforce) 모두 host 미러 fixture (in-process). pytest skip 신설 금지 (Phase C effective-skip cap 3). M2 게이트 (strictness 검증) 는 Option B.2 (자체 in-process host) default 채택 — §3 재작성 결과.
4. **No NDEBUG strip regression.** 신규 7 ctest 모두 `scripts/check_test_ndebug.sh` 가 `-UNDEBUG` 자동 검증. 스크립트가 `vst3/tests/` 디렉토리도 스캔하도록 확장 (Step 0.b).
5. **OFF-path byte-identical 불변량.** Option A 시점의 dual-gate (§15.B) 그대로 적용. `if(SPATIAL_ENGINE_VST3)` 격리 + `vst3/` 서브트리만 변경. `core/src/core/SpatialEngine.{h,cpp}` (§15.A 의 `dispatchCommand` 1줄 inline forwarder 는 이미 commit 됨) 와 `core/CMakeLists.txt` 의 `SPE_CORE_SOURCES`/`SPE_HAVE_JUCE` 블록 변경 금지.
6. **Commit 분할은 bisect 회복비용 우선.** Option A 의 fused 3 commit 패턴은 fused 단위가 너무 커서 bisect 회복비용 > 분할 overhead. Option B 는 5 commit 분할 (Step 0~4). 각 commit 은 **독립적으로 빌드 & ctest 통과** 가 게이트 (커밋 footer 에 명시). Phase C ADR Principle 3 exception 인정 — Architect R1 NOTE-6 회신.

> **v1→v2 변경:** Principle 2 를 selective include 화이트리스트 + factory 매크로 (BEGIN_FACTORY_DEF) + GUI/host helper 절대 금지 명문화 (AM-4). Principle 6 신규 — commit 분할 정당화 (M2/Architect NOTE-6).
> **v2→v3 변경:** Principle 2 의 factory 진입점을 매크로 시퀀스에서 **vtable 직접 작성** 으로 default 변경 (AM-R3-1, NOTE-R2-1 CRITICAL). 매크로 부재 사실을 vendored grep 결과로 잠금. helper 6개 BSD-3 검증 결과 추가 (AM-R3-8). v3 에서 §3 B.2 sysdep 인용은 §3 본문에서 정정 (AM-R3-2).
> **v3→v4 변경:** Principle 2 화이트리스트에 helper 6개 의 transitive 의존 (`base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h`) 추가 (AM-R4-4). `module_linux.cpp` filesystem includes mutex 명시 (R-4). `ipluginbase.h:194-216` boundary 를 `:192-214` 로 refinement (R-3).

---

## 2. Decision Drivers (top 3)

1. **OFF-path byte-identical 불변량 (Driver #1, Option A 와 동일).** dual-gate (`.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`) 유지. CI 매 step 검증.
2. **RT 스레드 안전성 + control plane 단일화 (Driver #2, AM-6 으로 SDK 표준화 + AM-R3-3 으로 SDK 인용 잠금).** Option A 와 동일한 cmd_fifo 경로:
   - Host param 변경 → host 가 `IComponentHandler::performEdit(paramID, normValue)` 콜백 (controller 측 user gesture 에서 호출, **SDK 인용 (v4 R-3 line drift 정정):** `pluginterfaces/vst/ivsteditcontroller.h:175-176` "To be called before calling a performEdit (e.g. on mouse-click-down event). **This must be called in the UI-Thread context!**" + `:179-180` "Called between beginEdit and endEdit ... **This must be called in the UI-Thread context!**" + `:183-184` "To be called after calling a performEdit ... **This must be called in the UI-Thread context!**" — 3회 반복 명시) → host event-thread 가 audio thread 의 `IAudioProcessor::process` 의 `inputParameterChanges` queue 에 적재 (**SDK 인용:** `pluginterfaces/vst/ivstaudioprocessor.h:316` (v4 R-3 off-by-1 정정) "The Process call, where all information (parameter changes, event, audio buffer) are passed.") → component 가 `process()` 시작 시 queue 의 마지막 point 만 stack snapshot (block-rate, virtual call 6회 고정) → control thread (host event-thread 신뢰 default — §10 C2-Q12 v3 권장 ②, AM-R3-3) → `engine_->dispatchCommand(spe::ipc::Command)` → `OSCBackend::injectCommand` → `cmd_fifo_.push` → audio thread drain.
   - **`process` (audio thread) 는 `inputParameterChanges` snapshot 만 읽음, 절대 `dispatchCommand` 직접 호출 금지** (단, §10 C2-Q12 v3 의 `dispatchCommand` RT-safety 검증 (§5 #5 ctest 확장) 통과 시 audio thread 에서 직접 호출 default 채택, SPSC ring 폐기) — Driver #2 위반 사례 사전 차단.
   - **`IConnectionPoint::notify` 채널 사용 금지** — host-별 thread guarantee 차이 (Cubase main / Reaper audio thread 가능, AM-8 시나리오 4) 로 RT-unsafe 위험. `IComponentHandler` + `inputParameterChanges` 만 사용.
   - **Component ↔ Controller 는 별 instance** (host 가 createInstance 로 별도 생성, IConnectionPoint 으로 묶음). `process` pointer 공유 불가 → controller 가 component 의 `engine_->dispatchCommand` 직접 호출하는 경로 (v1 C2-Q12 권장 ②) **AM-6 으로 폐기**, host event-thread 신뢰 (v3 C2-Q12 ②) 가 default.
3. **Sysdep-free 단일 의존성 채널 (Driver #3, Option A Driver #3 재서술).** vst3sdk 가 JUCE 트리 안 번들이므로 `bootstrap_juce.sh` 가 여전히 단일 부트스트랩 채널이지만, **`juce_add_plugin` macro 미사용** → juceaide 빌드 회피 → X11/freetype/alsa sysdep 자동 회피. CMake 가 `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` 의 헤더 search path 만 추가 (`add_library(spatial_engine_vst3 MODULE ...)` 직접 사용).

> **v1→v2 변경:** Driver #2 의 control plane 경로를 `IConnectionPoint::notify` (host thread guarantee 불확실) 에서 `IComponentHandler::performEdit` + `inputParameterChanges` (SDK 표준 보장) 로 교체 (AM-6).
> **v2→v3 변경:** Driver #2 의 SDK 인용 verbatim 잠금 (AM-R3-3): `ivsteditcontroller.h:175-181` UI-Thread context 3회 + `ivstaudioprocessor.h:317` audio thread drain. C2-Q12 권장이 ① (component std::thread) → ② (host event-thread 신뢰) 로 전환. SPSC ring 사용 여부는 `dispatchCommand` RT-safety 검증 (§5 #5 ctest 확장) 결과로 결정.
> **v3→v4 변경 (R-3 line drift 정정):** `:180-181` → `:179-180` (mid quote off-by-1), `:317` → `:316` (off-by-1). SDK 인용 boundary 정정만, 의미 변경 없음.

---

## 3. Viable Options (≥2)

> **AM-1 전면 재작성 (v2).** v1 의 §3 B.1 (Steinberg validator 툴) 은 vendored vst3sdk 트리에 **validator 디렉토리 자체 부재** 라는 사실 오류 위에 있었음 (Critic R1 fact-check). v2 는 B.2 (자체 in-process host) 를 default 로 승격하고, Architect R1 antithesis + Critic R1 H5 fair-alternatives 요청에 따라 Option A 회피책 2개 sub-option 을 추가 평가.
> **AM-R3-2 (v3 정정):** B.2 Pros 의 `module_linux.cpp` sysdep 인용을 vendored grep 결과로 정정 — POSIX 표준 헤더 + libstdc++ 표준만 (X11/freetype/alsa/webkit dev 패키지 0).

### B.2 — vst3sdk 헤더 + 자체 in-process VST3 host fixture (M2 default)

**개요:** `pluginterfaces/` + `public.sdk/source/vst/hosting/module_linux.cpp` (BSD-3, sysdep = POSIX 표준 헤더만 — Critic grep 확인) + `hostclasses.{cpp,h}` 를 사용해 in-process loader 직접 작성. 우리가 빌드한 `spatial_engine_vst3.so` 를 `dlopen` → `GetPluginFactory()` → `IPluginFactory::createInstance(processorCID, IComponent::iid)` + `createInstance(controllerCID, IEditController::iid)` → connection point 연결 → `IAudioProcessor::process` 호출. 7 ctest fixture 가 host 역할.

**Pros:**
- pluginval/validator 둘 다 우회 → 100% 자체 통제, sysdep 0.
- ctest 자체가 host = strictness 검증 lane 동시 수행.
- 라이선스: `module_linux.cpp` 가 BSD-3 (Steinberg 자사) → vst3sdk root LICENSE.txt 의 dual-strand (Steinberg agreement OR GPLv3) 와 별개. Host fixture 코드는 GPLv3 전염 회피 가능 (C2-Q16 변호사 검증 deferral, AM-R3-5).
- **v3 정정 (AM-R3-2):** `module_linux.cpp` 실제 includes (vendored grep, lines 30-67) = **POSIX 표준 헤더만** — `<algorithm>`, `<dlfcn.h>`, `<sys/types.h>`, `<sys/utsname.h>`, `<unistd.h>` + libstdc++ 표준. **X11/freetype/alsa/webkit 시스템 dev 패키지 의존 0**. v2 의 "`<dlfcn.h>` 외 시스템 헤더 0" 은 부정확 (POSIX 표준 5종 + filesystem) → v3 verbatim 정정.
- **v4 정정 (R-4):** filesystem 헤더는 `<experimental/filesystem>` (line 46-48) 또는 `<filesystem>` (line 67-72) 둘 중 하나만 — **`USE_EXPERIMENTAL_FS` macro 로 compile-time 1개 선택** (mutex), concurrent include 아님. v3 의 "POSIX 표준 5종 + filesystem 표현" 은 동시 include 처럼 읽힐 수 있으나 실제는 macro mutex.

**Cons:**
- "host 가 검증해야 할 strictness" 정의 = 우리가 직접 작성 → spec drift 위험 (Steinberg 표준 = pluginval/validator 와 다를 수 있음). 회피책: vst3sdk 의 `vstvalidator` 표준 점검 항목 (param count / process lifecycle / state IO) 를 ctest fixture 가 모방.
- M2 게이트 신뢰도 ↓ (자체 검증) — DAW 실호스트 confirm-only 로 보강.
- 호스트 코드 RT-safety / lifecycle bug 가 plugin bug 처럼 보일 위험. 회피책: host fixture 를 별 ctest (`test_vst3_host_fixture`) 로 분리 (AM-7).

### B.3 — Phase D6 부채 회계 (fallback)

**개요:** B.2 의 host fixture 자체 작성이 Step 0.c 검증에서 실패하면 (`module_linux.cpp` 빌드가 의외의 transitive 끌어옴 등) M2 게이트는 Phase D6 까지 부채로 미루고, C2 는 unit/integration ctest 7개만으로 종결.

**Pros:**
- 짧은 critical path → C2 D+9 안에 종결 가능.
- §11.395 C-L2 패턴 (정직 회계) 과 정합.
- 회피되는 위험: host 자체 작성에서 더 큰 yak-shave.

**Cons:**
- M2 게이트 미충족 = strictness 보장 약함 (DAW 실호스트 로딩만으로는 자동화 안 됨).
- Phase D6 entry 시 부채 청구 — 그 시점의 비용 추정 필요.

### Option A 회피책 sub-options (Architect R1 antithesis + Critic R1 H5)

> 라운드 1 input 에서 Architect/Critic 이 fair-alternatives 강화를 요구. Option A (juce_add_plugin) 자체는 §16.1 D+2 트리거로 폐기됐지만, **Option A 회피책 2종** 은 정량 비교 후 기각/채택 결정을 명시해야 §3 의 fair-alternatives 의무 충족.

#### Option A' — JUCE plugin client + `juce_gui_basics` stub

**개요:** `juce_add_plugin` 매크로는 사용하되, plugin client 가 transitively 끌고 들어오는 GUI 모듈 (`juce_gui_basics`, `juce_gui_extra`, `juce_graphics`) 만 stub 처리.

**정량 비교:**
- juceaide 의존: `juce_add_plugin` 호출 시 host-tool `juceaide` 가 항상 빌드됨 (CMake 단계의 BinaryBuilder 등 generation 용). juceaide 자체가 `juce_gui_basics` (X11/freetype) 에 link → **stub 만으로는 회피 불가**, juceaide 소스 자체 패치 필요.
- 시간 비용: juceaide 소스 패치 + JUCE upstream 트리 변경 = 매 JUCE 버전 bump 마다 재패치 부담. 추정 1-2d 초기 + JUCE 버전 bump 마다 0.5d.
- 라이선스: GUI 모듈 stub 은 JUCE 7 GPLv3 (또는 Educational tier) 사용 — Educational tier 가 stub 까지 허용?
- sysdep 깊이: stub 후에도 juceaide 가 ELF 로 X11 link 시도 → ldd 확인 필요.

**기각 결정:** juceaide 자체 패치 비용 + JUCE upstream 트리 변경 (Principle 5 위반: vst3sdk 변경 금지 와 유사한 룰을 JUCE 에도 적용해야 함) → Option B.2 가 더 적은 yak-shave. 기각.

#### Option A'' — conan/vcpkg user-local X11 prefix (sudo 회피)

**개요:** sudo 없이 user-local 디렉토리 (`~/.local/x11/`) 에 X11 dev headers + libfreetype + libasound 를 vcpkg/conan 으로 unpack. `bootstrap_juce.sh` 가 이 prefix 를 `CMAKE_PREFIX_PATH` 에 prepend. `juce_add_plugin` 정상 작동.

**정량 비교:**
- 시간 비용: vcpkg/conan setup + libX11 + libXrandr + libXinerama + libXcursor + libfreetype6 + libasound2 등 ≥6 패키지 빌드 = 초기 2-4h (machine-dependent). 추후 매 환경마다 재실행 필요.
- 라이선스: X11 (MIT-like), freetype (FTL/GPLv2), alsa (LGPL). Educational tier + GPLv3 fallback 시 모두 호환.
- sysdep 깊이: Phase D6 까지 의존 누적 — Phase D6 의 full custom editor 가 결국 X11 필요 시 재사용 가능 (positive spillover). but 현재 spatial_engine 자체는 PySide6 UI 별도 process 로 X11 사용 — 중복 구축.
- Critical path 영향: 본 환경에 sudo 없는 영구 제약 (`§16.1` 트리거) 이 reset 안 됨 → user-local prefix 가 매 새 워크스테이션에서 재구축 → **portability 약화**.

**기각 결정:** Option B.2 가 portable (vst3sdk 헤더 only), user-local prefix 는 매 환경 재구축 필요 + Phase D6 까지 의존 누적이 spatial_engine 의 단순성 (Driver #3) 침해. 기각. **단, Phase D6 의 full custom editor 진입 시 재고려 항목으로 등록** (§8 Follow-ups D6.g).

### **권장 선택 (v3):** **Option B.2 default**, Option B.3 fallback (Step 0.c 에서 `module_linux.cpp` cmake mock build 실패 시 자동 강등). Option A'/A'' 는 위 정량 비교로 기각, Option A 자체는 §16.1 트리거로 폐기.

> **단일 viable option 보강 (Critic R1 H5):** B.2 가 default 로 채택되지만 B.3 fallback 이 viable 한 second option 으로 보존됨. Option A/A'/A'' 의 invalidation rationale 이 명시적으로 위에 기록됨.

> **v1→v2 변경:** §3 전면 재작성 (AM-1). v1 의 B.1 (Steinberg validator) 은 fact-error (vendored 트리 부재) 로 폐기. B.2 default 승격. Option A 회피책 2개 sub-option (A'/A'') 추가 평가 + 기각 결정 명문화. fair-alternatives 의무 강화.
> **v2→v3 변경:** B.2 Pros 의 sysdep 인용을 vendored grep 결과로 정정 (AM-R3-2): POSIX 5 헤더 + libstdc++ filesystem (`module_linux.cpp` line 30-67 verbatim include 목록).
> **v3→v4 변경 (R-4):** filesystem 헤더 `<experimental/filesystem>` + `<filesystem>` 표현을 "`USE_EXPERIMENTAL_FS` macro 로 compile-time 1개 선택 (mutex)" 으로 정정. concurrent include 가 아님 명시.

---

## 4. Pre-mortem (5 시나리오, D+9)

> **AM-8 추가:** 시나리오 4 (`IConnectionPoint::notify` host thread guarantee 차이) + 시나리오 5 (vst3sdk ABI pin 정책) 신설. AM-5 로 시나리오 3 (License) 재작성.
> **AM-R3-10 (v3):** 시나리오 4 의 host-별 thread guarantee 인용을 명시화 — 정식 SDK 표준 문서 인용 부재 시 "관찰 사례 (3rd party reports)" 로 강등 명시. AM-R3-8 (v3) 으로 시나리오 3 의 helper 6개 BSD-3 검증 grep 결과 명시.

### 시나리오 1 — vst3sdk 헤더-only 빌드가 transitive 로 X11/freetype 끌어옴

**무엇이 잘못됐나:** `pluginterfaces/` 가 자체로는 sysdep-free 라 가정했지만, `public.sdk/source/vst/{vstcomponentbase,vstcomponent,vsteditcontroller,vstparameters}.cpp` 의 helper 가 transitive 로 plugin GUI stack 헤더를 #include 해서 X11/freetype 가 다시 필요해진다.

**조기 신호:**
- Step 1 의 `cmake -B build_vst3_on -DSPATIAL_ENGINE_VST3=ON ..` 에서 `Cocoa/...` (macOS) 또는 `X11/Xlib.h` (Linux) 헤더 not found
- `nm` 결과에 X11 weak 심볼 노출
- `ldd build_vst3_on/vst3/spatial_engine_vst3.so | grep -E 'libX11|libfreetype|libasound|libwebkit'` 출력 ≥ 1

**회피:**
- Step 1 에 **selective include 화이트리스트** 강제 (Principle 2): `pluginterfaces/base/*.h` + `pluginterfaces/vst/*.h` + `public.sdk/source/{vst,common}/*.cpp` (단 `pluginview.cpp` 제외) + `public.sdk/source/vst/hosting/{module,module_linux,hostclasses}.{cpp,h}` + **(v4 AM-R4-4 NEW)** `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` (helper 6개 transitive 의존, BSD-3 file-level header 검증 필요) 만 source list 진입.
- 추가 시점 마다 Step 1 의 X11 grep 게이트 재통과 검증 (M1: `LD_DEBUG=libs` env + `strings` 명령 보강 — AM/M1).
- **fallback:** transitive 가 어쩔 수 없이 끌고 오면 helper `.cpp` 를 source list 에서 제외하고 vtable 직접 작성. 그래도 fail 시 Option B.3 (M2 부채 회계) 로 강등 + Phase D6 에서 정식 sysdep 정리.
- **(v4 AM-R4-4 보강):** `base/source/*` 가 transitive sysdep (예: `<windows.h>` 또는 `<X11/...>`) 끌어오면 Step 0.c entry gate 추가 grep 으로 차단 — `grep -rE '<X11/|<windows\.h>|<freetype' core/JUCE/.../VST3_SDK/base/source/ core/JUCE/.../VST3_SDK/base/thread/` == 0 expected.

### 시나리오 2 — `IAudioProcessor::process` 의 `inputParameterChanges` 드레인이 RT-unsafe

**무엇이 잘못됐나:** vst3sdk 의 `Vst::IParameterChanges` API 가 `Vst::IParamValueQueue::getPoint` 호출에서 가상 호출 chain 이 길어 RT 위반 (`RT_ASSERT_NO_ALLOC` fail). 또는 host 가 보내는 `IParamValueQueue` 의 `getPointCount() > 1` 일 때 sample-accurate point 사이 interpolation 시도하면 alloc.

**조기 신호:**
- `test_vst3_audio_smoke.cpp` 의 1000회 process loop 에서 `RT_ASSERT_NO_ALLOC` 발현
- `nm` 가 `operator new` 호출 emit 잡음

**회피:**
- Step 2 작업 1: process 진입점에서 `inputParameterChanges` snapshot 을 stack-allocated `std::array<float, 6>` 에 즉시 복사 (queue 의 last point 만 채택, block-rate). virtual 호출은 6회 고정.
- Step 2 작업 2: `RT_ASSERT_NO_ALLOC` 1000회 fixture 추가.
- **fallback:** virtual 호출 자체가 unsafe 면 vst3sdk 의 `ParamValue` snapshot 을 컴포넌트 측 lock-free SPSC ring 에 미리 적재하는 layer 추가 (host event-thread 가 enqueue, audio thread 가 drain). 단, `engine_->dispatchCommand` 의 RT-safety 검증 (§5 #5 ctest 확장) 통과 시 SPSC ring 폐기 — audio thread 에서 직접 호출 default.

### 시나리오 3 — VST3 SDK 라이선스 dual-strand + spatial_engine 자체 라이선스 미정 (AM-5 재작성, AM-R3-8 v3 보강)

**무엇이 잘못됐나:** vst3sdk root `LICENSE.txt` (Critic grep 확인) = dual-license:
- (a) **Steinberg VST3 License** — written agreement 사전 필요 ("Before publishing a software under the proprietary license, you need to obtain a copy of the License Agreement signed by Steinberg Media Technologies GmbH").
- (b) **GPLv3 (case-by-case)** — written agreement 미회수 시 자동 fallback.

`public.sdk/LICENSE.txt` (Steinberg 자사 BSD-3, separate file) 와 `public.sdk/source/vst/hosting/module_linux.cpp` (Steinberg 자사 BSD-3, file-level header) 는 **별개 strand** — vst3sdk root LICENSE 에 영향받지 않음.

**v3 정정 (AM-R3-8): public.sdk/source/vst helper 6개 license header 검증 grep 결과:**
```bash
$ for f in vstcomponentbase.cpp vstcomponent.cpp vsteditcontroller.cpp vstparameters.cpp vstbus.cpp vstpresetfile.cpp; do
    head -40 core/JUCE/.../VST3_SDK/public.sdk/source/vst/$f | grep -E '(BSD|GPL|License|Copyright|Redistribution)'
  done
=== vstcomponentbase.cpp === LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
=== vstcomponent.cpp ===     LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
=== vsteditcontroller.cpp ===LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
=== vstparameters.cpp ===    LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
=== vstbus.cpp ===           LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
=== vstpresetfile.cpp ===    LICENSE / (c) 2023, Steinberg / Redistribution and use ... permitted ... AS IS  → BSD-3-Clause
```
**6개 모두 BSD-3-Clause "Redistribution and use in source and binary forms, with or without modification, are permitted ... THIS SOFTWARE IS PROVIDED ... AS IS"** 표준 문구 검증 완료. → §4 시나리오 3 strand 분리 가능 (vst3sdk root LICENSE GPLv3 strand 가 helper `.cpp` 에 적용 안 됨, file-level BSD-3 우선).

**spatial_engine 자체 라이선스 미정** 상태로 GPLv3 fallback 자동 동의 시:
- Phase A 산출물: PySide6 UI (PySide6 LGPLv3 — 호환), OSC bridge (자체 코드) → GPLv3 전염
- Phase B 산출물: FDN reverb / IRConvolutionStub (자체 코드) → GPLv3 전염
- Phase C 산출물: vst3 plugin (vst3sdk 헤더 + 자체 코드) → GPLv3 전염

**조기 신호:**
- `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt` 검토 시 GPLv3/Steinberg 택일 강제 표현 발견 (이미 확인됨)
- Step 0.a 라이선스 written confirmation 미회수
- `kSpatialEngineProcessorUID` 같은 128-bit CID 가 Steinberg 등록 portal 에 charging 안 되면 호스트 충돌 위험.

**회피 (v3 정정 AM-R3-5):**
- Step 0.a entry checklist 에 다음 3 strand 분리 결정 (Steinberg agreement 동의는 **deferral 가능**):
  1. **vst3sdk root LICENSE.txt** (Steinberg agreement OR GPLv3): **spatial_engine 자체 라이선스 미정 + 학술용도 = GPLv3 fallback 자동 동의 default**. 변호사 게이트는 commercial release 시점 (Phase D6.f 이후) 으로 deferral. **C2 진행 차단 사유 아님** (AM-R3-5).
  2. **public.sdk/LICENSE.txt + module_linux.cpp + 6 helper `.cpp`** (BSD-3 — 6개 grep 결과 위 잠금): 추가 동의 불필요, 호환.
  3. **spatial_engine 자체 라이선스**: GPLv3 fallback 자동 동의 — Phase A/B/C 산출물 모두 GPLv3 전염 (M3: 호환성 표 추가).
- Phase A 산출물 라이선스 호환성 표: PySide6 (LGPLv3 → GPLv3 호환), OSC liblo (LGPLv2.1 → GPLv3 호환), FDN/Hadamard 자체 구현 (GPLv3 자동 동의 가능).
- CID 는 자체 발급 (Phase D6 정식 등록 전까지 collision 회피용 random UUID v4 base64; ADR Consequences 명시; DEV prefix 표식 — C2-Q14 결정).
- **fallback:** GPLv3 fallback 자동 동의 default 가 학술용도에 부합 → Step 0 entry 차단 사유 아님 (v3 강등). 단, M3 호환성 표는 여전히 사전 작성 (결정 추적용).

### 시나리오 4 (NEW, AM-8) — `IConnectionPoint::notify` host-별 thread guarantee 차이 (v3 AM-R3-10 출처 보강)

**무엇이 잘못됐나:** SDK `IConnectionPoint::notify` 메시지 채널은 component↔controller **양방향 통신** 인데, host-별 thread guarantee 가 다름:
- Cubase / Nuendo: main thread 보장 (Steinberg 표준 권장 — Steinberg VST3 Workshop docs `VST3_API/Index.html` 의 "audio plugin processing model" 섹션 인용 — vendored 트리에 docs 미포함, **upstream 출처만**).
- Reaper: audio thread 에서 호출 가능 (Cockos forum thread "Plugin thread model" 관찰 사례 — **3rd party report**, 정식 SDK 표준 문서 인용 부재).
- Bitwig: hybrid (host event-thread + audio thread 가능 — Bitwig developer forum Linux native plugin thread guarantees 관찰 사례 — **3rd party report**).

**v3 정정 (AM-R3-10):** Steinberg 정식 SDK 표준 문서 인용 (vendored 트리에 docs 미포함) 부재 → 위 3 host 의 thread 차이는 **"관찰 사례 (3rd party reports)" 수준** 으로 강등 명시. Architect 의 fair-fact 의무 충족 위해 **회피책 default = "host-specific thread 가정 회피, 항상 SPSC ring 통한 audio thread isolation 또는 SDK 표준 `IComponentHandler::performEdit` + `inputParameterChanges` 만 사용"**.

→ controller 가 component 에 notify 로 메시지 보내고, component 가 audio thread 에서 그 메시지 수신해 `cmd_fifo_.push` 시도하면 RT-unsafe lock (cmd_fifo SPSC 가 single-producer 가정 깨짐).

**조기 신호:**
- Reaper 에서 plugin 로딩 후 fast param drag 시 `audio_underrun_count` 증가
- `RT_ASSERT_NO_ALLOC` fail
- `cmd_fifo_.push` 의 thread-id 로깅 시 audio thread id 검출

**회피 (AM-6 으로 채택, v3 AM-R3-10 출처 보강):**
- **`IConnectionPoint::notify` 채널 사용 안 함.**
- host parameter change 는 `IComponentHandler::performEdit` (host-side, controller 측 user gesture 콜백 — `ivsteditcontroller.h:175-181` UI-Thread context SDK 표준 보장 인용) → host event-thread 가 audio thread 의 `IAudioProcessor::process` 의 `inputParameterChanges` queue 에 적재 (host 책임, `ivstaudioprocessor.h:317` SDK 표준 보장) → component 가 process() 시작 시 stack snapshot 으로만 read.
- controller↔component 가 별 instance 인 SDK 사실 활용 — process pointer 공유 못함, 따라서 `engine_->dispatchCommand` 직접 호출 경로 (v1 C2-Q12 권장 ②) 자동 차단.
- **host-specific thread 가정 회피 default:** SDK 표준 보장 경로만 사용, 3rd party 관찰 사례 의존 0.

### 시나리오 5 (NEW, AM-8) — vst3sdk ABI pin 정책 — JUCE 7.0.12 번들 SDK stale

**무엇이 잘못됐나:** JUCE 7.0.12 의 번들 vst3sdk (`core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/`) 는 JUCE 시점 SDK snapshot. Steinberg 의 vst3sdk upstream 이 후속 ABI 변경 (예: `IAudioProcessor` 의 `processBlock` 추가 method) 시 우리 build 가 stale ABI 로 link 됨. DAW 가 새 ABI 요구하면 plugin 로딩 실패.

**조기 신호:**
- Reaper / Bitwig 최신 버전 (2026 후반) plugin 로딩 시 "Unsupported VST3 version" 경고
- vst3sdk upstream `master` 의 `pluginterfaces/vst/ivstcomponent.h` 와 우리 트리 diff 가 ABI-affecting

**회피 (v4 AM-R4-1 CRITICAL fix):**
- **v3 의 SHA pin 명령은 fact-error CRITICAL — outer git repo 가 `core/JUCE/` gitignore (`.gitignore:13`) 로 `git ls-tree -r HEAD core/JUCE/...` 결과 0 entries → empty SHA `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` trivially pass.** v4 에서 inner JUCE repo cd 명령으로 정정:
- `.ci/vst3sdk_sha.txt` 신규 (M-NEW): vst3sdk 트리 (`core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/`) 의 **file-level blob hash 의 SHA256** (inner JUCE repo 기준).
  ```bash
  # v4 정정 (AM-R4-1 CRITICAL): inner JUCE repo cd 필수 (outer repo 는 core/JUCE 가 gitignored)
  ( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum > .ci/vst3sdk_sha.txt
  # 검증: inner JUCE repo 기준 122 entries (Critic R3 cross-check 완료) → non-empty SHA 산출
  ```
  (트리 전체 file-level blob hash 의 SHA256, reproducible — inner `core/JUCE/.git` 의 git ls-tree 가 mode + type + blob-SHA + path 를 122 line emit, sha256sum 이 그 stream 의 SHA256 계산. JUCE 버전 bump 시 자동 변화 검출.)
- **대안 옵션 (R-fallback):** file content hash:
  ```bash
  find core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum > .ci/vst3sdk_sha.txt
  ```
  또는 JUCE upstream SHA dual-pin (`bootstrap_juce.sh` `EXPECTED_SHA=4f43011b...`) proxy.
- JUCE 버전 bump (7.0.12 → 7.0.x) 시 수동 reconciliation: 우리 vst3 코드의 ABI 호환성 ctest (`test_vst3_param_layout`/`test_vst3_audio_smoke`) 자동 검증.
- **fallback:** ABI 미스매치 시 vst3sdk 트리를 JUCE 트리 외부 (`vendor/vst3sdk/`) 로 별도 vendoring + 독립 update 정책. Phase D6 까지 deferral 가능.

> **v1→v2 변경:** 시나리오 3 재작성 (AM-5) — vst3sdk dual-license + public.sdk BSD-3 별개 분리 + spatial_engine 자체 라이선스 미정 → GPLv3 전염 영향 정량 평가. 시나리오 4 (AM-8 IConnectionPoint::notify host thread guarantee) + 시나리오 5 (AM-8 ABI pin) 신설.
> **v2→v3 변경:** 시나리오 3 에 helper 6개 BSD-3 grep 결과 verbatim 잠금 (AM-R3-8). 시나리오 3 회피책의 Steinberg agreement entry gate 를 deferral 로 강등 (AM-R3-5). 시나리오 4 의 host thread guarantee 인용 부재 명시 + "host-specific 가정 회피" default 명문화 (AM-R3-10). 시나리오 5 의 SHA pin 정의를 git ls-tree 명령으로 잠금 (AM-R3-6).
> **v3→v4 변경:** 시나리오 1 회피 list 에 `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 추가 (AM-R4-4). 시나리오 5 SHA pin 명령 정정 — outer repo 의 `core/JUCE/` gitignore 로 인한 empty SHA fact-error → inner JUCE repo cd 명령 으로 정정 (AM-R4-1 CRITICAL).

---

## 5. 확장 테스트 플랜 (Deliberate)

> Option A 의 §5 (~145 assertion) 에서 APVTS 의존 항목 제거 + vst3sdk 직접 항목 추가 + AM-7 host fixture 분리. 총 ~125 assertion 목표.
> **v3 AM-R3-9 (ctest dependency 메커니즘):** `set_tests_properties(... DEPENDS ...)` 명시. AM-R3-7 (v3): DAW confirm 에서 Cubase deferral.

### Unit (ctest, `-UNDEBUG` 강제)

> **AM-7 재구성:** controller-component pair fixture 를 별도 `test_vst3_host_fixture.cpp` 로 분리 — 4 ctest (param-layout / param-roundtrip / state-persist / bypass) 의 prerequisite. `test_vst3_audio_smoke` 는 component-only (controller 없이도 동작).

0. **`test_vst3_host_fixture`** (9 assertion (v4 NOTE-R3-C +1), prerequisite for #1/#2/#4/#5)
   - `dlopen` `spatial_engine_vst3.so` → `GetPluginFactory()` non-null
   - `IPluginFactory::countClasses()` == 2 (Component + Controller)
   - `createInstance(processorCID, IComponent::iid)` 성공 + non-null
   - `createInstance(controllerCID, IEditController::iid)` 성공 + non-null
   - `IConnectionPoint::connect(component, controller)` 양방향 OK
   - mock `IComponentHandler` 등록 + `performEdit` 콜백 capture
   - lifecycle: `IComponent::initialize` → `setActive(true)` → `setActive(false)` → `terminate` 정상
   - dlclose 후 leak 0 (valgrind 자동 검증 — local-runner only)
   - **(v4 NOTE-R3-C NEW)** `IPluginFactory2` query negative test — vtable 직접 작성 default 가 IPluginFactory v1 만 지원하므로 `kNotImplemented` 또는 `kNoInterface` 반환 검증:
     ```cpp
     Steinberg::IPluginFactory2* factory2 = nullptr;
     auto result = factory->queryInterface(Steinberg::IPluginFactory2::iid, (void**)&factory2);
     assert(result == Steinberg::kNotImplemented || result == Steinberg::kNoInterface);
     assert(factory2 == nullptr);
     ```
1. **`test_vst3_param_layout`** (10 assertion, depends on #0)
   - `IEditController::getParameterCount() == 6`
   - 6 param 의 `ParameterInfo`: id / title / units / step count / default normalized / flags 검증
   - tolerance: choice param step count = 정수 일치, float param tolerance 0
2. **`test_vst3_param_roundtrip`** (24 assertion = 6 param × 4 query, depends on #0)
   - 각 param: `normalizedToPlain(0.0)` / `normalizedToPlain(1.0)` / `plainToNormalized(default)` / `getParamStringByValue` 라운드트립
   - tolerance: float 1e-5 (gain skew 적용 후), choice 정수 일치
3. **`test_vst3_audio_smoke`** (8 assertion, **component-only**, no controller dependency)
   - in-process loader 가 plugin 의 `IComponent::setActive(true)` → `IAudioProcessor::process` 1000회 호출
   - `RT_ASSERT_NO_ALLOC` 자동 검증 (NDEBUG strip 안 됨)
   - silence in → silence out (allowance 1e-7 RMS), 6 param mid value 시 mono → stereo 패닝 sanity check (+per-sample max |Δgain| < 1/MAX_BLOCK + FFT 검증 — Critic v0 MA #8 패턴 재사용)
4. **`test_vst3_state_persist`** (12 assertion, depends on #0)
   - `IComponent::getState(IBStream)` → 새 component instance → `setState(IBStream)` → 6 param normalized value 라운드트립
   - schema v1 + v=2 (forward incompatible) → 로그 + 기본값 유지 (§15.F 패턴)
   - schema 미지정 → 기본값 유지
5. **`test_vst3_bypass`** (6 assertion + dispatchCommand RT-safety 3 assertion (v4 AM-R4-5 +2), depends on #0) — **v3 AM-R3-3 + v4 AM-R4-5 확장**
   - `IAudioProcessor::setProcessing(false)` → silence out, latency 보고 0
   - `setProcessing(true)` → 정상 처리
   - bypass on/off 라운드트립 1000회 RT-safe
   - **NEW (v3 AM-R3-3):** `engine_->dispatchCommand` 1000회 loop 에서 `RT_ASSERT_NO_ALLOC` (alloc 0) + `pthread_mutex_*` 호출 0 (mutex 0) 검증 — 통과 시 audio thread 에서 직접 호출 default, SPSC ring 폐기 결정.
   - **NEW (v4 AM-R4-5):** `OSCBackend::injectCommand` **직접 호출** 1000회 RT-safety 검증 — `dispatchCommand` 가 `osc_backend_.injectCommand(cmd)` 인라인 호출하므로, injectCommand 자체의 alloc 0 / mutex 0 분리 검증 필요 (Critic R3 MA-5 지적: §5 #5 가 dispatchCommand 만 검증, injectCommand 의 SPSC push 검증 누락).
   - **NEW (v4 AM-R4-5):** `cmd_fifo_.push` thread-id capture 검증 — SPSC (single-producer single-consumer) 가정 위반 시 즉시 fail. Audio thread 단일 호출 (host event-thread 의 `inputParameterChanges` 적재 → audio thread 만 push) 보장 검증. Multiple-producer SPSC 위반 (예: control thread 가 직접 push) 시 ctest fail. 구현:
     ```cpp
     std::thread::id capture_tid = std::thread::id{};
     // hook OSCBackend::injectCommand → cmd_fifo_.push 의 thread::id 캡처
     // 1000 loop 후 capture_tid 가 단일 thread (audio thread) 인지 검증
     assert(capture_tid == audio_thread_id);
     ```
6. **`test_vst3_ndebug_enforce`** (1 assertion via `scripts/check_test_ndebug.sh` 확장)
   - 위 6 ctest 모두 `-UNDEBUG` 컴파일 플래그 정확히 적용 검증 (스크립트가 `vst3/tests/` 도 스캔)

### ctest dependency 메커니즘 (v3 AM-R3-9 신설)

```cmake
add_test(NAME vst3_host_fixture COMMAND test_vst3_host_fixture)
add_test(NAME vst3_param_layout COMMAND test_vst3_param_layout)
set_tests_properties(vst3_param_layout PROPERTIES DEPENDS vst3_host_fixture)
add_test(NAME vst3_param_roundtrip COMMAND test_vst3_param_roundtrip)
set_tests_properties(vst3_param_roundtrip PROPERTIES DEPENDS vst3_host_fixture)
add_test(NAME vst3_state_persist COMMAND test_vst3_state_persist)
set_tests_properties(vst3_state_persist PROPERTIES DEPENDS vst3_host_fixture)
add_test(NAME vst3_bypass COMMAND test_vst3_bypass)
set_tests_properties(vst3_bypass PROPERTIES DEPENDS vst3_host_fixture)
add_test(NAME vst3_audio_smoke COMMAND test_vst3_audio_smoke)
# audio_smoke 는 component-only, host_fixture 의존 없음 (controller pair 미사용)
```
이 메커니즘으로 `test_vst3_host_fixture` 실패 시 의존 4 ctest 자동 NOT_RUN 처리 (skip 아님 — ctest 가 `Skipped` 으로 보고).

### Integration

7. **CMake configure / build matrix** (각 1 assertion = 4)
   - `cmake -B build_off -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON` → `libspe_core.a` + `spatial_engine_core` 빌드 → byte-identical sha256 게이트 통과
   - `cmake -B build_on -DSPATIAL_ENGINE_VST3=ON` → `spatial_engine_vst3.so` 빌드 → ldd 에 `libX11` / `libfreetype` / `libasound` / `libwebkit` 부재 (sysdep-free 게이트, M1 보강: `LD_DEBUG=libs ./build_on/vst3/tests/test_vst3_host_fixture` 실행 후 `dlopen` 결과 strings 에 X11 부재)
   - `nm --defined-only build_on/spatial_engine_vst3.so | grep -c "GetPluginFactory"` == 1
   - OFF-baseline 재생성 시 Driver #1 dual-gate (`.ci/off_baseline.{bytes,symbols}.sha256`) 통과
8. **vst3sdk SHA pin grep** (1 assertion, AM-8 시나리오 5 회피, v3 AM-R3-6 잠금)
   - `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt` 가 변경되지 않았음 (spec drift 방지 — `git ls-files -s` SHA pin 검증)
   - `.ci/vst3sdk_sha.txt` 의 vst3sdk 트리 SHA256 (`git ls-tree -r HEAD vst3sdk_path | sha256sum`) 와 실제 트리 SHA256 일치 검증

### E2E (옵션, confirm-only) — v3 AM-R3-7 정정

9. **DAW 실호스트 confirm-only:** **Reaper (Linux native, free) 또는 Bitwig (Linux native) 중 1종 이상** D+9 confirm. **Cubase 는 commercial DAW 부재 시 Phase D6 까지 deferral.** (AM-R3-7 — v2 의 "Cubase 추가 confirm 권장" 은 commercial license 부재 환경에서 schedule pressure 야기, v3 deferral)

### Observability

10. **CI 로그:**
   - 매 sub-step 의 `cmake --build` 시간, `ctest` 통과 수, sha256 게이트 결과 (양쪽) 기록
   - `nm` symbol diff 변화 시 경고
   - M2 결정 추적 라인 1줄 (B.2 host fixture 빌드 OK / B.3 부채 강등)
   - `LD_DEBUG=libs` 로딩 trace 보관 (M1 보강)

> **v1→v2 변경:** AM-7 으로 `test_vst3_host_fixture` (8 assertion) 신설 + 4 ctest 의 prerequisite 명시. test_vst3_audio_smoke 는 component-only 로 명시. AM-8 시나리오 5 회피용 vst3sdk SHA pin grep ctest 추가. M1 보강: `LD_DEBUG=libs` + `strings` 명령. v0 MA #8 패턴 (FFT 클릭 검증) 을 audio_smoke 에 적용.
> **v2→v3 변경:** #5 bypass ctest 에 `dispatchCommand` RT-safety 검증 1 assertion 추가 (AM-R3-3 — SPSC ring 사용 여부 결정 게이트). ctest dependency 메커니즘 cmake 코드 명시 (AM-R3-9). #9 DAW confirm 에서 Cubase deferral, Reaper OR Bitwig 1종 이상 (AM-R3-7).
> **v3→v4 변경:** #0 host fixture 에 IPluginFactory2 query negative test 1 assertion 추가 (NOTE-R3-C, 8→9). #5 bypass ctest 에 `OSCBackend::injectCommand` 직접 호출 RT-safety + `cmd_fifo_.push` thread-id 단일 검증 2 assertion 추가 (AM-R4-5, 7→9 total). 총 assertion ~125 → ~128.

---

## 6. 구현 시퀀스 (4 step + 분리 Step 0)

### Step 0 — Bootstrap & License & Cleanup (≤ 1d, **분리 commit**, M4)

**작업 (v3 AM-R3-4 강제 5단계 순서 + AM-R3-5 license deferral + AM-R3-6 SHA pin 정의):**

- **0.a (entry gate, AM-5 + v3 AM-R3-5 강등):** 다음 3 strand 결정을 `.omc/plans/c2-licensing.md` 에 기록:
  1. vst3sdk root LICENSE.txt (Steinberg agreement OR GPLv3): **spatial_engine 자체 라이선스 미정 + 학술용도 = GPLv3 fallback 자동 동의 default**. 변호사 게이트는 Phase D6.f (commercial release) 로 deferral. **C2 진행 차단 사유 아님** (AM-R3-5).
  2. public.sdk/LICENSE.txt + module_linux.cpp + 6 helper `.cpp` BSD-3 (AM-R3-8 grep 결과 잠금): 추가 동의 불필요, 호환 확인.
  3. spatial_engine 자체 라이선스: GPLv3 fallback 자동 동의 + Phase A 산출물 라이선스 호환성 표 (PySide6 LGPLv3 / OSC liblo LGPLv2.1 / FDN 자체 GPLv3 — M3) 작성 commit.

- **0.b (cleanup, v3 AM-R3-4 강제 5단계 순서):**
  ```bash
  # 1. 현재 git 상태 확인
  git status

  # 2. staged-delete 확정 (이미 D 면 no-op)
  git rm --cached vst3/SpatialEngineVST3.cpp 2>/dev/null || true

  # 3. 수정된 vst3/CMakeLists.txt 를 HEAD 로 리셋 (juce_add_plugin 변경 되돌리기)
  git checkout HEAD -- vst3/CMakeLists.txt

  # 4. untracked 4개 plain rm
  rm vst3/AudioBlockAdapter.hpp
  rm vst3/SpatialEngineProcessor.hpp
  rm vst3/SpatialEngineProcessor.cpp
  rm -rf vst3/tests/

  # 5. cleanup commit
  git add -u vst3/
  git commit -m "chore(C2): roll back JUCE Option A WIP — Option B 활성 결정 적용"
  ```
  **v4 NOTE-R3-B failure mode 명시:** Step 1-4 사이 swap (예: 4 → 3 → 2 → 1) 은 결과 동일 (`--cached` 플래그가 working tree 보존). 단 **Step 5 (commit) 가 Step 1-4 보다 먼저 실행되면 untracked 4 + modified 1 이 cleanup commit 에서 누락 → Principle 6 (단일 commit 의 bisect 회복비용) 위반**. 다른 1-4 순서 swap 은 working tree 일관성 보존 → 유일한 실질 failure mode 는 Step 5 prefix.

  부수 작업:
  - `scripts/check_test_ndebug.sh` 가 `vst3/tests/` 도 스캔하도록 확장 (별 commit 또는 cleanup 에 포함)
  - `.ci/vst3sdk_sha.txt` 신규 생성 (**v4 AM-R4-1 CRITICAL fix** — outer git repo 의 `core/JUCE/` gitignore 로 인한 empty SHA fact-error 정정, inner JUCE repo cd 명령으로 변경):
    ```bash
    # v4 AM-R4-1 CRITICAL FIX: outer repo (.gitignore:13) 가 core/JUCE 차단 → inner JUCE repo cd 필수
    ( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum > .ci/vst3sdk_sha.txt
    # 검증: 122 entries (Critic R3 cross-check 완료) → non-empty SHA 산출
    ```
    **v3 의 `git ls-tree -r HEAD core/JUCE/...` (outer repo) 명령은 사용 금지** — `.gitignore:13` 으로 0 entries → empty SHA `e3b0c44...` trivially pass.

- **0.c (B.2 sysdep 검증 sub-step + v3 entry gate 추가):** `module_linux.cpp` cmake mock build — `cmake -B build_mock_host -DSPATIAL_ENGINE_VST3_HOST_MOCK=ON` 으로 host fixture 만 빌드 → `ldd build_mock_host/test_vst3_host_fixture | grep -cE 'libX11|libfreetype|libasound'` == 0 검증. NO-GO 면 §10 C2-Q10 = Option B.3 (부채 회계) 자동 강등. **L1 보강:** `set -e` + `[ -d module_linux.cpp의 디렉토리 ]` 가드 추가, false-pass 차단.
  - **v4 AM-R4-3 entry gate 정정 (HIGH):** v3 의 grep 명령은 `.cpp` 구현 부재 검증 의도였으나 `ipluginbase.h:201` 의 docstring (`* CPluginFactory::registerClass.`) 1건 매치되어 `wc -l == 1` (≠ 0) → 항상 NOTE-escalate logic flip. v4 에서 `.cpp` 한정 + comment exclude 로 정정:
    ```bash
    # v4 AM-R4-3 (HIGH) FIX: .cpp 한정 + comment line exclude
    grep -rn --include="*.cpp" "CPluginFactory::CPluginFactory\|CPluginFactory::registerClass\|CPluginFactory::createInstance" core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/ \
      | grep -vE '^\s*\*|^\s*//' \
      | wc -l
    # expected: 0 (헤더 docstring 제외, .cpp 구현 부재)
    ```
    또는 더 단순한 file existence check (대안):
    ```bash
    test ! -f core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/public.sdk/source/common/pluginfactory.cpp && echo "absent (vtable direct OK)"
    ```
    결과 = 0 (또는 file absent) → vtable 직접 작성 default 채택 확정 commit footer 에 기록.
    결과 ≠ 0 → 매크로 시퀀스 재고려 NOTE escalation.
  - **v4 NEW entry gate (AM-R4-4 sync):** `base/source/*` + `base/thread/*` BSD-3 file-level header 검증 grep:
    ```bash
    for f in fstring fobject fdebug fbuffer futils updatehandler; do
      head -40 core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/base/source/$f.cpp 2>/dev/null \
        | grep -E '(BSD|Redistribution|AS IS|Copyright)' || echo "$f.cpp: HEADER MISSING"
    done
    head -40 core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/base/thread/include/flock.h \
      | grep -E '(BSD|Redistribution|AS IS|Copyright)' || echo "flock.h: HEADER MISSING"
    # expected: 모두 BSD-3 표준 문구 검출
    ```
    결과 ≠ all-BSD → AM-R4-4 화이트리스트 회수 + 매크로 시퀀스 fallback 또는 vtable-only path (helper 6개 미사용) 진입.

- **0.d (baseline 재생성):** `.ci/off_baseline.bytes.sha256` / `.ci/off_baseline.symbols.sha256` 가 Step 0 작업 (특히 0.b 의 `scripts/check_test_ndebug.sh` 변경) 후에도 OFF 빌드 산출물 변화 없는지 재확인. OFF 빌드 산출물은 `if(SPATIAL_ENGINE_VST3)` 격리이므로 변화 0 기대.

**Exit (M4):**
- Step 0.a checklist 100% 회수 (`.omc/plans/c2-licensing.md` 3 strand 결정 commit; Steinberg agreement 회수 deferral 가능)
- Step 0.b commit 적용 후 `git status` clean (untracked 4 + staged-delete 1 + modified 1 모두 정리)
- Step 0.c 결과 = "B.2 host fixture sysdep-free OK" + **(v4 AM-R4-3)** "CPluginFactory `.cpp` 구현 부재 grep (`.cpp` 한정 + comment exclude) = 0 또는 pluginfactory.cpp file absent" + **(v4 AM-R4-4)** "base/source/* + flock.h BSD-3 file-level header 검증 OK" 명시 commit footer
- Step 0.d OFF 빌드 dual-gate 통과 (`.ci/off_baseline.bytes.sha256` 일치)
- `.ci/vst3sdk_sha.txt` 등록 — **(v4 AM-R4-1)** inner JUCE repo cd 명령 결과 (122 entries non-empty SHA)

### Step 1 — Plugin entry + IComponent + IAudioProcessor skeleton (1.5d, M1.a)

**작업 (AM-2 Step 1.1 source list + v3 AM-R3-1 vtable 직접 작성):**
- **1.1** `vst3/CMakeLists.txt` 재작성: `add_library(spatial_engine_vst3 MODULE ...)` 직접 사용. `juce_add_plugin` 미사용. Source list (selective include 화이트리스트, Principle 2):
  - 우리 산출물:
    - `vst3/SpatialEnginePluginFactory.cpp` — **v3 정정 (AM-R3-1) + v4 R-3 line drift 정정:** 우리 자체 `class SpatialEnginePluginFactory : public Steinberg::IPluginFactory` 구현 — 4 virtual method 직접 작성 (`pluginterfaces/base/ipluginbase.h:192-214` verbatim signature, 클래스 본체 L192, virtuals L197/202/205/208, IID L211/`DECLARE_CLASS_IID` L214):
      - `virtual tresult PLUGIN_API getFactoryInfo (PFactoryInfo* info)` (line 197)
      - `virtual int32 PLUGIN_API countClasses ()` (line 202, 우리 = 2 반환: Component + Controller)
      - `virtual tresult PLUGIN_API getClassInfo (int32 index, PClassInfo* info)` (line 205)
      - `virtual tresult PLUGIN_API createInstance (FIDString cid, FIDString _iid, void** obj)` (line 208)
      - + `extern "C" SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()` C export (`fplatform.h:164,231` SMTG_EXPORT_SYMBOL = `__attribute__ ((visibility ("default")))`)
      - `CPluginFactory` 클래스 **미사용** (vendored 트리에 .cpp 구현 부재 — Step 0.c grep 결과 (v4 AM-R4-3 정정 명령) = 0 잠금).
    - `vst3/SpatialEngineProcessor.cpp` (IComponent + IAudioProcessor)
    - `vst3/SpatialEngineController.cpp` (IEditController)
    - `vst3/SpatialEngineProcessData.hpp` (header-only adapter)
  - vst3sdk source (selective include 화이트리스트):
    - `public.sdk/source/vst/vstinitiids.cpp` (필수 — class IID 정의)
    - `public.sdk/source/vst/{vstcomponentbase,vstcomponent,vsteditcontroller,vstparameters,vstbus,vstpresetfile}.cpp` (helper, 화이트리스트 — 6개 모두 BSD-3 검증 완료 §4 시나리오 3; Step 1 의 X11 grep 게이트 통과 시 채택; transitive 끌어오면 vtable 직접 작성 fallback)
    - **(v4 AM-R4-4 NEW)** `base/source/{fstring,fobject,fdebug,fbuffer,futils}.{cpp,h}` + `base/source/updatehandler.{cpp,h}` (helper 6개 의 transitive 의존: `vstcomponentbase.cpp:38: #include "base/source/fstring.h"` + `vsteditcontroller.cpp:38: #include "base/source/updatehandler.h"`; `fstring.h` → `fobject.h`; `fobject.cpp` → `flock.h`. Step 0.c v4 NEW entry gate BSD-3 검증 통과 후 채택)
    - **(v4 AM-R4-4 NEW)** `base/thread/include/flock.h` (`fobject.cpp` thread 의존, BSD-3 file-level 검증 통과 후 채택)
    - `public.sdk/source/common/{memorystream,readfile}.cpp` (state IO — `pluginview.cpp` 제외)
  - **Factory 진입점 (v3 AM-R3-1 잠금):** v2 의 `BEGIN_FACTORY_DEF` 매크로 시퀀스 **완전 폐기** — vendored 트리에 `END_FACTORY` / `DEF_CLASS2` `#define` 0 hit + `CPluginFactory` `.cpp` 구현 부재. **vtable 직접 작성 default**.
  - Include path: `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/`
  - Link: `spe_core` (audio 콜백 forwarding 용); JUCE 모듈 link **0** (sysdep 회피).
- **1.2 (v4 AM-R4-2 CRITICAL fix)** `vst3/SpatialEngineProcessor.{hpp,cpp}` 신규 작성:
  - **v3 의 `Vst::AudioEffect` 클래스는 vendored 트리에 부재 — `find ... -iname "*audioeffect*"` = 0 hit. 실제 helper 클래스 = `class Component : public ComponentBase, public IComponent` at `public.sdk/source/vst/vstcomponent.h:52`.** v4 default 진입 경로:
    - **option-α (default):** `class SpatialEngineProcessor : public Steinberg::Vst::Component` (helper `vstcomponent.h:52` 상속) — Step 1 grep 게이트 + AM-R4-4 base/source/* BSD-3 검증 통과 시 helper 상속 default. `Component` helper 가 `IComponent` 와 `IAudioProcessor` 둘 다 wiring 미제공 (helper 는 IComponent 만) — **`IAudioProcessor` 는 다중 상속 추가 필요**: `class SpatialEngineProcessor : public Steinberg::Vst::Component, public Steinberg::Vst::IAudioProcessor`.
    - **option-β (fallback, helper transitive 끌어오기 실패 시):** `class SpatialEngineProcessor : public Steinberg::Vst::IComponent, public Steinberg::Vst::IAudioProcessor` 직접 다중 상속 — helper 미사용 path. `FUnknown` (`addRef`/`release`/`queryInterface`) 도 직접 구현.
  - `processSetup` / `process` 구현 — `process` 는 `Vst::ProcessData` 의 `inputs[0]` planar buffer → `spe::audio_io::AudioBlock` 어댑터 → `engine_->audioBlock(block)` 호출
  - `setActive` / `setProcessing` lifecycle
  - `inputParameterChanges` queue 의 last point 만 stack snapshot (block-rate, AM-6, v3 AM-R3-3 SDK 인용 잠금 + v4 R-3 line drift 정정 — `ivstaudioprocessor.h:316`)
- **1.3** `vst3/SpatialEngineProcessData.hpp` 신규 작성: `Vst::ProcessData` ↔ `spe::audio_io::AudioBlock` lock-free 어댑터 (포인터만 복사, 메모리 복사 없음).
- **1.4 (v3 AM-R3-1 정정 + v4 R-3 line drift):** `vst3/SpatialEnginePluginFactory.cpp` 신규 작성 — **vtable 직접 작성**:
  - `class SpatialEnginePluginFactory : public Steinberg::IPluginFactory { ... };` 4 virtual method 직접 구현 (signature `ipluginbase.h:192-214` verbatim, virtuals L197/202/205/208)
  - `kSpatialEngineProcessorUID` / `kSpatialEngineControllerUID` 자체 발급 UUID v4 + DEV prefix 표식 (C2-Q14 결정 권장)
  - `extern "C" SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()` C export — singleton instance 반환
  - `ModuleEntry` / `ModuleExit` Linux entry — `module_linux.cpp` 의 host loader 가 호출
  - **매크로 시퀀스 (`BEGIN_FACTORY_DEF` / `DEF_CLASS2` / `END_FACTORY`) 미사용** — vendored 트리에 부재 잠금
  - **(v4 NOTE-R3-C)** `queryInterface(IPluginFactory2::iid, ...)` / `queryInterface(IPluginFactory3::iid, ...)` 호출 시 `kNotImplemented` 또는 `kNoInterface` 반환 + out-param nullptr 검증 — host fixture #0 negative test 통과 필수.
- **1.5** `vst3/SpatialEngineController.{hpp,cpp}` skeleton (IEditController) — Step 2 에서 구현.
- **1.6** `vst3/tests/test_vst3_host_fixture.cpp` 신규 작성 (AM-7) — Step 0.c 의 mock host 를 정식 ctest 화. 이후 ctest 의 prerequisite. ctest dependency CMake 구성 (v3 AM-R3-9):
  ```cmake
  add_test(NAME vst3_host_fixture COMMAND test_vst3_host_fixture)
  set_tests_properties(vst3_param_layout vst3_param_roundtrip vst3_state_persist vst3_bypass
                       PROPERTIES DEPENDS vst3_host_fixture)
  ```

**Exit (M1.a):**
- `cmake -B build_vst3_on -DSPATIAL_ENGINE_VST3=ON && make spatial_engine_vst3` 통과 (X11 sysdep 부재 환경)
- `ldd build_vst3_on/vst3/spatial_engine_vst3.so | grep -E 'libX11|libfreetype|libasound|libwebkit'` 출력 0
- `LD_DEBUG=libs ./build_vst3_on/vst3/tests/test_vst3_host_fixture 2>&1 | grep -cE 'X11|freetype|alsa|webkit'` == 0 (M1 보강)
- `nm --defined-only build_vst3_on/vst3/spatial_engine_vst3.so | grep -c GetPluginFactory` == 1
- `nm --defined-only build_vst3_on/vst3/spatial_engine_vst3.so | grep -c "SpatialEnginePluginFactory::countClasses"` == 1 (v3 NEW — vtable 직접 작성 검증)
- **(v4 NOTE-R3-E NEW)** vtable footprint 4-method 모두 검증: `nm --defined-only build_on/vst3/spatial_engine_vst3.so | grep -cE "SpatialEnginePluginFactory::(getFactoryInfo|countClasses|getClassInfo|createInstance)"` == 4
- `test_vst3_host_fixture` 9 assertion 통과 (v4 NOTE-R3-C IPluginFactory2 negative test +1)
- `test_vst3_audio_smoke` ctest **skeleton** (in-process loader → process 1회) 통과
- OFF dual-gate 변화 없음

### Step 2 — IEditController + 6 host param + dispatchCommand wiring (2d, M1.b)

**작업 (AM-6 SDK 표준화 + v3 AM-R3-3 SDK 인용 + dispatchCommand RT-safety 게이트):**
- **2.1** `vst3/SpatialEngineController.cpp`: `IEditController::initialize` 에서 6 `Parameter` 등록:
  - `pan_az` (RangeParameter, [-π, π], default 0, flags=automate)
  - `pan_el` (RangeParameter, [-π/2, π/2], default 0)
  - `source_width` (RangeParameter, [0, π], default 0)
  - `master_gain` (RangeParameter, [-60, 6] dB, default 0, mid-skew 0dB — `normalizedToPlain` 자체 구현으로 Option A 의 `NormalisableRange::setSkewForCentre` 로직 재현)
  - `ambi_order` (StringListParameter, choice 3개 [1,2,3], default 1)
  - `room_preset_idx` (StringListParameter, choice 4개 [Dry, Small, Medium, Large], default 0)
- **2.2 (AM-6 으로 재작성, v3 AM-R3-3 SDK 인용 잠금 + v4 R-3 line drift 정정)** Component ↔ Controller 통신 — **`IConnectionPoint::notify` 채널 사용 안 함**:
  - controller 측 user gesture (UI param 변경) → controller 가 `IComponentHandler::performEdit(paramID, normValue)` 호출 (**SDK 인용 잠금 (v4 R-3 line 정정):** `pluginterfaces/vst/ivsteditcontroller.h:175-176` "To be called before calling a performEdit (e.g. on mouse-click-down event). **This must be called in the UI-Thread context!**" + `:179-180` (v4 정정, v3 의 `:180-181` off-by-1) "Called between beginEdit and endEdit ... **This must be called in the UI-Thread context!**" + `:183-184` "To be called after calling a performEdit ... **This must be called in the UI-Thread context!**" — 3회 반복) → host event-thread 가 처리.
  - host event-thread 가 audio thread 의 `IAudioProcessor::process` 의 `inputParameterChanges` queue 에 적재 (host 책임, **SDK 인용:** `pluginterfaces/vst/ivstaudioprocessor.h:316` (v4 R-3 정정, v3 의 `:317` off-by-1) "The Process call, where all information (parameter changes, event, audio buffer) are passed.").
  - component 가 `process()` 시작 시 queue 의 last point 만 stack snapshot → control thread (v3 default = host event-thread 신뢰 ②, AM-R3-3) → `engine_->dispatchCommand(cmd)` 호출.
  - **`IConnectionPoint::connect` 자체는 SDK 표준 절차이므로 호출** (Component ↔ Controller pair 가 SDK 의 connection point handshake 필요), 단 **`notify` 메시지 채널은 사용 안 함** (시나리오 4 회피, AM-R3-10).
- **2.3 (v3 AM-R3-3 재검토)** `process` 의 `inputParameterChanges` 드레인 (호스트 automation 경로):
  - process 진입 시 `IParameterChanges* inputParameterChanges` 가 nullptr 아니면 6개 param queue 만 검사
  - 각 queue 의 마지막 point 만 채택 (block-rate, sample-accurate 미지원 — Phase D6 deferral 명시)
  - **`engine_->dispatchCommand` RT-safety 결정 게이트:** §5 #5 ctest 확장 결과 (alloc 0 / mutex 0) 통과 → audio thread 에서 직접 호출 default, **SPSC ring 폐기**. 통과 안 하면 SPSC ring layer 1개 추가 (audio thread → background thread, Driver #2 정합성 재검증 — 별도 thread lifecycle plugin lifecycle 외 관리 명시).
  - **C2-Q12 v3 default = ② host event-thread 신뢰** (AM-R3-3) — controller 측 component std::thread 신설 안 함, host event-thread 의 SDK 표준 보장 신뢰.
- **2.4** Param ↔ Command 매핑 표 (ADR Consequences 와 동기):
  - `pan_az` / `pan_el` / `source_width` → `PayloadObjMove` (obj_id=0, az/el/dist=1.0); `source_width` 는 `PayloadObjDsp::Width` (obj_id=0)
  - `master_gain` → `PayloadObjGain` (obj_id=0; dB→linear 변환)
  - `ambi_order` → `PayloadSysAmbiOrder`
  - `room_preset_idx` → preset → reverb param 매핑 테이블 (FDN T60 + ER mix) 자체 구현 (C2-Q13 결정 권장)

**Exit (M1.b):**
- `test_vst3_param_layout` 10 assertion 통과
- `test_vst3_param_roundtrip` 24 assertion 통과 (gain skew 1e-5 tolerance)
- `cmd_fifo` enqueue 모니터링 fixture 가 6 param 변경마다 정확히 1 Command enqueue 검증
- `RT_ASSERT_NO_ALLOC` 1000회 process loop 통과
- `dispatchCommand` RT-safety 결정 게이트 (§5 #5 ctest 확장) 결과 commit footer 기록 (audio thread 직접 호출 OK / SPSC ring 추가)

### Step 3 — Bypass + State persistence (1d, M1.c)

**작업:**
- **3.1** `IComponent::setIoMode(IoMode::kAdvancedKBypass)` + `IAudioProcessor::setProcessing(false)` 처리 — bypass on 시 `process` 가 input → output passthrough (또는 silence)
- **3.2** `IComponent::getState(IBStream*)` 구현:
  - 6 param 의 normalized value 를 작은 binary header 로 직렬화: `[magic 'SPE1' u32][version u16=1][param_count u16=6][6 × float normalized]`
  - `memorystream.cpp` 화이트리스트 (Principle 2) 사용
- **3.3** `IComponent::setState(IBStream*)` 구현:
  - magic / version 검증 → v=1 만 허용; magic mismatch 또는 v≠1 → 기본값 유지 + `fprintf(stderr, ...)` 1회 (Driver #2 control thread 호출이므로 stderr 허용)
- **3.4** `test_vst3_bypass` + `test_vst3_state_persist` ctest 활성

**Exit (M1.c):**
- `test_vst3_bypass` 6+1 assertion 통과 (v3 AM-R3-3 dispatchCommand RT-safety 1 추가)
- `test_vst3_state_persist` 12 assertion 통과 (v1 라운드트립, v=2 fallback, 빈 state fallback)
- `RT_ASSERT_NO_ALLOC` bypass on/off 라운드트립 1000회 통과

### Step 4 — M2 게이트 결정 + 통합 검증 + 부채 회계 (1.5d, M2)

**작업:**
- **4.1** Step 0.c 결정 적용:
  - **B.2 채택 시 (default):** in-process VST3 host fixture (`test_vst3_host_fixture`) 가 strictness 검증 항목 자체 정의 (vst3sdk `vstvalidator` 표준 점검 항목 모방 — param count / process lifecycle / state IO). 이미 ctest 화. M2 게이트 통과.
  - **B.3 채택 시 (fallback):** `.omc/plans/c2-debt.md` 에 부채 명시 + Phase D6 entry 시 청구 항목 등록.
- **4.2** OFF dual-gate 최종 통과 검증 — `cmake -B build_off ... && make spe_core spatial_engine_core` 후 `.ci/off_baseline.{bytes,symbols}.sha256` 일치
- **4.3** sysdep-free 게이트 최종 — `ldd build_vst3_on/vst3/spatial_engine_vst3.so | grep -cE 'libX11|libfreetype|libasound|libwebkit'` == 0 + `LD_DEBUG=libs` runtime 확인 (M1)
- **4.4** vst3sdk SHA pin 검증 — `.ci/vst3sdk_sha.txt` ↔ 실제 트리 SHA256 (`git ls-tree | sha256sum`) 일치 (AM-8 시나리오 5, v3 AM-R3-6 잠금)
- **4.5 (v3 AM-R3-7 정정)** DAW confirm-only: 사용자가 **Reaper 또는 Bitwig 1종 이상** 에 plugin 로딩 → 6 param 동작 확인 (D+9 user confirm). **Cubase deferral** Phase D6.

**Exit (M2):**
- §5 ctest 7 (~125 assertion) 모두 통과
- OFF dual-gate 통과
- sysdep-free 게이트 통과 (LD_DEBUG 보강 포함)
- vst3sdk SHA pin 통과
- M2 게이트 = (B.2 host fixture 통과) or (B.3 부채 명시) — 둘 다 정직 회계

### Commit 구조 (Critic M4 패턴 재사용)

| commit | 범위 | prefix |
|---|---|---|
| 1 | Step 0 (license + cleanup 5단계 + baseline 재확인 + check_test_ndebug.sh 확장 + vst3sdk SHA pin + CPluginFactory grep entry gate) | `chore(C2B-bootstrap):` |
| 2 | Step 1 (entry + IComponent + IAudioProcessor skeleton + audio smoke + host fixture + IPluginFactory vtable 직접 작성) | `feat(C2B.1):` |
| 3 | Step 2 (IEditController + 6 param + dispatchCommand wiring + RT-safety 결정 게이트) | `feat(C2B.2):` |
| 4 | Step 3 (bypass + state persist) | `feat(C2B.3):` |
| 5 | Step 4 (M2 결정 + final gate + DAW Reaper/Bitwig confirm) | `chore(C2B.4):` |

각 commit 메시지에 Phase C ADR Principle 3 exception 인정 명시 (Option B 에선 fused 안 하고 5 step 분할 — Option A 보다 작은 단위라 부채 회복 비용 낮음). Principle 6 (commit 분할은 bisect 회복비용 우선) 으로 명문화 (§1).

### 조기 정지 + Fallback 게이트

- Step 0.c 에서 vst3sdk header-only 빌드가 X11 transitive dep 끌어옴 → 즉시 Option B.3 자동 강등 + §10 C2-Q10 escalation. Architect/Critic 라운드 3 결정 후 진행.
- Step 0.c 에서 `CPluginFactory .cpp` 구현 grep ≠ 0 → 매크로 시퀀스 재고려 NOTE escalation (v3 AM-R3-1 entry gate).
- Step 1 에서 `nm GetPluginFactory` count ≠ 1 → entry symbol 재작성 + 라운드 3 escalation.
- Step 2 에서 `RT_ASSERT_NO_ALLOC` fail → SPSC ring layer 추가; 그래도 fail 이면 §4 시나리오 2 회피책 검토.
- Step 4 의 M2 게이트 미달 → Option B.3 (부채 회계) 자동 강등 + 사용자 confirm.

> **v1→v2 변경:** §6 Step 1.1 source list 전면 재작성 (AM-2): `BEGIN_FACTORY_DEF` 매크로 (header-only) + selective `.cpp` 화이트리스트 (`pluginview.cpp` 제외 명시). Step 0.b cleanup 메커니즘 정정 (AM-3). Step 0.c sysdep 검증을 B.2 host fixture mock build 로 재정의. Step 2.2 통신 경로 `IComponentHandler::performEdit` 단일화 (AM-6).
> **v2→v3 변경:** §6 Step 1.1/1.4 매크로 시퀀스 폐기 → `IPluginFactory` 4 virtual method vtable 직접 작성 (AM-R3-1). Step 0.b cleanup 5단계 강제 순서 (AM-R3-4). Step 0.c entry gate 추가 — `CPluginFactory .cpp` 부재 grep (AM-R3-1). Step 0.a license 변호사 게이트 deferral (AM-R3-5). `.ci/vst3sdk_sha.txt` 생성 명령 명시 (AM-R3-6). Step 2.2 SDK 인용 verbatim 잠금 (AM-R3-3): `ivsteditcontroller.h:175-181` + `ivstaudioprocessor.h:317`. Step 2.3 dispatchCommand RT-safety 결정 게이트 추가 (AM-R3-3). Step 4.5 DAW Reaper/Bitwig 1종 이상 + Cubase deferral (AM-R3-7).
> **v3→v4 변경:** Step 0.b SHA pin 명령을 inner JUCE repo cd 로 정정 (AM-R4-1 CRITICAL — outer repo `.gitignore:13` empty SHA fact-error 해소). Step 0.b 에 NOTE-R3-B failure mode (Step 5 prefix 위반) 명시. Step 0.c entry gate 정정 — `.cpp` 한정 + comment exclude 또는 file existence check (AM-R4-3 HIGH, v3 logic flip 정정) + base/source/* BSD-3 검증 grep 추가 (AM-R4-4 sync). Step 1.1 source list 에 `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 추가 (AM-R4-4 HIGH). Step 1.2 base class `Vst::AudioEffect` → `Vst::Component` (option-α) 또는 IComponent+IAudioProcessor 직접 다중 상속 (option-β, fallback) (AM-R4-2 CRITICAL — vendored 트리에 AudioEffect 부재). Step 1.4 NOTE-R3-C IPluginFactory2/3 query negative 처리 명시. Step 2.2 SDK 인용 line drift 정정 — `:180-181` → `:179-180`, `:317` → `:316` (R-3). Exit (M1.a) 에 NOTE-R3-E vtable footprint 4-method `nm` 검증 추가. host fixture 8→9 assertion (NOTE-R3-C +1).

---

## 7. 파일 변경 목록 (절대경로 + 1줄 목적)

### 신규 (NEW — REWRITE 카테고리, L2)

> **L2 분리:** v1 의 `vst3/SpatialEngineProcessor.{hpp,cpp}` / `vst3/AudioBlockAdapter.hpp` / `vst3/tests/*` 는 untracked (Option A WIP) → Step 0.b plain rm 후 신규 작성. "REWRITE (untracked 폐기 → 신규)" 카테고리.

- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEnginePluginFactory.cpp` — **v3 정정 (AM-R3-1) + v4 R-3 line drift:** `class SpatialEnginePluginFactory : public Steinberg::IPluginFactory` 4 virtual method 직접 vtable 작성 (`getFactoryInfo` / `countClasses` / `getClassInfo` / `createInstance`, signatures from `pluginterfaces/base/ipluginbase.h:192-214`, virtuals L197/202/205/208) + `extern "C" SMTG_EXPORT_SYMBOL GetPluginFactory()` C export + **(v4 NOTE-R3-C)** `queryInterface(IPluginFactory2/3::iid)` → `kNotImplemented` 반환. 매크로 시퀀스 미사용. (REWRITE: v1 의 `pluginfactory.cpp` selective include 폐기, v2 의 매크로 시퀀스 폐기, v3 vtable 직접 작성, v4 R-3 boundary 정정)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.hpp` — **v4 AM-R4-2 base class 정정**: `class SpatialEngineProcessor : public Steinberg::Vst::Component, public Steinberg::Vst::IAudioProcessor` (option-α default, helper `vstcomponent.h:52` 상속) 또는 `: public IComponent, public IAudioProcessor` 직접 다중 상속 (option-β fallback). v3 의 `Vst::AudioEffect` 는 vendored 트리 부재 (REWRITE: 이전 JUCE 버전 폐기 후 재작성)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` — process 콜백 + lifecycle + setState/getState + `inputParameterChanges` stack snapshot (`ivstaudioprocessor.h:316` v4 R-3 정정) (REWRITE: 이전 JUCE 버전 폐기 후 재작성)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineController.hpp` — `IEditController` 헤더
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineController.cpp` — 6 param 등록 + `IComponentHandler::performEdit` 호출 (notify 채널 미사용, v3 AM-R3-3 SDK 인용 코멘트 잠금)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessData.hpp` — `Vst::ProcessData` ↔ `spe::audio_io::AudioBlock` lock-free 어댑터 (REWRITE)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/CMakeLists.txt` — 7 ctest + `set_tests_properties(... DEPENDS vst3_host_fixture)` (v3 AM-R3-9) (REWRITE)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_host_fixture.cpp` — in-process loader + factory + connection point + lifecycle (8 assertion, AM-7 NEW) — 4 ctest 의 prerequisite
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_param_layout.cpp` — IEditController 6 param 메타데이터 검증 (10 assertion)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_param_roundtrip.cpp` — normalize ↔ plain 라운드트립 (24 assertion) (REWRITE)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_audio_smoke.cpp` — in-process loader + process 1000회 (8 assertion, component-only)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_state_persist.cpp` — getState/setState 라운드트립 + v=2 fallback (12 assertion)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/test_vst3_bypass.cpp` — setProcessing on/off (6 assertion + v3 AM-R3-3 dispatchCommand RT-safety 1 추가)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/c2-licensing.md` — Step 0.a 라이선스 written confirmation 기록 (3 strand 결정 + spatial_engine 자체 라이선스 + Phase A/B/C 산출물 GPLv3 호환성 표 — AM-5 + M3 + v3 AM-R3-5 deferral, AM-R3-8 helper 6개 BSD-3 grep 결과)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/c2-debt.md` — (Option B.3 채택 시) Phase D6 부채 등록 표
- `/home/seung/mmhoa/spatial_engine/.ci/vst3sdk_sha.txt` — vst3sdk 트리 SHA256 pin — **v4 AM-R4-1 CRITICAL fix**: `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` (inner JUCE repo, 122 entries non-empty SHA). v3 의 outer-repo `git ls-tree -r HEAD core/JUCE/...` 명령은 `.gitignore:13` 으로 0 entries empty SHA fact-error → 사용 금지.
- `/home/seung/mmhoa/spatial_engine/vst3/LICENSE-third-party.txt` — **v4 NOTE-R3-D NEW:** BSD-3 attribution 동봉 — `module_linux.cpp` (Steinberg BSD-3) + helper 6개 `.cpp` (BSD-3, AM-R3-8 grep 잠금) + base/source/* + base/thread/flock.h (AM-R4-4) 의 (c) Steinberg copyright + BSD-3 standard disclaimer 통합. binary 배포 동봉 필수.
- `/home/seung/mmhoa/spatial_engine/.omc/plans/c2-licensing.md` 의 helper transitive entry — **v4 AM-R4-4 추가:** base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h} + base/thread/include/flock.h 의 BSD-3 file-level header grep 결과 + LICENSE-third-party.txt 작성 추적.

### 수정 (MODIFY)

- `/home/seung/mmhoa/spatial_engine/vst3/CMakeLists.txt` — `juce_add_plugin` 제거 (Step 0.b 의 `git checkout HEAD` 으로 modified 0 복원 후), `add_library(... MODULE ...)` 직접 사용으로 재작성, vst3sdk include path 만 추가, JUCE 모듈 link 0
- `/home/seung/mmhoa/spatial_engine/scripts/check_test_ndebug.sh` — `vst3/tests/` 디렉토리도 스캔하도록 확장 (Step 0.b)
- `/home/seung/mmhoa/spatial_engine/.omc/plans/open-questions.md` — C2-Q10/Q11/Q12/Q13/Q14/Q15/Q16 등록 (v3 권장값 갱신)

### 삭제 (DELETE) — Step 0.b 의 `chore(C2): roll back` commit (v3 AM-R3-4 강제 5단계 순서)

- `/home/seung/mmhoa/spatial_engine/vst3/AudioBlockAdapter.hpp` (untracked → plain rm)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.hpp` (untracked → plain rm)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineProcessor.cpp` (untracked → plain rm)
- `/home/seung/mmhoa/spatial_engine/vst3/tests/` (untracked 디렉토리 → plain rm -r)
- `/home/seung/mmhoa/spatial_engine/vst3/SpatialEngineVST3.cpp` (staged-delete `D` → `git rm --cached` 으로 확정)

### 변경 금지 (Driver #1 보호)

- `/home/seung/mmhoa/spatial_engine/core/src/core/SpatialEngine.h` (§15.A 의 `dispatchCommand` 1줄 inline forwarder 는 commit 완료, 이번 플랜에선 추가 변경 0)
- `/home/seung/mmhoa/spatial_engine/core/src/core/SpatialEngine.cpp` (변경 금지)
- `/home/seung/mmhoa/spatial_engine/core/CMakeLists.txt` 의 `SPE_CORE_SOURCES` / `SPE_HAVE_JUCE` 블록 (변경 금지)
- `/home/seung/mmhoa/spatial_engine/core/src/ipc/OSCBackend.h` (§15.A 의 `injectCommand` 는 commit 완료, 이번 플랜에선 추가 변경 0)
- `/home/seung/mmhoa/spatial_engine/.ci/off_baseline.bytes.sha256` / `.ci/off_baseline.symbols.sha256` (Step 0.d 에서 변화 없음 검증; 변경 시 즉시 stop)
- `/home/seung/mmhoa/spatial_engine/core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/**` (vst3sdk 자체 파일 절대 변경 금지 — `.ci/vst3sdk_sha.txt` SHA pin 검증)

> **v1→v2 변경:** REWRITE 카테고리 분리 (L2). vst3sdk SHA pin 파일 신설 (AM-8). c2-licensing.md 에 3 strand + GPLv3 호환성 표 (M3) 추가. test_vst3_host_fixture.cpp 신규 (AM-7).
> **v2→v3 변경:** `SpatialEnginePluginFactory.cpp` 의 구현이 매크로 시퀀스 → vtable 직접 작성으로 변경 (AM-R3-1). `vst3/tests/CMakeLists.txt` 에 ctest dependency 명시 (AM-R3-9). `c2-licensing.md` 에 helper 6개 BSD-3 grep 결과 + variable license deferral (AM-R3-5/AM-R3-8). `.ci/vst3sdk_sha.txt` 생성 명령 잠금 (AM-R3-6).
> **v3→v4 변경:** `SpatialEnginePluginFactory.cpp` line ref `:194-216` → `:192-214` boundary 정정 (R-3) + IPluginFactory2/3 negative처리 명시 (NOTE-R3-C). `SpatialEngineProcessor.hpp` base class `Vst::AudioEffect` → `Vst::Component` (option-α) 또는 IComponent+IAudioProcessor 직접 다중 상속 (option-β) 정정 (AM-R4-2 CRITICAL). `.ci/vst3sdk_sha.txt` 명령 outer-repo → inner JUCE repo cd 정정 (AM-R4-1 CRITICAL). 신규 `vst3/LICENSE-third-party.txt` BSD-3 attribution 동봉 (NOTE-R3-D).

---

## 8. ADR 초안

### Decision

Phase C / C2 sub-phase 의 VST3 plugin 통합을 **Option B (vst3sdk 직접 핸드롤)** 으로 구현한다. JUCE plugin client / APVTS / juceaide 의존을 사용하지 않고, `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` 의 헤더 + selective `.cpp` 화이트리스트 (Principle 2, helper 6개 BSD-3 검증 완료 + **v4 AM-R4-4 base/source/* + base/thread/flock.h transitive 의존 추가, BSD-3 file-level 검증**) 만 사용해 `IPluginFactory` / `IComponent` / `IAudioProcessor` / `IEditController` 를 직접 구현한다. **`IPluginFactory` 는 4 virtual method vtable 직접 작성** (`pluginterfaces/base/ipluginbase.h:192-214` signature (v4 R-3 정정), v3 AM-R3-1 — vendored 트리에 매크로 본체 + `CPluginFactory.cpp` 부재 잠금). **(v4 AM-R4-2 CRITICAL fix) `SpatialEngineProcessor` base class** = `Vst::Component` (helper `vstcomponent.h:52`) + `IAudioProcessor` 다중 상속 (option-α default), 또는 `IComponent` + `IAudioProcessor` 직접 다중 상속 (option-β fallback). v3 의 `Vst::AudioEffect` 는 vendored 트리 부재로 사용 불가. M2 strictness 게이트는 **Option B.2 (자체 in-process VST3 host fixture)** default 채택 — `module_linux.cpp` (BSD-3, sysdep POSIX 표준 헤더 + libstdc++ filesystem `USE_EXPERIMENTAL_FS` macro mutex 1선택, v4 R-4) 활용. NO-GO 시 Option B.3 (Phase D6 부채 회계) fallback.

### Drivers

1. OFF-path byte-identical 불변량 (Driver #1) — dual-gate (`bytes.sha256` + `symbols.sha256`) 강제.
2. RT 스레드 안전성 + control plane 단일화 (Driver #2) — host param → `IComponentHandler::performEdit` (UI-Thread context, `ivsteditcontroller.h:175-176` + `:179-180` (v4 R-3 정정) + `:183-184` SDK 표준 보장 인용) → `inputParameterChanges` queue (`ivstaudioprocessor.h:316` (v4 R-3 정정) audio thread drain SDK 표준 보장 인용) → component stack snapshot → `engine_->dispatchCommand` 단일 경로 (RT-safety 검증 통과 시 audio thread 직접 호출, 미통과 시 SPSC ring layer 추가; **v4 AM-R4-5: cmd_fifo_.push thread-id 단일 검증 + injectCommand 직접 호출 RT-safety 검증 추가**). `IConnectionPoint::notify` 채널 사용 안 함 (host thread guarantee 차이 위험, AM-R3-10).
3. Sysdep-free 단일 의존성 채널 (Driver #3) — vst3sdk 헤더 + selective `.cpp` 화이트리스트만 사용, X11/freetype/alsa 시스템 패키지 없이 빌드 가능.

### Alternatives considered

- **Option A (`juce_add_plugin` + APVTS):** §16.1 D+2 fallback 트리거 발현으로 폐기. 시스템 dev 패키지 영구 미설치 환경에서 juceaide 빌드 불가.
- **Option A' (JUCE + juce_gui_basics stub):** juceaide 자체가 X11 link → stub 만으로 회피 불가, juceaide 소스 패치 필요. Principle 5 위반 (JUCE upstream 변경) + JUCE 버전 bump 마다 재패치 부담. 기각 (§3 정량 비교).
- **Option A'' (conan/vcpkg user-local X11 prefix):** sudo 회피 가능하나 매 환경 재구축 필요 + Phase D6 까지 의존 누적이 Driver #3 침해. 기각, Phase D6 재고려 (§8 Follow-ups D6.g).
- **Option C (Hybrid APVTS + 자체 OSC bridge):** Option A 와 동일 sysdep 차단 + 두 control plane 공존 결함 — 폐기 유지.
- **Option B sub-options:**
  - B.1 (vst3sdk 헤더 + Steinberg validator): vendored vst3sdk 트리에 validator 부재 — fact-error, 폐기.
  - B.2 (vst3sdk 헤더 + 자체 in-process VST3 host): default 채택.
  - B.3 (M2 부채 회계): fallback (B.2 NO-GO 시).
- **Factory 진입점 sub-options (v3 AM-R3-1 신설):**
  - F.1 (`BEGIN_FACTORY_DEF` 매크로 시퀀스): vendored 트리에 매크로 본체 (`END_FACTORY` / `DEF_CLASS2` `#define`) 부재 + `CPluginFactory .cpp` 구현 부재 — fact-error, 폐기.
  - F.2 (`IPluginFactory` 4 virtual method vtable 직접 작성): default 채택. signature는 `pluginterfaces/base/ipluginbase.h:192-214` (v4 R-3 boundary 정정, virtuals L197/202/205/208) 에서 verbatim 인용.
- **(v4 AM-R4-2 NEW) Component base class sub-options:**
  - C.α (`Vst::Component` helper, `vstcomponent.h:52`) + `IAudioProcessor` 다중 상속: default. helper 6개 + base/source/* (AM-R4-4) BSD-3 검증 통과 시 채택.
  - C.β (`IComponent` + `IAudioProcessor` 직접 다중 상속, helper 미사용): fallback. helper transitive 끌어오기 실패 시 채택. `FUnknown` 직접 구현 부담 추가.
  - C.γ (`Vst::AudioEffect`): **vendored 트리 부재** (`find ... -iname "*audioeffect*"` = 0) — fact-error, 폐기. v3 plan 의 §6 Step 1.2 잘못된 인용으로 v4 에서 정정.

### Why chosen

- D+2 fallback 트리거가 이미 발현 → Option A 는 사용자 환경에서 실행 불가능.
- vst3sdk 가 JUCE 트리 안 번들 + `module_linux.cpp` 가 BSD-3 별개 strand + sysdep POSIX 표준만 → Option B.2 가 self-contained 검증 가능.
- `BEGIN_FACTORY_DEF` 매크로 본체 + `CPluginFactory .cpp` 부재 잠금 (vendored grep 결과 0) → vtable 직접 작성이 단 하나의 viable factory 구현 (F.2 default).
- §15.A 의 `dispatchCommand` / `injectCommand` inline forwarder 가 이미 commit `c2a5ad0` 에 적용 → Driver #2 의 control plane 단일화는 Option B 에서도 그대로 사용 가능 (단, `IComponentHandler::performEdit` SDK 표준 경로로 AM-6 정정, v3 AM-R3-3 SDK 인용 잠금).
- Helper 6개 file-level BSD-3 검증 (AM-R3-8) → vst3sdk root LICENSE GPLv3 strand 와 별개로 재배포 가능.

### Consequences

- **+** X11/freetype/alsa 시스템 dev 패키지 없이 CI 통과. Phase D6 까지 sudo 부재 환경 호환성 유지.
- **+** JUCE plugin client 모듈 link 0 — `spatial_engine_vst3.so` 의 의존성 footprint 작음.
- **+** VST3 SDK 직접 핸들 → Phase D6 의 full custom editor 진입 시 boilerplate 재사용 가능.
- **+** `module_linux.cpp` BSD-3 별개 strand + helper 6개 BSD-3 (file-level) → host fixture 코드 GPLv3 전염 회피 가능 (**v4 R-2 분리 명시**: technical fact = helper 6개 + base/source/* + flock.h file-level BSD-3 grep 잠금 — `vst3/LICENSE-third-party.txt` (NOTE-R3-D) 으로 attribution 명문화 / legal conclusion = 변호사 게이트 commercial release (Phase D6.f) deferral, AM-R3-5; technical 과 legal 은 별개 layer).
- **+** vtable 직접 작성 (F.2) → SDK 매크로 dependency 0, factory 구현 self-contained.
- **-** 6 param boilerplate 직접 작성 (Option A 의 APVTS 자동 wiring 부재) — Step 2 가 길어짐 (2d 추정).
- **-** `IPluginFactory` 4 virtual method 직접 작성 → SDK 가 향후 `IPluginFactory2` 등 확장 ABI 추가 시 우리 코드 수동 update 필요 (Phase D6 정식 등록 시 재고려).
- **-** `getState/setState` 자체 binary schema 작성 → migration 정책 (Phase D6.z) 필요.
- **-** M2 strictness 게이트 정의가 자체 host fixture — Steinberg pluginval/validator 와 spec drift 위험. DAW 실호스트 confirm-only (Reaper OR Bitwig 1종 이상, Cubase Phase D6 deferral) 로 보강.
- **-** Steinberg License 등록 (3 strand 분리) + 자체 CID 발급 부담 — Phase D6 시 정식 등록 필요.
- **-** spatial_engine 자체 라이선스 GPLv3 fallback 자동 동의 → Phase A/B/C 산출물 모두 GPLv3 전염 (AM-5, M3 호환성 표). 변호사 게이트 commercial release 시점 deferral (AM-R3-5).

### Follow-ups

- Phase D6.a — 동적 propagation delay reporting (Option A C2-Q6 와 동일하게 Phase D6 deferral)
- Phase D6.b — full custom editor (VSTGUI 또는 자체) — Educational tier 한계 검토
- Phase D6.c — APVTS/state schema v2 migration (현재 v1 만 지원)
- Phase D6.d — sample-accurate parameter automation (현재 block-rate 만)
- Phase D6.e — M2 strictness 게이트 정식화 = pluginval 또는 Steinberg validator 정식 등록 + Steinberg agreement formal sign + 변호사 게이트 (v3 AM-R3-5 deferral 청구)
- Phase D6.f — 자체 발급 CID → Steinberg portal 정식 등록 CID 로 교체
- Phase D6.g — Option A'' (user-local X11 prefix) 재고려 시 vcpkg/conan 의존성 plan
- Phase D6.h — vst3sdk 트리 외부 vendoring (`vendor/vst3sdk/`) + 독립 update 정책 (시나리오 5 fallback)
- Phase D6.i (v3 NEW, **v4 R-1 axis 분리**) — Cubase commercial DAW confirm 추가 (AM-R3-7 deferral 청구). **2 axis 분리 명시**:
  - **(i.1) thread axis** = host-별 thread guarantee 차이 (시나리오 4) → AM-R3-10 SDK 표준 경로 default 로 **이미 mitigated** (host-specific 가정 회피, IComponentHandler::performEdit + inputParameterChanges 만 사용).
  - **(i.2) module-discovery axis** = Cubase module manifest validation rule + PClassInfo2 category 차이 → **D6.i validation 잔존**. Cubase 정식 module discovery + PClassInfo2 등록은 commercial license + DAW 보유 시점에 진행.
- Phase D6.j (v3 NEW) — `IPluginFactory2` / `IPluginFactory3` 확장 ABI 지원 (vtable 직접 작성 update)

> **v1→v2 변경:** Decision 의 M2 게이트를 Option B.2 default 로 명시 (AM-1). Alternatives 에 Option A'/A'' 추가. Drivers #2 의 통신 경로 `IComponentHandler::performEdit` 으로 정정 (AM-6). Consequences 에 GPLv3 전염 영향 (AM-5 M3) + module_linux BSD-3 별개 strand 명시.
> **v2→v3 변경:** Decision 에 `IPluginFactory` vtable 직접 작성 (F.2) 명시 (AM-R3-1). Drivers #2 SDK 인용 verbatim 잠금 (AM-R3-3). Alternatives 에 Factory sub-options (F.1 폐기 / F.2 default) 추가. Why chosen 에 helper 6개 BSD-3 grep 결과 (AM-R3-8) + vendored 매크로 부재 fact 추가. Consequences 에 vtable 직접 작성 의 +/- + 변호사 게이트 deferral (AM-R3-5) 추가. Follow-ups D6.i (Cubase) + D6.j (Factory2/3 ABI) 추가.
> **v3→v4 변경 (ADR 보강):** Decision 에 base/source/* + flock.h 추가 (AM-R4-4) + Component base class option-α/β (AM-R4-2 CRITICAL — vendored AudioEffect 부재) + filesystem mutex 명시 (R-4). Drivers #2 line drift 정정 (R-3) + cmd_fifo SPSC 검증 추가 (AM-R4-5). Alternatives 에 Component base class sub-options C.α/β/γ 신설 (AM-R4-2). Consequences 에 R-2 분리 명시 (technical fact vs legal conclusion) + LICENSE-third-party.txt 동봉 (NOTE-R3-D). Follow-ups D6.i 에 R-1 axis 분리 (i.1 thread axis mitigated / i.2 module-discovery axis 잔존).

---

## 9. 수락 기준 체크리스트 (v3)

### 정량 게이트

- [ ] `test_vst3_host_fixture` 9 assertion 통과 (AM-7 + v4 NOTE-R3-C IPluginFactory2 negative test +1)
- [ ] `test_vst3_param_layout` 10 assertion 통과 (tolerance 0 / 정수)
- [ ] `test_vst3_param_roundtrip` 24 assertion 통과 (tolerance 1e-5)
- [ ] `test_vst3_audio_smoke` 8 assertion 통과 + `RT_ASSERT_NO_ALLOC` 1000회 통과
- [ ] `test_vst3_state_persist` 12 assertion 통과 (v1 라운드트립 + v=2 fallback)
- [ ] `test_vst3_bypass` 6+3 assertion 통과 (v3 AM-R3-3 dispatchCommand RT-safety 1 + v4 AM-R4-5 injectCommand 직접 호출 RT-safety + cmd_fifo_.push thread-id 단일 검증 2 추가)
- [ ] `scripts/check_test_ndebug.sh` 가 7 ctest 모두 `-UNDEBUG` 검증
- [ ] OFF dual-gate (`bytes.sha256` + `symbols.sha256`) 통과 — `cmake -B build_off -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON && make spe_core spatial_engine_core`
- [ ] sysdep-free 게이트 — `ldd build_vst3_on/vst3/spatial_engine_vst3.so | grep -cE 'libX11|libfreetype|libasound|libwebkit'` == 0
- [ ] sysdep-free 보강 (M1) — `LD_DEBUG=libs ./build_vst3_on/vst3/tests/test_vst3_host_fixture 2>&1 | grep -cE 'X11|freetype|alsa|webkit'` == 0
- [ ] `nm --defined-only build_vst3_on/vst3/spatial_engine_vst3.so | grep -c GetPluginFactory` == 1
- [ ] `nm --defined-only build_vst3_on/vst3/spatial_engine_vst3.so | grep -c "SpatialEnginePluginFactory::countClasses"` == 1 (v3 NEW AM-R3-1, vtable 직접 작성 검증)
- [ ] **v4 NEW (NOTE-R3-E):** vtable footprint 4-method 모두 검증 — `nm --defined-only build_on/vst3/spatial_engine_vst3.so | grep -cE "SpatialEnginePluginFactory::(getFactoryInfo|countClasses|getClassInfo|createInstance)"` == 4
- [ ] vst3sdk SHA256 pin 통과 — `.ci/vst3sdk_sha.txt` ↔ **(v4 AM-R4-1 CRITICAL fix)** `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` (inner JUCE repo, 122 entries non-empty SHA) 일치 (AM-8 시나리오 5, v3 AM-R3-6 명령 outer-repo 사용 금지 — `.gitignore:13` empty SHA fact-error)
- [ ] **v3 NEW (AM-R3-1) + v4 정정 (AM-R4-3 HIGH):** `CPluginFactory .cpp` 구현 부재 grep — `grep -rn --include="*.cpp" "CPluginFactory::CPluginFactory\|CPluginFactory::registerClass\|CPluginFactory::createInstance" core/JUCE/.../VST3_SDK/ | grep -vE '^\s*\*|^\s*//' | wc -l == 0` (`.cpp` 한정 + comment exclude, v3 명령 logic flip 정정)
- [ ] **v4 NEW (AM-R4-4 HIGH):** `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.cpp` + `base/thread/include/flock.h` 의 file-level BSD-3 header grep 통과 (Step 0.c v4 NEW entry gate)
- [ ] Driver #1 보호 파일 (§7 변경 금지 목록) 의 `git diff` 비어있음
- [ ] `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/**` 의 git ls-files SHA 변화 없음 (vst3sdk 자체 변경 금지)

### 정성 게이트

- [ ] M2 게이트 결정 (Option B.2 / B.3) 가 Step 0.c 결과에 따라 명시 commit footer 에 기록됨
- [ ] (B.3 채택 시) `.omc/plans/c2-debt.md` 에 Phase D6 부채 항목 등록
- [ ] Step 0.a 라이선스 3 strand 결정 (`.omc/plans/c2-licensing.md`) — vst3sdk root GPLv3 fallback default / public.sdk + helper 6개 BSD-3 검증 (AM-R3-8 grep 결과 잠금) / spatial_engine 자체 GPLv3 fallback. 변호사 게이트 commercial release deferral (v3 AM-R3-5).
- [ ] Phase A/B/C 산출물 GPLv3 호환성 표 작성 (M3, AM-5)
- [ ] DAW (**Reaper OR Bitwig 1종 이상**) 사용자 confirm-only 통과 (D+9, v3 AM-R3-7 — Cubase deferral)
- [ ] `core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt` 갱신 없이 우리 산출물과 라이선스 호환 검증
- [ ] **v3 NEW (AM-R3-3):** `dispatchCommand` RT-safety 결정 게이트 결과 (audio thread 직접 호출 OK / SPSC ring 추가) 가 Step 2 commit footer 에 기록됨
- [ ] **v4 NEW (AM-R4-5 HIGH):** `cmd_fifo_.push` thread-id 단일 검증 + `OSCBackend::injectCommand` 직접 호출 RT-safety 결과 (alloc 0 / mutex 0 / single-thread) 가 Step 2 commit footer 에 기록됨
- [ ] **v4 NEW (NOTE-R3-D):** `vst3/LICENSE-third-party.txt` 신규 작성 — `module_linux.cpp` + helper 6개 + base/source/* + base/thread/flock.h 의 BSD-3 (c) Steinberg attribution + standard disclaimer 동봉. binary 배포 시 동행.

### CI 워크플로

- [ ] `.github/workflows/vst3.yml` (또는 `.github/workflows/c2.yml` 신설) — `runs-on: ubuntu-22.04` pin, sysdep-free 환경에서 7 ctest 자동 실행, dual-gate 검증
- [ ] PR 머지 전 OFF dual-gate sha256 자동 비교 (실패 시 빨강)
- [ ] PR 머지 전 sysdep-free 게이트 자동 검증 (LD_DEBUG 보강 포함)
- [ ] PR 머지 전 vst3sdk SHA256 pin 자동 검증 (v3 AM-R3-6 잠금 명령)

> **v1→v2 변경:** ctest 7 (host fixture +1), LD_DEBUG 보강, vst3sdk SHA pin, 라이선스 3 strand + GPLv3 호환성 표.
> **v2→v3 변경:** vtable 직접 작성 검증 (AM-R3-1), CPluginFactory grep entry gate (AM-R3-1), bypass +1 assertion (AM-R3-3 dispatchCommand RT-safety), DAW Cubase deferral (AM-R3-7), 변호사 deferral (AM-R3-5), helper 6개 BSD-3 grep 결과 잠금 (AM-R3-8), SHA pin 명령 잠금 (AM-R3-6).
> **v3→v4 변경:** host fixture 8→9 assertion (NOTE-R3-C). bypass +1→+3 assertion (AM-R4-5 HIGH cmd_fifo SPSC + injectCommand 직접). vtable footprint 4-method `nm` 검증 (NOTE-R3-E). SHA pin 명령 outer→inner JUCE repo cd 정정 (AM-R4-1 CRITICAL). entry gate `.cpp` 한정 + comment exclude (AM-R4-3 HIGH). base/source/* + flock.h BSD-3 grep 추가 (AM-R4-4 HIGH). LICENSE-third-party.txt 동봉 (NOTE-R3-D).

---

## 10. Open Questions (라운드 3 검토용)

- [ ] **C2-Q9 (재발현, v3 AM-R3-5 강등):** VST3 SDK 라이선스 — Steinberg agreement 등록 vs GPLv3 fallback. spatial_engine 자체 라이선스 미정 시 GPLv3 fallback 가능한가? — **v3 권장:** 3 strand 분리 (vst3sdk root GPLv3 fallback default / public.sdk + helper 6개 BSD-3 별개 grep 결과 잠금 / spatial_engine 자체 GPLv3 fallback) + Phase A/B/C 산출물 GPLv3 호환성 표 (M3) 사전 작성. 변호사 게이트 = Phase D6.f (commercial release) deferral. C2 진행 차단 사유 아님 (AM-R3-5).
- [ ] **C2-Q10 (v3 유지):** Option B 의 M2 strictness 게이트 — **v3 권장:** B.2 (자체 in-process host fixture) default → B.3 (부채 회계) fallback. 변경 없음.
- [ ] **C2-Q11 (v3 유지):** Option B.2 의 in-process host fixture 위치 — **v3 권장:** `vst3/tests/test_vst3_host_fixture.cpp` (Driver #1 격리 정합). AM-7 으로 4 ctest 의 prerequisite 명시 + ctest dependency cmake 코드 (AM-R3-9).
- [ ] **C2-Q12 (v3 AM-R3-3 전환):** `process` audio thread 의 `inputParameterChanges` 를 control thread 로 forward 하는 layer — **v3 권장: ② host event-thread 신뢰 default** (`ivsteditcontroller.h:175-181` UI-Thread context + `ivstaudioprocessor.h:317` audio thread drain SDK 표준 보장 인용). v2 권장 ① (component 측 std::thread) 폐기. SPSC ring 사용 여부는 §5 #5 ctest 확장 결과 (alloc 0 / mutex 0) 에 따라 결정 — RT-safe 확인 시 SPSC ring 폐기, audio thread 직접 호출 default.
- [ ] **C2-Q13 (v3 유지):** `room_preset_idx` (4 choice: Dry/Small/Medium/Large) ↔ `PayloadReverbSelect` (which: u8 0=fdn, 1=ir) semantic mismatch — **v3 권장:** preset → reverb param 매핑 테이블 (FDN T60 + ER mix) 자체 구현. Phase B reverb 설계 정합 검증.
- [ ] **C2-Q14 (v3 유지):** `kSpatialEngineProcessorUID` / `kSpatialEngineControllerUID` (128-bit CID) 자체 발급 (UUID v4) — **v3 권장:** DEV prefix 표식 (예: `0xDEAD0001` 16바이트 prefix) → Phase D6 정식 등록 시 교체. Phase D6.f.
- [ ] **C2-Q15 (v3 AM-R3-1 전환):** `BEGIN_FACTORY_DEF` 매크로 (header-only) 단독 사용 시 sysdep transitive 정말 0 인가? — **v3 폐기:** 매크로 시퀀스 자체 폐기 (vendored 트리 grep 결과 = 본체 부재). `IPluginFactory` 4 virtual method vtable 직접 작성 default (AM-R3-1). Step 0.c entry gate 에서 `CPluginFactory .cpp` 구현 부재 grep 검증.
- [ ] **C2-Q16 (v3 AM-R3-5 deferral):** `module_linux.cpp` BSD-3 라이선스가 우리 host fixture 에 link 시 vst3sdk root LICENSE.txt 의 GPLv3 strand 와 함께 GPLv3 전염 회피 가능한가? — **v3 권장:** 3 strand 분리 명문화 (file-level BSD-3 우선 — helper 6개 grep 결과 잠금) + 변호사 게이트 commercial release 시점 (Phase D6.f) deferral. C2 진행 차단 사유 아님.
- [ ] **C2-Q17 (v3 NEW, AM-R3-3 + v4 AM-R4-5 scope 확장):** `engine_->dispatchCommand` RT-safety 검증 결과 — alloc 0 / mutex 0 이면 audio thread 직접 호출 default, SPSC ring 폐기. 통과 안 하면 SPSC ring layer 추가 (별도 thread lifecycle plugin lifecycle 외 관리, Driver #2 정합성 재검증). §5 #5 ctest 확장 결과로 결정. **v4 scope 확장 (AM-R4-5 HIGH):** dispatchCommand 가 `osc_backend_.injectCommand` 인라인 호출 → injectCommand 가 SPSC `cmd_fifo_.push` 호출. v3 의 `dispatchCommand` 1000 loop alloc/mutex 검증만으로는 SPSC single-producer 가정 검증 불충분. v4 결정 게이트 = (a) `OSCBackend::injectCommand` 직접 호출 alloc 0 / mutex 0 + (b) `cmd_fifo_.push` thread-id capture 후 audio thread 단일 검증 (multiple-producer 위반 시 즉시 fail). 두 검증 모두 통과 시에만 audio thread 직접 호출 default. 미통과 시 SPSC ring layer + control thread 명시화.
- [ ] **C2-Q18 (v4 NEW, AM-R4-4 HIGH):** helper 6개 의 transitive `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 의 BSD-3 file-level header 검증 — Step 0.c v4 NEW entry gate grep 결과 = "all-BSD-3"? **v4 권장:** Step 0.c entry gate grep 통과 시 화이트리스트 채택 (option-α, helper + base/source/* 사용). NO-GO 시 화이트리스트 회수 + Component base class option-β (`IComponent` + `IAudioProcessor` 직접 다중 상속, helper + base/source/* 미사용) fallback. 화이트리스트 회수 후에도 transitive sysdep 발현 시 Option B.3 (M2 부채 회계) 강등.

> **v1→v2 변경:** C2-Q15 (factory 매크로 sysdep) + C2-Q16 (module_linux BSD-3 strand) 신설. C2-Q10 권장 B.1→B.3 에서 B.2→B.3 으로 변경 (AM-1). C2-Q12 권장 ②→① (AM-6).
> **v2→v3 변경:** C2-Q9 변호사 게이트 commercial release deferral (AM-R3-5). C2-Q12 권장 ①→② 재전환 (AM-R3-3 SDK 인용 잠금). C2-Q15 폐기 (AM-R3-1 매크로 자체 폐기). C2-Q16 변호사 게이트 deferral (AM-R3-5). C2-Q17 신설 (AM-R3-3 dispatchCommand RT-safety 결정 게이트).
> **v3→v4 변경:** C2-Q17 scope 확장 — cmd_fifo SPSC push thread-id 단일 검증 + injectCommand 직접 호출 검증 추가 (AM-R4-5 HIGH). C2-Q18 신설 — helper transitive base/source/* + flock.h BSD-3 검증 게이트 + option-α (helper) / option-β (vtable-only) 결정 (AM-R4-4 HIGH).

---

## 11. 위험 / Fallback table

| ID | 위험 | 조기 신호 | Fallback |
|---|---|---|---|
| R1 | vst3sdk 헤더 transitive sysdep | Step 1 cmake configure X11/freetype 헤더 not found / ldd 비-zero | Selective include 화이트리스트 (Principle 2) + helper `.cpp` 직접 작성 fallback. 그래도 실패 시 Option B.3. |
| R2 | `process` RT-unsafe (`inputParameterChanges` virtual chain) | `RT_ASSERT_NO_ALLOC` 1000회 fail | stack snapshot + (필요시) SPSC ring layer; 그래도 fail 시 Phase D6 deferral. v3: dispatchCommand RT-safety 결정 게이트 (§5 #5 확장) 통과 시 SPSC ring 폐기. |
| R3 (v3 AM-R3-5 강등) | Steinberg License 미회수 (3 strand) | Step 0.a checklist 미완 | C2 진행 차단 사유 아님 (변호사 게이트 commercial release deferral); GPLv3 fallback default + M3 호환성 표 사전 작성. |
| R4 | M2 strictness 게이트 (B.2 host fixture sysdep) | Step 0.c host fixture build X11 의존 | Option B.3 강등 + Phase D6 부채. |
| R5 | OFF dual-gate 깨짐 | 매 Step 의 sha256 mismatch | 즉시 stop + 변경 원인 추적 (`git diff core/src/core/`). |
| R6 | CID 충돌 | DAW 호스트 로딩 시 conflict 경고 | Random UUID v4 + DEV prefix (C2-Q14) → Phase D6 정식 등록. |
| R7 (v3 AM-R3-3 전환) | C2-Q12 (param forward layer) 결정 미완 | Step 2 D+5 시점 cmd_fifo enqueue 0 | v3 default = ② host event-thread 신뢰 (SDK 인용 잠금). NO-GO 시 component 측 std::thread (①) fallback. |
| R8 (NEW, v3 AM-R3-10 출처 보강) | `IConnectionPoint::notify` host thread 차이 (시나리오 4) | Reaper / Cubase / Bitwig 에서 `audio_underrun_count` 증가 | notify 채널 사용 안 함 (AM-6); SDK 표준 `IComponentHandler::performEdit` 만 사용. host-specific thread 가정 회피 default (3rd party 관찰 사례 의존 0). |
| R9 (NEW, v3 AM-R3-6 잠금 + v4 AM-R4-1 CRITICAL fix) | vst3sdk ABI pin stale (시나리오 5) | DAW "Unsupported VST3 version" | `.ci/vst3sdk_sha.txt` SHA256 pin (**v4 정정**: `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` — inner JUCE repo cd 필수, outer-repo `.gitignore:13` empty SHA fact-error 정정) + ABI 호환성 ctest. JUCE 버전 bump 시 수동 reconciliation. Phase D6.h 외부 vendoring fallback. |
| R10 (NEW) | GPLv3 전염 (AM-5) | Phase A/B 산출물 라이선스 미정 | M3 호환성 표 (PySide6 LGPLv3 / OSC liblo LGPLv2.1 / FDN 자체) 사전 작성. spatial_engine 자체 라이선스 결정 deferral 가능. |
| R11 (v3 NEW, AM-R3-1) | Factory 매크로 시퀀스 부재로 vtable 직접 작성 부담 | Step 0.c `CPluginFactory .cpp` grep ≠ 0 면 매크로 fallback 검토 | vtable 직접 작성 default (signature `ipluginbase.h:194-216` verbatim). `IPluginFactory2`/`IPluginFactory3` 확장 ABI 추가 시 우리 코드 update 필요 (Phase D6.j). |
| R12 (v3 NEW, AM-R3-7 + v4 R-1 axis 분리) | Cubase 부재로 commercial DAW thread 검증 누락 | Reaper/Bitwig 만으로 실호스트 confirm 통과 | Cubase Phase D6.i deferral. **v4 R-1 axis 분리:** (i.1) thread axis = SDK 표준 경로로 mitigated (AM-R3-10) / (i.2) module-discovery axis = Cubase manifest validation + PClassInfo2 category 잔존, D6.i validation. host-specific 가정 회피 default 로 thread axis 위험 완화 (AM-R3-10). |
| R13 (v4 NEW, AM-R4-2 CRITICAL) | `Vst::AudioEffect` 클래스 vendored 트리 부재로 v3 plan Step 1.2 빌드 실패 위험 | Step 1 빌드 시 `error: 'AudioEffect' is not a member of 'Steinberg::Vst'` | v4 정정으로 Component (option-α default) 또는 IComponent+IAudioProcessor 직접 다중 상속 (option-β fallback) 채택. v3 의 잘못된 base class 인용 즉시 무효. |
| R14 (v4 NEW, AM-R4-1 CRITICAL) | SHA pin empty hash trivially pass | `.ci/vst3sdk_sha.txt` 가 `e3b0c44...` (empty SHA) 인 경우 | v4 inner JUCE repo cd 명령 (`( cd core/JUCE && git ls-tree -r HEAD ... )`) 으로 122 entries non-empty SHA 산출. CI 게이트가 empty SHA 검출 시 즉시 fail. |
| R15 (v4 NEW, AM-R4-4 HIGH) | base/source/* + flock.h transitive 끌어오기 실패 (BSD-3 미검증 또는 sysdep) | Step 0.c v4 entry gate BSD-3 grep mismatch 또는 Step 1 sysdep 발현 | option-β (`IComponent`+`IAudioProcessor` 직접 다중 상속, helper + base/source/* 미사용) fallback. 그래도 fail 시 Option B.3. |

> **v1→v2 변경:** R8 (notify host thread 차이) + R9 (ABI pin stale) + R10 (GPLv3 전염) 신설.
> **v2→v3 변경:** R3 강등 (license 변호사 deferral, AM-R3-5). R7 전환 (v3 default ②, AM-R3-3). R8 출처 보강 (AM-R3-10 host-specific 가정 회피 default). R9 SHA pin 명령 잠금 (AM-R3-6). R11 (factory vtable 직접 작성, AM-R3-1) + R12 (Cubase deferral, AM-R3-7) 신설.
> **v3→v4 변경:** R9 명령 outer→inner repo cd 정정 (AM-R4-1 CRITICAL). R12 axis 분리 (R-1). R13 (AudioEffect 부재, AM-R4-2 CRITICAL) + R14 (SHA empty hash, AM-R4-1 CRITICAL) + R15 (base/source/* transitive, AM-R4-4 HIGH) 신설.

---

## 12. 종료 조건

- §9 정량 게이트 100% 통과
- §9 정성 게이트 100% 통과 (특히 Step 0.a 라이선스 3 strand + M3 호환성 표 + M2 결정 회계 + dispatchCommand RT-safety 결정 회계)
- §10 C2-Q10/Q11/Q12/Q13/Q14/Q16/Q17 라운드 3 결정 commit 완료 (Q15 v3 폐기)
- DAW confirm-only (D+9 사용자 확인) 통과 — Reaper OR Bitwig 1종 이상 (Cubase Phase D6 deferral)
- ADR §8 의 Consequences/Follow-ups 가 Phase D6 plan 에 백로그 항목으로 등록 (.omc/plans/spatial-engine-phaseD.md 또는 후속 plan)

---

## 12.5 v1 → v2 변경 이력 요약 (Amendment 표) — v2 보존

> v1 라운드 1 검토 (Architect REVISE, Critic REJECT) 의 amendment 8개 + open question 2 + 부수 수정 4 가 모두 v2 에 반영됨. 사실 검증은 vendored vst3sdk 트리 grep 으로 cross-check 완료 (Critic R1 fact-check 100% 정확).

| ID | 분류 | 적용 위치 | 핵심 변경 |
|---|---|---|---|
| AM-1 | CRITICAL | §3 전면 재작성 | B.1 (Steinberg validator) 폐기 — vendored 트리 부재 fact-error. B.2 (자체 in-process host) default 승격. Option A'/A'' (juce_gui stub / user-local X11) 정량 비교 + 기각 명문화 (fair-alternatives 강화). |
| AM-2 | CRITICAL | §6 Step 1.1 source list | `pluginfactory.cpp` 부재 → `BEGIN_FACTORY_DEF` 매크로 (header-only at `pluginterfaces/vst/ivstcomponent.h:36`) + selective include 화이트리스트. **(v3 추가 정정 = AM-R3-1: 매크로 시퀀스 본체 부재 → vtable 직접 작성)** |
| AM-3 | CRITICAL | §0/§6 Step 0.b | cleanup 메커니즘 정정 — untracked 4 (plain rm) + staged-delete 1 + modified 1 (`git checkout HEAD`). v1 의 "별도 revert(C2) commit" fact-error 정정. **(v3 추가 = AM-R3-4: 5단계 강제 순서)** |
| AM-4 | CRITICAL | §1 Principle 2 | "Minimal SDK surface" → 5-bullet 정밀 화이트리스트. v1 의 §3 B.1 폐기로 Pros/Cons 모순 자동 해소. **(v3 보강 = AM-R3-8: helper 6개 BSD-3 검증)** |
| AM-5 | CRITICAL | §4 시나리오 3 재작성 | dual-license + public.sdk BSD-3 별개 strand + spatial_engine 자체 라이선스 미정 → GPLv3 전염 영향 정량 평가. **(v3 강등 = AM-R3-5: 변호사 게이트 commercial release deferral)** |
| AM-6 | CRITICAL | §6 Step 2.2 + §2 Driver #2 | `IConnectionPoint::notify` 폐기 → `IComponentHandler::performEdit` + `inputParameterChanges` 만 사용. **(v3 잠금 = AM-R3-3: SDK 인용 verbatim)** |
| AM-7 | CRITICAL | §5 unit test 재구성 | `test_vst3_host_fixture.cpp` 별도 ctest 분리. **(v3 보강 = AM-R3-9: ctest dependency cmake 명시)** |
| AM-8 | CRITICAL | §4 시나리오 4/5 신설 + R8/R9 | 시나리오 4 (notify host thread 차이) + 시나리오 5 (ABI pin stale). **(v3 보강 = AM-R3-10 출처 부재 강등 + AM-R3-6 SHA pin 명령 잠금)** |

(M1/M2/M3/L1/L2 v2 항목 그대로 보존)

---

## 12.6 v2 → v3 변경 이력 요약 (Amendment 표 — v3 신설)

> v2 라운드 2 검토 (Architect REVISE NOTE-R2-1 CRITICAL + NOTE-R2-2/3 MEDIUM, Critic ITERATE AM-R3-1~10 CRITICAL 3 / HIGH 4 / MEDIUM 3) 의 amendment 10개 모두 v3 에 반영됨. fact 검증 = vendored vst3sdk 트리 grep cross-check 완료 (helper 6개 BSD-3 헤더 / `BEGIN_FACTORY_DEF` 단독 정의 / `END_FACTORY` `#define` 0 hit / `CPluginFactory .cpp` 부재 / `module_linux.cpp` includes / `ivsteditcontroller.h:175-181` UI-Thread context / `ivstaudioprocessor.h:317` audio thread drain / `ipluginbase.h:194-216` 4 virtual method).

| ID | 분류 | 적용 위치 | 핵심 변경 (v2 → v3) |
|---|---|---|---|
| AM-R3-1 | CRITICAL | §1 Principle 2 + §6 Step 1.1/1.4 + §8 ADR + §10 C2-Q15 + R11 | 매크로 시퀀스 폐기 → `IPluginFactory` 4 virtual method (`ipluginbase.h:194-216` verbatim) vtable 직접 작성 default. Step 0.c entry gate = `CPluginFactory .cpp` 부재 grep wc -l == 0. |
| AM-R3-2 | MEDIUM | §3 B.2 Pros | `module_linux.cpp` sysdep 인용 정정 — POSIX 표준 5 헤더 + libstdc++ filesystem (vendored grep line 30-67 verbatim). v2 의 "`<dlfcn.h>` 외 0" 부정확. |
| AM-R3-3 | CRITICAL | §2 Driver #2 + §6 Step 2.2/2.3 + §10 C2-Q12 + §10 C2-Q17 신설 + R7 | C2-Q12 ①→② 전환. SDK 인용 verbatim 잠금: `ivsteditcontroller.h:175-181` UI-Thread context 3회 + `ivstaudioprocessor.h:317` audio thread drain. dispatchCommand RT-safety 결정 게이트 (§5 #5 확장) — RT-safe 시 SPSC ring 폐기. |
| AM-R3-4 | HIGH | §0 + §6 Step 0.b | cleanup 5단계 강제 순서: git status → git rm --cached → git checkout HEAD → plain rm → cleanup commit. |
| AM-R3-5 | HIGH | §4 시나리오 3 + §10 C2-Q9/Q16 + R3 | 변호사 게이트 commercial release (Phase D6.f) deferral. C2 진행 차단 사유 아님. GPLv3 fallback default 학술용도. |
| AM-R3-6 | MEDIUM | §4 시나리오 5 + §6 Step 0.b + §9 + R9 | `.ci/vst3sdk_sha.txt` 생성 명령 정의 잠금: `git ls-tree -r HEAD VST3_SDK | sha256sum` (file-level blob hash 의 SHA256, reproducible). |
| AM-R3-7 | HIGH | §5 #9 + §6 Step 4.5 + §9 + R12 | DAW confirm = Reaper OR Bitwig 1종 이상. Cubase Phase D6.i deferral (commercial DAW 부재). |
| AM-R3-8 | HIGH | §1 Principle 2 + §4 시나리오 3 + §10 C2-Q16 + ADR | helper 6개 (vstcomponentbase / vstcomponent / vsteditcontroller / vstparameters / vstbus / vstpresetfile) `.cpp` file-level header 모두 BSD-3-Clause 검증 grep 결과 잠금. strand 분리 가능 명문화. |
| AM-R3-9 | MEDIUM | §5 + §6 Step 1.6 + §7 | ctest dependency 메커니즘 cmake 코드 명시: `set_tests_properties(... PROPERTIES DEPENDS vst3_host_fixture)`. |
| AM-R3-10 | HIGH | §4 시나리오 4 + R8 | host-별 thread guarantee 인용 명시화 — Steinberg 정식 SDK 표준 문서 인용 부재 시 "관찰 사례 (3rd party reports)" 강등. 회피책 default = "host-specific 가정 회피, SDK 표준 경로만 사용". |

---

## 12.7 v3 → v4 변경 이력 요약 (Amendment 표 — v4 신설)

> v3 라운드 3 검토 (Architect APPROVE-WITH-MINOR-NOTES / NOTE-R3-A~E 5 minor + Critic ITERATE / MA-1~5 CRITICAL 2 + HIGH 3 + R-1~4 reservations) 의 amendment 5개 + Architect 5 notes + Critic 4 reservations 모두 v4 에 반영됨. fact 검증 = vendored vst3sdk 트리 + outer/inner git repo grep cross-check 완료 (`.gitignore:13` core/JUCE block / `git ls-tree -r HEAD core/JUCE/...` 0 entries / `find ... -iname "*audioeffect*"` 0 hit / `vstcomponent.h:52` Component class / docstring `ipluginbase.h:201` 1 hit / helper transitive `vstcomponentbase.cpp:38` + `vsteditcontroller.cpp:38` `base/source/*` includes / `module_linux.cpp` `USE_EXPERIMENTAL_FS` macro mutex line 46-72).

| ID | 분류 | 적용 위치 | 핵심 변경 (v3 → v4) |
|---|---|---|---|
| AM-R4-1 | CRITICAL | §4 시나리오 5 + §6 Step 0.b + §7 + §9 + R9 + R14 | SHA pin 명령 outer-repo `git ls-tree -r HEAD core/JUCE/...` (0 entries empty SHA `e3b0c44...` trivially pass) → inner JUCE repo cd `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` (122 entries non-empty SHA). `.gitignore:13` 으로 core/JUCE 가 outer repo 에서 ignored 인 점이 fact-error 원인. |
| AM-R4-2 | CRITICAL | §6 Step 1.2 + §7 + §8 ADR Decision/Alternatives + R13 | `Vst::AudioEffect` 클래스 vendored 트리 부재 (`find ... -iname "*audioeffect*"` = 0) → 실제 helper = `class Component : public ComponentBase, public IComponent` at `vstcomponent.h:52`. v4 default = option-α (`Vst::Component` + `IAudioProcessor` 다중 상속), option-β fallback (IComponent + IAudioProcessor 직접 다중 상속, helper 미사용), option-γ (`AudioEffect`) 폐기. |
| AM-R4-3 | HIGH | §6 Step 0.c + §9 정량 게이트 | entry gate grep `wc -l == 0` 가 실측 `== 1` (docstring `ipluginbase.h:201` `* CPluginFactory::registerClass.`) 매치 → logic flip 항상 NOTE-escalate. v4 정정 = `.cpp` 한정 (`--include="*.cpp"`) + comment exclude (`grep -vE '^\s*\*\|^\s*//'`) 또는 file existence check (`test ! -f public.sdk/source/common/pluginfactory.cpp`). |
| AM-R4-4 | HIGH | §1 Principle 2 + §4 시나리오 1 + §6 Step 0.c + §6 Step 1.1 + §7 + §10 C2-Q18 + R15 | helper 6개 의 transitive `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 화이트리스트 추가 — `vstcomponentbase.cpp:38` + `vsteditcontroller.cpp:38` + `fstring.h` → `fobject.h` → `flock.h` chain. Step 0.c v4 NEW BSD-3 file-level header 검증 entry gate. NO-GO 시 option-β fallback. |
| AM-R4-5 | HIGH | §5 #5 bypass + §8 Driver #2 + §9 정량 게이트 + §10 C2-Q17 | bypass ctest dispatchCommand RT-safety scope 확장 — (a) `OSCBackend::injectCommand` 직접 호출 alloc 0 / mutex 0 + (b) `cmd_fifo_.push` thread-id capture 후 audio thread 단일 검증 (multiple-producer SPSC 위반 즉시 fail). v3 의 `dispatchCommand` 1000 loop 만 으로는 SPSC single-producer 가정 검증 불충분. |
| NOTE-R3-A | MINOR (Architect) | §1 Principle 2 + §6 Step 1.4 + §7 + §8 ADR Alternatives + §13 | `ipluginbase.h:194-216` boundary 를 `:192-214` 로 정정 (실측 클래스 본체 L192, virtuals L197/202/205/208, IID L211, DECLARE_CLASS_IID L214). `:217` 인용 → `:211` 또는 `:214`. |
| NOTE-R3-B | MINOR (Architect) | §0 v2→v3 변경 + §6 Step 0.b | cleanup 5단계 failure mode 명시 — Step 5 (commit) prefix 가 Step 1-4 보다 먼저 실행되면 untracked 4 + modified 1 누락 → Principle 6 위반. Step 1-4 사이 swap 은 결과 동일 (`--cached` working tree 보존). |
| NOTE-R3-C | MINOR (Architect) | §5 #0 host fixture + §6 Step 1.4 + §9 정량 게이트 | `IPluginFactory2` query negative test 1 assertion 추가 — `factory->queryInterface(IPluginFactory2::iid, ...)` 가 `kNotImplemented` 또는 `kNoInterface` 반환 + out-param nullptr 검증. host fixture 8→9 assertion. |
| NOTE-R3-D | MINOR (Architect) | §7 + §9 정량 게이트 + §8 ADR Consequences | `vst3/LICENSE-third-party.txt` 신규 — `module_linux.cpp` + helper 6개 + base/source/* + flock.h 의 BSD-3 (c) Steinberg copyright + standard disclaimer 통합. binary 배포 시 동봉. |
| NOTE-R3-E | INFORMATIONAL (Architect) | §6 Step 1 Exit (M1.a) + §9 정량 게이트 | vtable footprint `nm` 검증을 4 method 모두로 확장 — `nm --defined-only build_on/vst3/spatial_engine_vst3.so | grep -cE "SpatialEnginePluginFactory::(getFactoryInfo|countClasses|getClassInfo|createInstance)" == 4`. v3 의 countClasses 만 검증 → 4-method 모두 검증. |
| R-1 | RESERVATION (Critic) | §8 ADR Follow-ups D6.i + R12 | Cubase D6.i deferral 의 axis 분리 — (i.1) thread axis = AM-R3-10 SDK 표준 경로로 mitigated (host-specific 가정 회피, IComponentHandler::performEdit + inputParameterChanges 만 사용) / (i.2) module-discovery axis = Cubase module manifest validation + PClassInfo2 category 잔존, D6.i validation. |
| R-2 | RESERVATION (Critic) | §8 ADR Consequences | helper 6개 BSD-3 strand 분리를 technical fact (file-level header grep 잠금) 와 legal conclusion (변호사 게이트 commercial release D6.f deferral) 두 layer 로 분리 명시. |
| R-3 | RESERVATION (Critic) | §1 Principle 2 + §2 Driver #2 + §6 Step 2.2 + §7 + §8 Drivers + §13 | SDK 인용 line drift 정정 — `ivsteditcontroller.h:180-181` → `:179-180` (mid quote off-by-1), `ivstaudioprocessor.h:317` → `:316` (off-by-1), `ipluginbase.h:194-216` → `:192-214` (boundary refinement), `SpatialEngine.h:57-58` → `:58-60` (참고). |
| R-4 | RESERVATION (Critic) | §1 Principle 2 + §3 B.2 Pros | `module_linux.cpp` filesystem includes 표현 정정 — `<experimental/filesystem>` + `<filesystem>` 가 `USE_EXPERIMENTAL_FS` macro 로 compile-time 1개 선택 (mutex), concurrent include 아님. |

---

## 13. v4 자체평가 + 다음 라운드 가이드 (Architect / Critic 라운드 4 검토 포인트)

### v4 verdict 자체평가

- **APPROVE 후보 항목 (확정도 높음, 모두 vendored grep + outer/inner git ls-tree cross-check 완료):**
  - AM-R4-1 (CRITICAL) §4/§6/§7/§9/R9/R14 SHA pin 명령 outer→inner JUCE repo cd 정정 — `.gitignore:13` 으로 outer repo 의 `core/JUCE/` ignored → empty SHA `e3b0c44...` fact-error. v4 inner repo cd 명령 = 122 entries non-empty SHA (Critic R3 cross-check 완료).
  - AM-R4-2 (CRITICAL) §6 Step 1.2 / §7 / §8 Decision/Alternatives `Vst::AudioEffect` → `Vst::Component` (option-α default) + `IComponent`+`IAudioProcessor` 직접 다중 상속 (option-β fallback) — `find ... -iname "*audioeffect*"` = 0 vendored 트리 부재 잠금. `vstcomponent.h:52` 실제 helper class 명문화.
  - AM-R4-3 (HIGH) §6 Step 0.c / §9 entry gate `.cpp` 한정 + comment exclude — `ipluginbase.h:201` docstring 1 hit logic flip 정정.
  - AM-R4-4 (HIGH) §1 / §4 / §6 / §7 / §10 / R15 Principle 2 화이트리스트 helper transitive `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 추가 — vendored grep `vstcomponentbase.cpp:38` + `vsteditcontroller.cpp:38` chain.
  - AM-R4-5 (HIGH) §5 #5 / §8 / §9 / §10 C2-Q17 dispatchCommand RT-safety scope 확장 — injectCommand 직접 호출 + cmd_fifo_.push thread-id 단일 검증 추가.
  - NOTE-R3-A (MINOR) §1/§6/§7/§8 line ref `:194-216` → `:192-214` boundary refinement.
  - NOTE-R3-B (MINOR) §0/§6 Step 0.b cleanup failure mode 명시 — Step 5 prefix 위반 = Principle 6 위반.
  - NOTE-R3-C (MINOR) §5/§6/§9 IPluginFactory2 query negative test +1 assertion.
  - NOTE-R3-D (MINOR) §7/§9 LICENSE-third-party.txt 신규 BSD-3 attribution 동봉.
  - NOTE-R3-E (INFORMATIONAL) §6/§9 vtable footprint 4-method `nm` 검증 확장.
  - R-1 (RESERVATION) §8 D6.i Cubase deferral axis 분리 (i.1 thread mitigated / i.2 module-discovery 잔존).
  - R-2 (RESERVATION) §8 BSD-3 strand technical fact vs legal conclusion 분리.
  - R-3 (RESERVATION) SDK 인용 line drift 정정 4건 (`ivsteditcontroller.h:179-180` + `ivstaudioprocessor.h:316` + `ipluginbase.h:192-214` + `SpatialEngine.h:58-60`).
  - R-4 (RESERVATION) §1/§3 `module_linux.cpp` filesystem `USE_EXPERIMENTAL_FS` macro mutex 명시.
- **추가 라운드 가능 항목 (Architect/Critic R4 NOTE 가능 — 위험도 LOW):**
  - **AM-R4-2 option-α (Component helper) vs option-β (직접 다중 상속) 결정 게이트 명료성** — Step 0.c v4 NEW BSD-3 entry gate 통과/실패 결과로 자동 결정되나, Architect 가 "Component helper + IAudioProcessor 다중 상속의 ABI 호환성 (vtable 정렬, FUnknown 충돌)" 추가 NOTE 가능. 위험도 LOW (option-β fallback 으로 자동 해소).
  - **AM-R4-4 base/source/* transitive sysdep 위험** — `base/source/fobject.cpp` 가 (`fobject.cpp` includes `base/thread/include/flock.h`) thread 의존 — Linux 환경에서 `pthread.h` 만 사용 가정이지만, vendored grep 미실시 시 Critic 가 "Step 0.c entry gate 에 base/source/*.cpp 의 transitive sysdep grep 추가" 요구 가능. 위험도 LOW (Step 1 X11 grep 게이트 통과 시 자동 해소).
  - **AM-R4-5 cmd_fifo_.push thread-id capture 메커니즘** — `std::thread::id` capture 가 RT-safe 인가? `std::this_thread::get_id()` 호출 자체가 alloc 0 검증 — Critic 가 "thread-id capture 도 alloc 0 검증" 추가 요구 가능. 위험도 LOW (libstdc++ `std::this_thread::get_id` 는 lock-free + alloc-free 표준 보장).
- **라운드 4 verdict 예상:** Architect = **APPROVE** (R3 5 minor notes 모두 inline 적용 + R4 5 amendment 모두 vendored grep cross-check 가능). Critic = **ACCEPT-WITH-RESERVATIONS** (5 amendment + 4 reservation 모두 inline 적용; ACCEPT 가능). **라운드 5 0회 추가 가능성 높음** (R4 5 amendment 가 모두 즉시 검증 가능한 fact-error 정정 + 4 reservation 이 ADR/문서 보강이라 추가 fact-error 발생 여지 LOW).

### 라운드 4 Architect / Critic 검토 포인트

1. **AM-R4-1 SHA pin 명령 정정 (CRITICAL)** — `( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum` 이 inner JUCE repo 에서 122 entries non-empty SHA 산출하는가? `core/JUCE/.git` 가 submodule 또는 nested repo 인지 verify 필요. (Architect + Critic 검토 요청)
2. **AM-R4-2 Component base class 정정 (CRITICAL)** — option-α (`Vst::Component` + `IAudioProcessor` 다중 상속) 의 vtable 정렬 + FUnknown ABI 충돌 위험은? `Vst::Component` 가 이미 `IComponent` (FUnknown chain) 상속하므로 추가 `IAudioProcessor` 다중 상속 시 FUnknown vtable 두 번 등장 위험. option-β (직접 다중 상속, helper 미사용) 가 더 안전한 default 인지 재고려? (Architect 검토 요청)
3. **AM-R4-3 entry gate `.cpp` 한정 + comment exclude (HIGH)** — `grep -vE '^\s*\*|^\s*//'` 이 모든 C++ comment style 커버하는가 (예: `/* ... */` 블록 comment)? file existence check (`test ! -f ...`) 가 더 단순/robust 한 대안인지 — Critic 검토 요청.
4. **AM-R4-4 base/source/* + flock.h 화이트리스트 (HIGH)** — `fobject.cpp` 가 `flock.h` 통해 `pthread.h` 외 추가 sysdep 끌어오는가? Linux 환경에서 BSD-3 file-level header 검증 + Step 1 X11 grep 게이트 통과만으로 충분한가? (Critic 검토 요청)
5. **AM-R4-5 cmd_fifo_.push thread-id capture (HIGH)** — `std::this_thread::get_id()` 호출이 RT-safe 인가 (alloc 0 / mutex 0)? libstdc++ 구현 검증 필요. capture 메커니즘 자체가 ctest 에 추가 alloc 발생 시키지 않는지 — Critic 검토 요청.
6. **NOTE-R3-A line ref `:192-214`** — Critic R3 의 actual L192-214 와 Architect R3 의 L192-212 사이 1줄 차이 — clean class boundary verbatim 재확인. (Architect 검토 요청)
7. **NOTE-R3-D LICENSE-third-party.txt 동봉 충분성** — binary 배포 시 동봉 절차가 CI 자동화되는가? `cmake --install` target 에 포함 여부 확인. (Architect 검토 요청)
8. **R-1 D6.i axis 분리 (Cubase)** — (i.2) module-discovery axis 의 PClassInfo2 category strings 가 vendored 트리에 명시되어 있는가? — Critic 검토 요청.

---

> **v4 종료. Architect/Critic 라운드 4 검토 요청.** v3 의 Architect APPROVE-WITH-MINOR-NOTES (NOTE-R3-A~E 5 minor) + Critic ITERATE (MA-1~5 CRITICAL 2 + HIGH 3 / R-1~4 reservations) 모두 amendment AM-R4-1~AM-R4-5 + NOTE-R3-A~E + R-1~4 로 반영됨. fact 검증 완료 (vendored vst3sdk 트리 + outer/inner git ls-tree cross-check: `.gitignore:13` core/JUCE block / inner JUCE repo 122 entries / `find ... -iname "*audioeffect*"` 0 hit / `vstcomponent.h:52` Component class / `ipluginbase.h:201` docstring 1 hit / helper transitive `vstcomponentbase.cpp:38` + `vsteditcontroller.cpp:38` base/source includes / `module_linux.cpp` `USE_EXPERIMENTAL_FS` macro mutex). **라운드 5 verdict 예상:** Architect = **APPROVE** (R3 5 minor + R4 5 amendment 모두 vendored grep 즉시 검증 가능). Critic = **ACCEPT-WITH-RESERVATIONS** → **ACCEPT** 가능 (5 amendment 모두 fact-error 정정 + 4 reservation 모두 ADR 보강). **라운드 5 0회 추가 가능성 높음** (모든 변경이 mechanical patch + grep verifiable).
