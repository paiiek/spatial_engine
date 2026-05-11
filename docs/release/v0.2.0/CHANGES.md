# Spatial Engine v0.2.0 — Detailed Changes

**Range**: `v0.1.0` (commit `24c62c7a`) → `v0.2.0` candidate HEAD (commit `0282f6b` + this release branch)
**Total commits**: 88 (87 + this v0.2.0 release commit)
**Date range**: 2026-05-01 → 2026-05-10

This file enumerates every commit since v0.1.0 grouped by Keep-a-Changelog 1.1.0
heading. The summary lives in `/CHANGELOG.md`; this file is the audit trail.

---

## Added

### ADM-OSC v1.0 compatibility (Phase C3)

| Commit | Subject |
| ------ | ------- |
| `166f0c9` | feat(C3-adm-osc): Phase C3 ADM-OSC v1.0 compatibility layer |

Includes 21 files / 1348 LOC: 4 new `CommandTag`s, 88-row CSV compliance fixture,
3 synthetic vendor `.osc.bin` fixtures (L-ISA, Spat Revolution, d&b Soundscape),
soak harness `core/tests/perf/soak_adm_osc_flood.cpp`, ADR 0006 spec freeze.

### HOA decoder diversification (Phase M2-extended, A-γ sprint)

| Commit | Subject |
| ------ | ------- |
| `e5924da` | feat(M2-hoa-extended): add 4 HOA decoder algorithms + OSC control (A-γ sprint) |

Includes MaxRE / AllRAD / EPAD / InPhase decoders, 5-value `DecoderType` enum,
RT-safe runtime dispatch, OSC `/sys/ambi_decoder_type i {0..4}` plumbing,
4 new ctest fixtures (51/51 PASS).

### VST3 plugin production-grade integration (Phase C C2 Option B + C2B postmortem)

| Commit | Subject |
| ------ | ------- |
| `9c8f82b` | chore(C2B-bootstrap): Step 0 — Option A roll back + license + entry gates |
| `cb4737d` | feat(C2B.1): Step 1 — VST3 plugin entry + IPluginFactory vtable + 21-assertion host fixture |
| `744c0e6` | feat(C2B.2): Step 2 — IEditController 6 param + dispatch wiring + RT-safety 1000-iter PASS |
| `20a4da6` | feat(C2B.3): Step 3 — Bypass + State persistence (M1.c) PASS |
| `d1261f1` | chore(C2B.4): Step 4 — M2 gate (B.2 host fixture default) PASS + DAW deferral |
| `acb8c27` | feat(C2B-postmortem): discharge 7 critic defects — kIsBypass, state v2, dry pass-through, RT probe, LD_DEBUG CI gate |
| `15fdb52` | fix(ci+test): vst3.yml Option B 정렬 + p2_layout configs 경로 양방향 빌드 호환 |
| `572cc2d` | chore(C2-bootstrap): JUCE 7.0.12 wiring + pluginval + dual-gate baseline |

JUCE-free vst3sdk hand-roll, 7 parameters (Pan Az / Pan El / Source Width /
Master Gain / Ambi Order / Room Preset / Bypass), state v2 binary format with
v1 multi-version reader at `vst3/SpatialEngineProcessor.cpp:267-289`, bypass
dry pass-through, `kIsBypass` flag on parameter id=6, `restartComponent`
notification on `setComponentState`, 53 ctest tests including 7 vst3-specific.

### LTC sync (Phase C1)

| Commit | Subject |
| ------ | ------- |
| `c3edb6a` | feat(C1.a): NullBackend audio-input path + NDEBUG strip lint hook |
| `6145a53` | feat(C1.b): SpscRing<T,N> + QueuedCmd POD ltc_chase_enable |
| `59df7b7` | feat(C1.c): LtcChase — audio→ring→control-thread LTC consumer |
| `6716cf9` | feat(C1.d): /sys/ltc_chase opcode 0x14 + SpatialEngine integration |
| `6cf851c` | feat(M7): SMPTE LTC biphase 디코더 — 25fps 합성 신호 검증 |

### Phase B feature parity (M1..M9, B1..B5)

