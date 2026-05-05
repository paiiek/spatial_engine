# Open Questions

## spatial-engine-v1 - 2026-05-01

- [ ] F1: 3D dimensionality probe threshold (`1e-3` on `max(|speaker.y|)`) — should it be exposed as a `LayoutCompatibilityChecker` constant for consistency with v0 ADR M4? — Affects future layout-validation refactor; v1 keeps it private in `AlgorithmAnalyticReference.cpp`.
- [ ] F2: namespace placement for future HOA decoder — `core/src/ambi/` (sibling to encoder) or `core/src/render/` (co-located with VBAP/DBAP/WFS)? — Defer to v2 decoder spec.
- [ ] F3: `ElevationView` integration into `app.py` main window — this milestone or follow-up? — Decision: follow-up, to keep F3 scope minimal.
- [ ] F4: on hosts without `mido`, should `MidiBridge.start()` log a stderr warning or stay silent? — Decision: log to stderr (matches v0 backend-availability log style).
