# Architect Final Validation — v0.3.0 Phase C4 Track A (HEAD `72f9cbb`)

## Final report

**Verdict: APPROVE-with-followups.**

Sprint hit core deliverables — every step S1..S7 produced cited commits, 70/70 ctest (modulo bind_collision env-flake) and 193 pytest pass, A.6/A.9/A.10/A.15a/A.15b are closed with falsifiable evidence, ADR 0010/0011 transitioned Draft→Accepted with spec pin `v0.3.0-c4-final` (`docs/adr/0010-vst3-osc-binding-model.md:3`, `docs/adr/0011-vst3-osc-multi-instance-discovery.md:3`). Frozen A1-ε contract is honoured: zero `#include <juce_...>` in new OSC files, no sidecar / UDS / systemd binary added, file-based registry with `boot_id` staleness guard (`vst3/osc/PluginInstanceRegistry.cpp:59,372,401`) and XDG empty-string fallback (`vst3/osc/RegistryPath.h:18-19`) match ADR 0011 rules verbatim. SDK audit in S2.6 cited the exact VST3 SDK lines (`ivsteditcontroller.h:176-188`) and picked strategy (a) message-thread queue marshaling consistently with the plan's decision rule.

**Seven follow-ups are required before v0.3.0 tag**; none are structural, but two are functional regressions silently re-litigating frozen contracts.

## Findings (file:line)

**F1 — kMute reverse path silently DEFERRED past S7 (functional regression).** `vst3/SpatialEnginePluginUdp.cpp:138,151,218` still reads "kMute (7): DEFERRED to S7" after S7 has shipped. `audioCommandToParamEdit()` maps the 7 pre-existing params but skips `ObjMute`. Console-driven kMute via the reverse path therefore never reaches the controller's `performEdit`. S7 commit `72f9cbb` activated the param + state v3 writer + audio mute, but missed wiring through `SpatialEnginePluginUdp`. This silently violates the plan's S4+S7 contract that the 8-param surface is reverse-path-complete.

**F2 — `vst3/SpatialEngineRegistry.{h,cpp}` planned but never created.** Plan §11 line 670 mandates a Controller-side registry client wrapper "for forward-loop guard." Git tree shows only `vst3/osc/PluginInstanceRegistry.{h,cpp}`; the controller-side wrapper is absent. The S4 forward-loop guard is instead satisfied structurally (no `sendto()` in recv-only thread per `SpatialEnginePluginUdp.cpp` recv-loop) — defensible but should be reconciled by either creating the file or amending the plan §11 manifest.

**F3 — `test_vst3_registry_stale_cleanup.cpp` (plan §7.2 integration table) absent.** ctest #59-61 covers registry + corruption + bind-collision, but not the multi-instance start/kill/GC scenario as a dedicated test. Stale-PID GC is unit-tested inside `test_p_instances_registry` (S2); the integration-level scenario is missing.

**F4 — Absorbed-edits commit from Critic R3 §3.1/§3.3/§3.4 never landed.** Critic R3 (`critic-r3-v0.3-review.md:233,288,318`) mandated: "autopilot agent should apply edits §3.1, §3.3, §3.4 verbatim as the first commit in the sprint (a 'Round-3 absorbed-edits' doc-only commit), before S1 ratification work begins." Git log shows sprint started directly at `5677e5b` S1 ratification. The plan body still carries the citation drift at line 91 ("matches parent §1.4") that §3.1 fix was supposed to scrub. A.7 row at line 646 is independently sharpened, so the impact is doc-only, but the audit trail expected by Critic is broken.

**F5 — A.7-prereq still PARTIAL, gating A.7.** S2.6 commit `94a6e9a` explicitly states "PARTIAL: SDK audit + code + 1000-iter harness PASS. Full closure (PARTIAL → FULL) requires user-side smoke matrix sign-off (user offline week)." Plan §3 S2.6 made A.7-prereq a binary gate for S4 entry. The sprint proceeded with the prereq partial. Acceptable given the smoke matrix is hardware-bound, but the plan should explicitly downgrade S4's A.7 closure from CLOSED to CLOSED-pending-smoke (currently A.7 short-duration p50=0.57ms/p99=1.11ms is reported as PASS).

**F6 — OFF byte-baseline drift unreconciled.** S2 commit `22899c8` deliberately re-pinned `spatial_engine_core` hash (justification: `-fPIC` invariant + 150 LOC standalone forwarder). S7 commit `72f9cbb` states "local build hashes differ from GHA-canonical baseline due to toolchain/path environment differences ... GHA CI canonical match expected." This is unverified locally and re-introduces the drift A.11 (`plan:650`) made binary. CI run on the merged branch must be cited as evidence.

**F7 — `vst3_bind_collision` excluded from S7's 70/70 claim.** S7 commit explicitly excludes `vst3_bind_collision` ("pre-existing env port conflict on port 9100"). But the test's own purpose (A.13 PM1) is precisely to verify walk-fallback when port 9100 is held. If the harness holds 9100 and the plugin fails to walk to 9101+, that is the regression we wanted to catch, not an env-flake. Worth a 5-minute audit.

## Architectural concerns (new)

- **No cyclic deps introduced.** VST3 SDK headers stay inside vst3/ (`SpatialEngineController.cpp` includes `ivsteditcontroller.h` only inside the controller TU); `core/` remains JUCE-free and SDK-free.
- **`vst3/SpscRing.h` (S2.6, `spe::vst3` namespace, controller-side) coexists with `core/src/util/SpscRing.h` (S3, `spe::util` namespace, audio-side).** Two SPSC implementations in the tree — intentional per S3 commit footer but worth a single-source-of-truth follow-up in v0.3.1.
- **No hidden allocations on audio thread.** `test_vst3_intra_plugin_spsc_drain` + `soak_vst3_console_flood` both assert `alloc_total==0` across 3920 samples; AudioCommand POD wrapper (`vst3/AudioCommand.h`) correctly hoists the `std::variant`→flat-union conversion off the audio thread.
- **`mute_stash_` promoted cleanly** in S7; no orphaned member left in header.

## References
- `vst3/SpatialEnginePluginUdp.cpp:138,151,218` — F1 kMute DEFERRED past S7
- `vst3/osc/` — F2 missing `SpatialEngineRegistry.{h,cpp}` Controller wrapper
- `.omc/plans/critic-r3-v0.3-review.md:233,288,318` — F4 absorbed-edits commit mandate
- `vst3/tests/smoke_matrix.md` — F5 PARTIAL prereq evidence
- `.ci/off_baseline.bytes.sha256` — F6 re-pinned hash awaiting GHA verification
- Sprint commits `5677e5b → 72f9cbb` — full chain validated against plan §3 S1..S7
