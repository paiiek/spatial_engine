# Spatial Engine — Phase C2B Post-mortem Sprint Plan

> **Mode:** RALPLAN-DR SHORT (single-sprint, autopilot-eligible, no DAW dependency)
> **Predecessor:** `.omc/plans/spatial-engine-phaseC-C2-optionB.md` (M2 PASS @ commit `d1261f1`, GHA run #5 `ec2510d` SUCCESS)
> **ETA:** 1 – 1.5 day autopilot
> **Scope envelope:** all changes inside `if(SPATIAL_ENGINE_VST3)` — `core/` untouched; OFF byte-baseline (`.ci/off_baseline.{bytes,symbols}.sha256`) MUST stay green
> **Author:** planner agent (`a5a28ee3925a3a322`) on 2026-05-09
> **Revision:** Round-2 (architect APPROVE-WITH-ITERATE-LITE + critic APPROVE-WITH-ITERATE), 12 amendments A4..A12 applied 2026-05-09. See "Round-2 Changelog" appendix.
> **Synthesis path:** Option B (single-sprint, 7 sub-tasks, autopilot) — Option C "defer entire sprint" REJECTED by critic (violates user's explicit "this sprint then DAW confirm" flow per `.claude/CLAUDE.md` workflow policy).

---

## 0. Context

The M2 strictness gate passed but the critic produced an **ACCEPT-WITH-RESERVATIONS** verdict surfacing 4 MAJOR defects and 3 HIGH technical-debt items. All 7 are *autopilot-eligible* (no external host needed, validated by `ctest` + GHA). Cleaning them BEFORE handing the build to the user for Reaper/Bitwig hands-on confirmation maximises the success probability of the manual DAW step (which itself is a separate, non-autopilot task).

The ground problem clusters into three areas:

1. **Bypass semantics break SDK contract** — `vst3/SpatialEngineProcessor.cpp:124-133` hijacks `IComponent::setIoMode(kAdvanced)` as a bypass switch and `vst3/SpatialEngineProcessor.cpp:388-400` *silences* output instead of dry-pass-through. Both are violations of `pluginterfaces/vst/ivsteditcontroller.h:71` (`kIsBypass = 1 << 16`, *"only one allowed: plug-in can handle bypass"*) and the SDK's bypass-must-be-dry convention. Hosts that drive bypass via the standard `kIsBypass` automation parameter (Reaper, Bitwig, Studio One) currently see a non-bypass plug-in that cannot be muted via the UI bypass button at all.
2. **CI gates are incomplete vs the published plan** — `.omc/plans/spatial-engine-phaseC-C2-optionB.md` §6 Step 4.3 promised an `LD_DEBUG=libs` runtime sysdep gate; `.github/workflows/vst3.yml:52-57` only ships `ldd`-time check. RT-safety guard at `vst3/tests/test_vst3_dispatch_rt_safety.cpp:43-60` overrides only `operator new/new[]`, missing direct `malloc()` calls inside libstdc++ or syscalls (`futex`, `getrandom`, `clock_gettime`).
3. **Negative-path test coverage is thin** — `vst3/tests/test_vst3_state_persist.cpp` covers 12 cases but no multi-block / concurrent / version-rollover. `vst3/tests/test_vst3_bypass.cpp:171-181` (assertion 6) is trivially true (`r == kResultOk` of a setter, not the audio result).

This sprint discharges all 7 items in one autopilot pass.

---

## 1. Principles (5)

1. **State binary backward-compat is non-negotiable.** Any user that opened a DAW project saved with M2 v1 plug-in MUST round-trip cleanly on the post-sprint build. The 32-byte v1 format defined at `vst3/SpatialEngineProcessor.cpp:208-213` (`magic 'SPE1' u32 | version u16=1 | param_count u16=6 | 6 × float`) and mirror at `vst3/SpatialEngineController.cpp:281-285` is contractually frozen for *reads*; only the *write* side may emit a newer version once a real layout migration is required.
2. **OFF dual-gate baseline is invariant.** Any change must keep `.github/workflows/vst3.yml:97-103` (bytes diff) and `:99-103` (symbols diff) green without re-pinning `.ci/off_baseline.{bytes,symbols}.sha256`. Concretely: zero edits to `core/`, zero new transitive deps for the OFF target.
3. **Steinberg SDK conventions over local re-interpretation.** `kIsBypass = 1 << 16` (single param) per `pluginterfaces/vst/ivsteditcontroller.h:71` and bypass = dry pass-through (host expects audio continuity, *not* silence) per VST3 SDK Architecture §"Bypass" are the source of truth. Any "creative" mapping (current `setIoMode(kAdvanced)` hijack at `vst3/SpatialEngineProcessor.cpp:131`) is a defect, not a feature.
4. **RT-safety is a *measured* property, not a *believed* one.** Any path inside `process()` (`vst3/SpatialEngineProcessor.cpp:355-409`) must remain alloc=0 / mutex=0 *under instrumentation that catches direct `malloc`*, not just `operator new`. The 1000-iter loop at `vst3/tests/test_vst3_dispatch_rt_safety.cpp:214-228` stays the floor.
5. **CI duration budget is hard.** Current vst3 job wall-clock is ~3-5 min on `ubuntu-24.04`. Every added gate must fit in <30 s incremental cost (LD_DEBUG step + extended state-persist test + bypass test). Total post-sprint job <6 min.

---

## 2. Decision Drivers (top 3)

1. **DAW UX safety on the next manual step.** A Reaper/Bitwig session where the bypass button does the wrong thing (silence instead of dry) is a *visible* bug that tanks the user's confidence; fixing it before the manual confirmation step is the highest-leverage move in this sprint.
2. **Regression safety net before licensing/release activity.** With `.omc/plans/c2-licensing.md` and Phase D6 (real IIDs / GUI) coming, missing negative-path tests on state I/O and bypass become future defect amplifiers. Strengthen the net *now*, while context is fresh.
3. **CI-time non-bloat.** The current ~3-5 min cycle keeps developer feedback tight and enables the autopilot runtime budget; doubling it would erode the entire workflow. New gates must measure < 30 s incremental.

---

## 3. Viable Options (per decision point, ≥ 2 each)

### Decision A — state binary format migration when adding 7th `bypass` param

| Option | Description | Pros | Cons |
|---|---|---|---|
| **α (chosen)** v2-bump + v1-fallback | Writer emits `version=2`, `param_count=7`, 7 floats, total **36 bytes**. Reader detects `version==1 && param_count==6` → restore 6 floats + set `bypass=0.0` (off). Reader detects `version==2 && param_count==7` → restore 7 floats. Anything else → defaults. | Loss-less roundtrip in both directions; zero risk for v1 users; conservative; matches all 12 negative-path assertions in `test_vst3_state_persist.cpp` (Critic Minor #1 corrected count) with simple extension. | Adds 4 bytes to all new sessions; doubles getState/setState code paths; requires synchronous controller-side mirror update at `vst3/SpatialEngineController.cpp:281-285`. |
| β v1 stays + `param_count` bumped to 7 | Keep `kStateVersion=1`, change `kStateNParams=6→7`, total **36 bytes**. | Single code path. | **BREAKS** assertion 10 (`test_vst3_state_persist.cpp:191-209`) which currently treats `param_count=7` as "defaults retained". More importantly, breaks any user with v1 file saved by M2 (param_count mismatch → defaults). **Invalidated**: violates Principle 1. |
| γ Side-channel stream for bypass only | 6-float main stream + auxiliary "ext" stream containing the bypass float. | Zero diff to v1 layout. | VST3 `IComponent::getState` is *single* `IBStream`; no host-defined side channel exists. **Invalidated**: not implementable per `pluginterfaces/vst/ivstcomponent.h` `getState` signature (single `IBStream*`). |

**Conclusion:** option-α only viable choice; β/γ explicitly invalidated.

### Decision B — RT-safety malloc interposition mechanism

| Option | Description | Pros | Cons |
|---|---|---|---|
| **α (chosen, Round-2 A4)** Strong-symbol `malloc/free/calloc` override delegating to glibc internal `__libc_malloc` / `__libc_free` / `__libc_calloc` | Define `extern "C" void* malloc(size_t)` (and `free`, `calloc` per A9 narrowed scope) in `test_vst3_dispatch_rt_safety.cpp` (and the bypass test, via shared header). Forward to `__libc_malloc` declared as `extern "C" void* __libc_malloc(size_t);` directly — this is a documented glibc internal symbol exported since glibc 2.30, present in `ubuntu-24.04` runner's glibc 2.39 (`ldd --version`). **No `dlsym(RTLD_NEXT,...)` is used** — that path was Round-1 plan content and was removed in Round-2 A4 because libdl's own `dlsym` may transitively call malloc → SIGSEGV recursion. Thread-local guard increments counter when armed. | Catches direct libc `malloc/free/calloc` from libstdc++/syscalls; no extra .so to ship; works in CI without env-var dance; **no recursion risk** (no libdl involvement, no `call_once`, no warm-up needed since `__libc_malloc` is a leaf-level symbol resolved at link time); portable across glibc 2.30+ where `__libc_malloc` is exported as part of GLIBC_PRIVATE / standard internal API. | Requires glibc-specific symbol (NOT portable to musl/BSD — but CI is `ubuntu-24.04`-pinned per `.github/workflows/vst3.yml:30,67` so this is a non-issue); `__libc_malloc` is technically a GLIBC_PRIVATE symbol but stable since 2.30 (no breakage in 2.30→2.39 window). Fallback if glibc ever removes it: bootstrap-bump-allocator (8 KB static buffer used during warm-up). |
| β `LD_PRELOAD` external interposer .so | Build separate `librt_alloc_probe.so`, set `LD_PRELOAD` in `add_test()` env. | Cleaner separation; reusable across tests. | Adds a new build target + GHA env-var plumbing; conflicts risk with sanitizers; doubles ctest binary count. |
| γ `mallinfo()` delta sampling | Sample `mallinfo2().uordblks` before/after `process()`. | Zero ABI override. | `mallinfo` itself takes a libc lock and may itself allocate; not RT-thread safe; gives byte delta not call count → cannot detect zero-byte allocations from custom allocators. **Invalidated**: cannot satisfy "alloc==0" semantic (only "delta in heap usage = 0"). |

**Conclusion:** option-α primary, β documented as fallback if dlsym warm-up proves brittle. γ invalidated.

### Decision C — Bypass dry pass-through definition under cardinality mismatch

The plug-in advertises stereo-in / stereo-out (`vst3/SpatialEngineProcessor.cpp:144-170`, `bus.channelCount = 2`), so the canonical mapping is **stereo→stereo identity**. Edge case: future ambisonic outs (currently not exposed, but `engine_->audioBlock()` supports it via `ProcessDataAdapter`).

| Option | Description | Pros | Cons |
|---|---|---|---|
| **α (chosen)** ch-by-ch identity copy up to `min(numIn, numOut)`, remainder zeroed | For each `ch < min(inBus.numChannels, outBus.numChannels)` `memcpy(out[ch], in[ch], n*sizeof(float))`; for `ch >= min`, `memset(out[ch], 0, …)`. | Standard DAW expectation (Reaper, Bitwig, Studio One all do this); RT-safe (memcpy/memset both alloc-0); deterministic; covers current stereo→stereo trivially. | If future bus arrangement is mono-in/ambi-out, only ch0 carries dry; remaining HOA channels are silent (acceptable — bypass means "as if the plug-in were not present"). |
| β Broadcast input ch0 to all output channels | `memcpy(out[ch], in[0], …)` for all ch. | "Loud" bypass for mismatched cardinalities. | Wrong for stereo→stereo (mono-collapses L+R to L); diverges from every DAW convention. |
| γ Skip engine + leave output untouched (host pre-zeroed) | No memset, no memcpy; rely on host-supplied zeroed buffers. | Tiny code. | VST3 spec does NOT guarantee output buffer initial state (`pluginterfaces/vst/ivstaudioprocessor.h` `process()` doc: "audio buffers are not initialised"). Risk: stale buffer leaks into output → audible click. **Invalidated**: violates VST3 audio buffer contract. |

**Conclusion:** option-α only fully-spec-compliant choice. β kept only as documentation of why we did NOT broadcast.

---

## 4. Pre-mortem (3 scenarios)

### Scenario S1 — User upgrades from v1 plug-in with saved DAW project, gets defaults
**How it could happen:** version-detection branch in `setState` (option-α) has a subtle bug — e.g., reads `version` field at offset 4 but mis-uses `kStateBytes==32` constant for the v1 read while the buffer is 36 bytes from the v2 writer side, or the controller-side mirror at `vst3/SpatialEngineController.cpp:287-331` only handles v1 path and silently drops on v2.
**Mitigation:** test matrix at `vst3/tests/test_vst3_state_persist.cpp` adds the four cells:
- write-v1 → read-v2-capable (must round-trip 6 params, 7th → bypass=0)
- write-v2 → read-v2 (must round-trip 7 params)
- write-v2 → read-v1-only (impossible if both halves of plug-in upgrade together; document as out-of-scope)
- write-v1 controller-side, processor write-v2 (host loads project) → both halves end at same canonical 7-param state via dispatchParamChange fan-out at `vst3/SpatialEngineProcessor.cpp:434-503`

### Scenario S2 — Strong-symbol `malloc` override breaks sanitizer or breaks unrelated tests
**How it could happen:** `extern "C" void* malloc(size_t)` in option-α captures malloc *globally* in the linked test executable. If the same TU is linked with ASan (currently not on, but a future flag flip in `vst3/CMakeLists.txt`), ASan's own malloc shim conflicts. (Round-2 A4 update: the dlsym-recursion sub-scenario is now eliminated because the implementation uses `extern "C" void* __libc_malloc(size_t);` direct declaration — no libdl involvement, no recursion path. The Round-1 dlsym risk is no longer applicable.)
**Mitigation:** (i) gate the override behind a CMake option `SPATIAL_ENGINE_RT_PROBE=ON` (default ON, but switchable for ASan run); (ii) **NO `pthread_once` warm-up needed** — `__libc_malloc` is a leaf-level glibc-exported symbol resolved at link time, no init phase; (iii) document in the test file header (`test_vst3_dispatch_rt_safety.cpp:1-9`) the ASan-incompatibility AND the glibc 2.30+ requirement; (iv) negative-control assertion (Step S5) confirms the override is actually intercepting libc malloc/calloc, not a no-op. (v) LTO inline-defeat sanity probe per Critic Gap #5: see Risk R10.

### Scenario S3 — Bypass dry pass-through reveals upstream cardinality mismatch (stereo bus reported but host sends mono)
**How it could happen:** `vst3/SpatialEngineProcessor.cpp:155` advertises `channelCount = 2` but a host (e.g., Reaper sidechain) sends a mono input bus with `numChannels=1`. Current code `ch < outBus.numChannels` reads `inBus.channelBuffers32[ch]` for ch=1 → out-of-bounds.
**Mitigation:** option-α implementation strictly bounds via `min(inBus.numChannels, outBus.numChannels)` AND null-checks `inBus.channelBuffers32[ch]` per ch. Add a new bypass-test variant `bypass_cardinality_mismatch` that builds a `ProcessData` with `inBus.numChannels=1, outBus.numChannels=2` and asserts: out0==in0, out1 all-zero, no SIGSEGV.

---

## 5. Implementation Steps (7 sub-tasks, file:line precision)

> **Step ordering rationale:** S1 → S2 → S3 establishes the kIsBypass param + state migration + dry-pass impl as a coherent block; then S4–S5 strengthen CI gates around it; S6–S7 add the negative-path test coverage that validates everything.

### Step S1 — Add 7th `kIsBypass` param to controller

**Files & lines:**
- `vst3/SpatialEngineController.hpp:24` — change `static constexpr int kParamCount = 6;` → `7`
- `vst3/SpatialEngineController.hpp:32-39` — extend `enum ParamId` with `kBypass = 6` (kept here for type-safety even though enum lives in Processor.hpp; Controller side uses literal id `6`)
- `vst3/SpatialEngineController.hpp:80` — `param_infos_[kParamCount]` array auto-resizes
- `vst3/SpatialEngineController.cpp:107-201` — append a 7th block after Room Preset (line 200) with: `id = 6`, title "Bypass", shortTitle "Byp", units "", `stepCount = 1`, `defaultNormalizedValue = 0.0`, `flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass`, `norm_values_[6] = 0.0`
- `vst3/SpatialEngineController.cpp:391-407` — extend `getParamStringByValue` switch with `case 6: snprintf(buf,…, valueNormalized >= 0.5 ? "On" : "Off");`
- `vst3/SpatialEngineController.cpp:418-436` — extend `getParamValueByString` with `case 6: "On"→1.0, "Off"→0.0`
- `vst3/SpatialEngineController.cpp:443-453, 460-483` — extend `normalizedParamToPlain` / `plainParamToNormalized` with `case 6: return v >= 0.5 ? 1.0 : 0.0;`

**Acceptance criteria:**
- `getParameterCount()` returns 7 (verify in new ctest assertion)
- `getParameterInfo(6, info)` returns `kResultOk` and `info.flags & ParameterInfo::kIsBypass != 0`
- Existing 6 param IDs (0..5) keep IID/UID byte-for-byte identical; verified via `vst3/tests/test_vst3_param_layout.cpp` no regression
- `ctest -R vst3_param_layout` passes; new assertion `param_layout_bypass_is_kIsBypass` PASS

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_param_layout` exits 0 in <1 s.

---

### Step S2 — State binary format v2 with v1 fallback (Decision A option-α)

**Files & lines:**
- `vst3/SpatialEngineProcessor.cpp:208-213` — introduce parallel constants:
  ```
  kStateBytesV1   = 32
  kStateBytesV2   = 36   // 8 hdr + 7×4
  kStateMagic     = 0x31455053  (unchanged)
  kStateVersionV1 = 1
  kStateVersionV2 = 2
  kStateNParamsV1 = 6
  kStateNParamsV2 = 7
  ```
- `vst3/SpatialEngineProcessor.cpp:215-263` — rewrite `setState`:
  1. read header (8 bytes) → validate magic
  2. branch on `version`:
     - `==1 && nparams==6`: read 24 bytes (6 floats), restore `norm_values_[0..5]`, set `norm_values_[6].store(0.f)` (bypass off — v1 fallback default), dispatch params 0..5 to engine (bypass param 6 has no engine dispatch path; the audio branch in `process()` reads `norm_values_[6]` directly per Step S3)
     - `==2 && nparams==7`: read 28 bytes (7 floats), restore `norm_values_[0..6]`, dispatch params 0..5 to engine
     - else: defaults retained, log via stderr (control thread per Driver #2)
  3. **(Round-2 A7) Host-side automation cache refresh:** after successful setState, the controller half (NOT the processor — this responsibility lives in `vst3/SpatialEngineController.cpp::setComponentState`) MUST call `comp_handler_->restartComponent(Steinberg::Vst::kParamValuesChanged)` to instruct the host to refresh its parameter automation cache for all 7 params (including bypass). SDK references: `pluginterfaces/vst/ivsteditcontroller.h:85-103` (`enum RestartFlags { ..., kParamValuesChanged = 1 << 2, ... }`) and `:189` (`virtual tresult PLUGIN_API restartComponent (int32 flags) = 0`). Without this call, hosts that aggressively cache parameter automation (Reaper, Studio One) will display stale values for the bypass param after a project reload.
- `vst3/SpatialEngineProcessor.cpp:265-295` — rewrite `getState` to ALWAYS write v2 (36 bytes):
  - magic + `kStateVersionV2` + `kStateNParamsV2` + 7 floats from `norm_values_[0..6]`
- `vst3/SpatialEngineController.cpp:280-285, 287-331` — mirror change (Round-2 A5 explicit pattern): `kCtlStateBytes` is **NOT** a single read size. `setComponentState` MUST follow the same two-phase read pattern as the processor:
  1. **Phase 1: header read (8 bytes)** — `state->read(buf, 8, &numRead)`; validate `numRead == 8`; parse magic (offset 0, 4 bytes), version (offset 4, 2 bytes), nparams (offset 6, 2 bytes)
  2. **Phase 2: payload read branched on version**:
     - `version==1 && nparams==6`: `state->read(buf+8, 24, &numRead)`; validate `numRead == 24`; restore 6 floats; explicitly set `norm_values_[6] = 0.0` (bypass off default for v1 fallback)
     - `version==2 && nparams==7`: `state->read(buf+8, 28, &numRead)`; validate `numRead == 28`; restore 7 floats
     - else: defaults retained, `fprintf(stderr, ...)` log
  3. **Phase 3 (Round-2 A7):** at function exit (success path only), invoke `comp_handler_->restartComponent(Steinberg::Vst::kParamValuesChanged)` — see Step S2 final acceptance criterion below.
- Single-read of 36 bytes is REJECTED because v1 streams supply only 32 bytes → short-read fail desyncs controller from processor.

**Acceptance criteria:**
- New ctest assertion `state_v2_writeread_roundtrip`: write all 7 norms via setParam, getState → 36 bytes, setState fresh proc → all 7 norms recovered within 1e-5
- New ctest assertion `state_v1_to_v2_upgrade`: hand-craft 32-byte v1 buffer with kTestVals[0..5], setState → 6 norms recovered + `norm_values_[6]==0.f`
- **(Round-2 A5)** New ctest assertion `controller_setComponentState_v1_32byte_ok`: feed a 32-byte v1 stream to `controller.setComponentState()`; verify `controller.getParamNormalized(0..5)` matches v1 vals AND `controller.getParamNormalized(6) == 0.0` (v1 fallback bypass off)
- **(Round-2 A5)** New ctest assertion `controller_setComponentState_v2_36byte_ok`: feed a 36-byte v2 stream with `bypass=1.0`; verify `controller.getParamNormalized(6) == 1.0` and other 6 params correctly restored
- **(Round-2 A7)** New host-fixture-side acceptance: after v2 setState with bypass=1.0 saved, `IComponentHandler::restartComponent(kParamValuesChanged)` is invoked exactly once (verified by mock ComponentHandler in `test_vst3_host_fixture.cpp` counting calls); host-side `getParamNormalized(6) == 1.0` confirmed via fixture's controller proxy. SDK reference: `pluginterfaces/vst/ivsteditcontroller.h:103` (`kParamValuesChanged = 1 << 2`) and `:189` (`restartComponent` declaration)
- Existing assertion 10 (`test_vst3_state_persist.cpp:191-209` `param_count_mismatch_defaults`): MUST be UPDATED — old buffer with `version=1, nparams=7` is now an inconsistent state (v1 only allows nparams=6) and must still default-retain
- **(Round-2 A8)** Existing assertion 12 (`test_vst3_state_persist.cpp:239-252`, the `// Assertion 12: getState writes exactly 32 bytes` block, CHECK at `:251`) renamed `12_getState_32_bytes` → `12_getState_36_bytes`, CHECK literal `pos == 32` → `pos == 36`. Also bump `kStateBytes` constant in test file at `vst3/tests/test_vst3_state_persist.cpp:37` from `32` → `36` (or introduce parallel `kStateBytesV1=32`, `kStateBytesV2=36` for the v1↔v2 fixtures used by A6/A10/A11 assertions)
- All 12 (existing, with #12 renamed) + 2 (S2 new) + 2 (A5 new) + 1 (A7 host fixture) = 17 assertions PASS in <1 s — additional 2 assertions (A10 + A11 reduced concurrent) added in Step S6 for total 19. Aggregate test count specified per-step.

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_state_persist` exits 0; binary size delta <100 bytes.

---

### Step S3 — Bypass dry pass-through + remove setIoMode hijack (Decision C option-α)

**Files & lines:**
- `vst3/SpatialEngineProcessor.cpp:124-133` — strip the `bypass_active_.store(mode == kAdvanced…)` line; replace with `return kResultOk` only (or `kNotImplemented` if we want to be strict; use `kResultOk` to match SDK examples which accept any IoMode silently)
- `vst3/SpatialEngineProcessor.cpp:355-409` — in `process()`, REPLACE the bypass-driven path:
  - **Before (`:388-400`):** `if (bypass_active_.load(...)) { memset all output channels to 0; }`
  - **After:** `if (norm_values_[6].load(std::memory_order_acquire) >= 0.5f) { /* dry pass-through */ for (ch=0..min(in,out)-1) memcpy(out[ch], in[ch], n*sizeof(float)); for (ch=min..outNum-1) memset(out[ch], 0, n*sizeof(float)); }`
  - Null-guard `data.inputs && data.numInputs > 0 && inBus.channelBuffers32[ch]` per channel before memcpy (Pre-mortem S3)
- `vst3/SpatialEngineProcessor.cpp:434-503` — extend `dispatchParamChange` switch with `case kBypass: /* no engine-side action — bypass is checked directly in process() */ break;` (keeps the param "addressable" by hosts and round-trippable via state, but engine itself does nothing — the audio branch in process() handles it)
- `vst3/SpatialEngineProcessor.hpp:38` — append `kBypass = 6` to `enum ParamId`
- `vst3/SpatialEngineProcessor.hpp:100-102, 105` — `bypass_active_` atomic remains BUT becomes a **mirror** of `norm_values_[6]`. Decision: remove `bypass_active_` entirely; `process()` reads `norm_values_[6]` directly (one less atomic, one less invariant to maintain)
- `vst3/SpatialEngineProcessor.hpp:105` — `norm_values_[6]` → `norm_values_[7]` (size bump)

**Acceptance criteria:**
- `test_vst3_bypass.cpp` rewritten (see S7) — old assertion 4 (`setIoMode(kAdvanced)→bypass active`) replaced with `setParamNormalized via dispatched ParamID=6 → bypass active`
- New ctest assertion `bypass_dry_passthrough`: input `[0.5,-0.5,0.5,…]` for both ch, bypass on, process → output ch0/ch1 equal input bit-exactly
- New ctest assertion `bypass_off_engine_path`: bypass off, process → output NOT bit-equal to input (engine altered it)
- Cardinality test `bypass_cardinality_1in_2out`: inBus.numChannels=1, outBus.numChannels=2 → out0==in0, out1 all zero, no crash
- `LD_DEBUG=libs ./test_vst3_bypass` shows zero `libX11/freetype/alsa/webkit` lines (Driver #1 unaffected)
- RT-safety assertion 7 (`test_vst3_bypass.cpp:184-203`) passes: 1000-iter alternating bypass on/off, alloc==0 (now under both `operator new` AND `malloc` strong-symbol probe from S5)

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_bypass` exits 0 in <2 s.

---

### Step S3.5 (Round-2 A12) — OFF baseline local sanity checkpoint (between S3 commit and S4 push)

**Why:** Critic Gap #3 — Principle 2 ("OFF baseline invariant") was asserted by `if(SPATIAL_ENGINE_VST3)` source-tree partitioning, but transitive header includes (e.g., a public `core/` header that newly transitively pulls a vst3 header) could leak symbols into the OFF target unnoticed. Detecting drift in CI takes ~5 min (full `vst3.yml` cycle); detecting locally in seconds is a strict improvement.

**Files & lines:** No source edits. Add a manual checkpoint command (autopilot will execute it as a shell step between S3 commit and S4 push):
```
cmake -B build_off_check -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON
cmake --build build_off_check --target spe_core spatial_engine_core -j$(nproc)
sha256sum build_off_check/core/libspe_core.a build_off_check/core/spatial_engine_core \
  | sed 's|build_off_check/||g' > /tmp/off.bytes.local.sha256
diff /tmp/off.bytes.local.sha256 .ci/off_baseline.bytes.sha256 \
  || (echo "OFF byte baseline DRIFT detected post-S3 — diagnose before push"; exit 1)
```

**Acceptance criteria:**
- `diff` exits 0 (zero drift)
- Total checkpoint runtime < 60 s on `ubuntu-24.04` autopilot host (cold OFF build is ~30-45 s; sha256 < 1 s)
- If FAIL: autopilot MUST stop, diagnose which transitive include leaked, fix at source level (move guarded code to a vst3-only header), re-run S3.5 before proceeding to S4

**Verification:** local shell exit code 0; if non-zero, sprint blocks at S3.5 with explicit diagnostic.

---

### Step S4 — `LD_DEBUG=libs` CI gate (plan §6 Step 4.3 fulfilment)

**Files & lines:**
- `.github/workflows/vst3.yml:58-61` — insert a new step after "Run M2 B.2 host fixture + 6 ctest":
  ```
  - name: Runtime sysdep gate (LD_DEBUG=libs, Driver #1 belt-and-suspenders)
    run: |
      cd build_vst3_on
      LD_DEBUG=libs ./vst3/tests/test_vst3_host_fixture 2>&1 \
        | tee /tmp/ld_debug.out \
        | grep -cE 'libX11|libfreetype|libasound|libwebkit' > /tmp/sysdep_count.txt || echo 0 > /tmp/sysdep_count.txt
      n=$(cat /tmp/sysdep_count.txt)
      echo "sysdep transitive runtime hits: $n"
      test "$n" = "0" \
        || (echo "RUNTIME sysdep loaded — see /tmp/ld_debug.out"; head -50 /tmp/ld_debug.out; exit 1)
  ```
- Optional: also run `LD_DEBUG=libs ./vst3/tests/test_vst3_audio_smoke` and `LD_DEBUG=libs ./vst3/tests/test_vst3_bypass` for coverage of every entry point

**Acceptance criteria:**
- `n == 0` from the grep on host fixture invocation
- Step adds < 5 s wall-clock to the job (LD_DEBUG produces ~100 KB stdout; grep is O(n))
- Job still completes in < 6 min total

**Verification:** push commit → GHA `vst3-build-and-host-fixture` job green; manual local repro `LD_DEBUG=libs ./build_vst3_on/vst3/tests/test_vst3_host_fixture 2>&1 | grep -cE 'libX11|libfreetype|libasound|libwebkit'` returns `0`.

---

### Step S5 — RT-safety probe upgrade (Decision B option-α)

**Files & lines:**
- `vst3/tests/test_vst3_dispatch_rt_safety.cpp:25-60` — REPLACE the new/delete-only override block (Round-2 A4 + A9 narrowed scope: `malloc/free/calloc` ONLY, NOT `realloc/aligned_alloc/posix_memalign`):
  ```
  // Direct glibc-internal symbol declarations — no <dlfcn.h>, no dlsym, no recursion.
  // Stable in glibc 2.30..2.39 (CI is ubuntu-24.04 pinned, glibc 2.39).
  extern "C" void* __libc_malloc(size_t);
  extern "C" void  __libc_free(void*);
  extern "C" void* __libc_calloc(size_t, size_t);

  extern "C" void* malloc(size_t sz) {
      if (g_rt_guard_active) ++g_alloc_count;
      return __libc_malloc(sz);
  }
  extern "C" void  free(void* p) {
      __libc_free(p);
  }
  extern "C" void* calloc(size_t n, size_t sz) {
      if (g_rt_guard_active) ++g_alloc_count;
      return __libc_calloc(n, sz);
  }
  /* operator new/new[] retained for completeness, delegate to ::malloc which we now own */
  ```
- **NO warm-up needed** (Round-2 A4): `__libc_malloc` is leaf-level, no init phase. The Round-1 `:170-175` warm-up block is REMOVED.
- **NO `-ldl` link needed** (Round-2 A4): `__libc_*` symbols are part of libc itself. The Round-1 `${CMAKE_DL_LIBS}` line in `vst3/CMakeLists.txt` is NOT added.
- Apply the SAME probe TU to `vst3/tests/test_vst3_bypass.cpp` (de-dup via a small header `vst3/tests/rt_alloc_probe.hpp` that holds the override). The probe header is the only "new" file in this sprint.
- **(Round-2 A9 deferral note in code comment)** Add comment in `rt_alloc_probe.hpp`: `// realloc/aligned_alloc/posix_memalign NOT intercepted — verified zero hits in core/src by grep (Round-2 A9). Re-evaluate if introduced. See R10.`

**Acceptance criteria:**
- 1000-iter loop at `:214-228` still passes with `alloc_total == 0`
- **(Round-2 A9 NEW negative control for `malloc`)** assertion `probe_observes_malloc`: `g_rt_guard_active = true; void* p = std::malloc(64); std::free(p); g_rt_guard_active = false; assert(g_alloc_count == 1);` BEFORE the main 1000-iter loop confirms the override actually intercepts libc malloc (not a no-op due to LTO inline)
- **(Round-2 A9 NEW negative control for `calloc`)** assertion `probe_observes_calloc`: `g_rt_guard_active = true; void* p = std::calloc(1, 64); std::free(p); g_rt_guard_active = false; assert(g_alloc_count == 1);` confirms calloc interposition
- **(Round-2 R10 LTO probe)** post-build sanity: `nm build_vst3_on/vst3/tests/test_vst3_dispatch_rt_safety | grep -E '^[0-9a-f]+ T (malloc|free|calloc)$'` returns exactly 3 lines (3 strong symbols emitted, not inlined away). Add as a CMake `add_test()` shell step or as the test binary's preflight.
- Probe overhead < 5 % on the 1000-iter loop wall time (currently ~50 ms → <55 ms)

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_dispatch_rt_safety` exits 0; runtime <0.5 s.

---

### Step S6 — State I/O negative-path coverage extension

**Files & lines:**
- `vst3/tests/test_vst3_state_persist.cpp` — append assertions (current count 12, with #12 RENAMED per A8 to `12_getState_36_bytes`; new round-2 total = 19):
  - **(Round-2 A6 corrected)** `assertion_13_multi_block_roundtrip_fresh_instance`: **Two FRESH `SpatialEngineProcessor` instances**, both `setupProcessing()` + `setActive(true)` + `setState(same v2 buf)` + immediately `process(512 samples)` ×1 block (no warm-up), then compare the two 512-sample output buffers BIT-EQUAL (0 ULP). The fresh-instance ordering eliminates the VBAP `cache_slots_` (`core/src/render/VBAPRenderer.cpp:21,105`) state-residue interference flagged by Critic #3. Multiple-block extension is OUT OF SCOPE for assertion 13 (deferred to a future test that mocks the engine). Option (a) per Critic #3 chosen over (b) tolerance-1e-5 because semantic clarity matters: state restore must be deterministic, not approximate.
  - `assertion_14_oversize_stream_36plus_bytes`: hand-craft 64-byte buffer with valid v2 header but trailing garbage → setState reads first 36, ignores rest, no crash, all 7 norms restored
  - **(Round-2 A11 reduced)** `assertion_15_concurrent_set_get_state_1000iter`: spawn `std::thread` calling setState in a tight loop while main calls getState; after **1 000** round-trips (reduced from Round-1 10 000 per A11 to fit Driver 3 CI budget <2 s), no crash, no torn read (latest setState value visible in next getState). Uses `std::mutex` *outside* RT path; this is control-thread only, allocations permitted. Architect's "SDK forbids concurrent setState/getState" claim was retracted per A11 — `pluginterfaces/vst/ivstcomponent.h:188,191` setState/getState doc-comments do NOT specify thread-safety, so this stays as a defensive sanity test.
  - `assertion_16_v1_to_v2_backward`: write v1 32-byte buffer (kTestVals[0..5]) → setState v2-aware proc → getState writes 36 bytes (v2) → setState fresh v2 proc → 6 original norms preserved + bypass=0
  - `assertion_17_v2_to_v2_roundtrip`: 7 distinct test values, full round-trip
  - `assertion_18_v2_truncated_at_byte_28`: v2 header valid + 5 floats only (28 bytes total) → defaults retained, no partial restore
  - **(Round-2 A10 NEW)** `assertion_19_multicall_v2_then_v1_bypass_reset`: `setState(v2-buf with bypass=1.0)` → verify `norm_values_[6] == 1.0` → `setState(v1-buf)` → `getState` → verify `norm_values_[6] == 0.0` (v1 fallback resets bypass to off; idempotent across calls)

**Acceptance criteria:**
- 19/19 assertions PASS (12 existing with #12 renamed per A8, + 7 new: 13-fresh-instance, 14-oversize, 15-concurrent-1000iter, 16-v1-to-v2, 17-v2-to-v2, 18-truncated, 19-multicall-A10). Plus the 4 controller-side / host-fixture assertions from S2 (`controller_setComponentState_v1_32byte_ok`, `controller_setComponentState_v2_36byte_ok`, `state_v2_writeread_roundtrip`, `state_v1_to_v2_upgrade`, `host_fixture_restartComponent_called_once`) — those live in their own test files
- Concurrent test (assertion 15) runs **1 000** iterations (Round-2 A11 reduced from 10 000) on a 4-core CI runner in < 1 s without TSan complaints (TSan run is not in CI but the test must be safe per `std::memory_order` annotations)
- Total `vst3_state_persist` test runtime < 2 s (Driver 3 budget)

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_state_persist` exits 0.

---

### Step S7 — Bypass test strengthening + new kIsBypass round-trip

**Files & lines:**
- `vst3/tests/test_vst3_bypass.cpp:170-181` — REPLACE assertion 6 (`6_setIoMode_clear_bypass_ok`) which currently only checks `r == kResultOk` of the setter:
  ```
  // After: bypass off via param=6 set to 0.0 → process → output is NOT identity copy of input
  // (engine path active means engine modifies output)
  // Assertion: NOT all-equal-to-input AND NOT all-zero
  ```
- `vst3/tests/test_vst3_bypass.cpp` — INSERT new assertions (current 7 → 12):
  - `assertion_8_bypass_dry_identity_stereo`: bypass on, in0=ramp, in1=−ramp, process → out0==in0, out1==in1 byte-for-byte (0 ULP)
  - `assertion_9_bypass_off_engine_modifies_output`: bypass off, set non-zero pan_az, process → output differs from input (at least one sample differs by > 1e-3)
  - `assertion_10_bypass_param_setget_roundtrip`: drive via `IParameterChanges` (mock from `test_vst3_dispatch_rt_safety.cpp:114-153`) with paramId=6, value=1.0 → after `process()`, `bypass` is active (verified by next process having identity output)
  - `assertion_11_bypass_cardinality_1in_2out`: makeData with `inBus.numChannels=1`, out=2 → bypass on → out0==in0, out1 all-zero, no SIGSEGV, no out-of-bounds (run under valgrind in dev; CI just functional)
  - `assertion_12_bypass_state_persist`: setParam(6, 1.0) → getState → setState fresh proc → next process has identity output (round-trip integrates with S2)
- `vst3/CMakeLists.txt` — register `test_vst3_bypass` test name unchanged; just bump expected pass count in any harness that hard-codes it

**Acceptance criteria:**
- 12/12 assertions PASS
- `LD_DEBUG=libs ./test_vst3_bypass 2>&1 | grep -cE 'libX11|libfreetype|libasound|libwebkit' == 0`
- RT-safety assertion (current #7, renumber to #12) keeps `alloc_total == 0` over 1000 iter under the upgraded malloc probe from S5
- Total `vst3_bypass` runtime < 1 s

**Verification:** `cd build_vst3_on && ctest --output-on-failure -R vst3_bypass` exits 0.

---

### Cumulative verification (post-S1..S7)

| Gate | Command | Pass criterion |
|---|---|---|
| Local ctest full | `cd build_vst3_on && ctest --output-on-failure` | 53+ tests (post-S6/S7: 53 base + 12 new state + 5 new bypass = ~70), 100 % pass |
| **(Round-2 A12)** Local OFF baseline checkpoint (between S3 commit and S4 push) | `cmake -B build_off_check -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON && cmake --build build_off_check --target spe_core spatial_engine_core -j$(nproc) && sha256sum build_off_check/core/libspe_core.a build_off_check/core/spatial_engine_core \| sed 's\|build_off_check/\|\|g' \| diff - .ci/off_baseline.bytes.sha256` | exit 0 (zero drift), <60 s wall |
| OFF baseline byte (CI) | `cmake -B build_off -DSPATIAL_ENGINE_VST3=OFF -DSPATIAL_ENGINE_NO_JUCE=ON && cmake --build build_off --target spe_core spatial_engine_core -j$(nproc) && sha256sum …` | matches `.ci/off_baseline.bytes.sha256` exactly |
| OFF baseline sym (CI) | nm-pipeline | matches `.ci/off_baseline.symbols.sha256` exactly |
| GHA `vst3-build-and-host-fixture` | push to feature branch | green, < 6 min wall |
| GHA `off-byte-identical` | same push | green |
| LD_DEBUG runtime gate | new step S4 | sysdep count == 0 |
| RT-safety probe | `ctest -R vst3_dispatch_rt_safety` | alloc_total == 0 |
| **(Round-2 R10)** LTO inline-defeat probe | `nm build_vst3_on/vst3/tests/test_vst3_dispatch_rt_safety \| grep -E '^[0-9a-f]+ T (malloc\|free\|calloc)$' \| wc -l` | == 3 |
| **(Round-2 A9)** Probe negative controls | `ctest -R vst3_dispatch_rt_safety` includes `probe_observes_malloc` + `probe_observes_calloc` | both PASS (g_alloc_count == 1 each) |
| State persist extended | `ctest -R vst3_state_persist` | 19/19 (Round-2 A8 renamed #12 + 7 new) |
| **(Round-2 A5)** Controller v1/v2 split-read | `ctest -R vst3_state_persist` includes `controller_setComponentState_v1_32byte_ok` + `controller_setComponentState_v2_36byte_ok` | both PASS |
| **(Round-2 A7)** restartComponent host fixture | `ctest -R vst3_host_fixture` includes `host_fixture_restartComponent_called_once` | PASS |
| Bypass strengthened | `ctest -R vst3_bypass` | 12/12 |

---

## 6. Risks and Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | ~~dlsym recursion~~ **(Round-2 A4: REMOVED)** Original Round-1 risk eliminated by switching to `extern "C" void* __libc_malloc(size_t);` direct declaration. No libdl involvement → no recursion path. Risk slot retained for traceability; new content: `__libc_malloc` symbol unavailability on non-glibc libc | None on `ubuntu-24.04` (glibc 2.39); N/A on musl/BSD | Test fails to link on non-glibc | CI is `ubuntu-24.04`-pinned per `.github/workflows/vst3.yml:30,67`; documented in `rt_alloc_probe.hpp` header comment as "GLIBC ≥ 2.30 required" |
| R2 | GLIBC future bump removes `__libc_malloc` from public symbol table | Very low (stable since glibc 2.30, no deprecation notice through 2.39) | Test fails to link | Bootstrap-bump-allocator fallback documented in Decision B option-α Cons column; if glibc 2.40+ removes the symbol, autopilot lands the bootstrap variant in a follow-up plan |
| R3 | v1 → v2 state migration loses bypass=on for users who set it via the (defunct) `setIoMode` mechanism | Effectively none | Aesthetic | The defunct path was only triggered by hosts that called `setIoMode(kAdvanced)`, which (per Steinberg docs) no real DAW does. Old saved sessions all have `bypass=0` implicitly. |
| R4 | `LD_DEBUG=libs` step output exceeds GHA log line limit (~10 MB) | Low | CI log truncation, not failure | Pipe through `grep -cE` immediately, only count is retained for the gate; first 50 lines shown only on failure |
| R5 | Adding `kBypass=6` invalidates a host's parameter cache from M2 build | Low | Host shows "missing param" once, then re-enumerates | `.vstpreset` file format not used in M2 (no users to migrate); fresh enumeration on plug-in version bump is standard host behaviour; **(Round-2 A7)** `restartComponent(kParamValuesChanged)` call after setState ensures host caches refresh deterministically |
| R6 | Cardinality null-guard inside `process()` adds branches that hurt RT determinism | Negligible | <1 % timing | Guards are predictable branches (always taken in stereo→stereo case); no allocation; covered by 1000-iter probe |
| R7 | New strong-`malloc/free/calloc` override in test TU leaks into other tests sharing the same executable | None | N/A | Each `test_vst3_*` is a standalone executable per `vst3/CMakeLists.txt`; no cross-contamination possible |
| R8 | Concurrent setState/getState assertion 15 needs `<thread>` which pulls in libpthread → may bloat OFF baseline if header leaked | None | OFF re-pin churn | Test code lives under `vst3/tests/`, never compiled when `SPATIAL_ENGINE_VST3=OFF`; OFF target untouched. **(Round-2 A12)** Step S3.5 local OFF baseline checkpoint catches transitive leaks before push |
| R9 | `kIsBypass` flag co-existence with `kCanAutomate` may be rejected by very strict hosts | Very low | Host warns | SDK header `pluginterfaces/vst/ivsteditcontroller.h:71` explicitly documents `kIsBypass` as a flag *combinable* with `kCanAutomate`; behaviour matches Steinberg's own AGain example |
| **R10 (Round-2 A9 + Critic Gap #5 NEW)** | LTO inlines `malloc/free/calloc` overrides into call sites at link time, defeating the strong-symbol interposition; alloc counter stays at 0 even when allocations occur (false-pass) | Low (current `vst3/CMakeLists.txt` does NOT set `-flto` for tests; future enable possible) | RT-safety guard becomes a no-op silently | Three-layer defence: (i) post-build `nm` sanity check `nm build_vst3_on/vst3/tests/test_vst3_dispatch_rt_safety \| grep -E '^[0-9a-f]+ T (malloc\|free\|calloc)$'` returns 3 lines (added as preflight in test binary or as separate `add_test` shell step); (ii) negative-control assertions `probe_observes_malloc` + `probe_observes_calloc` (Round-2 A9) actively prove the override fires; (iii) document in `rt_alloc_probe.hpp` that test TUs MUST NOT enable LTO. Future `realloc/aligned_alloc/posix_memalign` introduction in `core/src` requires extending the override (zero hits today per Round-2 A9 grep — see Open Question C2B-Q6). |

---

## 7. ADR (Decision Record)

### Decision
Discharge all 7 critic-flagged defects in a single autopilot sprint by:
1. Adding 7th `kIsBypass` parameter with VST3 SDK-conformant flag and dry pass-through audio path
2. Migrating state binary format to v2 (36 bytes, version=2, param_count=7) with v1 read-back fallback
3. Removing the `setIoMode(kAdvanced) → bypass` hijack
4. Adding `LD_DEBUG=libs` runtime sysdep gate to GHA
5. Upgrading RT-safety probe to strong-symbol `malloc` interposition (catches libc-direct calls)
6. Extending state-persist tests from 12 → 18 assertions (multi-block, oversize, concurrent, v1↔v2)
7. Strengthening bypass tests from 7 → 12 assertions (dry-identity, cardinality mismatch, kIsBypass round-trip, state integration)

### Drivers
1. DAW UX safety on the next manual confirmation step (Reaper/Bitwig)
2. Regression net before licensing/release activity (Phase D6 + commercialization)
3. CI duration budget hard cap at <6 min total

### Alternatives considered (and why rejected)
- **State migration via param_count bump only** (Decision A option-β) — rejected: violates Principle 1, breaks v1 user round-trip
- **Side-channel state stream** (option-γ) — rejected: VST3 `IComponent::getState` is single `IBStream*` per `pluginterfaces/vst/ivstcomponent.h`, not implementable
- **`dlsym(RTLD_NEXT, "malloc")` + warm-up** (Round-1 Decision B option-α prior form) — REJECTED in Round-2 A4: libdl's own `dlsym` may transitively call malloc → SIGSEGV recursion. Replaced with `extern "C" void* __libc_malloc(size_t);` direct declaration (no libdl, no recursion path).
- **`LD_PRELOAD` external probe .so** (Decision B option-β) — kept as fallback if `__libc_malloc` ever becomes unavailable on the pinned runner; not chosen primary because of GHA env-var plumbing cost and `__libc_malloc` is simpler.
- **`mallinfo()` delta sampling** (option-γ) — rejected: cannot satisfy "alloc==0 calls" semantic, only byte-delta
- **Broadcast input ch0 to all output channels in bypass** (Decision C option-β) — rejected: wrong for stereo→stereo, diverges from DAW conventions
- **Untouched output buffer in bypass** (option-γ) — rejected: VST3 spec does NOT guarantee buffer initial state, audible click risk
- **(Round-2 A11 retracted Architect concern)** "VST3 SDK forbids host concurrent setState/getState" — Architect's claim was retracted upon SDK header review (`pluginterfaces/vst/ivstcomponent.h:188,191` setState/getState doc-comments do NOT specify thread-safety); concurrent assertion 15 retained as defensive sanity test (with iteration count reduced 10000 → 1000 per A11 to fit Driver 3 CI budget)
- **(Round-2 reaffirmed) Synthesis path Option C — defer entire sprint, ship M2 build to user as-is for DAW confirmation** — REJECTED twice: (i) Round-1 ADR rationale: critic ACCEPT-WITH-RESERVATIONS verdict gives explicit risk that DAW hands-on reveals one of 7 defects; (ii) Round-2 critic restated: violates user's explicit `.claude/CLAUDE.md` workflow policy ("ralplan → autopilot → DAW confirm" sequencing) — user's flow REQUIRES this sprint completes before manual DAW step. Single-sprint Option B is the only path consistent with declared workflow.
- **(Round-2 A9) Wide-scope override of `realloc/aligned_alloc/posix_memalign`** — narrowed to `malloc/free/calloc` ONLY: zero hits in `core/src` (Critic-verified grep), STL containers route through `operator new` (already overridden). Future re-evaluation tracked in Open Question C2B-Q6 and Risk R10.

### Why chosen
The chosen package:
- preserves OFF byte-baseline (no `core/` touch)
- preserves v1 state backward-compat (Decision A option-α)
- aligns with VST3 SDK convention (kIsBypass + dry-pass)
- *measurably* tightens RT-safety net (probe upgrade + new negative control)
- raises CI gate strictness without exceeding the time budget
- finishes the §6 Step 4.3 LD_DEBUG promise from the predecessor plan
- is fully autopilot-eligible (no external host, no manual step)

### Consequences

**Positive**
- Bypass button in real DAWs (Reaper, Bitwig, Studio One) works correctly on first manual test
- **(Round-2 A7)** Host parameter cache refresh via `restartComponent(kParamValuesChanged)` ensures saved bypass state is accurately reflected in DAW UI after project reload (no stale-cache UI bugs)
- State binary forward-compatible: future param additions can repeat the v2→v3 pattern without breaking older users
- RT-safety guarantees proven against direct libc `malloc/free/calloc` (Round-2 A9 narrowed scope), not just `operator new`; LTO inline-defeat probe (Round-2 R10) prevents silent regression
- **(Round-2 A12)** OFF baseline drift detected locally in <60 s instead of via 5-min CI cycle
- LD_DEBUG gate prevents future X11/freetype regressions at runtime, not just at link time
- Bypass test no longer has the trivially-passable assertion 6
- State persist test covers concurrent (1000-iter, A11), multi-block (fresh-instance, A6), and v2↔v1 multi-call (A10) scenarios
- (Round-2 A4) Probe code simpler: no `dlsym`, no `call_once`, no warm-up — direct symbol declaration → ~15 LOC instead of ~30

**Negative / costs**
- State binary grows by 4 bytes per session (negligible)
- Test build pulls in `<thread>` only (Round-2 A4 eliminated `-ldl` requirement; `__libc_malloc` is in libc itself)
- CI job wall-clock grows by ~5 s (well within budget); local autopilot run grows ~60 s for the new S3.5 OFF baseline checkpoint (Round-2 A12)
- Two atomic stores instead of one in bypass control path (`norm_values_[6]` only, vs prior `norm_values_[i]` + `bypass_active_`)
- (Round-2 A4) Test probe is glibc-only; non-glibc CI lanes would need the bootstrap-bump-allocator fallback (deferred until needed)

**Neutral**
- Param IID/UID byte layout for params 0..5 unchanged
- Controller class IID unchanged
- Plug-in factory layout unchanged

### Follow-ups (NOT in this sprint, explicit)
- **F1 — User DAW hands-on confirmation in Reaper + Bitwig + Studio One** (manual; not autopilot-eligible). After this sprint completes green, hand the build to the user. Acceptance: all three DAWs load the .vst3, scan returns "Spatial Engine", bypass button toggles dry/wet correctly, save/reload session preserves all 7 param values.
- **F2 — Phase D6 real IIDs + GUI** (separate plan). Replace DEV-prefix UIDs at `vst3/SpatialEngineProcessor.hpp:24-29` and `vst3/SpatialEngineController.hpp:17-22` with registered IIDs from Steinberg developer portal.
- **F3 — Commercialization branch** (see `.omc/plans/c2-licensing.md`).
- **F4 — TSan-mode CI lane** (separate plan if needed). Run `vst3_state_persist` assertion 15 (concurrent) under TSan to validate memory ordering claims at `vst3/SpatialEngineProcessor.cpp:380-381` and the new probe.

---

## Appendix A — File touch summary

| File | Sub-task | Change kind |
|---|---|---|
| `vst3/SpatialEngineController.hpp` | S1 | `kParamCount` 6→7; enum extended |
| `vst3/SpatialEngineController.cpp` | S1, S2, **S2-A5**, **S2-A7** | 7th param block; v1/v2 split-read fallback in `setComponentState` (header-first, branch-on-version); `restartComponent(kParamValuesChanged)` invocation at successful setState |
| `vst3/SpatialEngineProcessor.hpp` | S3 | `enum ParamId` += `kBypass`; `norm_values_[7]`; remove `bypass_active_` member |
| `vst3/SpatialEngineProcessor.cpp` | S2, S3 | state v1/v2 read+write; setIoMode hijack removed; bypass dry-pass in `process()`; `dispatchParamChange` += case kBypass |
| `vst3/tests/test_vst3_bypass.cpp` | S7 | rewrite assertion 6 + 5 new assertions; uses `rt_alloc_probe.hpp` |
| `vst3/tests/test_vst3_state_persist.cpp` | S6, **S6-A6**, **S6-A10**, **S6-A11**, **S6-A8** | 7 new assertions (13 fresh-instance, 14 oversize, 15 concurrent-1000iter, 16 v1↔v2 backward, 17 v2-roundtrip, 18 truncated, 19 multicall-bypass-reset); existing #12 renamed `12_getState_36_bytes`; `kStateBytes` 32→36 (or split to `kStateBytesV1/V2`) |
| `vst3/tests/test_vst3_host_fixture.cpp` | **S2-A7** | mock `IComponentHandler` counts `restartComponent(kParamValuesChanged)` calls; new assertion `host_fixture_restartComponent_called_once` |
| `vst3/tests/test_vst3_dispatch_rt_safety.cpp` | S5, **S5-A4**, **S5-A9** | strong-`malloc/free/calloc` override using `extern "C" __libc_malloc/__libc_free/__libc_calloc` (no dlsym, no warm-up); negative controls `probe_observes_malloc` + `probe_observes_calloc` |
| `vst3/tests/rt_alloc_probe.hpp` (new) | S5 | shared probe header for bypass + rt_safety tests; comment documents narrowed scope (Round-2 A9) and glibc 2.30+ requirement (Round-2 A4) |
| `vst3/CMakeLists.txt` | S5 | NO `${CMAKE_DL_LIBS}` (Round-2 A4 eliminated `-ldl` need); register new tests; document "DO NOT enable `-flto` on test TUs" per R10 |
| `.github/workflows/vst3.yml` | S4 | new "Runtime sysdep gate" step after host fixture |
| (no source edit) | **S3.5-A12** | autopilot shell step: local OFF baseline checkpoint between S3 and S4 |

**Net file count:** 10 modified (vs Round-1's 9, +`test_vst3_host_fixture.cpp` for A7), 1 added (`rt_alloc_probe.hpp`). Zero touches under `core/`.

---

## Appendix B — Sequencing for autopilot

```
S1 (kIsBypass param)         ──┐
                                ├── S2 (state v2 + A5 split-read + A7 restartComponent)
                                │     ── S3 (dry-pass + remove hijack)
                                │                                  │
                                │                                  ▼
                                │                          S3.5 (Round-2 A12: local OFF
                                │                              baseline checkpoint)
                                │                                  │
                                │                                  ▼
                                │                          S7 (bypass tests)
                                │
                                ▼
                       S5 (RT probe: A4 __libc_malloc + A9 calloc + R10 LTO probe)
                                                          │
                                                          ▼
                                                  S4 (LD_DEBUG CI gate)
                                                          │
                                                          ▼
                                                    S6 (state tests:
                                                       A6 fresh-instance,
                                                       A8 rename-12,
                                                       A10 multicall,
                                                       A11 concurrent-1000)
                                                          │
                                                          ▼
                                              local ctest + GHA push  →  green
```

Critical path: S1 → S2 → S3 → **S3.5 (mandatory blocking gate)** → S7 (interleaved with S5 → S4 → S6 in parallel). Autopilot can fan out S5/S6 to a parallel executor while the main S1-S3-S3.5-S7 chain proceeds, BUT **must merge before S4 push** because S3.5 fail blocks the entire pipeline.

---

## Appendix C — Open Questions (persisted to `.omc/plans/open-questions.md`)

- [ ] Should `bypass` parameter be `defaultNormalizedValue = 0.0` (off) or honour a host-provided default? — Affects how a host that loads the plug-in fresh sees it. Default `0.0` (off) matches Steinberg AGain example; recommend stay with `0.0`.
- [ ] On v2 state with future 8th param added in Phase D6, should reader accept v2 with `param_count=7` AND v3 with `param_count=8`? — Documents the version-ladder pattern; recommend yes (multi-version reader, single-version writer = newest).
- [ ] LD_DEBUG step pattern `'libX11|libfreetype|libasound|libwebkit'` mirrors the ldd-time pattern. Should it widen to also catch `libGL|libdrm|libxcb`? — Defensive, may catch headless-vs-X11 host-driven regressions; defer until after first GHA green.
- [ ] **(Round-2 A9 NEW — Q6)** When `core/src` introduces `realloc/aligned_alloc/posix_memalign` (currently zero hits), the `rt_alloc_probe.hpp` strong-symbol set must extend. Re-evaluation triggered by either (i) explicit grep regression in CI or (ii) RT-probe assertion firing on unexpected path. — Not blocking this sprint; tracked in Risk R10.

---

## Appendix D — Round-2 Changelog (architect APPROVE-WITH-ITERATE-LITE + critic APPROVE-WITH-ITERATE)

| ID | Source | Severity | Affected section | Round-1 content (rejected) | Round-2 content (applied) | Why |
|---|---|---|---|---|---|---|
| **A4** | Critic #1 | CRITICAL (autopilot blocker) | §3 Decision B option-α + §4 Pre-mortem S2 + §5 Step S5 + §7 ADR Alternatives + Appendix A | `dlsym(RTLD_NEXT, "malloc")` + `std::call_once` + warm-up `std::malloc(1)/free` + `${CMAKE_DL_LIBS}` link | `extern "C" void* __libc_malloc(size_t);` direct declaration + same for `__libc_free`/`__libc_calloc`; no dlsym, no warm-up, no `-ldl`; "GLIBC ≥ 2.30 required" doc | libdl's `dlsym` may transitively call malloc → SIGSEGV recursion. `__libc_*` are leaf-level glibc-exported, no recursion path. |
| **A5** | Critic #1 | MAJOR | §5 Step S2 (Files & lines for Controller) + Acceptance criteria | "kCtlStateBytes becomes a max (36); setComponentState reads header first, branches v1/v2, restores 6 or 7 floats" (vague) | Explicit two-phase pattern: Phase 1 read 8-byte header → validate magic → branch; Phase 2 read 24 (v1) or 28 (v2) bytes; new assertions `controller_setComponentState_v1_32byte_ok` + `controller_setComponentState_v2_36byte_ok` | A naive single 36-byte read fails on v1 32-byte streams (short-read), desyncing controller from processor. |
| **A6** | Critic #3 | MAJOR | §5 Step S6 assertion 13 | "process(512 samples) ×4 blocks → output bit-equal at 0 ULP" between original and restored on the SAME instance | "Two FRESH `SpatialEngineProcessor` instances, both setupProcessing+setActive+setState(same v2 buf), then process(512 samples) ×1 block, output bit-equal" — no instance reuse, no warm-up | VBAP `cache_slots_` (`core/src/render/VBAPRenderer.cpp:21,105`) carries time-varying state across blocks; same-instance bit-equal is unreachable. Fresh-instance ordering eliminates state residue. |
| **A7** | Critic #4 | MAJOR | §5 Step S2 dispatch policy + Risks R5 + ADR Consequences (positive) + Appendix A (host_fixture file-touch) | "dispatch params 0..5 (skip 6 since dispatch is no-op for bypass)" — silent host cache (Reaper, Studio One stale-display risk) | After successful setState, controller calls `comp_handler_->restartComponent(Steinberg::Vst::kParamValuesChanged)` per `pluginterfaces/vst/ivsteditcontroller.h:103,189`; new host-fixture assertion `host_fixture_restartComponent_called_once` | SDK convention: host caches param values; bypass param needs explicit cache-flush signal so DAW UI shows correct state after project reload. |
| **A8** | Critic #2 | MAJOR | §5 Step S2 (cite update) | Existing assertion 12 cite `:241-252` referencing `pos == 32` literal | Cite corrected to `:239-252` with CHECK at `:251`; assertion renamed `12_getState_32_bytes` → `12_getState_36_bytes`; literal `pos == 32` → `pos == 36`; `kStateBytes` constant at `:37` 32→36 (or split V1/V2) | Round-1 cite was off-by-1 and the rename was specified but not given file:line precision. |
| **A9** | Architect (narrowed by Critic) | major | §3 Decision B option-α scope + §5 Step S5 + ADR Alternatives + R10 + Appendix C C2B-Q6 | "malloc/free/calloc/realloc" override scope | Narrowed to `malloc/free/calloc` only (3 symbols, not 4+); `realloc/aligned_alloc/posix_memalign` deferred to R10 + Open Question C2B-Q6 (zero hits in `core/src` per Critic-verified grep); negative-control assertions added for both `malloc` and `calloc` | STL containers route allocations through `operator new` (already overridden); `realloc` family is unused, so wider scope is dead code that adds maintenance burden without coverage gain. |
| **A10** | Architect (ACCEPTED) | minor | §5 Step S6 assertion 19 NEW | (no Round-1 content — new Round-2 assertion) | `assertion_19_multicall_v2_then_v1_bypass_reset`: setState(v2 with bypass=1) → setState(v1) → getState → verify `norm_values_[6] == 0.0` (v1 fallback resets bypass) | Defends against the v1↔v2 idempotency edge case Architect raised. |
| **A11** | Architect (PARTIAL — claim retracted, count reduced) | minor | §5 Step S6 assertion 15 + ADR Alternatives | "10 000 iterations" + Architect's "SDK forbids concurrent setState/getState" framing | Iteration reduced to **1 000** (Driver 3 CI budget <2 s); Architect's SDK-forbidden claim REMOVED from plan (`pluginterfaces/vst/ivstcomponent.h:188,191` doc-comments are silent on thread-safety, claim was fact-erroneous); concurrent assertion retained as defensive sanity test | 10 000-iter exceeded CI budget; SDK header inspection invalidated the "forbidden" framing. |
| **A12** | Critic Gap #3 | major | §5 NEW Step S3.5 + Cumulative verification table + Risks R8 + Appendix A + Appendix B sequencing diagram | (no Round-1 content — Principle 2 was asserted by source-tree partitioning only, no checkpoint) | New mandatory autopilot shell step S3.5 between S3 commit and S4 push: `cmake -B build_off_check ... && diff sha256sum .ci/off_baseline.bytes.sha256` — fail blocks pipeline, requires source-level fix | Transitive header includes can leak symbols into OFF target unnoticed; CI cycle takes ~5 min, local checkpoint takes ~60 s — strict improvement. |
| **(Critic Minor #1)** | Critic | minor | §3 Decision A option-α prose | "matches all 11 negative-path assertions" | "matches all 12 negative-path assertions" — actual test file `vst3/tests/test_vst3_state_persist.cpp` has 12 (not 11) per assertion line headers `1-6/7/8/9/10/11/12` | Off-by-one count error. |
| **(Critic Gap #5 / R10)** | Critic | major | §6 Risks R10 NEW + Cumulative verification table | (no Round-1 content) | New R10: "LTO inlines malloc/free/calloc overrides → silent no-op". Three-layer defence: (i) post-build `nm` probe, (ii) negative-control assertions (A9), (iii) test TUs MUST NOT enable `-flto` documented in `rt_alloc_probe.hpp` | Without the LTO-defeat probe, the entire RT-safety guarantee can silently regress under future build flag changes. |
| **(Synthesis Path C reject reaffirmation)** | Critic | n/a | §header + §7 ADR Alternatives | (Round-1 had Option C in ADR alternatives) | Reaffirmed REJECT with explicit cite: `.claude/CLAUDE.md` workflow policy requires `ralplan → autopilot → DAW confirm` sequencing; Option C ("defer entire sprint") violates user's declared workflow | Critic flagged that Architect's hint at Option C contradicted the user's mandatory flow. |

**Round-2 net effect:** 12 amendments applied, 0 rejected. Plan is ready for autopilot execution. Next gate: Round-3 architect/critic review (per user note "Architect/Critic 다음 라운드 진입 예정").
