# Phase C4 — VST3 plugin ADM-OSC integration  +  v0.2.0 release planning

**Status**: Round-1 draft (RALPLAN-DR consensus, DELIBERATE mode — release-impact + cert-risk)
**Date**: 2026-05-10
**Owner**: Planner agent (consensus loop, awaiting Architect / Critic review)
**Mode**: DELIBERATE (pre-mortem + expanded test plan + ADR required — see Constraints)
**Tracks**: A — Phase C4 VST3 ADM-OSC; B — v0.2.0 release
**ETA banner**: Track A 8–13d implementation, Track B parallel 1–2d release prep, total 2–3 weeks
**Related**:
* `.omc/plans/spatial-engine-phaseC3-adm-osc-compat.md` (esp. §3 Decision C, §S4 VST3 deferral, §6 Risks, Appendix D)
* `docs/adr/0006-adm-osc-v1-spec-freeze.md` (ADM-OSC v1.0 contract — frozen in C3)
* `docs/adr/0003-ipc-osc-udp.md` (single-schema invariant + p99 IPC falsifier 70-77)
* `core/src/ipc/CommandDecoder.cpp:317-373` (ADM-OSC dispatch table, full v1.0 receive)
* `core/src/ipc/CommandDecoder.cpp:422-602` (encode path, AdmV1 dialect added in C3)
* `core/src/ipc/OSCBackend.{h,cpp}` (JUCE-free POSIX UDP listener, `std::thread`)
* `vst3/SpatialEngineProcessor.cpp:32` (constructed with `0 /*no UDP*/` — C3 deferral source)
* `vst3/SpatialEngineProcessor.hpp:32-40` (7 ParamId enum: PanAz, PanEl, SourceWidth, MasterGain, AmbiOrder, RoomPreset, Bypass)
* `vst3/SpatialEngineController.cpp:107-216` (buildParamInfos — 7 ParameterInfo entries)
* `docs/manual_kr/install/README.md`, `docs/manual_kr/operation/README.md` (just landed in 40fcc9b — must be updated if C4 ships pre-v0.2)
* `CMakeLists.txt:3` (`project(... VERSION 0.1.0)`) and `core/CMakeLists.txt:57-59` (`SPE_VERSION_MAJOR/MINOR/PATCH`) — version bump targets

---

## 0. Context — what already exists, what is missing

### 0.1 What v0.1.0 → main contains (delta inventory)

`git log v0.1.0..HEAD --oneline` since `19679c6` produces the following groups
(57 commits, abridged by topic):

