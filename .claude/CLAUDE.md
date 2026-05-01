# Spatial Engine — 프로젝트 작업 방침

## 자율 실행 방침 (필수)

이 프로젝트의 모든 구현 작업은 다음 워크플로를 따른다:

1. **계획**: `/oh-my-claudecode:ralplan` — Planner → Architect → Critic 합의 후 플랜 확정
2. **실행**: `/oh-my-claudecode:autopilot` — 플랜 기반 자율 수행, 중단 없이 완료까지

### 세션 중단 대응
- 세션이 끊겨도 `.omc/plans/` 의 활성 플랜이 기준
- 재개 시: `autopilot` 가 `.omc/plans/` 에서 플랜을 읽어 이어서 진행
- **절대 처음부터 다시 계획하지 않는다** — 기존 플랜 이어서 실행

### 검토 기준
- 모든 피처: `ralplan` 으로 Architect/Critic 합의 필수
- 하드웨어 불필요 테스트: CI 완전 통과 확인 후 커밋
- 커밋 후 `v*` 태그는 사용자 명시 요청 시만

### 현재 플랜 위치
- `.omc/plans/spatial-engine-v0.md` — v0 기준 플랜 (P0~P12 완료)
- 다음 작업은 새 플랜 생성: `.omc/plans/spatial-engine-v1.md`

## 프로젝트 개요
- C++ JUCE 코어 + PySide6 UI
- 빌드: `cd core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j$(nproc)`
- 테스트: `ctest --output-on-failure` + `python3 -m pytest`
- v0.1.0 태그 완료 (commit 19679c6)
