# Critic R3 Review — spatial-engine-phaseC-C2-optionB.md v3

## Verdict: **ITERATE**

(MA = 4 must-amend items including 2 CRITICAL fact-errors that block execution. ACCEPT-WITH-RESERVATIONS is not appropriate because the SHA pin command produces a meaningless hash, the AudioEffect class doesn't exist, the entry gate has a false-fail, and helper transitive includes are not addressed in the whitelist.)

---

## Fact-check 결과 (adversarial)

| Plan claim | Verified? | Evidence |
|---|---|---|
| AM-R3-1: `BEGIN_FACTORY_DEF` only `#define`d once at `ivstcomponent.h:36`, no `END_FACTORY`/`DEF_CLASS2` | TRUE | `grep "#define BEGIN_FACTORY_DEF"` → 1 hit at `ivstcomponent.h:36`; `grep "#define END_FACTORY"` → 0 hits; `grep "#define DEF_CLASS2"` → 0 hits |
| AM-R3-1: `BEGIN_FACTORY_DEF` body references `new CPluginFactory(...)` and `gPluginFactory` | TRUE | `ivstcomponent.h:36-41` body shows `gPluginFactory = new CPluginFactory (factoryInfo);` |
| AM-R3-1: `IPluginFactory` 4 virtuals at `ipluginbase.h:194-216` | MOSTLY TRUE | actual: class L192, virtuals L197/202/205/208, IID L211/214. **Cite L192-214 instead of 194-216.** |
| AM-R3-3: SDK quote `ivsteditcontroller.h:175-176`/`:180-181`/`:183-184` | MOSTLY TRUE | actual: 175-176/**179-180**/183-184 (mid quote off-by-1) |
| AM-R3-3: SDK quote `ivstaudioprocessor.h:317` | OFF-BY-ONE | actual line **316** |
| AM-R3-6: `git ls-tree -r HEAD core/JUCE/...VST3_SDK \| sha256sum` reproducible | **FALSE — CRITICAL** | `core/JUCE/` gitignored at `.gitignore:13`. `git ls-tree -r HEAD` returns **0 entries**. SHA256 = `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (empty input). Trivially passes any environment. |
| AM-R3-8: helper 6 file-level BSD-3 standard text | TRUE | All 6 confirmed verbatim. |
| AM-R3-2: `module_linux.cpp` POSIX 5 + filesystem includes | MISLEADING | `<experimental/filesystem>` and `<filesystem>` are mutex via `USE_EXPERIMENTAL_FS` macro (l.46-72), NOT concurrent. |
| AM-R3-1 entry gate `... | wc -l == 0` | **FALSE** | actual `wc -l == 1` (docstring at `ipluginbase.h:201`). Gate as written would always NOTE-escalate. |
| §6 Step 1.2: `class SpatialEngineProcessor : public Steinberg::Vst::AudioEffect` | **FALSE — CRITICAL** | `class AudioEffect` does NOT exist in vendored tree. `find ... -iname "*audioeffect*"` = 0. `vstcomponent.h:52` defines `class Component : public ComponentBase, public IComponent`. Helper to inherit is `Vst::Component`. |
| Trigger commits `c2a5ad0` / `572cc2d` | TRUE | `git show` confirms. |
| `vst3/SpatialEngineVST3.cpp` staged-delete `D` | TRUE | confirmed. |
| §15.A `dispatchCommand` at `SpatialEngine.h:57-58` | MOSTLY TRUE | actual line 58-60 (off by 1). Calls `osc_backend_.injectCommand(cmd)`. |

---

## §13 의 R3 검토 포인트 4건 (Critic 책임)

### #2 — AM-R3-3 dispatchCommand RT-safety 검증 범위 (cmd_fifo SPSC push 누락?)

**검토 결과:** **누락 확인 — MA 발생 (조건부, R-아닌 MA-2.5 또는 R-5).**

§5 #5 ctest 확장은 `engine_->dispatchCommand` 1000회 alloc=0/mutex=0 만 검증. dispatchCommand는 `osc_backend_.injectCommand(cmd)` 인라인 호출, injectCommand는 SPSC `cmd_fifo_` push. **SPSC single-producer 가정이 audio thread + control thread 양쪽 호출 시 깨진다.** 

추가 권고: `cmd_fifo_.push` thread-id 단일 검증 + `OSCBackend::injectCommand` 직접 호출 ctest. **Severity: HIGH (MA-2.5, AM-R4 추가).**

### #4 — AM-R3-6 SHA pin reproducibility

**검토 결과:** **CRITICAL FAIL — MA-1.** (위 Fact-check 표 참고)

### #6 — AM-R3-7 Cubase deferral (R12 자동 해소?)

**검토 결과:** **부분 해소 — R-1.** thread axis는 AM-R3-10 SDK 표준 경로로 해소. **잔존 axis** = Cubase module manifest validation rule + PClassInfo2 category 차이. ADR §8 D6.i 에 axis 분리 명시 권고.

### #7 — AM-R3-8 helper 6 BSD-3 별개 strand 법적 근거

**검토 결과:** **technical fact 정확, 법적 결론은 변호사 의무.** 6 helpers 모두 `(c) 2023, Steinberg` + BSD-3 표준 문구 verbatim. 별개 strand 인정은 변호사 의무 (AM-R3-5 deferral 정합). ADR §8 Consequences 에 "technical fact 잠금 / legal conclusion 변호사 deferral" 분리 권고. **R-2.**

---

## MA (must-amend) 항목

### MA-1 (CRITICAL) — AM-R3-6 SHA pin command produces empty hash

**Severity:** CRITICAL — 게이트 자체 무의미.

**근거:**
- `core/JUCE/` gitignored at `.gitignore:13`.
- `git ls-tree -r HEAD core/JUCE/.../VST3_SDK` → **0 lines**.
- 결과 sha256: `e3b0c44...` (empty string sha256).
- §9 정량 게이트 "vst3sdk SHA pin 통과" trivially pass.

**Fix 권고:**
1. inner JUCE repo cd:
   ```bash
   ( cd core/JUCE && git ls-tree -r HEAD modules/juce_audio_processors/format_types/VST3_SDK ) | sha256sum > .ci/vst3sdk_sha.txt
   ```
   (122 entries 산출 확인됨)
2. 또는 file content hash:
   ```bash
   find core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum > .ci/vst3sdk_sha.txt
   ```
3. 또는 JUCE upstream SHA (`bootstrap_juce.sh` `EXPECTED_SHA=4f43011b...`) dual-pin 으로 proxy.

§4 시나리오 5 + §6 Step 0.b + §9 + R9 모두 정정.

### MA-2 (CRITICAL) — `Vst::AudioEffect` 헬퍼 클래스 부재

**Severity:** CRITICAL — 빌드 실패.

**근거:**
- §6 Step 1.2 line 424 가 `class SpatialEngineProcessor : public Steinberg::Vst::AudioEffect` 명시.
- `find ... -iname "*audioeffect*"` = **0 hits**.
- `vstcomponent.h:52` 실제 = `class Component : public ComponentBase, public IComponent`.

**Fix 권고:** §6 Step 1.2 + §7 NEW 의 base class를 `Steinberg::Vst::Component` 으로 (helper) 또는 `IComponent` + `IAudioProcessor` 직접 다중 상속. ADR §8 정합 정정.

### MA-3 (HIGH) — AM-R3-1 entry gate grep returns 1 hit not 0

**Severity:** HIGH — entry gate logic flip.

**근거:**
- `grep "CPluginFactory::CPluginFactory\|CPluginFactory::registerClass\|CPluginFactory::createInstance" .../VST3_SDK/ | wc -l` → **1** (docstring `ipluginbase.h:201`).
- Plan: `expected: 0` → 결과 ≠ 0 시 매크로 시퀀스 재고려 NOTE escalation.

**Fix 권고:**
1. `.cpp` 한정 + comment exclude:
   ```bash
   grep -rn --include="*.cpp" "CPluginFactory::CPluginFactory\|CPluginFactory::registerClass\|CPluginFactory::createInstance" core/JUCE/.../VST3_SDK/ | wc -l
   ```
   expected: 0.
2. 또는 file existence check:
   ```bash
   test ! -f core/JUCE/.../VST3_SDK/public.sdk/source/common/pluginfactory.cpp && echo "absent"
   ```

### MA-4 (HIGH) — Principle 2 화이트리스트가 helper transitive `base/source/` 누락

**Severity:** HIGH — Step 1 빌드 실패.

**근거:**
- `vstcomponentbase.cpp:38: #include "base/source/fstring.h"` (필수)
- `vsteditcontroller.cpp:38: #include "base/source/updatehandler.h"` (필수)
- `fstring.h` includes `base/source/fobject.h`; `fobject.cpp` includes `base/thread/include/flock.h`.

**Fix 권고:**
- §1 Principle 2 화이트리스트에 추가:
  - `base/source/{fstring,fobject,fdebug,fbuffer,futils}.{cpp,h}` + `base/source/updatehandler.{cpp,h}`
  - `base/thread/include/flock.h`
- §4 시나리오 1 회피 list + §6 Step 1.1 source list 동기화.
- 대안: vtable-only fallback (helper 6개 미사용) — 진입 조건 명시.

---

## R (reservations) — ADR 보강 권고

### R-1 — AM-R3-7 Cubase deferral thread axis vs module-discovery axis 분리

R12 잔존 위험 = Cubase module manifest validation + PClassInfo2 category strings. ADR §8 D6.i 에 "thread axis SDK 표준 mitigated, module-discovery axis Phase D6.i validation" 명시.

### R-2 — AM-R3-8 BSD-3 별개 strand: technical fact vs legal conclusion 분리

ADR §8 Consequences 마지막 라인 분리: "technical fact: helper 헤더 BSD-3 (grep 잠금) / legal conclusion: 변호사 게이트 commercial release (D6.f) deferral".

### R-3 — SDK 인용 line numbers minor drift

- `ivsteditcontroller.h:175-176/180-181/183-184` → 실제 175-176/**179-180**/183-184
- `ivstaudioprocessor.h:317` → 실제 **316**
- `ipluginbase.h:194-216` → 실제 **192-214** (boundary refinement)
- `SpatialEngine.h:57-58` → 실제 **58-60**

executor 코멘트 옮김 시 1줄 drift. 정정 권고.

### R-4 — `module_linux.cpp` filesystem includes mutex 명시

§1 Principle 2 (line 41) + §3 B.2 Pros (line 84) 가 `<experimental/filesystem>` + `<filesystem>` 둘 다 concurrent 표현. 실제는 `USE_EXPERIMENTAL_FS` macro mutex (l.46-72). "compile-time 1개 선택" 명시.

---

## 다음 단계 권고

**verdict = ITERATE.** Planner 가 v4 amendment AM-R4-1 ~ AM-R4-5 작성 필요:

1. **AM-R4-1 (CRITICAL):** SHA pin 명령을 inner JUCE cd 또는 file content hash 로 정정.
2. **AM-R4-2 (CRITICAL):** `Vst::AudioEffect` → `Vst::Component` (또는 IComponent+IAudioProcessor 직접 다중 상속).
3. **AM-R4-3 (HIGH):** entry gate grep `.cpp` 한정 + comment exclude 또는 file existence check.
4. **AM-R4-4 (HIGH):** Principle 2 화이트리스트에 `base/source/{fstring,fobject,fdebug,fbuffer,futils,updatehandler}.{cpp,h}` + `base/thread/include/flock.h` 추가.
5. **AM-R4-5 (HIGH):** §5 #5 ctest 에 cmd_fifo SPSC push thread-id 단일 검증 + `OSCBackend::injectCommand` 직접 호출 ctest 추가.

R 항목 1-4 ADR 보강만 권고. Architect NOTE-R3-A~E (line drift, cleanup failure mode, IPluginFactory2 negative test, BSD-3 attribution gate, vtable footprint 4-method) 도 inline 적용.

라운드 4 verdict 예상: 5 amendment + Architect 5 notes 모두 반영 시 → Critic ACCEPT-WITH-RESERVATIONS, Architect APPROVE. R5 0회 추가 가능성 높음 (각 fact grep 즉시 검증 가능).