| Commit | Subject |
| ------ | ------- |
| `50864bd` | feat(M1): per-object Source WIDTH (0..π rad) — VBAP/DBAP/WFS/HOA fan-out |
| `e558149` | feat(M2): HOA AmbiDecoder + AmbisonicRenderer 1차 + Ambisonic algorithm dispatch |
| `3d10dc3` | feat(M3): IRConvReverb (OLA) + /reverb/select OSC + 런타임 FDN/IR 전환 |
| `57254d5` | feat(M4): Snapshot Crossfade — 시간 기반 scene 전환 보간 |
| `677dcbc` | feat(M5): VST3 플러그인 빌드 옵션 — SPATIAL_ENGINE_VST3 (default OFF) |
| `134062e` | feat(M6): per-speaker time-alignment (delay_ms / gain_db) — output 단계 적용 |
| `7662b11` | feat(M8): Object Trajectory Animation — circle / line / lissajous + WebGUI API |
| `680c47b` | feat(M9): per-channel ChannelLimiter + /output/{ch}/{gain,limit} OSC |
| `b8d2c8e` | feat(B2): WebGUI Trajectory 정적 패널 (HTML + JS) |
| `b5b4ec1` | feat(B3): IRConvReverb WAV 로딩 + scripts/fetch_ir.py 외부/합성 IR |
| `fc95a59` | feat(B4): DBAP width 정밀화 — 3 가상소스 power-sum + energy 보존 정규화 |
| `f73caff` | feat(B5): HOA 2/3차 디코더 — Tikhonov pseudo-inverse + /sys/ambi_order |

### v0 통합 회로 + Phase 1/2 측정 게이트

| Commit | Subject |
| ------ | ------- |
| `0a436e0` | core: v0 통합 회로 완성 — PerObjectChain/DBAP/WFS/FdnReverb/Binaural + Noise + Transport |
| `86f970b` | tests: Phase 1/2 측정 게이트 정량화 (p99 지연·AzMAE 회귀·모드전환·60fps 헤드룸) |
| `0a91d0a` | Full audio render chain: OSC UDP → SPSC FIFO → VBAP → WAV |

### vid2spatial integration

| Commit | Subject |
| ------ | ------- |
| `4240b10` | feat: Phase 2 vid2spatial 프로덕션 브리지 + 이중 모드 전환 |
| `3c8f3f8` | vid2spatial WebGUI 통합: bridge aed 수정 + start/stop API + UI 버튼 |
| `3c12dee` | feat: Phase 0 스파이크 + Phase 1 WebGUI MVP + ADR 문서 |

### Korean documentation

| Commit | Subject |
| ------ | ------- |
| `40fcc9b` | docs(manual_kr): add Korean install + operation manuals (v0.1.0) |

### Phase C4 design contract drafts (for v0.3.0)

| Commit | Subject |
| ------ | ------- |
| (this release) | docs(adr): ADR 0010 / 0011 / 0012 — VST3 OSC binding model + multi-instance discovery + vendor-quirks slot |

### Test infrastructure + scientific corrections

| Commit | Subject |
| ------ | ------- |
| `fc00a30` | VBAPRenderer gain cache: 0.5° bins, open-addressing FIFO, prepareToPlay invalidation, +test_p_vbap_cache |
| `a0853e4` | AmbisonicEncoder: 2nd/3rd-order ACN/SN3D encode + tests (9ch / 16ch closed-form) |
| `89db643` | test_p_vbap3d test5: VBAP 3D fallback gain pattern (>=2 nonzero, energy-normalised) |
| `f69e34a` | test: VBAP 3D elevation numerical tests (p_vbap3d) — 30/30 ctest |
| `20a1e66` | feat: F1 VBAP 3D triplet selection — dimensionality probe, C(N,3) enumeration, min-spread |
| `c825add` | feat: F2 AmbisonicEncoder 1st-order (ACN, W=1.0 SN3D-consistent) + test_p_ambi |
| `718e57a` | feat: F3 ElevationView side-view widget (r vs y scatter) + test_elevation_view |
| `9878ea5` | feat: F4 MidiBridge.start() real impl — iter_pending loop, port discovery, stop race-free |
| `6698f97` | feat: Elevation UI slider — ElevationControl widget + test |
| `1c2dd84` | v1c BinauralMonitor pure-C++ HRTF pipeline: SofaBinReader + HrtfLookup + OlaConvolver |
| `324e985` | feat: MIDI PC→/scene/load OSC bridge (midi_bridge.py) |

