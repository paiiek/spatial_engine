# Open Questions

## spatial-engine-v0 (round 2 revised draft) - 2026-04-27
- [ ] ADR 0003 IPC transport — confirm OSC/UDP for v0, or argue for shm+UDS based on a concrete observable (e.g., p99 drag-to-render latency under N-object load) — affects v0→v1 IPC migration triggers.
- [ ] P13 listener count — N≥6 (lab pilot) vs N≥12 (per user's vid2spatial v3 listening-test bar) — affects whether perceptual sign-off can claim statistical significance.
- [ ] Geometry-edit Non-Goal — round 2 chose option (b) "geometry editing is v0 Non-Goal" with three concrete UI-modification examples; if Architect/Critic prefers option (a) "add `/sys/edit_geometry` to OSC v0 so geometry editing IS the demonstrable end-to-end UI modification," revert to (a) in round 3.
- [ ] `nih-plug` 1.0 release timing — current ADR 0002 falsifier triggers re-litigation of JUCE only at v1 kickoff (v0 acceptance + 6 months); confirm this delay is acceptable vs an earlier checkpoint.
- [ ] Lab machine confirmation — Risk #2 names Linux 6.x generic kernel + USB class-compliant audio interface (Focusrite Scarlett family) OR built-in HDA via PipeWire-JACK, but the actual lab target machine has not yet been pinned; P2 follow-up needed.
- [ ] DELIBERATE-mode escalation — Architect R1 escalated to DELIBERATE; round 2 added pre-mortem and expanded test plan. Round 2 reviewer should confirm DELIBERATE remains correct (vs reverting to SHORT) before round 3.
