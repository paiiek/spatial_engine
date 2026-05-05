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
