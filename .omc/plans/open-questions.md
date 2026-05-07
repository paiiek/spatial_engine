# Open Questions

## spatial-engine-v1 - 2026-05-01

- [ ] F1: 3D dimensionality probe threshold (`1e-3` on `max(|speaker.y|)`) — should it be exposed as a `LayoutCompatibilityChecker` constant for consistency with v0 ADR M4? — Affects future layout-validation refactor; v1 keeps it private in `AlgorithmAnalyticReference.cpp`.
- [ ] F2: namespace placement for future HOA decoder — `core/src/ambi/` (sibling to encoder) or `core/src/render/` (co-located with VBAP/DBAP/WFS)? — Defer to v2 decoder spec.
- [ ] F3: `ElevationView` integration into `app.py` main window — this milestone or follow-up? — Decision: follow-up, to keep F3 scope minimal.
- [ ] F4: on hosts without `mido`, should `MidiBridge.start()` log a stderr warning or stay silent? — Decision: log to stderr (matches v0 backend-availability log style).

## spatial-engine-phaseC-C2 - 2026-05-05

- [ ] C2-Q1: JUCE pin version — 7.0.12 vs 7.0.x latest? — Phase B 검증된 minor 확인 필요. 너무 새 버전은 `juce_audio_plugin_client` API 변동 리스크.
- [ ] C2-Q2: MVP 의 6 파라미터 매핑 범위 — obj 0 만 노출 vs 다중 obj? — Phase D6 와 충돌 가능성 검토 필요. 현재 plan 은 obj 0 매핑 가정.
- [ ] C2-Q3: Generic editor 라벨 polish 를 C2 에 포함할지 Phase D6 까지 미룰지 — Reaper 의 6 슬라이더 라벨 사용자 혼동 가능.
- [ ] C2-Q4: pluginval strictness 5 fallback — D+9 시 strictness 5 fail 시 strictness 4 완화 vs C4 → D8 강등? — Phase C ADR Option C 와 상호작용 확정 필요.
- [ ] C2-Q5: VST3=ON CI job venue — `.github/workflows/vst3.yml` 신설 vs `.pre-commit-config.yaml` 로컬 hook? — 현재 repo 는 `.github/workflows/` 부재.
- [x] C2-Q6 (v2 결정, C-M3): `setLatencySamples(0)` 보고. 동적 propagation delay reporting 은 Phase D6 (D6.a) 검토.
- [ ] C2-Q7: D+2 bootstrap 실패 시 Option B 재활성화 의사결정자 — Architect 단독 vs Architect+Critic 합의? — 시간 손실 최소화를 위한 escalation 경로.
- [x] C2-Q8 (2026-05-06 결정): **JUCE 7 Educational tier 채택** (paik402@snu.ac.kr SNU 학술 사용). VST3 SDK 는 JUCE 번들 그대로 — Educational tier 가 GPLv3 fallback 자동 회피. C2 진행 전제 해소.

## spatial-engine-phaseC-C2-optionB - 2026-05-06

- [ ] C2-Q9 (재발현): VST3 SDK 라이선스 — Steinberg agreement 등록 vs GPLv3 fallback. spatial_engine 자체 라이선스 미정 시 GPLv3 fallback 가능한가? — Step 0.a entry 게이트 결정 의존.
- [ ] C2-Q10 (재정의): Option B 의 M2 strictness 게이트 후보 ① Steinberg validator (B.1) ② 자체 in-process VST3 host (B.2) ③ Phase D6 부채 회계 (B.3). 권장 라운드 1 draft = B.1 → B.3 fallback. — Architect/Critic 라운드 2 결정 필요.
- [ ] C2-Q11: Option B.2 채택 시 in-process host fixture 위치 — `vst3/tests/` (Driver #1 격리) vs `core/tests/` (host 코드 재사용). 권장 라운드 1 draft = `vst3/tests/`. — Architect 검토.
- [ ] C2-Q12 (신규): `process` audio thread 의 `inputParameterChanges` 를 control thread 로 forward 하는 layer 후보 ① component 측 std::thread ② host event-thread 신뢰 (controller→component IConnectionPoint) ③ Phase D6 부채. 권장 라운드 1 draft = ②. — SDK 문서 인용 검증 필요.
- [ ] C2-Q13: `room_preset_idx` (Dry/Small/Medium/Large) ↔ `PayloadReverbSelect` (fdn/ir) semantic mismatch — preset 매핑 테이블 신설 vs Phase D6 deferral. — Phase B reverb 설계 정합 검토.
- [ ] C2-Q14: `kSpatialEngineProcessor/ControllerUID` (128-bit CID) 자체 발급 (UUID v4) 시 호스트 충돌 가능성 — DEV prefix 표식 사용 여부, Phase D6 정식 등록 시 교체 정책.
- [x] C2-Q15 (2026-05-07 Step 0.c gate 발현, v5 amendment): v4 AM-R4-4 화이트리스트 fact-error 정정 — `futils.{cpp,h}` 가 vendored 트리에 부재 (실측 `core/JUCE/.../VST3_SDK/base/source/`: `baseiids.cpp` + `fbuffer{.cpp,.h}` + `fdebug{.cpp,.h}` + `fobject{.cpp,.h}` + `fstreamer{.cpp,.h}` + `fstring{.cpp,.h}` + `updatehandler{.cpp,.h}` + headers `classfactoryhelpers.h` / `fcommandline.h`, `futils` 부재). Step 1 helper 진입 default 강등: **option-β vtable-only path (helper 6개 미사용)** — `IComponent` + `IAudioProcessor` 직접 다중 상속 + `FUnknown` 직접 구현. 추후 helper 사용 재검토는 vendored 트리 실측 file list 기준 (`baseiids` / `fstreamer` 추가, `futils` 제거) 으로 재구성. — Step 0 commit footer 잠금.
- [x] C2-Q16 (2026-05-07 Step 1 link gate 발현, v6 amendment): plan §1 Principle 2 화이트리스트가 SDK base layer 누락. Step 1 link 단계에서 `Steinberg::FUnknown::iid` / `Steinberg::FUnknownPrivate::atomicAdd` / `Steinberg::IBStream::iid` undefined reference 발현 → `pluginterfaces/base/{funknown,coreiids}.cpp` 추가 link 필수. 두 파일은 file-level header 가 "Steinberg LICENSE file 종속" (BSD-3 표준 문구 부재) → Strand 1.b 분류 (Steinberg agreement OR GPLv3 dual-strand, GPLv3 fallback default). c2-licensing.md §"Strand 1.b" 잠금. plan §1 Principle 2 화이트리스트 확장 + §4 시나리오 3 별도 axis 분리 명시 권고. ODR fact-error 도 함께 발현 — wrapper 패턴 (INIT_CLASS_IID + #include vstinitiids.cpp) 이 SDK 헤더의 DECLARE_CLASS_IID 와 ODR 충돌. SDK 표준 패턴 = vstinitiids.cpp 를 INIT_CLASS_IID 없이 직접 컴파일 (헤더는 constexpr TUID 만 emit, vstinitiids.cpp 의 DEF_CLASS_IID 가 ClassName::iid emit, 1 link target 1회). wrapper 폐기. PIC 강제 추가 (top-level CMake: VST3=ON → CMAKE_POSITION_INDEPENDENT_CODE ON, OFF baseline 영향 0 — VST3=OFF default 분기). Step 1 commit footer 잠금.
