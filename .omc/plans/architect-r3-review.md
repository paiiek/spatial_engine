# Architect R3 Review — spatial-engine-phaseC-C2-optionB.md v3

## Verdict: APPROVE-WITH-MINOR-NOTES

Rationale: All CRITICAL/HIGH amendments AM-R3-1 ~ AM-R3-10 are factually verified against the vendored vst3sdk tree. Three minor notes (one off-by-line citation, one stronger-than-stated grep finding, one deferred-ABI ergonomic gap) are mechanical fixes that the planner can apply without amendment escalation.

---

## Fact-check 결과 (grep cross-check)

| 검증 항목 | 플랜 인용 | 실측 | 결과 |
|---|---|---|---|
| AM-R3-1 IPluginFactory 4 virtual method signatures | `ipluginbase.h:194-216` `getFactoryInfo` / `countClasses` / `getClassInfo` / `createInstance` | line 197 / 202 / 205 / 208 (verbatim signature 일치) — 클래스 본체 `:192-212`, `static FUID iid` `:211`, `DECLARE_CLASS_IID` `:214` | **PASS** (단, `:217` 인용은 off-by-line — 실측 `:211` 또는 `:214`) |
| AM-R3-1 `BEGIN_FACTORY_DEF` 단독 정의 | `ivstcomponent.h:36` 단 1건 | `ivstcomponent.h:36` `#define BEGIN_FACTORY_DEF(vendor,url,email)` 단 1건; `END_FACTORY` / `END_FACTORY_DEF` / `DEF_CLASS2` / `EXPORT_FACTORY` `#define` **0 hit** | **PASS** |
| AM-R3-1 `CPluginFactory::CPluginFactory` / `registerClass` `.cpp` 구현 부재 | vendored 트리 0 hit | `grep -rn "CPluginFactory::CPluginFactory\|registerClass\|class CPluginFactory"` = 1 hit (단순 doc-comment reference at `ipluginbase.h:201`) | **PASS+** (실제로는 클래스 선언 자체도 부재 — 플랜이 약하게 stated, NOTE-R3-A 참고) |
| AM-R3-1 `pluginfactory.cpp` 부재 | `public.sdk/source/common/` 부재 | 디렉토리 파일 = `memorystream.{h,cpp}`, `pluginview.{h,cpp}`, `readfile.{h,cpp}` — `pluginfactory.cpp` **0 hit** | **PASS** |
| AM-R3-2 `module_linux.cpp` line 30-67 includes | POSIX 5 헤더 + libstdc++ filesystem | line 37-44 = `module.h` / `optional.h` / `stringconvert.h` / `<algorithm>` / `<dlfcn.h>` / `<sys/types.h>` / `<sys/utsname.h>` / `<unistd.h>` + line 62/67 conditional `<experimental/filesystem>`/`<filesystem>` | **PASS** (X11/freetype/alsa/webkit dev 헤더 0) |
| AM-R3-3 `ivsteditcontroller.h` UI-Thread context 3회 | `:175-176`, `:180-181`, `:183-184` | `:176` "must be called in the UI-Thread context!" + `:180` 동일 + `:184` 동일 — 3회 verbatim | **PASS** |
| AM-R3-3 `ivstaudioprocessor.h:317` audio thread drain | "The Process call ..." | `:317` `virtual tresult PLUGIN_API process (ProcessData& data) = 0;` 직전 doc `:316` "The Process call, where all information (parameter changes, event, audio buffer) are passed." | **PASS** |
| AM-R3-8 helper 6개 file-level BSD-3 | vstcomponentbase / vstcomponent / vsteditcontroller / vstparameters / vstbus / vstpresetfile | 6개 모두 head -30 에서 "Redistribution and use in source and binary forms ..." + "AS IS" verbatim 검출 | **PASS** |
| `module_linux.cpp` BSD-3 file-level header | line 10-34 BSD-3 + (c) 2023 Steinberg | line 10 "LICENSE" / line 13-34 BSD-3 표준 문구 verbatim + (c) 2023 Steinberg | **PASS** |
| SMTG_EXPORT_SYMBOL Linux visibility | `fplatform.h:164,231` `__attribute__ ((visibility ("default")))` | line 164 + line 231 모두 `#define SMTG_EXPORT_SYMBOL __attribute__ ((visibility ("default")))` (각각 LINUX / MAC 분기) | **PASS** |
| `IPluginFactory2` / `IPluginFactory3` declaration | (플랜이 "deferral" 만 명시) | `:316-330` IPluginFactory2 + `:440-459` IPluginFactory3 — 헤더 자체는 vendored, vtable 직접 작성 시 query 처리 결정 필요 | **DEFERRED OK** (NOTE-R3-C 참고) |

**총평:** 핵심 fact-check 9/9 PASS. v3 의 vendored-grep cross-check 가 plan 본문에 verbatim 잠금 — Architect R2 의 NOTE-R2-1 CRITICAL (factory 매크로 본체 부재) 이 AM-R3-1 vtable 직접 작성으로 실효 해소.