| Group | Commits | Headline change |
| ----- | ------- | --------------- |
| **Phase A — Feature parity** (M1..M9, B1..B5) | ~22 commits | Object width, HOA decoder + AmbiDecoder, IRConvReverb (OLA + WAV), DBAP width refinement, Tikhonov HOA 2/3rd-order, ChannelLimiter, time-alignment, snapshot crossfade, SMPTE LTC decoder, VST3 build option (`SPATIAL_ENGINE_VST3=OFF` default) |
| **Phase B / vid2spatial bridge** | ~6 commits | OSC bridge prod + spike, FastAPI lifespan migration, Phase 2 AzMAE ≤ 2.0° gate |
| **Phase C — VST3 plugin** (C1.a..C1.d, C2 v3/v4, C2B, C2B-postmortem) | ~14 commits | C2 Option-B JUCE-free vst3sdk hand-roll; IComponent + IAudioProcessor + IConnectionPoint vtable; 7 params + bypass (id=6); state v2 (36-byte format); B.2 in-process host fixture (21 assertions); GHA `vst3.yml` dual-job + OFF byte-baseline gate (currently SUCCESS at run #9 / `587815c`) |
| **Phase C3 — ADM-OSC v1.0** | 1 commit (`166f0c9`) | Full v1.0 receive coverage at `CommandDecoder.cpp:317-373`; 4 new tags (ObjXYZ, ObjActiveAdm, ObjWidth, ObjName) at 0x06-0x09; AdmV1 encode dialect at `:422-602`; ADR 0006 frozen; soak harness; cross-prefix collision test; static_assert on `ADM_OSC_MAX_DIST=20.0f` |
| **Phase A-γ — HOA decoder diversification** | 1 commit (`e5924da`) | 4 HOA decoders (PINV, MAX_RE, ALLRAD, EPAD, IN_PHASE) + `/sys/ambi_decoder_type` OSC |
| **OFF byte-baseline maintenance** | 4 commits | Re-pin OFF hashes after each set of feature additions; CI emits hashes to `GITHUB_STEP_SUMMARY` for public re-pin |
| **Korean manual** | 1 commit (`40fcc9b`) | `docs/manual_kr/install/README.md` + `docs/manual_kr/operation/README.md` |
| **PRD / progress / docs / plans** | misc | `.omc/prd.json`, `progress.txt`, plan tracking |

**Test count today**: 53 vst3-side ctest + 51 core-side ctest (excluding pytest); ALL GREEN.
**GHA runs**: vst3.yml run #9 SUCCESS at `587815c` (current main is `40fcc9b` = run #9 unaffected — docs only).

### 0.2 What is missing for Track A (Phase C4 — VST3 ADM-OSC)

| Surface | File | Status |
| ------- | ---- | ------ |
| VST3 plugin OSC receive | `vst3/SpatialEngineProcessor.cpp:32` | **MISSING** — engine ctor passes `0 /*no UDP*/`; constructive absence |
| VST3 plugin OSC send | — | **MISSING** — no `OSCBackend::dispatch()` wiring in plugin |
| Plugin → standalone discovery | — | **MISSING** — no mDNS, no host config |
| Per-instance UDP port mgmt | — | **MISSING** — multi-instance collision unhandled |
| Plugin OSC enable / disable param | `Controller.cpp:107-216` | **MISSING** — no 8th param for OSC on/off |
| ADM-OSC reference doc for plugin | `docs/manual_kr/operation/README.md` Ch.4 | **MISSING** — currently standalone-only |
| ADR 0010 (VST3 OSC binding) | `docs/adr/` | **MISSING** — to be drafted in S1 (numbered 0010 per §1.4 to avoid `0006-*.md` collision) |

### 0.3 What is missing for Track B (v0.2.0 release)

| Surface | File | Status |
| ------- | ---- | ------ |
| Version bump `0.1.0 → 0.2.0` | `CMakeLists.txt:3`, `core/CMakeLists.txt:57-59` | **PENDING** — both files carry separate values; must move in lock-step |
| Changelog `v0.1.0..HEAD` | `CHANGELOG.md` | **MISSING** — no changelog file in repo today |
| Release notes | `docs/release/` | **MISSING** — no release directory; v0.1.0 had only `docs/v0.1.0_report.md` |
| DAW hands-on log | — | **MISSING** — Reaper / Bitwig 7-param verification not on record |
| Tag policy doc | — | per global `CLAUDE.md`: "v* 태그는 사용자 명시 요청 시만" |
| GHA artifact upload (prebuilt .so) | `.github/workflows/vst3.yml` | **CONDITIONAL** — depends on B3 |
| Linux distro packaging (deb/rpm) | — | **MISSING** — no packaging targets |

This framing is critical: Track A is **net-new code** in `vst3/`, Track B is
**net-new docs + tag** with conditional CI work.

---

## 1. RALPLAN-DR Summary  (place at top, per /plan --consensus contract)

### 1.1 Principles (5, non-negotiable invariants)

1. **JUCE-free preserved.** No new JUCE includes anywhere. Plugin remains
   Option-B vst3sdk hand-roll with `SPE_HAVE_JUCE=0` build path
   (`core/CMakeLists.txt:35-46`). The OFF byte-baseline gate in `vst3.yml`
   continues to validate this.
2. **ctest green.** All 53 vst3-side + 51 core-side tests stay green throughout
   the phase. New tests only add to the count; none are skipped or marked
   xfail. CI run #N+ (after merge) must SUCCESS on both jobs.
3. **OFF byte-baseline gate.** When `SPATIAL_ENGINE_VST3=OFF` (default),
   `spe_core` and `spe_util` archives must remain byte-identical to the
   pinned hashes in **`.ci/off_baseline.bytes.sha256`** and
   **`.ci/off_baseline.symbols.sha256`** (dual-gate). Any C4 work that
   changes `core/` must re-pin both OFF hashes (mirroring `8c0ca2d`,
   `587815c`).
4. **ADM-OSC v1.0 spec verbatim.** ADR 0006 contract is frozen. Plugin OSC
   path must speak the exact same wire dialect as standalone — same
   addresses, same type-tags, same `ADM_OSC_MAX_DIST=20.0f` semantic
   (single-source-of-truth via `core/src/ipc/AdmOscConstants.h`). No plugin-
   side schema invention.
5. **v0.2 preserves the v0.1.0 wire ABI** for `--osc-port`,
   `--osc-dialect`, Component/Controller IIDs, and CLI surface. State
   format bumped v1→v2 in C2B postmortem (`acb8c27`) with multi-version
   reader at `Processor.cpp:267-289`; no further state bump in v0.2.0.
   v0.1.0 `.vstpreset` files load cleanly via the v1 reader path.
   (Adding new params/flags is OK; renaming/reordering existing ones
   is forbidden.)

### 1.2 Decision Drivers (top 3, why this work happens NOW)

1. **Korean live-venue customer dependency.** Phase C3 Driver 1 satisfied
   ADM-OSC console compat at the **standalone** level. The customer DAW
   workflow (Reaper + Pro Tools on Linux/Mac) inherits a documented gap:
   the plugin can be automated by the DAW but cannot ingest a console
   stream directly. Cutting v0.2 with C4 unresolved means the manual's
   "OSC reference" chapter applies to standalone only — a known confusion
   point on first integration.
2. **Ecosystem parity with L-ISA / Spat Revolution / d&b Soundscape.**
   These competitors offer plugin instances that *are* OSC endpoints
   (per-instance port). Reducing the parity gap before next refactor cycle
   keeps the comparison table in `docs/v0.1.0_report.md` § market-comparison
   honest.
3. **Reduce technical debt before next major refactor.** Phase D (planned
   later in 2026) will likely revisit DAW automation precision (sample-
   accurate) and macOS/Windows port. Doing C4 now, while the C3 ADM-OSC
   contract is fresh in cache, prevents stale-knowledge handoff. C4 also
   forces us to choose a discovery / threading story before macOS adds
   sandbox constraints.

### 1.3 Mode classification

This plan is **DELIBERATE** because:
* Track A touches plugin certification surface (UDP socket lifetime in DAW
  process — Pro Tools / Logic restrictions documented in industry sources).
* Track B is a release tag — irreversible without `git tag -d` + force-push,
  which is restricted by the global `CLAUDE.md` policy.
* Cross-track coupling has user-visible consequences (manual chapter scope).

Therefore: pre-mortem (3 scenarios) + expanded test plan (unit / integration
/ e2e / observability) + ADR (Decision / Drivers / Alternatives / Why
chosen / Consequences / Follow-ups) all required.

### 1.4 Release deliverable matrix

Per Round-2 Architect §6 synthesis (path a) and Critic §4 deferral request,
v0.2.0 ships **only the ADRs** for the Track A protocol; the implementation
(sidecar binary, plugin↔sidecar IPC) lands in v0.3.0. This decouples Track A
complexity from the v0.2 cycle while preserving the design contract.

Additionally, this plan adopts new ADR numbers starting at **0010** to avoid
the existing `docs/adr/0006-*.md` collision (`0006-adm-osc-v1-spec-freeze.md`
and the older `0006-algorithm-runtime-swap.md` already share slot 0006 —
least-disruptive choice is to skip ahead rather than renumber the older ADR).

| Deliverable                             | v0.2.0           | v0.3.0          | v1.0.0          |
| --------------------------------------- | ---------------- | --------------- | --------------- |
| ADR 0010 (VST3 binding model)           | draft (S1)       | ratified + impl | —               |
| ADR 0011 (multi-instance discovery)     | draft (S1)       | ratified + impl | —               |
| ADR 0012 (vendor-quirk overlay slot)    | reserved (empty) | fill on demand  | —               |
| Sidecar binary (`spatial_engine_sidecar`) | —              | ✓ (impl)        | ✓               |
| Plugin↔sidecar IPC (UDS + SPSC)         | —                | ✓               | ✓               |
| State format v3 / `kMute` 8th param     | —                | ✓ (A2.1-β)      | —               |
| macOS / Windows port                    | —                | candidate       | ✓               |
| C3 ADM-OSC receive (standalone)         | ✓ already shipped | hardening      | spec freeze     |
| HOA decoder diversification (4 algos)   | ✓ already shipped | —              | —               |
| Korean manual (install + operation)     | ✓ updated to v0.2.0 | EN parity   | —               |
| Bypass param + state v2 multi-reader    | ✓ already shipped | —              | —               |

### 1.5 Final Decision Freeze (post-Round-2)

This freeze resolves every fork that previously blocked autopilot
(§S2 conditional on A1-δ, §S5 conditional on B1-α, §A2.1 + §B1-β
inconsistency, §S7 leak fork, §6 risk-row "Architect rejects A1-δ").
Each axis below has exactly **one** chosen value — no primary/escape pairs.

| Axis  | Final pick | Rationale |
| ----- | ---------- | --------- |
| **A1** (UDP binding model)  | **A1-ε** for v0.2 ADR + v0.3 implementation; **A1-δ** retained as v0.4+ cross-platform fallback if A1-ε proves cert-blocked on macOS | Architect §6 synthesis recommends A1-ε for v0.2/v0.3 (Linux has no audio sandbox; `bind()` proven via `OSCBackend.cpp:75-83`); A1-δ deferred until macOS evidence demands. Critic concurs with deferral and steelman calibration. |
| **A2** (direction)          | **A2-α** (recv-only) for v0.3 implementation; v0.2 ships ADR draft only | Pairs with A1-ε recv-only socket; cert-cleanest one-way path; matches console→plugin first-customer use case. |
| **A2.1** (mute mapping)     | **A2.1-α-temporary** for v0.2 (no new param; bypass-as-mute documented as known gap); **A2.1-β** (kMute id=7 + state v3) deferred to v0.3 | Avoids OFF re-pin churn for unused param. C2B-Q2 (`open-questions.md:51`) supports deferral. |
| **A3** (discovery)          | **A3-β** (file-based `~/.config/spatial_engine/instances.json`) for v0.3 implementation; v0.2 ships ADR 0011 draft only | Zero new dep; debuggable; aligns with v0.3 sidecar landing. |
| **A4** (threading)          | **A4-β** (dedicated `std::thread`, mirrors `OSCBackend.cpp:75-83`) for v0.3 implementation; documented as contract in ADR 0010 | Audio-thread (α) forbidden; host-thread (γ) unavailable without editor view. |
| **A5** (cert minimization)  | **A5-α** (Linux-only `SPATIAL_ENGINE_VST3_OSC=ON` build flag) for v0.3 fast path; **A5-β** (sidecar everywhere) reserved as v0.4+ cross-platform default if A5-α blocked on macOS | Architect synthesis: build for the customer we have (Linux Reaper/Bitwig), not the customer we project. |
| **B1** (semver scope)       | **B1-β** (v0.2.0 with C3-standalone only; no C4 implementation) | C3 alone is significant feature delta from v0.1.0; couple-to-riskiest avoided. Critic + Architect concur. |
| **B2** (release timing)     | **B2-β** (cut after 1–2d DAW hands-on) | Pairs with B1-β; reasonable confidence without coupling to Track A. |
| **B3** (release artifacts)  | **B3-β** (source tarball + prebuilt `spatial_engine_vst3.so` for ubuntu-24.04 / GLIBC 2.39) | Matches existing GHA pipeline; first-customer drop-in. Sidecar binary NOT included (item 5 deferral). |
| **B4** (DAW gate)           | **B4-α** (must pass before tag, recorded log committed) | First-customer hardness; Pre-mortem 3 fallback procedure now consistent (item 6). |
| **B5** (release-notes locale) | **B5-γ** (KR primary, EN below) | Matches v0.1.0 report layout; serves Korean primary customer + international ecosystem readers. |

**Autopilot guarantee**: every step in §S1..§S8 + §R1..§R7 below
references *only* these post-freeze axis values. No conditional branches
remain that would force autopilot to make architectural judgement calls.

---

## 2. Track A — Phase C4: VST3 plugin ADM-OSC integration

### 2.1 Decision points (≥2 options each)

#### A1 — UDP binding model

Where does the plugin bind a UDP socket — if at all?

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A1-α (per-instance auto-port)** | Each plugin instance opens its own UDP socket on an OS-assigned ephemeral port. Port surfaced via plugin's parameter / read-only host info | Each plugin instance is independently addressable from a console; matches L-ISA topology | Multi-instance: console operator must learn N port numbers; some hosts (Pro Tools, Logic) may sandbox `bind()` calls; auto-port complicates discovery; certification risk on macOS/iOS hosts |
| **A1-β (single shared port + multiplex by obj_id range)** | Plugin binds a fixed port (e.g. 9101); multiple instances negotiate via a static obj_id offset (instance-0 owns 0..15, instance-1 owns 16..31, …) | Single console-facing port; simple discovery doc | Only ONE instance can bind 9101; subsequent instances fail. Either second instance silently disables OSC, or all plugins forward through the first. Brittle and surprising |
| **A1-γ (no UDP — C3 deferral continued)** | Plugin keeps `0 /*no UDP*/` ctor; user runs standalone alongside DAW; plugin is automation-only | Zero certification risk; preserves Phase C3 invariant; manual "OSC reference" stays accurate | Defers competitive parity; documented gap remains |
| **A1-δ (DAW automation bridge)** ★ | Plugin exposes its parameter state via DAW's automation system AND a separate sidecar process (the existing standalone `spatial_engine_core`) bridges DAW automation ↔ ADM-OSC. NO UDP socket inside plugin process | Honors A1-γ certification safety + delivers ecosystem parity at the workflow level. DAW automation is **fully sample-accurate** today (see `Processor.cpp:391-414`), making the bridge round-trip practical. Standalone already has full ADM-OSC v1.0 (Phase C3 done) | Two-process workflow is novel for DAW users; needs a control-channel between plugin and sidecar (file-watch on a known path? local TCP? custom OSC channel on a pre-agreed port?) |
| **A1-ε (opt-in receive-only socket per instance)** | Like A1-α but socket is **read-only** (no `sendto`); the plugin only ingests `/adm/obj/N/...` to drive its parameters. No outgoing OSC | Smaller cert footprint; simpler thread model (one-way); preserves standalone as the canonical bidirectional endpoint | Multi-instance port collision still applies; DAW host vetoes on macOS may still block recv socket |

**Recommendation**: A1-δ as primary, A1-γ as escape (continued deferral with explicit Phase D/D6 horizon).

**Why δ over α / β / ε**: cert-risk is the single biggest unknown — every
"DAW host blocks `bind()`" anecdote is worth weeks of debugging on a closed
system. δ avoids the problem entirely by keeping the network surface in the
process where it already works (standalone). The DAW automation path
(`Processor.cpp:391-414`) is already sample-accurate per
`std::atomic<float> norm_values_[7]` snapshot — adding the bridge round-trip
is incremental.

**Why not ε**: still binds a socket; cert risk is not eliminated, just
reduced. And it gives up the symmetric "plugin publishes its state to
console" use case that customers actually want.

**Why not α**: cert-risk; multi-instance port chaos.
**Why not β**: silent degradation when N>1 instance loaded; operator confusion.

**Invalidation rationale (if Architect/Critic reduces to single option)**:
A1-α is invalidated by Pro Tools/Logic socket restrictions documented in
multiple Steinberg forums and the JUCE Pro Tools wrappers (which forbid
direct `bind()` and force AAX message-port indirection on macOS).
A1-β is invalidated because shipping a feature that silently breaks on
N>1 instance is a worse user experience than not shipping it.
A1-ε is a strict subset of α with the same cert risk.

#### A2 — Direction (read-only / send-only / bidirectional / DAW-automation-bridge)

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A2-α (receive-only)** | Plugin reads `/adm/obj/{0}/{azim,elev,dist,aed,gain,mute,xyz,active,width,name}` into its 7 params (mapping below). No outgoing OSC | Simplest; matches L-ISA "object track receives spatial coords" pattern | One-way; no console mirror of plugin state |
| **A2-β (send-only)** | Plugin publishes its 7 params as ADM-OSC (`/adm/obj/0/...`) on parameter changes | One-way mirror; useful for telemetry | Console cannot drive the plugin |
| **A2-γ (bidirectional)** | Both directions; subscribe + publish | Full L-ISA / Spat Revolution parity | Risk of OSC feedback loop (console → plugin → console → ...); needs origin-tag or epoch suppression |
| **A2-δ (DAW-automation-bridge)** ★ | Plugin exposes only DAW automation; standalone sidecar bridges DAW automation ↔ ADM-OSC bidirectionally | Cert-safe (no network in plugin); preserves C3 standalone authority; bidi achieved through sidecar | Sidecar must run; new "plugin↔sidecar" control channel needed |

**Recommendation**: A2-δ primary, A2-α escape (read-only direct UDP if A1-δ
rejected by Architect).

**Why δ**: same cert-risk reasoning as A1; lets us ship bidi semantics with
zero plugin-process socket surface.

**Param mapping (for either α or δ — same surface)**: plugin's existing 7
params already cover the relevant ADM addresses:

| ADM-OSC address | Plugin ParamId | Mapping |
| --------------- | -------------- | ------- |
| `/adm/obj/0/azim`  | `kPanAz` (id=0)       | degrees → norm via inverse of `Controller.cpp:497` |
| `/adm/obj/0/elev`  | `kPanEl` (id=1)       | degrees → norm via inverse of `Controller.cpp:498` |
| `/adm/obj/0/dist`  | (synthetic, no param) | distance not exposed in v0.2 plugin params; kept on engine side via existing `PayloadObjMove::dist_m` (Processor.cpp:494, hardcoded `1.f`) |
| `/adm/obj/0/aed`   | `kPanAz`+`kPanEl`+(dist) | combined; az/el reflect, dist drives engine's hidden state |
| `/adm/obj/0/width` | `kSourceWidth` (id=2)  | rad → norm `value/π` (per `Processor.cpp:516`) |
| `/adm/obj/0/gain`  | `kMasterGain` (id=3)   | linear → dB → norm via `Controller::gainPlainToNorm` |
| `/adm/obj/0/mute`  | `kBypass` (id=6)       | 1 → bypass on; 0 → off; **OR** add a separate `kMute` param (id=7) — see decision A2.1 below |

##### A2.1 — `mute` mapping sub-decision

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A2.1-α (reuse `kBypass`)** | `/adm/obj/0/mute` → `kBypass` toggle | No new param; smaller state | Bypass is dry pass-through (audio thru) at `Processor.cpp:421-447`; ADM mute typically means **silence** (zero output). Semantic mismatch |
| **A2.1-β (add `kMute` id=7)** ★ | New param `kMute` with `kIsMute = 1<<17` (VST3 SDK convention); state v3 (40 bytes, magic still `'SPE1'`, version=3) | Correct semantics; preserves bypass dry-thru behaviour; matches DAW conventions | State format bump to v3; `setState`/`getState`/`setComponentState` must read v1 / v2 / v3 (3-way fork). Param count increases 7 → 8. Backward compat: v1/v2 readers default `kMute=0` (off) |

**Round-2 freeze (per §1.5)**: A2.1-β is the **EVENTUAL** choice (kMute id=7
is the semantically correct mapping), but it is a **v0.3.0 deliverable, NOT
v0.2.0**. Rationale (Architect blocker #3 + Critic C-1 supporting):
* v0.2 ships C3-standalone only (B1-β). kMute lives in C4 plugin scope —
  shipping v3 schema in v0.2 with no consumer is pure churn.
* C2B-Q2 (`open-questions.md:51`) explicitly pre-resolved this favorably:
  defer the next state-format bump until a real consumer lands.
* Avoids OFF re-pin twice (once for v3 schema constants, again when
  sidecar lands). Preserves single C2B → v0.3 state-format transition.
* C2B was 2 days ago (acb8c27); bumping the state format again in the
  same release would signal churn to anyone reading the changelog.

For v0.2, A2.1-α-temporary applies: `kBypass` is documented in release
notes as the v0.2 mute proxy with the explicit note "ADM-OSC `mute`
semantics will switch to a dedicated `kMute` param in v0.3 alongside C4."
3-way state read fork (v1 / v2 / v3) lands in v0.3 with `test_vst3_state_v3_persist.cpp`.

#### A3 — Discovery (how plugin instances + standalone advertise endpoints)

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A3-α (mDNS / Bonjour)** | Plugin / sidecar advertises `_adm-osc._udp.local` with port + obj_id range | Auto-discovery; matches AES70 / Dante conventions | New dep (avahi-client on Linux, dns_sd on mac); heavyweight for v0; troubleshooting (firewall, multicast) is non-trivial |
| **A3-β (user-config file)** ★ | Each plugin instance writes its endpoint info (port, obj_id) to `~/.config/spatial_engine/instances.json`; standalone sidecar reads + multiplexes | No new dep; trivial to debug (just `cat` the file); FS path documented in operation manual Ch.X | Stale entries when DAW crashes; need a per-instance lockfile or PID-based GC |
| **A3-γ (`/sys/state` polling)** | Standalone exposes `/sys/state` with the discovery info; plugin polls on connect | Reuses existing OSC mechanism | Bootstrap chicken-and-egg: standalone must be running first; not an issue in workflow but is in unit tests |
| **A3-δ (no discovery — single-instance only)** | Plugin / sidecar pair supports exactly ONE plugin instance per machine. Multi-instance is documented as out-of-scope for v0.2 | Smallest scope; matches typical first-customer deployment (mixing suite has one Reaper instance) | Caps the ecosystem-parity story; needs explicit doc warning |

**Recommendation**: A3-β primary, A3-δ escape if Architect cuts scope.

**Why β**: zero-dep, debuggable. The lockfile / PID guard solves stale-entry
without overengineering. mDNS (α) is the right answer for v1.0+ but not for
the v0.2 release window.

#### A4 — Threading (which thread owns the UDP socket inside plugin process)

Applies only if A1-α/β/ε is selected (A1-δ has no plugin-side socket).

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A4-α (audio thread)** | UDP recv on the audio callback thread | Forbidden — violates Principle 3 (RT-safe) | — |
| **A4-β (dedicated `std::thread`)** ★ | Same pattern as `OSCBackend::start()` at `core/src/ipc/OSCBackend.cpp:75-83`. Spawn one thread per plugin instance; 100ms `SO_RCVTIMEO`; clean shutdown via `running_=false` + `close(fd)` | Reuses proven pattern; JUCE-free (`std::thread` only); no dep on host's message thread | One thread per plugin instance — many instances = many threads; mitigated by single-instance scope (A3-δ if reduced) |
| **A4-γ (host's message-thread callback)** | Use VST3's `IRunLoop::registerTimer` or platform-specific equivalent | Host-managed lifecycle; cert-friendly | Platform-specific; `IRunLoop` is editor-only in VST3 SDK; doesn't apply when no editor view (`Controller::createView` returns nullptr at `Controller.cpp:570`) |

**Recommendation**: A4-β. Audio-thread (α) is forbidden; host-thread (γ)
isn't available without an editor. Even though A1-δ avoids this decision
entirely, document A4-β as the contract for any future A1-α/ε path.

#### A5 — Pro Tools / certification risk minimization

What is the minimum-viable scope that doesn't risk plugin certification on
non-Linux hosts (which we plan to target in v0.3+)?

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **A5-α (Linux-only OSC, gate by build flag)** | `SPATIAL_ENGINE_VST3_OSC=ON` only on Linux; macOS/Windows builds force OFF | Smallest cert footprint; ships v0.2 on Linux today | Gates parity for mac/Windows users when those ports happen |
| **A5-β (sidecar bridge — same as A1-δ)** ★ | Sidecar process is the network endpoint on every platform; plugin process is always cert-clean | Single code path across Linux/Mac/Windows; cert-safe everywhere | Workflow novelty (covered above) |
| **A5-γ (defer entire C4 to Phase D6)** | Don't ship C4 in v0.2; ship v0.2 with C3 standalone-only + manual disclaimer | Zero cert risk; smallest release surface | Driver 1 partly satisfied only |

**Recommendation**: A5-β (consistent with A1-δ + A2-δ). If Architect rejects
the sidecar workflow → A5-γ (defer C4 entirely; ship v0.2 C3-only); A5-α is
reserved as a fallback if sidecar becomes too costly.

### 2.2 Implementation steps S1..S8

#### S1 — Decision ratification + ADR 0010 / 0011 / 0012 drafts (1 day) — **only S1 lands in v0.2.0**

> **Round-2 scope freeze (§1.4 + §1.5)**: S1 is the **only Track A step
> that ships in v0.2.0**. Steps S2..S8 below are all v0.3.0
> deliverables. Their text is preserved here as the v0.3 contract for
> the architects/executors who will pick this up next sprint.

* **Inputs**: this plan's §2.1 (A1..A5) + Architect/Critic feedback from
  ralplan Rounds 1 & 2 + the §1.5 freeze.
* **Deliverables (v0.2.0)**:
  - `docs/adr/0010-vst3-osc-binding-model.md` enumerating Decision A1..A5,
    invalidation rationale for rejected options, the §1.5 frozen choices,
    and the v0.3 contract (A1-ε + A2-α + A2.1-β + A3-β + A4-β + A5-α).
    Includes the Context paragraph from item 7 (UDS orthogonal to
    ADR 0003) and the thread-budget invariant from item 11.
  - `docs/adr/0011-vst3-osc-multi-instance-discovery.md` — JSON schema
    for `~/.config/spatial_engine/instances.json` plus the hardening
    rules from item 4 (atomic rename, flock, /proc PID liveness, EPIPE
    handling, schema_version field). Status: Draft for v0.2.0;
    ratified + impl in v0.3.
  - `docs/adr/0012-adm-osc-vendor-quirks.md` — reserved slot only;
    "fill on first vendor incompat" placeholder.
* **Acceptance**:
  - ADRs 0010 / 0011 reviewed by Architect/Critic in ralplan Round-3.
  - ADR 0010 cites ADR 0003 §Migration target as the orthogonality basis.
  - ADR 0011 hardening rules from item 4 all present.
  - Spec commit pin (mirror ADR 0006 format) — protocol version
    `v0.3.0-c4-draft` baked in (NOT `v0.2.0-c4`, since impl is v0.3).

#### S2 — Sidecar bridge protocol skeleton (2 days) — **DEFERRED to v0.3.0**

> Per §1.4 + §1.5 (B1-β + Architect §6 path-a synthesis), S2 is a v0.3
> deliverable. Text below is the v0.3 contract.

* **Files (new)**:
  - `vst3/sidecar_bridge/PluginInstanceRegistry.{h,cpp}` — JSON I/O for
    `~/.config/spatial_engine/instances.json` (schema in ADR 0011).
  - `vst3/sidecar_bridge/PluginAutomationProxy.{h,cpp}` — read DAW
    automation via existing `norm_values_` snapshot (`Processor.hpp:104`)
    via a thread-safe queue; on snapshot change, emit a delta packet on a
    UNIX domain socket at `~/.config/spatial_engine/sock-{pid}`.
  - `bin/spatial_engine_sidecar.cpp` — new executable that watches the
    registry file, opens the per-plugin UNIX socket, ingests deltas, and
    forwards them to the standalone `spatial_engine_core`'s existing UDP
    port (9100) using the `AdmV1` dialect at `CommandDecoder.cpp:546-604`.
* **Files (existing, additive only)**:
  - `vst3/SpatialEngineProcessor.cpp:32` — extend ctor to optionally
    register with `PluginInstanceRegistry` when env var
    `SPATIAL_ENGINE_VST3_BRIDGE=1` is set (default OFF preserves v0.1.0
    behaviour and OFF byte-baseline).
  - `vst3/CMakeLists.txt:8-37` — add new sidecar source files; gate
    behind `SPATIAL_ENGINE_VST3_BRIDGE` CMake option (default OFF).
  - `CMakeLists.txt:36-46` — extend the SPATIAL_ENGINE_VST3 → NO_JUCE
    forcing block to also handle SPATIAL_ENGINE_VST3_BRIDGE.
* **Acceptance**:
  - `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_BRIDGE=OFF`
    produces byte-identical OFF-baseline artifacts (mirrors current
    OFF-pin gate; verified via dual-gate `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`).
  - Sidecar binary builds; no JUCE includes (`grep -r juce vst3/sidecar_bridge/`
    must return zero hits).
  - New ctest target `test_vst3_sidecar_registry` passes: write+read+GC of
    JSON registry with PID liveness check.

#### S3 — Plugin → sidecar control channel (2 days) — **DEFERRED to v0.3.0** (per §1.4)

* **Files**:
  - `vst3/SpatialEngineProcessor.cpp` — extend `dispatchParamChange` at
    `:481-554` to also append the param change to the sidecar UNIX socket
    (when bridge mode is enabled). RT-safe: write to a SPSC ring; a
    dedicated `std::thread` (per A4-β) drains and `send()`s.
  - `vst3/sidecar_bridge/SpscRing.h` — reuse `core/src/ipc/SpscRing.h`
    (verify path; if not present in core, create reusable header at
    `core/src/util/SpscRing.h`). This must be the same SPSC primitive
    used by `LtcChase` (`feat(C1.b)` from `6145a53`).
* **Acceptance**:
  - `vst3/tests/test_vst3_sidecar_dispatch.cpp` (new): 1000-iteration
    parameter changes from a fake audio thread, assert `RT_ASSERT_NO_ALLOC`
    holds (mirrors `test_vst3_dispatch_rt_safety`).
  - Sidecar reads exactly N param changes; ordering preserved.

#### S4 — Sidecar → DAW automation reverse path (1.5 days) — **DEFERRED to v0.3.0** (per §1.4)

When ADM-OSC console sends `/adm/obj/0/aed` to the sidecar (via standalone's
9100 → sidecar relay), the sidecar must reflect this back into the plugin's
DAW-automation surface so the DAW writes the value into its own session
state.

* **Files**:
  - `vst3/sidecar_bridge/AutomationReflect.{h,cpp}` — given a `Command`
    (decoded ADM), look up the registered plugin instance by PID, write
    the new param value to the per-plugin UNIX socket.
  - `vst3/SpatialEngineProcessor.cpp` — reverse-channel reader: when bridge
    mode is on, the dedicated thread also consumes incoming deltas and
    calls `IComponentHandler::performEdit(id, value)` via the connection
    point (`Processor.cpp:560-565`). NOTE: `performEdit` is on
    `IEditController`, not `IComponent` — sidecar deltas must route via
    the `IConnectionPoint::notify` channel **OR** via a controller-side
    socket (preferred). This requires `Controller.cpp:592-597` (`notify`
    currently returns `kNotImplemented` per AM-R3-10) to be replaced with
    a real handler that dispatches `performEdit`.
* **Acceptance**:
  - `test_vst3_sidecar_reverse_path.cpp`: simulate console → sidecar →
    plugin; assert `Controller::norm_values_[i]` reflects the new value
    within 100ms (sidecar polling cadence) and within one DAW automation
    snapshot. RT-safety not violated on reverse path either (allocations
    only on the dedicated thread, not audio).

#### S5 — Manual update — Korean operation guide ADM-OSC chapter (0.5 day) — **DEFERRED to v0.3.0** (manual Ch.5 added when sidecar ships; v0.2 R4 Known-Limitations entry covers the gap per §4.3)

* **Files**:
  - `docs/manual_kr/operation/README.md` — new section "Ch.5: VST3 플러그인
    + ADM-OSC 콘솔 (사이드카 모드)" describing the workflow:
    1. Standalone `spatial_engine_core` running on host (port 9100).
    2. Sidecar binary running (auto-discovers plugin instances).
    3. Plugin loaded in DAW (Reaper / Bitwig).
    4. Console sends to standalone:9100; sidecar relays to plugin's DAW
       automation; plugin renders.
  - Diagram (Mermaid or ASCII) of the 4-process topology.
* **Acceptance**:
  - Doc renders cleanly in `mdcat` (or any markdown viewer).
  - New section linked from `docs/manual_kr/operation/README.md` table of
    contents.
  - Backward-compat note: standalone-only mode (no plugin) still documented
    as the canonical path for v0.1.0 users.

#### S6 — End-to-end integration test + soak (1.5 days) — **DEFERRED to v0.3.0** (per §1.4)

* **Files**:
  - `vst3/tests/test_vst3_e2e_console_to_plugin.cpp` — full chain:
    1. Spawn `spatial_engine_core` on port 9101 (test-only port, not
       conflicting with prod 9100).
    2. Spawn `spatial_engine_sidecar`.
    3. Instantiate plugin via existing host fixture
       (`test_vst3_host_fixture.cpp` pattern from C2B.4).
    4. Send `/adm/obj/0/aed (45.0, 30.0, 0.5)` via UDP (`sendto()`) to
       9101.
    5. Assert: plugin's `norm_values_[kPanAz]` ≈ `Controller::plainParamToNormalized(0, az_rad)` within tolerance.
  - `vst3/tests/perf/soak_vst3_console_flood.cpp` — 1 obj × 100 Hz × 60 s
    (lower than core soak's 64 obj × 1 kHz because plugin processes single
    obj per instance). Asserts:
    - audio thread alloc==0 (`rt_alloc_probe.hpp` from C2B.4).
    - p99 console-OSC → plugin-param-write latency < 5ms (less aggressive
      than ADR-0003's 3ms IPC stage budget because we add one inter-process
      hop through the sidecar).
* **Acceptance**:
  - Both tests pass in `ctest --output-on-failure`.
  - GHA `vst3.yml` extended with new ctest invocations; both jobs SUCCESS.

#### S7 — OFF byte-baseline re-pin (0.5 day) — **DEFERRED to v0.3.0** (no Track A code lands in v0.2; OFF baseline untouched by v0.2 except for R2 version-macro bump handled in Track B)

* `core/` source list does not change in C4 (all C4 code is in `vst3/` and
  `bin/spatial_engine_sidecar.cpp` — neither is in OFF baseline scope).
* **Acceptance**:
  - `cmake -DSPATIAL_ENGINE_VST3=OFF` produces hashes identical to current
    `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` (dual-gate). CI gate passes without re-pin.
  - If any C4 work *did* leak into `core/`, follow the existing re-pin
    workflow: `8c0ca2d`-style commit using GHA-canonical hashes from
    `GITHUB_STEP_SUMMARY`.

#### S8 — DAW hands-on smoke (Reaper + Bitwig) (1 day) — **DEFERRED to v0.3.0**; v0.2 Track B step R3 covers the standalone-only DAW hands-on for v0.2 release gating

* **Files**:
  - `docs/release/v0.2.0/daw-handson-log.md` — checklist with screenshots
    or capture logs:
    1. Reaper 7.x: load `spatial_engine_vst3.so` on a stereo track; verify
       7 (or 8 with `kMute`) params appear; automate `kPanAz` over 4 bars;
       confirm engine output reflects.
    2. Bitwig 5.x: same.
    3. Sidecar workflow: open Reaper + standalone + sidecar; send
       `/adm/obj/0/aed` from `oscchief` or similar; observe param value
       update in Reaper's Edit Cursor automation lane.
* **Acceptance**:
  - 7 params (or 8) visible and automatable in BOTH hosts.
  - Bypass param triggers dry pass-through (mirrors `Processor.cpp:421-447`).
  - State save/load round-trip works: save Reaper project, close, reopen,
    params restore (state v2 OR v3 if A2.1-β).
  - Sidecar console-to-plugin path observed working.

### 2.3 Track A acceptance criteria summary (Appendix A indexes these)

S1: 1 ADR + optional 2nd ADR drafted, reviewed.
S2: Sidecar binary builds, registry test passes, OFF baseline preserved.
S3: 1 new RT-safety test, 1000-iter alloc==0.
S4: 1 new reverse-path test, latency bounded.
S5: Manual chapter added, links updated.
S6: 2 new tests (e2e + soak), p99 < 5ms, alloc==0.
S7: OFF baseline re-pin (most likely no-op).
S8: DAW handson log committed.

---

## 3. Track B — v0.2.0 release planning

### 3.1 Decision points (≥2 options each)

#### B1 — Semver scope

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **B1-α (v0.2.0 with C3 + C4)** | Block release on C4 (Track A) finishing first; release with new ADM-OSC + plugin OSC features | Single coherent "ADM-OSC fully integrated" story; manual chapter applies to both standalone and plugin | Pushes release out 8–13d (C4 ETA); risks scope creep |
| **B1-β (v0.2.0 with C3 only)** ★ | Cut today (or after pre-mortem fixes only); manual disclaims plugin OSC; v0.3 carries C4 | Ships C3 ADM-OSC, A-γ HOA decoder, IRConv WAV, scene crossfade, LTC, Korean manual — significant value already; small cycle time | Manual mentions "plugin OSC future work"; competitive parity gap remains for 1 release cycle |
| **B1-γ (v0.1.1 patch + v0.2.0 after C4)** | Cut a v0.1.1 today covering C3 + manual + HOA decoder + soak fixes; v0.2.0 later with C4 | Keeps semver clean — patches don't add features | Semver violation: C3 added 4 new CommandTags (0x06-0x09) + new CLI flag (`--osc-dialect`). These are minor-bump material, not patch |
| **B1-δ (split: v0.1.1 today, v0.2.0 with C4)** | Cut v0.1.1 with manual + HOA decoder ONLY; defer C3 ADM-OSC packaging into v0.2.0 with C4 | Clearer semver per change | Re-tags previously-merged work; complex to back out C3 from a tag perspective; not worth |

**Recommendation**: B1-β (v0.2.0 with C3 only, C4 lands in v0.3).

**Why β over α**: C3 alone is already a significant feature delta from
v0.1.0. Blocking on C4 (with cert-risk pre-mortems) couples the release
to the riskiest piece of work. β honors the project policy "ralplan +
autopilot, ship in iterations" from `.claude/CLAUDE.md`.
**Why not γ**: C3 is not a patch — adding 4 new CommandTags (0x06–0x09)
and a new `--osc-dialect` CLI flag are minor-bump material per semver.
A v0.1.1 patch tag containing C3 is a **semver violation**.
**Why not δ**: re-tagging is not worth the perceived cleanliness.

**v0.1.1 reservation policy (item 6, resolves Pre-mortem 3 contradiction)**:
The v0.1.1 slot is reserved for **true patch-only emergencies** (e.g.,
compiler-emit security fixes, wire-format-preserving bug fixes). It is
**NOT** an escape hatch for "ship C3 + HOA decoders + manual as a patch".

If Architect favours single coherent story → B1-α; if Critic flags
v0.1.1 as cleaner semver → revisit γ/δ.

#### B2 — Release timing

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **B2-α (cut today, on current main `40fcc9b`)** | Release immediately, no DAW handson | Fastest | DAW B-side risk (B4-β); manual just landed (40fcc9b — uncommented) |
| **B2-β (cut after DAW hands-on)** ★ | 1–2 day verification window, then tag | Reasonable confidence | 1–2d cycle delay |
| **B2-γ (cut after C4 lands)** | Wait for Track A complete | Combined story | 8–13d delay; couples to cert-risk |
| **B2-δ (LTS branch)** | Cut release-0.2 branch, accept patches; main moves on to C4 | Both worlds | Branch maintenance overhead unnecessary at this scale |

**Recommendation**: B2-β (cut after DAW hands-on, 1–2d delay). Pairs with B1-β.

#### B3 — Release artifacts

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **B3-α (source tarball only)** | `git archive` + `.tar.gz` attached to GH release | Simplest; no binary support burden | User must build; raises bar for Korean console operators |
| **B3-β (source + prebuilt VST3 .so)** ★ | Source tarball + GHA-built `spatial_engine_vst3.so` (Linux ubuntu-24.04 / GLIBC 2.39) | Drop-in for Linux Reaper users; matches existing `vst3.yml` | Single platform for now; mac/Windows = next release |
| **B3-γ (source + prebuilt + standalone binary)** | Add `spatial_engine_core` Linux binary | Even more drop-in | More artifacts to QA; rebuild on each kernel/glibc change |
| **B3-δ (deb/rpm packages)** | Linux distro packages | Native install | Significant packaging work; defer to v1.0 |

**Recommendation**: B3-β. Matches the GHA pipeline output already.

#### B4 — DAW hands-on gate

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **B4-α (must pass before tag)** ★ | Reaper + Bitwig 7-param verify; recorded log committed | Confidence | 1–2d delay (covered in B2-β) |
| **B4-β (deferral with caveat)** | Tag without DAW verify; release notes warn "DAW handson pending" | Faster | First-customer risk |
| **B4-γ (DAW + standalone OSC console smoke)** | Add console-side smoke test (e.g. via `oscchief`) for `/adm/obj/0/aed` | Highest confidence | 0.5d additional vs α |

**Recommendation**: B4-α primary, B4-γ if Architect wants the console smoke
included. B4-β rejected for first-customer hardness.

#### B5 — Release notes locale

| Opt | Description | Pros | Cons |
| --- | ----------- | ---- | ---- |
| **B5-α (KR only)** | Match existing manual locale | Korean customer match | Excludes English-speaking community / OSS reviewers |
| **B5-β (EN only)** | Standard OSS practice | International reach | Korean primary customer reads slower |
| **B5-γ (KR + EN, KR primary)** ★ | Both, KR top, EN below | Both audiences served | 1.2x writing cost; small |
| **B5-δ (EN with KR appendix)** | Inverse of γ | International primary | Korean customer secondary feel |

**Recommendation**: B5-γ (KR primary, EN below). Aligns with v0.1.0 report
which had Korean primary text and English market-comparison appendix.

### 3.2 Implementation steps R1..R7

#### R1 — Inventory `git log v0.1.0..HEAD` + changelog draft (0.5 day)

* **File (new)**: `CHANGELOG.md` at repo root — **strict
  Keep-a-Changelog 1.1.0 format** with the canonical headings
  `Added` / `Changed` / `Deprecated` / `Removed` / `Fixed` / `Security`
  for the v0.2.0 entry. (Item 12 — single source of truth for both R1
  and R4 §3 sub-sections.)
* **File (new)**: `docs/release/v0.2.0/CHANGES.md` — detailed per-commit
  list grouped by the same six Keep-a-Changelog headings.
* **Source command**: `git log v0.1.0..HEAD --oneline | head -200` (already
  inventoried in this plan §0.1).
* **Acceptance**:
  - `CHANGELOG.md` exists with v0.2.0 entry under the canonical
    Keep-a-Changelog headings (Added / Changed / Deprecated / Removed /
    Fixed / Security).
  - All ~57 commits since `19679c6` triaged into one of the six
    headings; no commits accidentally omitted.

#### R2 — Version macro bump (0.25 day)

* **File `CMakeLists.txt:3`**: `VERSION 0.1.0` → `VERSION 0.2.0`.
* **File `core/CMakeLists.txt:57-59`**:
  ```
  SPE_VERSION_MAJOR=0
  SPE_VERSION_MINOR=1   →   SPE_VERSION_MINOR=2
  SPE_VERSION_PATCH=0
  ```
  (Both files must move in lock-step. If they ever drift, the runtime
  `--version` output will mismatch the CMake-reported package version.)
* **Korean manual file:line targets** (item 9, Critic C-4):
  - `docs/manual_kr/install/README.md:3` → "**버전:** v0.2.0"
  - `docs/manual_kr/install/README.md:397` → "spatial_engine_core v0.2.0
    (schema_version=1)" — keep `schema_version=1` because IPC
    `CURRENT_SCHEMA_VERSION` in `core/src/ipc/ProtocolVersion.h:11` is
    `== 1` (verified) and is independent of the VST3 plugin state-format
    bump v1→v2 from C2B. Only the human-readable version label changes.
  - `docs/manual_kr/install/README.md:591` → unchanged (`schema_version=1`
    in the example log line is still correct against runtime).
  - `docs/manual_kr/operation/README.md:3` → "**버전:** v0.2.0"
  - All `schema_version=1` references in
    `docs/manual_kr/operation/README.md:295,303,316,325,332,342,350,369`
    remain unchanged — they describe the IPC handshake constant, which
    has not bumped.
  - **Decision rule**: cross-check against
    `core/src/ipc/ProtocolVersion.cpp` for the actual runtime
    `CURRENT_SCHEMA_VERSION` constant. If the runtime ever differs from
    what manual says, **update manual to match runtime**, not vice versa.
* **Verify**: `grep -rn '버전: v0\.1' docs/manual_kr/` returns empty
  string. (Detection gate for autopilot.)
* **Verify**: `grep -rn "0\.1\.0" --include="*.md"` across docs to find
  any other stale label references; update version-label-only matches.
  Leave commit-hash and historical references alone.
* **Acceptance**:
  - `cmake --build` shows `-DSPE_VERSION_MAJOR=0 -DSPE_VERSION_MINOR=2 -DSPE_VERSION_PATCH=0`.
  - `spatial_engine_core --version` prints `0.2.0`.
  - Top-level `project(spatial_engine VERSION 0.2.0)` parsed without
    warnings (CMake 3.20+).
  - `grep -rn '버전: v0\.1' docs/manual_kr/` returns empty.
  - `grep -rn 'spatial_engine_core v0\.1' docs/manual_kr/` returns empty.

#### R3 — DAW hands-on test plan + execution (1 day)

* **Hosts**:
  - Reaper 7.x (Linux build, the test target)
  - Bitwig Studio 5.x (Linux)
* **Params verified** (7 today, 8 if A2.1-β chosen):
  1. `kPanAz` (-π, π) — automation curve over 4 bars
  2. `kPanEl` (-π/2, π/2) — automation curve
  3. `kSourceWidth` (0, π) — step at bar 2
  4. `kMasterGain` (-60, 6 dB) — skewed dB ramp
  5. `kAmbiOrder` (0, 1, 2 → orders 1, 2, 3) — discrete steps
  6. `kRoomPreset` (Dry, Small, Medium, Large) — discrete steps
  7. `kBypass` (off, on) — toggle, dry pass-through verified
  8. `kMute` (off, on) — silence verified, IF A2.1-β chosen
* **OSC paths smoked** (against standalone, not plugin in v0.2 per B1-β):
  - `/adm/obj/0/aed (45.0, 30.0, 0.5)` — combined
  - `/adm/obj/0/azim 90.0` — split
  - `/adm/obj/0/mute 1` — mute
  - `/adm/obj/0/xyz (0.5, 0.5, 0.5)` — Cartesian
  - `/adm/obj/0/active 1` — active
  - `/adm/obj/0/width 1.5` — width radians
  - `/adm/obj/0/name "obj0"` — string
* **Output**: `docs/release/v0.2.0/daw-handson-log.md`.
* **Acceptance** — falsifiable per-param table (item 11, Architect blocker
  #7); reuses existing tests where possible:

  | Param        | Falsifiable pass criterion (per-param) |
  | ------------ | -------------------------------------- |
  | `kPanAz`     | Pan Az slider 0°→90° → impulse position rotates 90° per `test_audio_smoke` polar log; verify with `python3 scripts/dump_polar_log.py`. Automate over 4 bars at 120 BPM in Reaper; recorded WAV shows correct LR balance shift. |
  | `kPanEl`     | Layout `lab_8ch.yaml` has elevated speakers; sweep -π/2 → +π/2 produces audible elevation shift in headphone-binaural test; polar log confirms elev rotation matches param value within ±1°. |
  | `kSourceWidth` | Width 0 → π changes spread of pink noise across speakers; measured by inter-speaker correlation in `test_audio_smoke` (correlation drops as width increases). |
  | `kMasterGain`  | -60 dB → +6 dB ramp produces 66 dB peak-meter delta in DAW; recorded RMS confirms log-scale mapping per `Controller::gainPlainToNorm`. |
  | `kAmbiOrder`   | Order 1/2/3 produces visibly different localisation precision (visual: VU meter pattern across 16 spks); existing HOA decoder tests (`test_hoa_decoder_*`) reused for unit-level confirmation. |
  | `kRoomPreset`  | Discrete steps (Dry / Small / Medium / Large) produce audible reverb-tail change; tail length doubles roughly between adjacent presets per `Processor.cpp:545`; no audio glitch on switch. |
  | `kBypass`      | Bypass on → output sample-identical to input. **Reuses existing `test_vst3_bypass`** for unit-level pass; `cmp` of recorded WAV with input confirms in-DAW. |

* **Top-level acceptance**: all 7 params + 7 OSC paths (against standalone)
  produce expected behaviour; 0 crashes, 0 stuck params, 0 audio glitches
  at default block size 512.

#### R4 — Release notes (KR + EN, R3 results integrated) (0.5 day)

* **Files (new)**:
  - `docs/release/v0.2.0/RELEASE_NOTES_KR.md` — KR primary text.
  - `docs/release/v0.2.0/RELEASE_NOTES_EN.md` — EN secondary text.
* **Format alignment (item 12)**: both files **mirror the Keep-a-Changelog
  headings used in R1's `CHANGELOG.md`** plus an audience-targeted
  Highlights section at the top. Every Added/Changed/Fixed item that
  appears in `CHANGELOG.md` v0.2.0 must appear in both release notes
  under the same heading; no drift permitted.
* **Sections (canonical order)**:
  1. **Highlights / 변경 요약** — audience-targeted (5 bullets max);
     KR file leads with Korean live-venue customer-facing language; EN
     file leads with international ecosystem-reader language (per Driver 2).
  2. **Added / 신규 기능** — ADM-OSC v1.0 receive coverage (4 new tags),
     4 HOA decoders, Korean install + operation manual, OFF byte-baseline
     gate (CI-side), Bypass param + state v2 multi-version reader.
  3. **Changed / 변경 사항** — `--osc-dialect` flag added (defaults
     preserved per Principle 5); CI workflow `vst3.yml` dual-job split.
  4. **Deprecated / 사용 중단 예정** — none in v0.2.0.
  5. **Removed / 제거됨** — none in v0.2.0.
  6. **Fixed / 버그 수정** — (per R1 inventory).
  7. **Security / 보안** — none in v0.2.0.
  8. **Compatibility / 호환성** — Built on Ubuntu 24.04 (GLIBC 2.39,
     GCC 13.3.0). Older distros (Ubuntu 22.04 / Debian 11) require
     building from source — see `docs/manual_kr/install/README.md`
     Chapter 3 (item 11, Pre-mortem Scenario 5 mitigation).
     `--osc-port` / `--osc-dialect` defaults preserved; VST3 state v2
     forward-compat with v1 via `Processor.cpp:267-289` reader.
  9. **Known limitations / 알려진 제약** — VST3 plugin ADM-OSC routing
     deferred to Phase C4 / v0.3 (per §1.4 deliverable matrix);
     macOS/Windows not yet built in CI; one console handson log limited
     to Reaper/Bitwig.
  10. **Install / 설치** — pointer to `docs/manual_kr/install/README.md`.
  11. **What's next / 다음** — link to `docs/release/v0.2.0/CHANGES.md`
      and the v0.3 Phase C4 plan.
* **Acceptance**:
  - Both files render; cross-links resolve.
  - Both files share **identical** Added/Changed/Fixed/Deprecated/
    Removed/Security item lists (locale differs only in language, not
    in content).
  - Highlights section content differs between KR and EN per audience.

#### R5 — GHA artifact upload + CI prep (0.5 day, B3-β only)

* **File (NEW) `.github/workflows/release.yml`**: dedicated release
  workflow triggered by `on: push: tags: [v*]` (separate from
  `vst3.yml` which gates main-branch CI). Builds the prebuilt
  `spatial_engine_vst3.so` on ubuntu-24.04 (GLIBC 2.39 — matches
  Pre-mortem 5 + B3-β), produces source tarball, uploads both as
  release assets via `softprops/action-gh-release@v2` (current major
  per item 13).
* **Asset naming (item 11, Pre-mortem 5)**:
  - `spatial_engine_v0.2.0_linux_glibc239.tar.gz` (source + prebuilt .so).
  - GLIBC version explicit in filename so first-customer sees ABI before
    download.
* **File `.github/workflows/vst3.yml`**: unchanged for main-branch CI; the
  new release.yml is **additive**, not a modification.
* **Acceptance**:
  - On a test pre-tag push (`v0.2.0-rc1` for example), assets appear on
    the GH release draft.
  - Asset filename matches the explicit-GLIBC convention above.
  - Existing `vst3-build-and-host-fixture` and OFF-baseline jobs in
    `vst3.yml` not affected.

#### R6 — Tag + release publication (0.25 day; user-explicit per `CLAUDE.md`)

Per global `CLAUDE.md` policy: "v* 태그는 사용자 명시 요청 시만". Therefore
this step is gated on user approval inside the autopilot loop.

**Authority split (item 13)**: The user (NOT autopilot) creates and
pushes the tag. The GHA workflow `.github/workflows/release.yml` (NEW;
created in R5) is triggered automatically by `on: push: tags: [v*]` and
performs the release-asset build + upload via
`softprops/action-gh-release@v2`. Autopilot does NOT execute the tag
command directly; it surfaces a paste-ready command block for the user.

* **User-run commands** (after user explicitly says "tag v0.2.0"):
  ```bash
  git tag -a v0.2.0 -m "$(cat docs/release/v0.2.0/RELEASE_NOTES_EN.md)"
  git push origin v0.2.0
  ```
  (Per `.claude/CLAUDE.md` policy, this is a user action; autopilot
  prepares the command but does not run it.)
* **Acceptance**:
  - Tag visible in `git tag -l`.
  - GHA `release.yml` triggered automatically by tag push; SUCCESS.
  - GH release page published with assets per R5 naming convention.
  - Existing `vst3.yml` main-branch CI still SUCCESS (untouched).

#### R7 — Post-release smoke + announcement (0.25 day)

* Verify `git clone` of v0.2.0 tag → `cmake -DSPATIAL_ENGINE_NO_JUCE=ON ..`
  → `make` → `ctest` SUCCESS on a fresh ubuntu-24.04 container.
* (Optional) post to Korean audio engineering Slack/Discord; announce
  manual + release.
* **Acceptance**: fresh-clone smoke passes.

---

## 4. Cross-track coupling

### 4.1 Does v0.2 cut block on C4?

**No.** Recommended path: B1-β (v0.2 with C3 only) + B2-β (after DAW
handson) decouples Track A from Track B. C4 lands in v0.3.

If Architect / user prefers a single coherent "ADM-OSC fully integrated"
release → B1-α + B2-γ couples them.

### 4.2 Does the manual (`docs/manual_kr/operation/` Ch.4) need an update if C4 ships in v0.2?

**Yes** — S5 explicitly adds Ch.5. If C4 defers, S5 collapses to a
disclaimer note in the existing manual ("플러그인 OSC = Phase C4 예정").

### 4.3 Risk: v0.2 without VST3-OSC

The manual's ADM-OSC reference chapter currently applies only to standalone
(C3 scope). v0.2 release notes (R4 §4 "Known limitations") must explicitly
state: "VST3 플러그인 ADM-OSC 라우팅은 Phase C4 / v0.3에서 지원 예정.
v0.2.0에서 콘솔 → 플러그인 자동화는 표준 DAW 자동화 채널을 사용하십시오."

### 4.4 OFF byte-baseline interaction

* C4 work touches `vst3/` and `bin/spatial_engine_sidecar.cpp`. None of
  these are in OFF-baseline scope (which gates `spe_core` + `spe_util`
  archives only).
* v0.2 version-macro bump in `core/CMakeLists.txt:57-59` **WILL** change
  OFF-baseline hashes (the macros are in the public CMake target's compile
  definitions). Therefore R2 must be paired with an OFF re-pin commit
  (mirror `8c0ca2d`/`587815c`).
* Order matters: R2 before R3 (DAW handson), so DAW tests run on the
  bumped version.

---

## 5. Pre-mortem (3 scenarios, DELIBERATE-mode required)

### Scenario 1 — Pro Tools / Logic Pro / Ableton sandbox blocks plugin UDP `bind()` (cert-risk)

**What goes wrong**: We're Linux-first today, but macOS and Windows are
v0.3+ targets. On macOS Pro Tools, AAX wrappers force network calls
through a host-mediated message-port; direct `bind()` returns `EACCES` or
the plugin is denied notarisation. Logic Pro's audio sandbox profile
forbids `bind()` on most ports. Ableton on Windows generally permits but
exposes the same issue when running in audio sandbox mode.

If we had chosen A1-α/β/ε, the plugin silently fails OSC ingestion on
non-Linux hosts; cross-platform release v0.3+ becomes a multi-month
debugging project.

**Mitigation (already locked in by recommendations)**:
* Decision A1-δ (sidecar bridge) puts the network surface in the
  standalone process, which has no host sandbox to navigate.
* Decision A5-β (sidecar everywhere) means the same code path on Linux,
  macOS, Windows.
* If we ever do A1-α/ε on Linux as a "fast path", it is feature-flagged
  via `SPATIAL_ENGINE_VST3_DIRECT_OSC` (default OFF) and the documentation
  explicitly says "Linux only, will be silently disabled on mac/Windows".
* If Architect rejects A1-δ → A5-γ (defer entire C4 to v0.3) is the
  abort path.

**Detection**: macOS / Windows port plan in 2026Q3 must include "verify
sidecar workflow on each host" as the first integration milestone, not the
last.

### Scenario 2 — v0.2 ships, console vendor reports an ADM-OSC v1.0 incompatibility

**What goes wrong**: A DiGiCo or Lawo console connects to standalone:9100
and we discover their packet stream uses normalisation rules different
from ADR 0006's `ADM_OSC_MAX_DIST=20.0f` — e.g. dist in metres, or az
sign convention left-handed vs right-handed. Symptom: panning is mirrored
or distance is collapsed.

**Patch path**:
1. **Reproduce**: capture the offending packet stream into a new test
   fixture `core/tests/fixtures/adm_osc_synthetic_<vendor>_v1.osc.bin`.
2. **Diagnose**: extend `test_p_adm_osc_v1_compat.cpp` with a fixture-
   driven case; expect mismatch.
3. **Fix at bridge layer**: per ADR 0006 § "Vendor note", do NOT change
   `core/`. Add a vendor toggle to `bridge/spike_vid2spatial_osc.py` and
   `bridge/_adm_osc_common.py` (planned in C3 plan §S4).
4. **Hotfix release**: v0.2.1 patch.
5. **Document deviation**: new ADR `0012-adm-osc-vendor-quirks.md`
   (reserved slot per §1.4; numbered 0012 to avoid `0006-*.md` collision).

**Reference**: this exact path was already pre-staged in C3 plan §6
"Risks" row "Real console behaviour diverges from public spec".

### Scenario 3 — DAW hands-on gate (R3) fails: Reaper rejects 7-param state

**What goes wrong**: Reaper's project file format expects either
`kCanAutomate` or `kIsBypass` flags in specific combinations. Our state v2
format (36 bytes, 7 floats including bypass at index 6) round-trips inside
our own host fixture but Reaper's bigger session-restore flow loads the
state without restoring all params, or worse, crashes.

**Possible causes**:
* `kIsBypass` flag at `Controller.cpp:213` interpreted by Reaper as a
  special bypass param requiring host-side double-bookkeeping.
* `defaultNormalizedValue` missing for `kBypass` (currently 0.0 — fine).
* `restartComponent(kParamValuesChanged)` at `Controller.cpp:371` not
  honoured by Reaper if called outside of `setComponentState`'s
  return-Ok window.

**Decision tree** (revised per item 6, Architect blocker #6 + Critic):
1. If only 1 param fails → fix the specific param's flags or default;
   re-run R3.
2. If state restore fails for >1 param → revert to a strict v1 state
   format (32 bytes, 6 floats, no bypass) for the v0.2 release, and
   move state v2/v3 to v0.3. Backward-compat preserved.
3. If Reaper crashes → **ship C3 + HOA decoders + manual + bypass param
   as v0.2.0** with an "experimental ADM-OSC subset" / "VST3 plugin
   hosting under continued investigation" note in release notes
   §알려진 제약 / Known limitations. **Reserve v0.1.1 strictly for true
   patch-only emergencies** (compiler-emit security fixes,
   wire-format-preserving bug fixes) — NOT for "downgrade C3 to a
   patch", which would be a semver violation per §B1 Why-not-γ.

**Detection threshold**: any R3 step that fails 2 retries triggers
fallback (3) above — ship v0.2.0 with the affected param marked
"experimental" in release notes, file a v0.3 issue for the underlying
DAW interaction.

### Scenario 4 — Sidecar process killed by user / OS / OOM (Architect Pre-mortem add)

> **Round-2 status**: Scenario 4 applies only once the sidecar lands
> (v0.3+). Documented here for contract-completeness; v0.2 is
> sidecar-free per §1.4.

**What goes wrong**: User runs DAW + plugin + sidecar + standalone.
Memory pressure on a Korean mixing PC (often 8–16 GB RAM with large
session files) causes the OS to OOM-kill the sidecar (lowest-priority of
the four processes). Plugin's UNIX socket peer disappears mid-session.
`send()` returns `EPIPE`. Plugin's RT-safety guarantee (no alloc, no log
on audio thread) means the plugin **silently drops** all subsequent
param changes until the sidecar restarts.

**Probability**: Moderate (sidecar is youngest, lightest process, most
likely OOM victim).
**Severity**: High (silent failure; user troubleshooting is very expensive).

**Mitigation (locked into ADR 0010 / 0011 hardening for v0.3)**:
* Plugin's `dispatchParamChange` extension (S3) must check socket health
  periodically and **set a Controller-side metric** (read by the editor
  view in v0.4, or polled via standalone `/sys/state` reflection in
  v0.3) — "sidecar disconnected, automation working but bridge inactive".
* Sidecar should include systemd unit file (or equivalent) for auto-restart.
* Manual Ch.5 (v0.3 deliverable) must include "if sidecar dies,
  console→plugin path stops; restart sidecar" troubleshooting note.
* ADR 0011 EPIPE handling rule (item 4) covers the sidecar-side dead-peer
  detection.

### Scenario 5 — GLIBC mismatch on customer machine (Critic Pre-mortem 5)

**What goes wrong**: B3-β ships prebuilt `spatial_engine_vst3.so` built
on ubuntu-24.04 (GLIBC 2.39, GCC 13.3.0). Korean live-venue mixing PCs
frequently run on stable LTS distros — ubuntu-22.04 (GLIBC 2.35) or
Debian 11 (GLIBC 2.31). User downloads `.so` → Reaper's plugin scan
reports "incompatible binary" or fails to resolve `GLIBC_2.39` symbols
at load time.

**Probability**: HIGH (Korean live-venue PCs commonly run LTS, not the
latest interim release).
**Severity**: HIGH (first-customer first-impression).

**Mitigation (item 11)**:
* Release-asset filename encodes the GLIBC ABI explicitly:
  `spatial_engine_v0.2.0_linux_glibc239.tar.gz`.
* Release notes §호환성 / Compatibility lists the build env explicitly:
  > Built on Ubuntu 24.04 (GLIBC 2.39, GCC 13.3.0). Older distros
  > (Ubuntu 22.04 / Debian 11) require building from source. See
  > `docs/manual_kr/install/README.md` Chapter 3.
* Manual `docs/manual_kr/install/README.md` Chapter 3 (build-from-source
  path) is already documented and tested for v0.1.0 — confirm still works
  during R7 fresh-clone smoke.
* Future v0.3 candidate: produce a second prebuilt .so on ubuntu-22.04
  to broaden the GLIBC support window if first-customer feedback
  demands it.

**Detection**: GHA release job (R5) names the artifact with the
explicit GLIBC tag; a curious user inspecting the release page sees
the GLIBC version before downloading.

---

## 6. Risks and Mitigations

| Risk | Track | Impact | Mitigation |
| ---- | ----- | ------ | ---------- |
| C4 sidecar protocol churn | A (v0.3 deliverable; v0.2 ships ADR 0010 + 0011 drafts only) | Two-process workflow surprises users | ADRs 0010 + 0011 freeze the protocol before any code; first integration on Linux only (A5-α v0.3 fast path); cross-platform sidecar (A5-β) reserved for v0.4+ if cert-blocked. |
| State v2 → v3 migration breaks v0.1.0 plugin users | **A — DEFERRED to v0.3** (was S2-conditional; per §1.5 freeze A2.1-β + state v3 are v0.3 deliverables) | DAW projects fail to load | v0.2 stays at state v2 (no churn). 3-way reader fork (v1 / v2 / v3) lands in v0.3 alongside C4 sidecar; all paths covered by `test_vst3_state_v3_persist.cpp`. |
| OFF byte-baseline drift from version-macro bump | B (R2) | CI fails after merge | R2 paired with re-pin commit; mirror `587815c` workflow |
| Korean manual stale on plugin OSC (C4 deferred to v0.3 per §1.4) | A/B coupling — **resolved by freeze** | First-customer doc confusion | R4 §9 (Known limitations) carries the explicit "VST3 플러그인 ADM-OSC 라우팅은 Phase C4 / v0.3에서 지원 예정" note; manual install/operation files updated to "v0.2.0" by R2 (item 9). |
| `IConnectionPoint::notify` re-purposed for sidecar reverse channel | A (v0.3 — S4 deferred) | AM-R3-10 decision (notify=kNotImplemented) reverted | ADR 0010 documents the v0.3 reversal; `notify` will dispatch PerformEdit when sidecar lands; existing v0.2 tests unchanged (notify still returns kNotImplemented). |
| GHA artifact upload exposes prebuilt .so without macOS/Windows parity | B (R5/B3-β) | User confusion ("why no .dylib?") | Release notes explicit: Linux-only artifact, mac/Windows = v0.3 |
| DAW handson uncovers blocking Reaper/Bitwig issue | B (R3) | Release slip | Pre-mortem 3 escape: cut v0.1.1 instead |
| GLIBC mismatch on customer machine (Pre-mortem 5) | B (R5/B3-β) | Prebuilt .so fails to load on ubuntu-22.04 / Debian 11 | Asset filename encodes GLIBC version (`spatial_engine_v0.2.0_linux_glibc239.tar.gz`); release notes §호환성 lists Built-on env explicitly; build-from-source path documented in install manual Ch.3. |

---

## 7. Expanded test plan (DELIBERATE-mode required)

### 7.1 Unit tests (new, all in `vst3/tests/`)

| Test file | What it asserts |
| --------- | --------------- |
| `test_vst3_sidecar_registry.cpp` | JSON instance file write/read/GC; PID liveness check |
| `test_vst3_sidecar_dispatch.cpp` | 1000-iter param change → SPSC ring → drain on dedicated thread; alloc==0 in audio thread |
| `test_vst3_sidecar_reverse_path.cpp` | sidecar → controller `IConnectionPoint::notify` → `Controller::norm_values_[i]` reflect |
| `test_vst3_state_v3_persist.cpp` (only if A2.1-β) | state v1, v2, v3 roundtrip + v1/v2 forward-compat reads |

### 7.2 Integration tests (new)

| Test file | What it asserts |
| --------- | --------------- |
| `test_vst3_e2e_console_to_plugin.cpp` | UDP `/adm/obj/0/aed` → standalone:9101 → sidecar → plugin → param value reflected |
| `test_vst3_e2e_plugin_to_console.cpp` | DAW automation → plugin → sidecar → standalone:9101 → external listener observes `/adm/obj/0/...` packet |

### 7.3 End-to-end (DAW hands-on, R3 / S8)

* Reaper 7.x: 7 (or 8) params automatable, 7 OSC paths smoked through
  standalone, sidecar reverse-relay observed in DAW automation lane.
* Bitwig 5.x: same.

### 7.4 Observability metrics (new)

* `OSCBackend` reject count (already added in C3) surfaced in `/sys/state`.
* Sidecar registry size (count of active plugin instances) exposed via
  sidecar's stderr log every 5 s.
* Sidecar relay latency p50/p99 — log to stderr every 30 s during operation.
* CI gate: any flaky test (>1 failure in 5 retries) blocks the v0.2 tag.

### 7.5 Soak (S6, perf job)

* 1 obj × 100 Hz × 60 s console-to-plugin flood.
* Audio thread alloc==0 (existing harness).
* p99 console-OSC → plugin-param-write < 5ms.
* Sidecar memory steady-state (no growth >5% over 60s).

---

## 8. ADR (decision record, ratified post-Round-2 — DELIBERATE-mode required)

> **NOTE (Round-2 update, item 10)**: New ADRs are numbered starting at
> **0010** (not 0007) to avoid the existing `docs/adr/0006-*.md` collision
> documented in §1.4. ADR 0010 = binding model; ADR 0011 = discovery; ADR
> 0012 = vendor-quirk overlay slot.
>
> **NOTE (Round-2 update, item 5 + §1.5 freeze)**: ADRs 0010/0011 are
> *drafted* in v0.2.0 (S1) but only *ratified + implemented* in v0.3.0.
> v0.2.0 ships zero plugin-side OSC code; the design contract is locked
> while C3 cache is fresh, but execution is decoupled from the v0.2 cycle.

* **Decision (v0.2.0 plan)**:
  - **Track A v0.2 scope** = S1 only (ADR 0010 + 0011 drafts; ADR 0012
    reserved). Implementation steps S2..S8 are **v0.3.0 deliverables**.
  - **Track A v0.3 contract**, frozen here for ADR-author guidance:
    **A1-ε + A2-α + A2.1-β + A3-β + A4-β + A5-α** (Linux fast path);
    A1-δ + A5-β reserved as v0.4+ cross-platform fallback if cert
    evidence demands.
  - **Track B v0.2.0 release**: **B1-β + B2-β + B3-β + B4-α + B5-γ**.
* **Drivers**:
  - Driver 1: Korean live-venue customer dependency (plugin ↔ console
    workflow) — addressed by A1-δ sidecar.
  - Driver 2: ecosystem parity with L-ISA / Spat Revolution — addressed by
    bidi A2-δ via sidecar.
  - Driver 3: reduce technical debt before next refactor — addressed by
    locking the protocol in ADR 0010 / 0011 NOW while C3 cache is fresh.
* **Alternatives considered**:
  - A1-α (per-instance auto-port): rejected — Pro Tools / Logic Pro
    sandbox; multi-instance port chaos.
  - A1-β (shared port + multiplex): rejected — silent failure on N>1
    instance.
  - A1-γ (continued deferral): held as escape-hatch if A1-δ blocked.
  - A1-ε (recv-only socket): rejected — strict subset of α with same
    cert risk.
  - A2-α, A2-β, A2-γ: viable but δ generalises them with cert safety.
  - A2.1-α (reuse `kBypass` for mute): rejected — semantic mismatch
    creates permanent footgun.
  - A3-α (mDNS): deferred to v1.0; new dep not justified at v0.2 scale.
  - A3-γ (`/sys/state` poll): deferred — chicken-and-egg.
  - A3-δ (single-instance only): held as scope-cut fallback.
  - A4-α (audio thread): forbidden by Principle 3.
  - A4-γ (host message thread): unavailable without editor view.
  - A5-α (Linux-only OSC): rejected — fragments cross-platform story.
  - A5-γ (defer entire C4 to v0.3): held as Track A abort path; pairs
    with B1-β.
  - B1-α (block release on C4): rejected — couples release to highest-
    risk work.
  - B1-γ (v0.1.1 patch): rejected — C3 is minor-bump material.
  - B1-δ (split tag re-issue): rejected — tag operations not worth.
  - B2-α (cut today, no DAW handson): rejected — Pre-mortem 3 risk.
  - B2-γ (cut after C4): rejected — couples to A.
  - B2-δ (LTS branch): rejected — overhead not justified.
  - B3-α (source-only): rejected — Korean console operators benefit from
    drop-in `.so`.
  - B3-γ (source + plugin + standalone): held as upgrade path; one extra
    binary at minor cost.
  - B3-δ (deb/rpm): deferred to v1.0.
  - B4-β (DAW handson deferral): rejected — first-customer risk.
  - B4-γ (DAW + console smoke): viable upgrade if Architect wants more.
  - B5-α / β / δ: cosmetic ordering only; γ chosen for Korean primary.
* **Why chosen**:
  - Maximum cert safety with maximum ecosystem parity (A1-δ + A5-β).
  - Smallest cycle time consistent with first-customer hardness
    (B1-β + B2-β + B4-α).
  - Single source of network surface (standalone) consistent with
    ADR 0003 single-OSC-schema invariant.
* **Consequences**:
  - New code surface: `vst3/sidecar_bridge/`, `bin/spatial_engine_sidecar.cpp`,
    new ADR 0010 + 0011 + possibly 0012 (vendor quirks if Pre-mortem 2
    fires) — all v0.3 deliverables; v0.2 ships drafts only per §1.4.
  - `IConnectionPoint::notify` on `Controller` becomes a real handler
    (was kNotImplemented per AM-R3-10) — small reversal, ADR 0010
    documents (v0.3 contract).
  - State format may bump v2 → v3 (if A2.1-β); tests updated; reader
    handles all 3.
  - Two-process workflow becomes the documented norm; manual Ch.5 added.
  - Korean live-venue contract path validated end-to-end at v0.2 (C3
    standalone) and v0.3 (C4 plugin).
  - OFF byte-baseline re-pinned once for R2 version bump; otherwise
    untouched.
* **Follow-ups**:
  - v0.3 / Phase D: macOS / Windows port; first cross-platform sidecar
    handson.
  - v0.3 / Phase D6: editor view (`createView`) — could move sidecar
    control channel into a host-friendly surface.
  - v1.0 candidate: mDNS discovery (A3-α upgrade).
  - v1.0 candidate: deb/rpm distro packaging (B3-δ).
  - 60-day post-first-contract: replace synthetic ADM-OSC fixture with
    real vendor capture (inherited from C3 ADR 0006 follow-ups).

---

## 9. ETA banner (post-Round-2 freeze)

> **Round-2 update**: Per §1.4 + §1.5 + Architect §6 path-a synthesis,
> only S1 lands in v0.2.0; S2..S8 are v0.3 deliverables. Banner below
> reflects that split.

### v0.2.0 release sprint (this plan's primary deliverable)

| Phase | Step | ETA | Cumulative |
| ----- | ---- | --- | ---------- |
| A | S1 — ADR 0010 / 0011 / 0012 drafts | 1.0d | 1.0d |
| B | R1 — CHANGELOG (Keep-a-Changelog) | 0.5d | 0.5d (parallel with A) |
| B | R2 — Version bump + manual updates + OFF re-pin | 0.5d | 1.0d |
| B | R3 — DAW hands-on (standalone-only, per B1-β) | 1.0d | 2.0d |
| B | R4 — Release notes KR/EN (Keep-a-Changelog mirror) | 0.5d | 2.5d |
| B | R5 — `release.yml` workflow + asset naming | 0.5d | 3.0d |
| B | R6 — Tag + release publication (user-gated) | 0.25d | 3.25d |
| B | R7 — Post-release fresh-clone smoke | 0.25d | 3.5d |
| **v0.2.0 total** | | | **~3.5d** (S1 in parallel with R1..R2) |

### v0.3.0 sprint (Track A implementation, contracts frozen here)

| Phase | Step | ETA | Cumulative |
| ----- | ---- | --- | ---------- |
| A | S2 — Sidecar skeleton + registry hardening (item 4) | 2.0d | 2.0d |
| A | S3 — Plugin → sidecar SPSC channel | 2.0d | 4.0d |
| A | S4 — Sidecar → DAW reverse path | 1.5d | 5.5d |
| A | S5 — Manual Ch.5 (sidecar workflow) | 0.5d | 6.0d |
| A | S6 — E2E + soak | 1.5d | 7.5d |
| A | S7 — OFF re-pin (likely no-op) | 0.5d | 8.0d |
| A | S8 — DAW hands-on (8 params incl. kMute v3) | 1.0d | 9.0d |
| **v0.3 Track A total** | | | **~9.0d** (8–13d banner with cert-eval slack) |

---

## Appendix A — Acceptance criteria index (≥90% testable)

### Track A

> **Round-2 re-scoping (item 3)**: per §1.4 deliverable matrix, only A.1
> + A.2 ship in v0.2.0. A.3..A.12 are v0.3.0 acceptance gates and are
> retained here as the v0.3 contract.

A.1  ADR 0010 (binding model) reviewed by Architect/Critic in ralplan
     Round-3. (S1, **v0.2.0**)
A.2  ADR 0011 (discovery) schema for `~/.config/spatial_engine/instances.json`
     ratified with hardening rules (item 4). ADR 0012 reserved slot
     created. (S1, **v0.2.0**)
A.3  Sidecar binary builds on ubuntu-24.04 GLIBC 2.39; zero JUCE includes
     (`grep -r juce vst3/sidecar_bridge/` returns 0). (S2, **v0.3.0**)
A.4  `cmake -DSPATIAL_ENGINE_VST3=ON -DSPATIAL_ENGINE_VST3_BRIDGE=OFF`
     produces byte-identical OFF artifacts vs the dual-gate baseline
     (`.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`).
     (S2, **v0.3.0**)
A.5  `test_vst3_sidecar_registry` PASS (registry write/read/GC + PID
     liveness via `/proc/{pid}/comm`). (S2, **v0.3.0**)
A.6  `test_vst3_sidecar_dispatch` PASS, 1000-iter, alloc==0 in audio
     thread. (S3, **v0.3.0**)
A.7  `test_vst3_sidecar_reverse_path` PASS, latency < 100ms. (S4, **v0.3.0**)
A.8  Manual Ch.5 added with topology diagram and TOC link. (S5, **v0.3.0**)
A.9  `test_vst3_e2e_console_to_plugin` PASS: console UDP → plugin param
     within 100ms. (S6, **v0.3.0**)
A.10 `soak_vst3_console_flood` PASS: 1 obj × 100 Hz × 60s, alloc==0,
     p99 < 5ms. (S6, **v0.3.0**)
A.11 OFF byte-baseline preserved or re-pinned cleanly. (S7, **v0.3.0**)
A.12 DAW hands-on log committed; 8 params (now including `kMute` from
     A2.1-β state v3) verified in Reaper + Bitwig. (S8, **v0.3.0**)

### Track B

B.1  `CHANGELOG.md` exists with full v0.1.0..HEAD inventory; no commit lost. (R1)
B.2  `cmake --build` reports `SPE_VERSION_MINOR=2`; `--version` prints 0.2.0. (R2)
B.3  OFF byte-baseline re-pin commit lands cleanly; CI run #N+ SUCCESS. (R2)
B.4  DAW hands-on log captures all 7+1+7 OSC paths × 2 hosts. (R3)
B.5  Release notes KR + EN render; cross-links resolve. (R4)
B.6  GHA tag-trigger uploads `spatial_engine_vst3.so` + source tarball. (R5)
B.7  Tag `v0.2.0` exists; `git verify-tag` OK; tag annotation contains release notes. (R6, user-gated)
B.8  Fresh-clone from tag → `cmake -DSPATIAL_ENGINE_NO_JUCE=ON` → `make` → `ctest` all green. (R7)

---

## Appendix B — File:line citation summary

### Track A — files touched (additive only unless noted)

* `vst3/SpatialEngineProcessor.cpp:32` — engine ctor; extend with optional
  registry registration (no behaviour change unless `SPATIAL_ENGINE_VST3_BRIDGE=1` env).
* `vst3/SpatialEngineProcessor.cpp:481-554` — `dispatchParamChange`; extend
  to write SPSC ring entry when bridge mode active.
* `vst3/SpatialEngineProcessor.hpp:32-40` — `ParamId` enum; add `kMute=7` if A2.1-β chosen.
* `vst3/SpatialEngineProcessor.hpp:104` — `norm_values_[7]` array; resize
  to `[8]` if A2.1-β.
* `vst3/SpatialEngineController.cpp:107-216` — `buildParamInfos`; add 8th
  param if A2.1-β; preserve existing 7.
* `vst3/SpatialEngineController.cpp:592-597` — `notify`; replace
  `kNotImplemented` with sidecar reverse-channel handler that calls
  `IComponentHandler::performEdit` (acquired at `:561-565`).
* `vst3/CMakeLists.txt:8-37` — plugin sources; add `SPATIAL_ENGINE_VST3_BRIDGE`
  CMake option (default OFF); when ON, add sidecar source files.
* `CMakeLists.txt:36-46` — extend `SPATIAL_ENGINE_VST3` block to also handle bridge option.
* (NEW) `vst3/sidecar_bridge/PluginInstanceRegistry.{h,cpp}`
* (NEW) `vst3/sidecar_bridge/PluginAutomationProxy.{h,cpp}`
* (NEW) `vst3/sidecar_bridge/AutomationReflect.{h,cpp}`
* (NEW) `bin/spatial_engine_sidecar.cpp`
* (NEW) `core/src/util/SpscRing.h` (or reuse from existing C1.b implementation)
* (NEW) `vst3/tests/test_vst3_sidecar_registry.cpp`
* (NEW) `vst3/tests/test_vst3_sidecar_dispatch.cpp`
* (NEW) `vst3/tests/test_vst3_sidecar_reverse_path.cpp`
* (NEW) `vst3/tests/test_vst3_e2e_console_to_plugin.cpp`
* (NEW) `vst3/tests/perf/soak_vst3_console_flood.cpp`
* (NEW) `vst3/tests/test_vst3_state_v3_persist.cpp` (only if A2.1-β)
* (NEW) `docs/adr/0010-vst3-osc-binding-model.md` (S1, v0.2.0 ships draft)
* (NEW) `docs/adr/0011-vst3-osc-multi-instance-discovery.md` (S1, v0.2.0 ships draft)
* (NEW) `docs/adr/0012-adm-osc-vendor-quirks.md` (S1, reserved slot)
* (UPDATED) `docs/manual_kr/operation/README.md` — add Ch.5

### Track B — files touched

* `CMakeLists.txt:3` — `VERSION 0.1.0` → `VERSION 0.2.0`.
* `core/CMakeLists.txt:57-59` — bump `SPE_VERSION_MINOR` to `2`.
* `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256` — re-pin
  both files after R2 (use existing GHA-canonical hash workflow from `587815c`).
* `.github/workflows/vst3.yml` — add release-job triggered on `tags: [v*]`.
* (NEW) `CHANGELOG.md`
* (NEW) `docs/release/v0.2.0/CHANGES.md`
* (NEW) `docs/release/v0.2.0/RELEASE_NOTES_KR.md`
* (NEW) `docs/release/v0.2.0/RELEASE_NOTES_EN.md`
* (NEW) `docs/release/v0.2.0/daw-handson-log.md`
* (UPDATED) `docs/manual_kr/install/README.md` — version references 0.1.0 → 0.2.0
* (UPDATED) `docs/manual_kr/operation/README.md` — known-limitations bump

### Track A reads-only (referenced but not modified)

* `core/src/ipc/CommandDecoder.cpp:317-373` — ADM-OSC decode (consumed by sidecar).
* `core/src/ipc/CommandDecoder.cpp:422-602` — ADM-OSC encode (consumed by sidecar reverse path).
* `core/src/ipc/OSCBackend.cpp:75-83` — UDP listener pattern referenced for sidecar UDP recv.
* `core/src/ipc/AdmOscConstants.h` — `ADM_OSC_MAX_DIST=20.0f` shared constant.
* `core/src/core/SpatialEngine.cpp:239-260` — `obj_cache_` direct path; sidecar consumes via standalone.
* `core/src/ipc/StateModel.cpp:37,54,69,84` — seq-drop guard; reaffirm
  invariant: ADM-OSC bypasses StateModel (per ADR 0006 §StateModel
  routing invariant).

---

## Appendix C — ADR draft slot (numbered post-Round-2 starting at 0010)

### ADR 0010 — VST3 plugin ADM-OSC binding model

* **Status**: Draft for v0.2.0; ratified + implementation in v0.3.0 (per §1.4 / §1.5 / item 5).
* **Context** (item 7, Architect blocker #1 + Critic C-1 carve-out):
  > ADR 0003 establishes OSC-over-UDP-9100 as the v0 IPC channel.
  > ADR 0010 introduces a UNIX-domain-socket control channel between the
  > VST3 plugin process and the `spatial_engine_sidecar` binary (v0.3+
  > deliverable). The UDS is **orthogonal to ADR 0003**: it carries
  > plugin-state messages, not OSC commands. ADR 0003 §Migration target
  > ("shm + UDS for v1+") explicitly anticipates this layering. The v0.3
  > A1-ε fast-path (Linux-only direct UDP recv on per-instance ephemeral
  > port) keeps full ADR 0003 OSC-over-UDP semantics; the A1-δ
  > sidecar+UDS fallback (v0.4+ if cert-blocked on macOS) is the layer
  > that requires the orthogonality clause above.
* **Decision (v0.3 contract)**: A1-ε + A2-α + A2.1-β + A3-β + A4-β + A5-α
  (Linux fast path); A1-δ + A5-β as cross-platform fallback in v0.4+.
* **Drivers**: see §1.2 (Korean live-venue, parity, debt reduction).
* **Alternatives considered**: see §8 above.
* **Thread-budget invariant** (item 11, Architect blocker #4 + Critic):
  - One SPSC ring (audio→sidecar) per plugin instance.
  - One reverse-channel reader thread (sidecar→Controller `notify`) per plugin instance.
  - Total threads per plugin instance = 2 (when bridge mode active).
  - **N ≤ 8 plugin instances per host process** (some hosts, e.g. Pro
    Tools, cap thread count; 8×2=16 net-new threads bounded).
  - Single sidecar consumer thread drains all per-instance SPSC rings.
* **Why chosen**: see §8 above.
* **Consequences**: see §8 above.
* **Follow-ups**: see §8 above.

### ADR 0011 — VST3 plugin instance discovery (A3-β file registry)

* **Status**: Draft for v0.2.0; ratified + implementation in v0.3.0.
* **Decision**: file-based registry at `~/.config/spatial_engine/instances.json`;
  PID-based liveness; 5s GC interval.
* **Schema** (v0.3 implementation; `schema_version=1` for the registry
  format itself, independent of IPC `schema_version` in `ProtocolVersion.h`):
  ```json
  {
    "schema_version": 1,
    "instances": [
      {
        "pid": 12345,
        "obj_id": 0,
        "session_path": "/tmp/spatial_engine/sock-12345",
        "registered_at": "2026-05-10T12:34:56Z",
        "last_heartbeat": "2026-05-10T12:35:01Z"
      }
    ]
  }
  ```
* **Hardening rules** (item 4, Architect blocker #2 + Critic):
  - **Atomic write**: writer creates `instances.json.tmp` and `rename(2)`s
    onto `instances.json` (POSIX-atomic, no torn read).
  - **Advisory locking**: writer takes `flock(LOCK_EX)` on `instances.json.lock`
    sidecar file during write to prevent concurrent torn registries.
    Reader takes `flock(LOCK_SH)` (or proceeds best-effort + JSON-parse-retry).
  - **Stale PID detection**: reader probes liveness via `/proc/{pid}/comm`
    matching the expected binary name (Linux). PID-mismatch entries are
    treated as dead and skipped.
  - **GC sweep**: dead entries swept by next writer (5s cadence) — cleans
    stale lockfiles from segfaulted plugins.
  - **EPIPE handling**: sidecar relay `send()` to a UDS peer that returns
    `EPIPE` drops the dead connection silently and logs to stderr at WARN
    level (not stdout — keeps DAW console clean). Subsequent writer GC
    pass removes the registry entry.
  - **`schema_version` field** (initial value `1`) at top of registry JSON
    enables future format evolution without ambiguity.
* **Drivers**: zero new dep; debuggable by `cat`.
* **Alternatives considered**: mDNS (A3-α) — deferred to v1.0; OSC poll
  (A3-γ) — chicken-and-egg.
* **Why chosen**: minimum-viable; matches v0.3 release scope.
* **Consequences**: stale entries on DAW crash; mitigated by hardening rules above.
* **Follow-ups**: mDNS upgrade in v1.0.

### ADR 0012 — ADM-OSC vendor quirks (slot reserved, fill on first incompat)

* **Status**: Reserved (Pre-mortem 2 escape).
* (To be filled when first vendor reports incompat. ADR slot 0009 was
  skipped to align with the §1.4 numbering decision; 0010/0011/0012 are
  the new contiguous block.)

---

## Appendix D — Cross-reference to Phase C3 plan (consistency check)

| Phase C3 reference | Phase C4 follow-through |
| ------------------ | ----------------------- |
| C3 §3 Decision C-β: VST3 standalone-only | C4 §2.1 A1: confirms; A1-δ adds sidecar instead of plugin-side UDP |
| C3 §S4 deferral note: "Phase C4 issue: VST3 plugin ADM-OSC strategy" | C4 §2.1 A1 + §2.2 S1 ADR 0010 = the strategy (v0.2 ships draft; v0.3 ratifies + implements) |
| C3 §6 Risks row 6: ADM bypasses StateModel via obj_cache_ | C4 reaffirms — sidecar receives ADM, forwards to standalone, standalone respects same invariant |
| C3 ADR 0006 §StateModel routing invariant | Inherited by C4; no plugin-side StateModel mutation |
| C3 ADR 0006 § "60-day real-vendor capture follow-up" | Inherited by v0.2 release notes (R4 §4 known-limits): "real vendor capture pending" |
| C3 §S5 soak: 64 obj × 1kHz × 60s, p99 < 3ms | C4 §S6 soak: 1 obj × 100Hz × 60s, p99 < 5ms (lower bar — single-obj plugin + extra sidecar hop) |

---

## 10. Recommended next step (post-Round-2)

The plan has applied all 13 patches from the consolidated revision list
(see Appendix D — Round-2 Changelog) and is **ready for Architect
re-review + Critic verification** in ralplan Round-3.

1. **Architect re-review (Round-3, lighter pass)**:
   - Verify item 5 path-a synthesis (defer A1-δ S2..S8 to v0.3) is
     reflected throughout (§1.4, §1.5, §S2..§S8 deferral markers,
     Appendix A re-scoping, ETA banner split).
   - Confirm ADR 0010 §Context paragraph (item 7) carves UDS out of
     ADR 0003 acceptably.
   - Confirm thread-budget invariant in ADR 0010 (item 11) bounds
     N ≤ 8 instances cleanly.
   - Confirm Pre-mortem 3 fallback (item 6) no longer contradicts §B1.
2. **Critic Round-3 verification (13-item closure pass)**:
   - Walk Appendix D items 1..13 against the plan; mark each "closed"
     or kick back to Round-3 if any one is incomplete.
   - Verify §1.5 freeze table has zero conditional branches.
   - Run `grep -rn 'off-baseline-pins' .omc/plans/spatial-engine-phaseC4-and-v0.2-release.md`
     → must be empty (item 8 detection gate).
   - Verify R2 acceptance includes the Korean manual file:line targets
     (item 9).
3. **On Round-3 APPROVE → autopilot** per project policy
   (`.claude/CLAUDE.md`): `/oh-my-claudecode:autopilot` with this plan
   as active spec. Track A v0.2.0 scope = S1 only (3 ADR drafts);
   Track B = R1..R7 full release sprint.
4. **If Round-3 raises any new HIGH-severity defect**: Round-4 patch.
   Otherwise direct to autopilot.

---

## 11. Open questions flagged for Architect

(See `.omc/plans/open-questions.md` — appended below)

1. **A1-δ cert-risk evidence**: do we have concrete proof that A1-α
   would fail on Pro Tools/Logic, or is this projection? Decision-quality
   improves if we cite a specific Steinberg / Avid / JUCE forum thread.
2. **A2.1-β state format bump**: is it preferable to land v3 in v0.2 (and
   re-pin OFF baseline twice — once for v3, once for sidecar?) or to defer
   v3 to v0.3 alongside C4? If v0.2 is C3-only (B1-β), v3 bump is wasted
   work for one release.
3. **B1 final scope**: B1-β (C3-only v0.2) recommended — does
   Architect accept, or insist on B1-α (block release on C4)?
4. **R3 / S8 DAW choice**: Reaper + Bitwig are Linux-friendly. Should we
   add Ardour or REAPER ARM for broader coverage at v0.2 cost?
5. **Sidecar protocol**: UNIX domain socket + JSON registry; is this the
   right primitive, or should we use shared memory (`shm_open` + ring) for
   lower latency?
6. **ADR 0010 vs 0011 split**: should they be one combined ADR (binding
   model + discovery) or kept separate as drafted here? (Renumbered
   from 0007/0008 per §1.4 + Critic C-3.)
7. **`IConnectionPoint::notify` reversal of AM-R3-10**: any back-compat
   risk with hosts that previously relied on `kNotImplemented`?

---

## Appendix D — Round-2 Changelog

This appendix records the 13-item consolidated revision list applied in
Round-2 of the ralplan consensus loop, prior to Round-3 Architect
re-review + Critic verification. Each item lists edit location, before /
after summary, and which reviewer demanded it.

1. **[C-1, CRITICAL] Rewrite §1.1 Principle 5.**
   * Edit location: §1.1 (lines ~94-99 pre-patch).
   * Before: "VST3 state v2 format (36 bytes, 'SPE1' magic, 7 floats),
     Component / Controller IIDs ... remain unchanged."
   * After: documents v1→v2 break-with-mitigation truthfully, citing
     `Processor.cpp:267-289` multi-version reader and v0.1.0 `.vstpreset`
     load path. Reviewer: Critic (C-1, CRITICAL — ABI lie).

2. **[C-5, MAJOR] Add §1.5 "Final Decision Freeze".**
   * Edit location: new section after §1.3.
   * Before: every axis (A1, A2, A2.1, A3, A4, A5, B1..B5) had primary +
     escape pairs that left autopilot to make architectural calls.
   * After: each axis has exactly one chosen value with rationale citing
     which reviewer prevailed. Zero conditional branches remain.
     Reviewer: Critic (C-5, MAJOR — autopilot-readiness).

3. **[Architect #3, MAJOR] Defer A2.1-β / state v3 to v0.3.**
   * Edit location: §A2.1 + §6 risk row + Appendix A acceptance gates.
   * Before: §A2.1 recommended A2.1-β (state v3) for v0.2; §8 ADR adopted
     A2.1-β; §B1-β said v0.2 = C3-only. Internal contradiction.
   * After: A2.1-α-temporary in v0.2 (kBypass = mute proxy, documented
     gap); A2.1-β + state v3 explicit v0.3 deliverable. C2B-Q2 cited.
     Reviewer: Architect (#3, MAJOR).

4. **[Architect #2, MAJOR] ADR 0011 (was 0008) hardening.**
   * Edit location: Appendix C ADR 0011 §Decision section.
   * Before: schema + 5s GC only; no locking, no EPIPE, no PID-liveness
     procedure.
   * After: atomic `tmpfile + rename(2)`, `flock(LOCK_EX)` advisory lock,
     `/proc/{pid}/comm` PID liveness, EPIPE handling at sidecar relay,
     `schema_version` field added. Reviewer: Architect (#2, MAJOR).

5. **[Architect §6 synthesis, MAJOR] C4 deliverable matrix.**
   * Edit location: new §1.4 "Release deliverable matrix" + §S2..§S8
     "DEFERRED to v0.3.0" markers + Appendix A v0.2/v0.3 split + ETA
     banner split.
   * Before: §9 ETA implied all S1..S8 in v0.2 sprint.
   * After: only S1 ships v0.2; S2..S8 = v0.3 deliverables. Sidecar
     binary explicitly NOT in v0.2 B3-β artifact. Reviewer: Architect
     (§6 synthesis, MAJOR).

6. **[Architect #6, MAJOR] Resolve Pre-mortem 3 ↔ §B1-β contradiction.**
   * Edit location: §B1 Why-not-γ + Pre-mortem 3 decision tree
     (lines ~463 + ~740-742).
   * Before: §B1 said "C3 is not a patch"; Pre-mortem 3 still proposed
     "cut v0.1.1 instead of v0.2.0 with C3 + manual + HOA decoder ONLY".
     Direct contradiction.
   * After: Pre-mortem 3 fallback = "ship v0.2.0 with affected param
     marked experimental"; v0.1.1 reserved strictly for true patch-only
     emergencies (compiler-emit security fix etc.). Reviewer: Architect
     (#6, MAJOR).

7. **[Architect #1 + carve-out, MAJOR] ADR 0010 §Context paragraph.**
   * Edit location: Appendix C ADR 0010 §Context.
   * Before: ADR did not address how plugin↔sidecar UDS relates to
     ADR 0003 OSC-over-UDP scope.
   * After: explicit one-paragraph carve-out citing ADR 0003 §Migration
     target ("shm + UDS for v1+") as the orthogonality basis. Reviewer:
     Architect (#1, MAJOR — downgraded from severity by Critic but
     applied per merged list).

8. **[C-2, MAJOR] Fix all OFF baseline filename references.**
   * Edit location: Principle 3 (line ~87), §S2 acceptance (line ~310),
     §S7 acceptance (line ~407), Appendix A A.4, Appendix B file list.
   * Before: 4 references to nonexistent `.ci/off-baseline-pins.txt`.
   * After: all replaced with the actual dual-gate
     `.ci/off_baseline.bytes.sha256` + `.ci/off_baseline.symbols.sha256`.
     Verified `grep -n 'off-baseline-pins'` returns empty.
     Reviewer: Critic (C-2, MAJOR — autopilot blocker).

9. **[C-4, MAJOR] R2 acceptance — explicit Korean manual targets.**
   * Edit location: §R2 (lines ~528-545).
   * Before: ambiguous "grep -rn '0.1.0' ... update as appropriate".
   * After: enumerates `docs/manual_kr/install/README.md:3` + `:397`,
     `docs/manual_kr/operation/README.md:3`. Cross-checks
     `core/src/ipc/ProtocolVersion.cpp` — schema_version stays at 1
     (verified `CURRENT_SCHEMA_VERSION = SCHEMA_VERSION; // == 1`).
     DONE gate = `grep -rn '버전: v0\.1' docs/manual_kr/` empty.
     Reviewer: Critic (C-4, MAJOR — first-customer brand impact).

10. **[C-3, MAJOR] ADR numbering collision.**
    * Edit location: §1.4 numbering note + Appendix C ADR section
      headers + §S1 Deliverables + §8 ADR header.
    * Before: plan added 0007/0008/0009 with two existing 0006-*.md
      files in `docs/adr/`.
    * After: new ADRs start at 0010 (binding model) / 0011 (discovery)
      / 0012 (vendor-quirk overlay). Older `0006-algorithm-runtime-swap.md`
      untouched. Reviewer: Critic (C-3, MAJOR — least-disruptive choice).

11. **[Architect #4 / #5 / #7 + Critic Pre-mortem 5, MAJOR/MINOR] Bundle.**
    * Edit location: ADR 0010 thread-budget invariant; §B3-β / §R5 / R4
      §호환성; §R3 acceptance table; new §Pre-mortem Scenario 5.
    * Before: thread topology silent; sidecar artifact mention vestigial;
      "automatable" non-falsifiable; GLIBC mismatch unmodeled.
    * After: ADR 0010 documents N≤8 ceiling, 2 threads/instance, single
      sidecar consumer drain; sidecar artifact dropped from B3-β
      (item 5 dissolves it); R3 acceptance table per-param falsifiable
      criterion (reuses `test_vst3_bypass`); Pre-mortem Scenario 5
      added with `spatial_engine_v0.2.0_linux_glibc239.tar.gz` asset
      naming + release-notes §호환성 explicit Built-on env.
    * Reviewers: Architect (#4 / #5 / #7) + Critic (Pre-mortem 5).

12. **[MINOR] Release notes format (Keep-a-Changelog mirror).**
    * Edit location: R1 + R4.
    * Before: R1 was "keep-a-changelog format" only by mention; R4 was
      6 freeform sections.
    * After: R1 produces `CHANGELOG.md` strict Keep-a-Changelog 1.1.0
      with Added/Changed/Deprecated/Removed/Fixed/Security headings.
      R4 mirrors the same headings + audience-targeted Highlights.
      Reviewer: Critic (§5, MINOR).

13. **[MINOR] Tag-vs-GHA authority + R5 release.yml.**
    * Edit location: R5 + R6.
    * Before: R5 added job to `vst3.yml`; R6 spelled command but didn't
      separate user-vs-CI authority.
    * After: R5 creates new `.github/workflows/release.yml` triggered
      by `tags: [v*]` using `softprops/action-gh-release@v2`. R6
      explicitly states user pushes tag (per `.claude/CLAUDE.md`),
      autopilot prepares command but does not run it. Asset naming
      includes GLIBC version per Pre-mortem 5. Reviewer: Critic (MINOR).

---
