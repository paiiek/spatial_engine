# Phase C / C2 — License Decision Record

**Plan:** `.omc/plans/spatial-engine-phaseC-C2-optionB.md` v4
**Decision date:** 2026-05-07
**Decision context:** Step 0.a entry gate, AM-5 + AM-R3-5 deferral, AM-R3-8 helper 6개 BSD-3 grep 잠금.

---

## Strand 1 — vst3sdk root LICENSE.txt (Steinberg agreement OR GPLv3)

**Decision:** **GPLv3 fallback 자동 동의 (default)**.

- **Rationale:** spatial_engine 자체 라이선스 미정 + 학술 (SNU paik402@snu.ac.kr) 사용. Steinberg dev portal 가입은 commercial release 시점 (Phase D6.f) 까지 deferral.
- **변호사 게이트:** Phase D6.f (commercial release pre-checklist) 에 명시.
- **C2 진행 차단 사유 아님** (AM-R3-5).

## Strand 2 — public.sdk + module_linux.cpp + helper `.cpp` (BSD-3-Clause)

**Decision:** 추가 동의 불필요, 호환 확인 완료.

- **AM-R3-8 grep 결과 잠금:** `vstcomponentbase` / `vstcomponent` / `vsteditcontroller` / `vstparameters` / `vstbus` / `vstpresetfile` 6개 모두 file-level "Redistribution and use in source and binary forms ... AS IS" + Copyright 2023 Steinberg 표준 BSD-3 문구 verbatim 검출.
- `module_linux.cpp` line 10-34 도 BSD-3 verbatim.
- **법적 결론은 변호사 deferral** (Critic R3 R-2): technical fact 잠금 / legal conclusion = D6.f.

## Strand 3 — spatial_engine 자체 라이선스

**Decision:** GPLv3 fallback 자동 동의 (Strand 1 의 결과로 자동 전염).

- **Phase A/B/C 산출물 GPLv3 호환성 표 (M3):**

| Phase | 산출물 | 외부 의존 | 라이선스 | GPLv3 호환 |
|---|---|---|---|---|
| A | PySide6 UI | PySide6 | LGPLv3 | ✓ |
| A | OSC bridge | liblo | LGPLv2.1+ | ✓ |
| B | FDN reverb (자체) | — | (자체 코드) | ✓ |
| B | IRConvolutionStub (자체) | — | (자체 코드) | ✓ |
| C | VST3 plugin (자체) | vst3sdk 헤더 + helper 6 BSD-3 | GPLv3 ↔ Steinberg dual (D6.f) | ✓ (BSD-3 호환), Steinberg dev = D6.f |

- 모든 외부 의존은 GPLv3 호환 또는 호환성 검증 완료.
- spatial_engine 자체 코드는 GPLv3 fallback 자동 동의.

---

## Plan-amendment hooks

- **AM-R3-5:** Strand 1 의 변호사 게이트 deferral.
- **AM-R3-8:** Strand 2 의 helper 6개 BSD-3 grep 잠금 — 본 문서 §"Strand 2".
- **AM-5 + M3:** Phase A 산출물 호환성 표 — 본 문서 §"Strand 3".

## Phase D6 follow-up (D6.f)

- Steinberg agreement 정식 회수 (commercial release pre-flight).
- spatial_engine LICENSE 파일 추가 (GPLv3 fulltext).
- BSD-3 attribution `vst3/LICENSE-third-party.txt` 작성 (Architect NOTE-R3-D 권고).
- Cubase module manifest validation (Critic R3 R-1 분리 axis).