---

## Changed

### OFF baseline GHA-canonical re-pin chain

| Commit | Subject |
| ------ | ------- |
| `587815c` | chore(off-baseline): re-pin bytes to GHA-canonical hashes (run #8) |
| `316f920` | ci(off-baseline): echo candidate hashes to GITHUB_STEP_SUMMARY for public re-pin |
| `8c0ca2d` | chore(off-baseline): re-pin OFF hashes after M2-hoa-extended decoder additions |
| `ec2510d` | ci(off-baseline): re-pin bytes to GHA ubuntu-24.04 + remove commit-comment spam |
| `a6fd294` | ci(vst3.yml): post OFF hashes as commit comment (public read path) |
| `26a275e` | ci(vst3.yml): OFF dual-gate diagnostics — capture+upload candidate hashes |

Final pinned hashes (v0.2.0 baseline, ubuntu-24.04 + GLIBC 2.39):

```
core/libspe_core.a:        25f1f4b2792ddb420ae23b4fe2522c8948408486a325845620ebee52b192e15b
core/spatial_engine_core:  b0663c5ea01030425b0ec6802cdd9d07695e97afd8c547c651efad9c35ea6e8c
```

### Audio engine + WebGUI runtime

| Commit | Subject |
| ------ | ------- |
| `5a60720` | fix(test+M9): assert() enforce + 피크 어택 limiter + 게인 램프 워밍업 |
| `c0aef3a` | ui/webgui: FastAPI lifespan 마이그레이션 + asyncio.run 전환 |

### Tooling

| Commit | Subject |
| ------ | ------- |
| `2445922` | chore: gitignore 확장 + 스테일 락파일 정리 |
| `658510a` | chore: project CLAUDE.md — ralplan+autopilot workflow policy, session resume guide |

### Documentation refreshes

| Commit | Subject |
| ------ | ------- |
| `3bc25eb` | docs(v1g): README 갱신 — Phase A/B 완료 마킹 + 신규 OSC 일람표 |
| `3ff72d5` | docs: README 갱신 — v0e 통합 회로 + v1e 측정 게이트 + 신규 OSC 일람표 |
| `841cc0e` | docs: README 전면 재작성 — 상세 한글 사용법 + WebGUI + vid2spatial |
| `c274986` | docs: README v1d — WebGUI, 오디오 렌더 체인, 포트 할당 업데이트 |
| `42a038f` | docs: update report §8 (v1b T4 완료) + Korean engineer README |
| `b822d8a` | docs: update report v1c — BinauralMonitor C++ HRTF 34/34 ctest |
| `1af5929` | docs/v0.1.0_report.md §8: reflect F1-F4 completion (VBAP3D, AmbisonicEncoder, ElevationView, MIDI start()) |
| `d42c910` | docs: v1 진행 현황 업데이트 — README 상태표 + 보고서 §8 추가 |

### Plan / decision tracking

| Commit | Subject |
| ------ | ------- |
| `0282f6b` | docs(plan): Phase C4 + v0.2.0 ralplan consensus (R2 APPROVE) |
| `bc38454` | docs(plan): track Phase C3 ADM-OSC v1.0 compat plan (R2-patched) |
| `1bde10e` | chore(prd): mark C2B-postmortem US-S1/S2/S3/S3.5/S5 as completed |
| `6cb680b` | chore(C2B-postmortem): update progress.txt — sprint COMPLETE, GHA run #6 SUCCESS |
| `47746c1` | docs(plans): C2 Option B 활성 — D+2 트리거 발현, sudo 부재로 JUCE 영구 차단 |
| `c2a5ad0` | docs(plans): C2 v3/v4 amendments — Architect/Critic 라운드 4 APPROVE 합의 |
| `e7b25d0` | chore: phaseC 잔여 fixture + phaseC-C2 v2 플랜 + EULA 결정 |
| `c2c1a79` | docs(plans): Phase A/B/C + v1/v1b 플랜 + open-questions 추가 추적 |

---

## Deprecated

(none in v0.2.0)

---

## Removed

(none in v0.2.0 — internal deslop passes removed dead code, see `Fixed` section)

---

## Fixed

| Commit | Subject |
| ------ | ------- |
| `08e5e91` | fix AmbisonicEncoder ACN4 coefficient: kSqrt3 → kSqrt3_2 (2x error); add test13 regression |
| `2c8086c` | fix: OlaConvolver alloc-free process(), SofaBinReader defensive validation |
| `d1b82f3` | fix 5 critical WebGUI+bridge bugs before user handoff |
| `a2b10f9` | fix: review fixes — NaN Z assert, blockSignals on set_object, sys import hoisted |
| `d8056db` | fix: SceneController handler, fromJson safety, path traversal guard, ctest 29/29 |
| `0bee66c` | fix: review revisions — MIDI OSC send + VBAP 2D limitation doc |
| `6af3778` | fix(B1): pytest collection — importlib mode + norecursedirs |
| `3b934e5` | fix(B1+): pytest pythonpath/testpaths 확장 — ui/ ui/webgui/ 통합 실행 가능 |
| `97056c6` | fix: sofa_inspector IR_len 384, flaky latency test, add ADDR_METRICS, utcnow deprecation |
| `1b49553` | ADM-OSC receive namespace: /adm/obj/n/{azim|elev|dist|gain|mute|aed}, ObjMute tag, 27/27 tests |
| `631fa99` | v1: SceneSnapshot + MAX_OBJECTS=64 + IR SOFA loader + ADM-OSC tests |
| `19679c6` | refactor: deslop SceneSnapshot/Controller/Decoder/IRStub — dead code, duplicates, clarity |
| `9c8fb2b` | chore: deslop — hoist duplicate to_screen_coords to module-level helper, remove dup import |
| `0f7c8d9` | chore: deslop — remove dead _FailLoader stub code + orphan _running field |
| `e2b9012` | refactor: deslop pass 2 — ADM-OSC setMove lambda, fromJson getI/getF helpers, scene name boundary tests |

---

## Security

| Commit | Subject | Notes |
| ------ | ------- | ----- |
| `d8056db` | path traversal guard added to `SceneController` | rejects malicious filename payloads via `fromJson` safety |
| `a2b10f9` | NaN Z assert in `SceneSnapshot` / Z-coord paths | defensive validation hardens against malformed wire input |

---

## Compatibility matrix

### Build environment (CI baseline)

| Component        | Version |
| ---------------- | ------- |
| Ubuntu           | 24.04 (Noble Numbat) |
| GLIBC            | 2.39 |
| GCC              | 13.3.0 |
| CMake            | 3.20+ |
| C++ standard     | C++20 |

### Runtime ABI

| Surface            | v0.1.0  | v0.2.0  | Reader compat |
| ------------------ | ------- | ------- | ------------- |
| `--osc-port`       | 9100    | 9100    | unchanged |
| `--osc-dialect`    | (n/a)   | `legacy`(default) `\|` `adm` | new flag, default preserves v0.1.0 |
| Component IID      | (frozen) | (same) | unchanged |
| Controller IID     | (frozen) | (same) | unchanged |
| VST3 state format  | v1 (28B / 6 floats) | v2 (36B / 7 floats) | multi-version reader at `Processor.cpp:267-289`, v0.1.0 `.vstpreset` loads via v1 path |
| IPC schema_version | 1       | 1 (unchanged) | wire-handshake stable |

### Older distros

Ubuntu 22.04 / Debian 11 / RHEL 9: GLIBC mismatch (2.35 / 2.31 / 2.34 < 2.39).
Build from source — see `docs/manual_kr/install/README.md` Chapter 3.

---

## Provenance

This file was generated by enumerating `git log v0.1.0..HEAD --oneline` (88
commits) at HEAD `0282f6b` plus the v0.2.0 release commit. Hashes are
truncated to 7 characters in tables; full SHAs are in the git history.