---

## §13 의 R3 검토 포인트 4건 (Architect 책임)

### 검토 포인트 #1 — AM-R3-1 vtable + IPluginFactory2/3 deferral 정합성
- 4 virtual method signature `ipluginbase.h:197/202/205/208` verbatim 일치
- `:217` 인용은 off-by-6 → 실측 `:211` `static const FUID iid;` 또는 `:214` `DECLARE_CLASS_IID`
- IPluginFactory2/3 query → `kNotImplemented` 반환만 하면 SDK FUnknown 표준 동작
- Phase D6.j follow-up deferral 적절. C2 game-stopping 아님.
- **PASS** + NOTE-R3-A + NOTE-R3-C

### 검토 포인트 #3 — AM-R3-10 host thread 출처 강등 fair-fact 충족?
- §4 시나리오 4 가 출처 evidence-level 명시 강등 ("3rd party report")
- 회피책 default = "host-specific 가정 회피, SDK 표준만" → 3rd party 의존 0
- 정식 docs 부재가 회피책 채택 자체를 차단하지 않음 (오히려 정당화)
- **PASS** (fair-fact 의무 충족)

### 검토 포인트 #5 — AM-R3-4 cleanup 5단계 순서 위반 failure mode 명시?
- 5단계 중 단 1건 (Step 5 commit 을 1-4 보다 먼저) 만 실질 failure mode
- 나머지 1-4 사이 swap 은 결과 동일 (`--cached` 플래그가 working tree 보존)
- 플랜 §0 v2→v3 변경이 "일반론" 만 명시, 구체 failure mode 미명시
- **MINOR NOTE** (NOTE-R3-B): "Step 5 가 1-4 보다 먼저 실행되면 untracked 4 + modified 1 이 cleanup commit 에서 누락되어 Principle 6 위반" 한 줄 추가 권장

### 검토 포인트 #8 — vtable footprint + IPluginFactory::iid FUID + helper BSD-3 link-level 전염 회피
- (a) vtable footprint: `nm` 검증을 4 method 모두로 확장 권장 (NOTE-R3-E)
- (b) FUID 비교: `vstinitiids.cpp` 가 selective include 화이트리스트에 포함 → link 단계 안전. `:217` → `:211/:214` 정정 (NOTE-R3-A)
- (c) helper BSD-3 link-level: file-level license 우선 적용 OSS 관행. 단 §9 정량 게이트에 binary 배포 시 BSD-3 attribution 동봉 절차 미명시 (NOTE-R3-D)
- **PASS** + NOTE-R3-A + NOTE-R3-D

---

## 추가 NOTE

### NOTE-R3-A (MINOR — line 인용 정정)
**위치:** §13 #8 — `:217` 인용을 `:211` 또는 `:214` 로 정정.

### NOTE-R3-B (MINOR — cleanup failure mode 명시)
**위치:** §0 v2→v3 변경 + §6 Step 0.b. "Step 5 (commit) 가 Step 1-4 보다 먼저 실행되면 untracked 4 + modified 1 이 cleanup commit 에서 누락 → Principle 6 (단일 commit 의 bisect 회복비용) 위반. Step 1-4 사이 swap 은 결과 동일." 한 줄 추가.

### NOTE-R3-C (MINOR — IPluginFactory2 query negative test)
**위치:** §5 #0 host fixture + §9 정량 게이트.
```cpp
Steinberg::IPluginFactory2* factory2 = nullptr;
auto result = factory->queryInterface(Steinberg::IPluginFactory2::iid, (void**)&factory2);
assert(result == Steinberg::kNotImplemented || result == Steinberg::kNoInterface);
assert(factory2 == nullptr);
```

### NOTE-R3-D (MINOR — BSD-3 attribution 정량 게이트)
**위치:** §9 정량 게이트 + ADR Consequences. `vst3/LICENSE-third-party.txt` 신규 — module_linux.cpp + helper 6개 BSD-3 copyright + disclaimer 동봉.

### NOTE-R3-E (INFORMATIONAL — vtable footprint 보강)
**위치:** §9 정량 게이트.
```bash
nm --defined-only build_on/vst3/spatial_engine_vst3.so | grep -cE "SpatialEnginePluginFactory::(getFactoryInfo|countClasses|getClassInfo|createInstance)" == 4
```

---

## 다음 단계 권고

**Verdict: APPROVE-WITH-MINOR-NOTES**

- Critic R3 = ACCEPT/ACCEPT-WITH-RESERVATIONS → planner 가 NOTE-R3-A~E 5건 inline patch (R4 라운드 불필요) → autopilot 진입.
- Critic R3 = ITERATE/REJECT → architect minor + critic major 합산 amendment → R4 진행.

**Driver 별 평가:** #1 (OFF dual-gate) / #2 (IComponentHandler+inputParameterChanges 단일 경로) / #3 (selective include + Step 0.c X11 grep) 모두 충분.
