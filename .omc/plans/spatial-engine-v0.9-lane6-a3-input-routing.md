# A3 — Input→Object Audio Routing (channel mapping + per-route gain)

> **Status: APPROVED (execution-ready)** — ralplan consensus reached (Planner REV3 · Architect REV1 SOUND-with-amendments · Critic APPROVE). All amendments source-verified. Ready for `/oh-my-claudecode:autopilot`.
> v0.9 Lane 6 · gap audit 2026-06-08, category A, item A3 (largest / highest RT-risk of the three remaining gaps)
> Repo: `/home/seung/mmhoa/spatial_engine` · Plan artifact (durable, self-contained)
> Workflow: ralplan (Planner → Architect → Critic consensus) → autopilot

---

## 1. Problem statement (verified grounding — file:line)

Today the engine can route real input PCM into objects ONLY via `--object-source input`, with a **hardcoded 1:1 mapping** (object `i` ← input channel `i`), **no per-route gain, no remap, no fan-out**. It needs real channel mapping / gain staging: route arbitrary input channels to arbitrary objects, with per-route gain, allowing many-to-one fan-out.

Verified against the current tree (re-read before planning):

- **The gap site (render loop):** `core/src/core/SpatialEngine.cpp:760-791` inside `audioBlock`. The dry-source selector:
  ```cpp
  const bool use_input =
      object_source_input_.load(std::memory_order_relaxed)
      && block.input_channels != nullptr
      && block.input_channel_count > 0;          // :760-763
  ...
  if (use_input && i < block.input_channel_count
          && block.input_channels[i] != nullptr) {     // :772-773  ← HARDCODED i
      std::copy(block.input_channels[i],
                block.input_channels[i] + block.num_frames,
                scratch.begin());                        // :774-777  ← no gain
      continue;
  }
  // else: internal sine tone fallback                    // :780-790
  ```
  When `use_input` is true but input ch `i` is unavailable, the code **falls through to the sine tone**. That is the current fallback semantics and A3 must preserve it.

- **Source-select flag:** `SpatialEngine.h:561` `std::atomic<bool> object_source_input_{false}`; setter `SpatialEngine.h:223 setObjectSourceInput(bool)` / getter `:224`. CLI parse `core/src/bin/spatial_engine_core.cpp:373,406,504-507` (`--object-source sine|input`, default `sine`). Added by commit 40788ad.

- **Per-object state — `ObjCache`:** `SpatialEngine.h:415-426`. Fields: `az/el/dist/active/algo/gain_lin/reverb_send/k_hf/user_delay_ms/eq_gain_db[4]/width_rad`. Has a per-object scalar `gain_lin` (applied later in `PerObjectChain`) but **NO `input_src_ch` / `input_gain`**. `std::array<ObjCache, MAX_OBJECTS> obj_cache_{}` at `:427`. The per-object source signal lands in `dry_scratch_[i]` (`SpatialEngine.h:496`) → fed to `PerObjectChain` (gain ramp → EQ → delay → distance → HF → reverb send).

- **Command FIFO path (control thread → audio thread):** `core/src/util/CommandFifo.h:11-46` `struct QueuedCmd` (POD only — no `std::string` in the hot path). OSC `Command` → `QueuedCmd` translation + echo marks happen in `dispatchCommand` (`SpatialEngine.cpp:~40-340`, push at `:337`); the audio thread drains `cmd_fifo_` at the top of `audioBlock` (`SpatialEngine.cpp:594-754`) into `obj_cache_`. Model cases: `ObjGain` (`:611-613` / decode `CommandDecoder.cpp:328-330`), `ObjDsp` (`:683-695` apply / `:87-103` translate+echo / `CommandDecoder.cpp:557-571` decode).

- **CommandTag map:** `core/src/ipc/Command.h:25-93`. Object block: `ObjMove=0x01 … ObjName=0x09`. **Next free in the object block: `0x0A`.** Payload structs at `:98-261` (e.g. `PayloadObjDsp` `:247-261` is the `,iif obj_id param value` template).

- **Input backends + channel count:** `DanteBackend.h:69`, `SharedRingBackend.h:100`, `NullBackend` (`make_null_backend(sr, out_ch, block, input_channels)`) all expose `inputChannelCount()`, fixed at `prepareToPlay()/start()` before the first `audioBlock`. CLI null-backend inits input channels = output channels (`spatial_engine_core.cpp:594-597`). `block.input_channel_count` is the live count the render loop must bounds-check against.

- **RT constraint (critical):** `audioBlock` opens `SPE_RT_NO_ALLOC_SCOPE()` (`SpatialEngine.cpp:571`) → NO alloc/lock/syscall. Routing state MUST be fixed-size members (in `ObjCache`, already a fixed `std::array`), set via `cmd_fifo_` only; the render change must be bounds-checked index + multiply.

- **Echo plane:** `core/src/ipc/EchoSubscriber.h`. **Fixed 8-address scheme** (`kEchoAddrCount = 8`, `enum EchoAddr { Aed,Xyz,Gain,Mute,Active,Width,Name,Dsp }`, `:43-53`) with a per-object dirty bitmask. Adding a 9th echo address is **invasive** (bitmask width, `EchoState`, flush). `dispatchCommand` calls `osc_backend_.echoPlane().markGain/markDsp/...` per object mutation.

- **Scene persistence:** `core/src/ipc/SceneSnapshot.h:10-27` `ObjectSnapshot`. C6 just added `k_hf/user_delay_ms/eq_gain_db` to the struct **but NOT to `toJson/fromJson`** (carried only by the `/sys/state_request` dump). No routing fields exist.

- **Test injection path (verified):** `core/tests/core_unit/test_p_scene_obj_state_e2e.cpp:41-81` drives the production path: build `ipc::Command`, `engine.dispatchCommand(c)`, then `engine.audioBlock(block)` (input-less here). `test_p_audio_io_input.cpp:25-89` shows the NullBackend input-injection pattern (`block.input_channels`, `make_null_backend(... input_channels=1)`). Test-only const accessor idiom: `SpatialEngine.h:346-349 objCacheActiveAt()/objCacheSize()`.

- **No existing input routing matrix/bus/mixer.** Only OUTPUT-side buses (ambisonic SH, reverb wet, per-speaker gain/limit/delay).

- **Build knobs:** object cap `core/CMakeLists.txt:75-81` `SPATIAL_ENGINE_MAX_OBJECTS` ∈ {64,128} → `SPE_MAX_OBJECTS` (CI builds only 64; 128 is a manual reconfigure). RT-asserts `core/CMakeLists.txt:67-68` + top `CMakeLists.txt:43` `SPATIAL_ENGINE_RT_ASSERTS` → `SPE_RT_ASSERTS=1`.

---

## 2. RALPLAN-DR summary

**Mode:** DELIBERATE (highest-RT-risk gap; render-loop change inside `SPE_RT_NO_ALLOC_SCOPE`; cross-thread control→audio mutation).

### Principles
1. **RT contract is inviolable.** No alloc/lock/syscall added to `audioBlock`. Routing state is fixed-size `ObjCache` members set only via `cmd_fifo_`.
2. **Reuse the established idiom.** Mirror `ObjGain`/`ObjDsp` end-to-end (decoder → `Command`/Payload → `QueuedCmd` → drain → `obj_cache_`). No new config dialect, no new transport.
3. **Backward compatibility is a hard gate.** Unset routing MUST be byte-identical to today's `--object-source input` 1:1 and `--object-source sine` must be untouched.
4. **Minimal blast radius.** Add the routing primitive; defer echo/persistence unless consensus says otherwise (documented, not silently dropped).
5. **Every claim is test-backed.** Each acceptance criterion maps to a concrete unit/regression test on the NullBackend injection path; RT-safety is CI-proven under `SPE_RT_ASSERTS`.

### Top-3 decision drivers
1. **RT-safety of the render-loop change** (must stay alloc/lock/syscall-free; bounds-checked index + multiply only).
2. **Backward-compatible default** (the sentinel that means "behave exactly as 1:1 today").
3. **Control surface consistency** (runtime reconfig over the same OSC/cmd_fifo/per-object path the rest of the engine uses; naturally echoable/persistable later).

### Options (≥2, with bounded pros/cons)

**Option 1 — Static CLI routing table** (`--input-routing <file|list>` parsed at startup into a fixed matrix).
- Pros: simplest render change (index lookup); deterministic; no cross-thread mutation.
- Cons: no runtime reconfig; introduces a new config file/dialect to design+parse+test; inconsistent with the OSC-driven control surface; not echoable/persistable through existing planes.

**Option 2 — Runtime OSC per-object routing (RECOMMENDED).** New OSC command `/obj/input ,iif obj_id src_ch gain` → `CommandTag::ObjInput` (`0x0A`) → `QueuedCmd{input_src_ch,input_gain}` → drained into RT-safe per-object `ObjCache` fields (`int32_t input_src_ch`, `float input_gain`). Render loop reads `input_channels[src]*gain` where `src = (input_src_ch>=0)?input_src_ch:i`. Default sentinel `-1` ⇒ exact 1:1.
- Pros: reuses the `ObjGain`/`ObjDsp` idiom; runtime reconfig; RT-safe (pre-sized fixed state, set via `cmd_fifo_` only); naturally echoable (C7/C6 symmetry) and scene-persistable later; many-to-one fan-out falls out for free (independent reads of one src ch).
- Cons: must bounds-check `src_ch` against the **live** `block.input_channel_count` at render time; must precisely define out-of-range fallback and many-to-one semantics.

**Option 3 — Scene-persisted bus/stem config** (model routes as a persisted bus matrix in scene JSON).
- Pros: persistence built in.
- Cons: heavier; scene schema bump now; **still needs the Option-2 runtime apply path anyway** → Option 2 is the underlying primitive; persistence is an additive follow-up on top of it.

**Chosen: Option 2.** **Invalidation:** Option 1 is invalidated because it adds a parallel config dialect for a capability the OSC/cmd_fifo plane already expresses, and gives no runtime reconfig (the engine is live-controlled). Option 3 is invalidated as the *first* step because it is a superset of Option 2 (it needs Option 2's runtime path regardless); persistence is deferred to a follow-up that layers on top of the Option-2 fields. Option 2 is the minimal primitive that satisfies every acceptance criterion.

### The 7 design decisions — resolved (planner recommendation; Architect/Critic to ratify)

1. **State representation in `ObjCache` (RT). [REV2 amendment 1 — int32_t]** Add two fixed members to `ObjCache` (`SpatialEngine.h:415-426`):
   `int32_t input_src_ch = -1;` (sentinel `-1` ⇒ "use object index `i`" ⇒ exact current 1:1) and `float input_gain = 1.f;` (per-route linear input trim). **Use `int32_t`, NOT `int16_t`:** `int32(4)+float(4)=8` aligned is the SAME 8-byte widening, but eliminates the int16-wrap mis-route bug class (`src_ch=65536`→casts to `0`→silent route to ch0; `32768`→`-32768`→treated as default-`i`). `QueuedCmd` already carries `int32_t`, so the drain apply is a direct assign with **no narrowing cast**. Both default to the current behavior. They are part of the already-fixed `std::array<ObjCache,MAX_OBJECTS>` → **no new allocation**, and they widen `ObjCache` by 8 bytes (also widens `snap_buf_[3]` and the F4b copy by `3*MAX_OBJECTS*8` ≈ 3 KB @128 — still a fixed O(MAX_OBJECTS) copy, no behavior change). Set RT-safely by adding an `ObjInput` case to the `audioBlock` drain switch (`SpatialEngine.cpp:606-728`), mirroring `ObjGain`. **[REV3 NIT 3] `/sys/reset` clears routing for free:** the `SysReset` drain case does `obj_cache_.fill(ObjCache{})` (`SpatialEngine.cpp:620-622`), and the new members' in-class defaults (`-1` / `1.f`) mean a reset object reverts to the default 1:1 route with no extra code. AC5 / T-resync assert this.

2. **Render-loop change (`SpatialEngine.cpp:772-777`).** Replace the hardcoded read with:
   ```cpp
   const int src = (c.input_src_ch >= 0) ? c.input_src_ch : i;
   if (use_input && src < block.input_channel_count
           && block.input_channels[src] != nullptr) {
       const float g = c.input_gain;
       const float* in = block.input_channels[src];
       for (int n = 0; n < block.num_frames; ++n)
           scratch[(size_t)n] = in[n] * g;   // scaled copy (was std::copy)
       continue;
   }
   // else: fall through to the existing sine-tone path (UNCHANGED).
   ```
   - **Fallback (precise) [REV2 amendment 6 — RATIFIED: keep sine]:** out-of-range or null `src` ⇒ fall through to the **sine tone**, exactly as today (`use_input && i<count` else sine). Rationale: a single fallback path (not two) for backward-compat simplicity. Documented precisely: `input_channel_count` is **FIXED at prepare/start** for all backends (`prepareToPlay()/start()` set it before the first `audioBlock`), so the "input-count shrink → stale route" scenario is effectively **unreachable in-session**; the realistic out-of-range case is a STATIC misconfig (e.g. `src=99` against an 8-ch input). An out-of-range route therefore emits the object's **sine test tone AND silently ignores `input_gain`** (the gain multiply lives only on the in-range branch). AC3 asserts sine specifically.
   - **Many-to-one fan-out:** two objects with the same `src` are just two independent reads of one input pointer → correct and lock-free by construction. No extra handling.
   - RT-safety: only bounds-checked indexing + a multiply loop; the `std::copy` was already a loop-equivalent; no alloc/lock added.

3. **OSC command + wire format.** **Combined** `/obj/input ,iif obj_id src_ch gain` → `CommandTag::ObjInput = 0x0A` (next free object-block tag), `struct PayloadObjInput { uint32_t obj_id=0; int32_t src_ch=-1; float gain=1.f; }`. **Justification (combined vs split):** src_ch and gain form one logical route; a single message sets both atomically, avoiding a transient "new channel at old gain" half-state across two packets. `src_ch = -1` is the explicit "reset to default 1:1" value. **Validation:** decoder (`CommandDecoder.cpp`, new `else if (addr=="/obj/input")` near the `/obj/dsp` block `:557`) requires `,iif`; accepts `src_ch ≥ -1`; passes `gain` through unclamped (negative gain = legitimate phase invert, matching how ObjGain is not clamped). obj_id ≥ `MAX_OBJECTS` is dropped in the drain (`SpatialEngine.cpp:604`, existing guard). Final bounds (`src_ch < live input_channel_count`) are enforced at **render** time (decision 2), because the live channel count is unknown at decode time.

4. **Gain staging semantics.** `input_gain` is an **input trim applied at the source-copy stage** (decision 2), *before* `PerObjectChain`. It therefore **composes multiplicatively** with the existing per-object `gain_lin` (`PerObjectChain`, `p.gain_lin = c.gain_lin * transport_gain`): effective object level = `input * input_gain * gain_lin * transport`. They are **separate** controls (input trim vs object fader). **Units: linear on the wire**, matching `PayloadObjGain.gain` (linear). Default `1.0` = unity = no trim. **[REV2 amendment 5 — block-stepped, NOT ramped]:** `input_gain` is applied as a per-block scalar (it changes only at block boundaries when the drain runs). This is **consistent with `gain_lin`**, whose ramp is already configured with `ramp_samples=0` (`PerObjectChain.h:77`) ⇒ the existing per-object gain also steps at block boundaries. So `input_gain` introduces **NO new click class**. Do NOT ramp `input_gain` — ramping would be more elaborate than, and inconsistent with, `gain_lin`.

5. **Backward compat + default.** With no `/obj/input` ever sent: `input_src_ch == -1` ⇒ `src = i` and `input_gain == 1.0` ⇒ the scaled-copy loop is numerically identical to the old `std::copy` of `input_channels[i]`. `--object-source input` 1:1 behavior is **exactly preserved**; `--object-source sine` is untouched (`use_input` false ⇒ sine path, never reads the new fields). Regression test asserts this (AC4).

6. **Echo / observability (C7/C6 symmetry). [REV2 amendment 3 + 4 — split LIVE-echo from RESYNC-dump.]** These are **two distinct planes** and A3 treats them differently:
   - **LIVE per-change echo (`flush`, dirty-mask): DEFER** (ratified OK). The live echo plane is a *fixed* 8-address bitmask (`kEchoAddrCount=8`, `EchoSubscriber.h:43-53`) and a 9th `EchoAddr` would widen the dirty bitmask, `EchoState`, and flush. Recorded as **follow-up F-A3-echo**. **Chosen shape (amendment 4):** fold routing into the existing `Dsp` echo as new pseudo-params rather than a 9th `EchoAddr` — note this still needs `dsp_param_dirty` widened past 8 bits (p7=Width is already reserved), so it is cheaper-than-a-9th-address but **not free**. No live consumer reads routing today → safe to defer.
   - **C6 `/sys/state_request` RESYNC dump: DO IT NOW (NOT deferred).** **The earlier "8-address bitmask invasiveness" rationale was FACTUALLY WRONG for resync and is retracted:** `emitStateDump` (`EchoSubscriber.cpp:274+`) is **decoupled from the dirty bitmask** — it emits per-object addresses straight from `ObjectSnapshot`. A3 adds two AUTHORITATIVE per-object fields to `obj_cache_`; if the resync dump omits them it **silently breaks the documented C6 invariant "client reconciles EXACTLY to `obj_cache_`"** (`SpatialEngine.cpp:326,409`) — reopening, for routing, exactly the grounding-drift class C6 just closed. The fix is ~15 lines and bitmask-independent (see new **Step 6**): extend `ObjectSnapshot`, add routing to the touched predicate, emit one `/adm/obj/N/input ,if` per dumped object. This **keeps the C6 "reconciles EXACTLY" guarantee literally true.**

7. **Scene persistence (`toJson/fromJson`). DEFER** (ratified OK). Persisting routing across save/reload requires extending `ObjectSnapshot`'s JSON serialization (`toJson/fromJson` — a scene schema bump, which C6 deliberately avoided for its own additive fields `k_hf/user_delay_ms/eq_gain_db`). A3 adds the two fields to the `ObjectSnapshot` **struct** now (needed by the resync dump, Step 6) but — exactly like C6 — does **NOT** add them to `toJson/fromJson`, so scene files stay byte-identical. Full persistence is **follow-up F-A3-persist** (JSON keys in `toJson/fromJson` + rehydrate on `/scene/load`). No in-session correctness impact; consistent with C6's own deferral.

---

## 3. Implementation plan (step-by-step, exact files/functions)

### Step 1 — Wire types: tag + payload + QueuedCmd fields
- `core/src/ipc/Command.h`: add `ObjInput = 0x0A` to `enum class CommandTag` (object block, after `ObjName=0x09`, `:37`). Add `struct PayloadObjInput { uint32_t obj_id=0; int32_t src_ch=-1; float gain=1.f; };` near `PayloadObjDsp` (`:261`). Add `PayloadObjInput` to the `Command::payload` `std::variant` alternative list (find the `using ...variant<...>` near the payload structs).
- `core/src/util/CommandFifo.h`: add to `struct QueuedCmd` (`:11-46`): `int32_t input_src_ch = -1;` and `float input_gain = 1.f;`. (POD-safe; keeps `QueuedCmd` trivially copyable.)
- **Acceptance:** NO_JUCE build compiles; `Command` variant holds `PayloadObjInput`.

### Step 2 — Decoder: `/obj/input ,iif`
- `core/src/ipc/CommandDecoder.cpp`: add `else if (addr == "/obj/input")` adjacent to the `/obj/dsp` branch (`:557`). Require `,iif` (`getInt(0)=obj_id`, `getInt(1)=src_ch`, `getFloat(0)=gain`); require `src_ch >= -1` else `makeUnknown()`; build `PayloadObjInput`; set `cmd.tag = CommandTag::ObjInput`.
- **Acceptance:** a decode unit test (extend the decoder test suite) maps `/obj/input ,iif 3 5 2.0` → `ObjInput{obj_id=3,src_ch=5,gain=2.0}`; `/obj/input ,iif 0 -1 1.0` decodes (reset); malformed arity → `Unknown`.

### Step 3 — Translate `Command` → `QueuedCmd` (+ echo decision) in `dispatchCommand`
- `core/src/core/SpatialEngine.cpp` (the `dispatchCommand` translate switch, model on `ObjDsp` `:87-103`): add `case ipc::CommandTag::ObjInput:` → `std::get_if<PayloadObjInput>` → `qc.obj_id = p->obj_id; qc.input_src_ch = p->src_ch; qc.input_gain = p->gain;`. **No echo mark** (decision 6 — defer; leave a `// F-A3-echo: deferred` comment).
- **Acceptance:** dispatched `ObjInput` enqueues a `QueuedCmd` with the right fields (covered transitively by the Step-5 integration test).

### Step 4 — Drain into `obj_cache_` (RT-safe apply)
- `core/src/core/SpatialEngine.cpp` drain switch (`:606-728`): add **(REV2 amendment 1 — no narrowing cast)**
  ```cpp
  case ipc::CommandTag::ObjInput:
      c.input_src_ch = qc.input_src_ch;   // int32 ← int32, direct assign (no cast)
      c.input_gain   = qc.input_gain;
      break;
  ```
  Any successful pop already sets `cache_dirty = true` (`:603`) → F4b publish stays correct (the new fields ride along in the fixed `snap_buf_` copy).
- `core/src/core/SpatialEngine.h`: add `int32_t input_src_ch = -1;` and `float input_gain = 1.f;` to `struct ObjCache` (`:415-426`).
- **Acceptance:** after `dispatchCommand(ObjInput)` + one `audioBlock`, `obj_cache_[id]` holds the route (observed via the Step-6 test accessor).

### Step 5 — Render-loop change (the gap fix)
- `core/src/core/SpatialEngine.cpp:772-777`: implement decision 2 (compute `src` from `c.input_src_ch`, bounds-check vs `block.input_channel_count` — note `input_channel_count` is `int`/signed (`AudioCallback.h:24`), so `src < block.input_channel_count` is a clean signed compare with no signed/unsigned pitfall — scaled-copy `in[n]*g`, else fall through to sine). Keep `use_input` (`:760-763`) unchanged. Update the explanatory comment block (`:756-759`) to describe routing + per-route gain + the `-1`→`i` default.
- **Acceptance (REV2 amendment 2 — proven by STATE + RELATIVE-audio, not absolute dry values):** the route is set correctly (proven by `objInputRouteAt`, Step 7 AC1a) and the render reflects `input_channels[src]*input_gain` *relative to the identical per-object chain* — i.e. two objects sharing one `src` at gains 1.0 vs 2.0 produce an output **ratio ≈ 2.0** (Step 7 AC1b), and fan-out objects produce **equal** output (AC2). The default/unset path is byte-identical to old behavior via the `x*1.0f==x` identity (AC4); out-of-range `src` → sine (AC3). The earlier "assert `dry_scratch_==input*gain`" criterion is RETRACTED — `dry_scratch_` is overwritten in place by the per-object chain (`:817`), so an absolute equality is unsound (see Step 7).

### Step 6 — C6 `/sys/state_request` resync dump (F-A3-resync — DONE NOW, NOT deferred) [REV2 amendment 3]
Preserves the C6 invariant "client reconciles EXACTLY to `obj_cache_`" (`SpatialEngine.cpp:326,409`). ~15 lines, bitmask-independent (the resync dump emits straight from `ObjectSnapshot`, decoupled from the live dirty bitmask).
- `core/src/ipc/SceneSnapshot.h`: add `int32_t input_src_ch = -1;` and `float input_gain = 1.f;` to `struct ObjectSnapshot` (`:10-27`), as additive trailing fields exactly like C6's `k_hf/user_delay_ms/eq_gain_db`. **Do NOT add them to `toJson/fromJson`** → scene files stay byte-identical (F-A3-persist owns JSON later).
- `core/src/core/SpatialEngine.cpp` `snapshotObjects` (`:369-426`): (a) **[REV3 NIT 2]** extend the touched predicate by folding the routing terms INTO the existing `include_dsp_only` paren (`:415-418`) rather than adding a separate gated term — i.e. append `|| c.input_src_ch != -1 || c.input_gain != 1.f` inside `(include_dsp_only && ( … ))`, so a pure-routing object is dumped (and reconciles exactly). Functionally identical, cleaner. (b) populate the two new fields in the `ObjectSnapshot{...}` aggregate-init (`:420-425`) from `c.input_src_ch`/`c.input_gain` (append after the eq array, matching the struct's new trailing order). `/scene/save` (default `include_dsp_only=false`) is unchanged.
- `core/src/ipc/EchoSubscriber.cpp` `emitStateDump` (`:274+`): after the `dsp` emit loop, add one per-object packet **only when routing is non-default** (mirror the `!= default` guards used for `k_hf`/`reverb_send`):
  ```cpp
  if (o.input_src_ch != -1 || o.input_gain != 1.f) {
      std::snprintf(addr, sizeof(addr), "/adm/obj/%u/input", oid);
      int   src  = o.input_src_ch;
      float gain = o.input_gain;
      // Type-tag ",if" ⇒ WIRE order = int src_ch first, float gain second.
      // buildAndSend's C++ params are (float_array, n_float, int_array, n_int),
      // but it serializes per the type-tag string, so passing &gain as the float
      // arg and &src as the int arg yields wire [int src_ch][float gain] — the
      // SAME slot mapping as the dsp emit (int=param_id, float=value).
      buildAndSend(addr, ",if", &gain, 1, &src, 1, nullptr, now_ms, send_fd);
  }
  ```
  **Authoritative wire format [REV3 NIT 1]:** `/adm/obj/N/input ,if <src_ch:int> <gain:float>` — type-tag `,if` ⇒ first (int) slot = `src_ch`, second (float) slot = `gain`, identical slot mapping to the `dsp` emit at `EchoSubscriber.cpp:~300+` (`,if` = int `param_id`, float `value`). The earlier "gain-then-src / value-then-id" prose was BACKWARDS and is corrected here; the emit CODE was already correct (types disambiguate, so no runtime swap is possible). Keep this wire order EXACTLY.
- **Acceptance:** see Step 7 **T-resync**. The resync dump now reconciles exactly to `obj_cache_` including routing.

### Step 7 — Test-only accessors + unit/regression/RT tests [REV2 amendment 2]
**`objDrySampleAt` is RETRACTED as the primary hook** — `dry_scratch_` is overwritten IN PLACE by the per-object chain (`SpatialEngine.cpp:817`: PropagationDelay ~140-sample transient @48k, DistanceLPF settling, distance gain at default `dist=1`), so an ABSOLUTE assertion `==input*gain` is unsound (fails for low `k`, only approximately true in steady state). Use STATE + RELATIVE-audio assertions instead.
- `core/src/core/SpatialEngine.h`: add const test accessors near `objCacheActiveAt` (`:346-349`), same idiom:
  - **(primary state proof)** `struct InputRoute { int32_t src_ch; float gain; };` + `InputRoute objInputRouteAt(size_t obj) const noexcept { return (obj<obj_cache_.size()) ? InputRoute{obj_cache_[obj].input_src_ch, obj_cache_[obj].input_gain} : InputRoute{-1,1.f}; }` — proves decode→drain→cache directly.
  - **(audio tap)** an OUTPUT accessor for the relative-ratio tests; name it accurately (it reads POST-chain mix/output, NOT "dry"). Reuse the existing output path: the test owns `block.output_channels`, so read the rendered output buffers directly — **no new accessor needed for output**; only `objInputRouteAt` is added.
- New test `core/tests/core_unit/test_a3_input_routing.cpp` (model on `test_p_scene_obj_state_e2e.cpp` driveBlock + `test_p_audio_io_input.cpp` input injection), register in `core/tests/core_unit/CMakeLists.txt`. Drive blocks with `block.input_channels` set to known constant buffers (e.g. ch0=0.1, ch1=0.5, ch2=0.9), `engine.setObjectSourceInput(true)`, activate objects via `ObjMove` (give the relative-ratio objects IDENTICAL position/DSP so their chains are identical), then:
  - **AC1a remap+gain (STATE):** route obj0 ← src1 gain 2.0 (`/obj/input` via dispatchCommand) → after a block, `objInputRouteAt(0) == {1, 2.0f}`. *(primary proof.)*
  - **AC1b remap+gain (RELATIVE audio):** two objects at IDENTICAL position, both routed to the same `src`, one at gain 1.0 the other at gain 2.0 → at matched sample indices the gain-2.0 object's output / gain-1.0 object's output **≈ 2.0** (the identical chain cancels). Pick non-silent indices (skip the propagation-delay transient).
  - **AC2 fan-out (RELATIVE audio):** two objects at IDENTICAL position, both routed src1 gain 1.0 → outputs **EQUAL sample-for-sample** (independent reads of one input pointer).
  - **AC3 out-of-range fallback:** route obj0 ← src=99 (against a 3-ch input) → obj0's output is the **(non-constant) sine** path, distinguishable from a constant-fed reference object — assert obj0 output is non-constant across `n` AND differs from a same-position constant-input-routed reference. (Confirms sine fallback + that `input_gain` is ignored on the out-of-range branch.)
  - **AC4 backward-compat default (REGRESSION):** no `/obj/input`; `objInputRouteAt(i) == {-1, 1.0f}` for all `i`; and with `object-source input` + input chans set, obj_i output equals a reference engine WITHOUT the A3 change is impractical — instead assert obj_i routes from ch `i` (via the same relative method: obj fed ch0=0.1 vs obj fed ch1=0.5 at identical position → output ratio ≈ 0.5/0.1 = 5.0). The `x*1.0f==x` identity guarantees the default path is numerically unchanged.
  - **AC5 reset sentinel:** route obj0←src1 gain 2.0, then `/obj/input ,iif 0 -1 1.0` → `objInputRouteAt(0) == {-1, 1.0f}` (reverts to default `i`). **[REV3 NIT 3] also:** route several objects, fire `/sys/reset`, drive a block → `objInputRouteAt(i) == {-1, 1.0f}` for all `i` (the `obj_cache_.fill(ObjCache{})` path clears routing to defaults).
  - **T-resync (Step 6):** route an object (non-default `src`/`gain`), drive a block, fire `/sys/state_request`, and assert the emitted dump contains `/adm/obj/N/input ,if <src_ch:int> <gain:float>` with the **int slot == src_ch** and the **float slot == gain** carrying the set values (model on the existing C6 resync test `test_state_resync.cpp`). Also assert a pure-routing object (only routing changed) IS dumped (touched-predicate coverage), and that **after `/sys/reset` the object is NO LONGER dumped** (routing cleared to default, see Decision 1 / AC5).
- **Acceptance:** all assertions pass at `MAX_OBJECTS=64` and `=128`.

### Step 8 — CLI help text + docs
- `core/src/bin/spatial_engine_core.cpp:436-452`: add a one-line note that per-object input routing is set at runtime via `/obj/input ,iif obj_id src_ch gain` (no new CLI flag — runtime OSC only). No new startup flag in A3.
- `docs/` + `CHANGELOG.md`: publish the EXACT wire spec — **inbound** live control `/obj/input ,iif <obj_id:int> <src_ch:int> <gain:float>` and **outbound** resync echo `/adm/obj/N/input ,if <src_ch:int> <gain:float>` (Step 6) — plus gain-staging (block-stepped) semantics, the `-1` default/reset, the sine-on-out-of-range fallback, and the deferred follow-ups (F-A3-echo live-echo; F-A3-persist scene JSON). Note in the ADM-OSC / OSC reference doc that `/obj/input` is a native (non-ADM) `/obj/*` command alongside `/obj/gain` and `/obj/dsp`. (Inbound and outbound differ: inbound carries `obj_id` in the address-or-args per the `/obj/dsp` convention; outbound encodes `N` in the address.)
- **Acceptance:** `--help` shows the routing note; CHANGELOG + OSC reference updated; the C6 resync-invariant doc notes routing is now included.

### Step 9 — Optional concurrency gate (control-set vs audio-read)
- The `int32_t`+`float` write in the drain runs on the audio thread (drain) after a single-producer `cmd_fifo_` pop, and the render read is the **same** audio thread → no new cross-thread race on the routing fields themselves (they live in `obj_cache_`, audio-thread-owned; the F4b snapshot copy is already TSan-gated by `soak_scene_save_race`). **No new TSan gate required** (Architect ratified). If belt-and-suspenders is wanted, extend `soak_scene_save_race` to also mutate `/obj/input` concurrently and assert 0 races (reuses the existing Release-OR-RUN_SOAK TSan mechanism, cf. `state_resync_race` 4821d9a). Document the conclusion either way.

---

## 4. Test plan + pinned gate commands (NO hardware)

All gates run without audio hardware. Use **fresh build dirs** for each object-count.

```bash
# Gate 1 — NO_JUCE clean build + ctest @ MAX_OBJECTS=64 (CI default)
cmake -S /home/seung/mmhoa/spatial_engine/core -B /home/seung/mmhoa/spatial_engine/build_a3_64 \
      -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=64 -DCMAKE_BUILD_TYPE=Release
cmake --build /home/seung/mmhoa/spatial_engine/build_a3_64 -j"$(nproc)"
ctest --test-dir /home/seung/mmhoa/spatial_engine/build_a3_64 --output-on-failure

# Gate 2 — ctest @ MAX_OBJECTS=128 (manual reconfigure, FRESH dir)
cmake -S /home/seung/mmhoa/spatial_engine/core -B /home/seung/mmhoa/spatial_engine/build_a3_128 \
      -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128 -DCMAKE_BUILD_TYPE=Release
cmake --build /home/seung/mmhoa/spatial_engine/build_a3_128 -j"$(nproc)"
ctest --test-dir /home/seung/mmhoa/spatial_engine/build_a3_128 --output-on-failure

# Gate 3 — RT-asserts variant (proves audioBlock stays alloc/lock-free) FRESH dir
cmake -S /home/seung/mmhoa/spatial_engine/core -B /home/seung/mmhoa/spatial_engine/build_a3_rt \
      -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /home/seung/mmhoa/spatial_engine/build_a3_rt -j"$(nproc)"
ctest --test-dir /home/seung/mmhoa/spatial_engine/build_a3_rt --output-on-failure

# Gate 4 — Python suite
cd /home/seung/mmhoa/spatial_engine && python3 -m pytest

# Gate 5 (optional, only if Step-8 TSan extension is adopted)
#   build soak target with the existing Release-OR-RUN_SOAK TSan mechanism and
#   run soak_scene_save_race with concurrent /obj/input mutation → assert 0 races.
```

**Expanded test matrix (DELIBERATE):**
- **Unit:** decoder `/obj/input` (`,iif`, src_ch≥-1, arity/Unknown) — Step 2.
- **Integration:** AC1a/AC1b/AC2/AC3/AC4/AC5 (state + relative-audio) on the NullBackend injection path — Step 7, at 64 **and** 128.
- **Resync invariant:** T-resync — `/sys/state_request` dump carries `/adm/obj/N/input` and reconciles exactly to `obj_cache_` (Step 6/7).
- **Regression/e2e:** AC4 default `objInputRouteAt=={-1,1.0}` + `x*1.0f==x` numerical identity; confirm `--object-source sine` untouched (existing sine tests stay green).
- **RT/observability:** Gate 3 RT-asserts green; engine `/sys/metrics` overrun count unaffected; (optional) TSan Gate 5.

---

## 5. Pre-mortem (failure scenarios) + mitigations

1. **Silent backward-compat regression.** The scaled-copy loop differs numerically from `std::copy` only if `input_gain≠1` or `src≠i`; a bug in the `-1`→`i` sentinel could flip default routing. *Mitigation:* AC4 regression asserts byte-for-byte 1:1 with unset routing; AC5 asserts the reset sentinel; both at 64 and 128.
2. **RT contract break.** A careless apply (e.g. logging an out-of-range route, or a `std::vector` for the matrix) allocates on the audio thread. *Mitigation:* state is fixed `ObjCache` members only; no logging in the render path; Gate 3 RT-asserts is a hard gate; code review checks no new heap/lock in `audioBlock`.
3. **Out-of-range route → unexpected audio.** A static misconfig (`src=99` against an 8-ch input) routes an object out of range. *Mitigation (REV2 amendment 6):* `input_channel_count` is FIXED at prepare/start, so the dynamic "input-count shrink" path is effectively unreachable in-session; the realistic case is a static misconfig. Ratified behavior = fall through to the object's **sine** (single fallback path, audible = diagnosable), with `input_gain` ignored on that branch. AC3 asserts sine specifically; the Step-6 resync dump surfaces the effective `src_ch`/`gain` for observability.

4. **C6 resync invariant silently broken (the sharpest hole — now closed).** Adding authoritative routing fields to `obj_cache_` while omitting them from the `/sys/state_request` dump would reopen the C6 grounding-drift class. *Mitigation (REV2 amendment 3):* F-A3-resync is done NOW (Step 6) — `ObjectSnapshot` carries the fields, the touched predicate dumps pure-routing objects, and `emitStateDump` emits `/adm/obj/N/input`; T-resync proves the dump reconciles exactly to `obj_cache_`.

---

## 6. Risks + mitigations (summary)
- **R1 ObjCache widening** ripples into `snap_buf_[3]`/F4b copy size → still fixed O(MAX_OBJECTS), no behavior change; covered by existing F4b/soak gates.
- **R2 Decoder ambiguity** (`/obj/input` vs future `/obj/*`) → exact-string match, adjacent to `/obj/dsp`; unit-tested.
- **R3 Echo/persist asymmetry** (routing not surfaced while other obj state is) → **resync dump done now (Step 6)** so the C6 "reconciles EXACTLY" invariant holds; only LIVE per-change echo (F-A3-echo) and scene-JSON persistence (F-A3-persist) are deferred, each with no in-session correctness impact.
- **R4 Negative/extreme gain** → passed through (phase invert is valid, matches ObjGain); downstream `PerObjectChain` + output limiters already guard clipping.

---

## 7. ADR — A3 input→object routing

- **Decision:** Implement input→object routing as a **runtime OSC per-object command** `/obj/input ,iif obj_id src_ch gain` (`CommandTag::ObjInput=0x0A`), carried through the existing `cmd_fifo_` into two new RT-safe `ObjCache` fields (`int32_t input_src_ch=-1`, `float input_gain=1.f`), applied as a block-stepped input trim at the dry-source copy stage. The **C6 `/sys/state_request` resync dump is extended now** (`/adm/obj/N/input ,if`) to keep the "reconciles EXACTLY to obj_cache_" invariant. Only LIVE per-change echo and scene-JSON persistence are deferred to named follow-ups.
- **Drivers:** RT-safety of the render-loop change; backward-compatible default; control-surface consistency (runtime reconfig over the established OSC/per-object idiom); preservation of the C6 resync invariant.
- **Alternatives considered:** (1) static CLI routing table/file — rejected: new config dialect, no runtime reconfig, off the OSC plane; (3) scene-persisted bus matrix — rejected as first step: superset that still needs this runtime primitive; persistence layered as follow-up.
- **Why chosen:** Option 2 is the minimal primitive that satisfies every acceptance criterion, reuses `ObjGain`/`ObjDsp` end-to-end, stays alloc/lock-free, and is the substrate echo/persistence will build on.
- **Consequences:** `ObjCache`, `ObjectSnapshot`, and the F4b snapshot copy widen by 8 bytes/object; render loop does a scaled copy instead of `std::copy`; one new OSC address + tag + payload + QueuedCmd fields + one resync emit; default behavior byte-identical to today (`x*1.0f==x`); scene JSON byte-identical (routing not in `toJson/fromJson` yet).
- **Follow-ups:** **F-A3-echo** (LIVE per-change echo — fold routing into the `Dsp` echo as pseudo-params, needs `dsp_param_dirty` widened past 8 bits; no current consumer); **F-A3-persist** (routing in `toJson/fromJson` + `/scene/load` rehydrate); optional **F-A3-tsan** (concurrent `/obj/input` race gate on `soak_scene_save_race`). **F-A3-resync is NOT a follow-up — done in Step 6.**

---

## Consensus 진행 로그

### Architect (REV1)

**Verdict: SOUND-with-amendments.**

Grounding verified against tree @ current HEAD (paths are `core/src/core/...`, not `core/src/...` as the plan's §1 abbreviates):
- Render gap site `SpatialEngine.cpp:760-791` matches the plan's quoted 1:1 `std::copy`; `use_input` selector `:760-763`; in-place chain overwrite at `:817` (critical — see A2). Drain switch `:602-729`; `ObjGain` `:611`, `ObjDsp` `:683-695`; OOB `obj_id` guard `:604`; `cache_dirty` on every pop `:603`; F4b publish `:746-753`. `SPE_RT_NO_ALLOC_SCOPE()` `:571`.
- `ObjCache` `SpatialEngine.h:415-426` (ends `width_rad`), fixed `std::array<...,MAX_OBJECTS>` `:427`; `snap_buf_[3]` `:457`. `QueuedCmd` POD `CommandFifo.h:11-46` (trivially copyable; int32+float keeps it POD).
- `CommandTag` object block `Command.h:27-37` = 0x01..0x09; **0x0A confirmed FREE** (next used 0x10). Variant list `:318-363` (must add `PayloadObjInput`).
- Decoder `/obj/dsp` `CommandDecoder.cpp:557-571` uses `getInt(0)/getInt(1)/getFloat(0)` — new `/obj/input ,iif` mirrors it cleanly.
- `dispatchCommand` ObjDsp translate `SpatialEngine.cpp:87-103`.
- **`block.input_channel_count` is `int` (signed), `AudioCallback.h:24`** → `src < block.input_channel_count` is a clean signed compare; NO signed/unsigned pitfall. Bounds check correct vs the LIVE block.
- C6 resync: `SysStateRequest` handler `:307-333` → `snapshotObjects(...,true)` `:329` → `emitStateDump` `:330`. Touched predicate `:411-418` (NO routing). `emitStateDump` (`EchoSubscriber.cpp:274+`) emits per-object addresses DIRECTLY from `ObjectSnapshot` and does **NOT** touch the dirty bitmask. Live `flush` uses the bitmask + `dsp_param_dirty` submask (`EchoSubscriber.cpp:244-257`); `kEchoAddrCount=8` `EchoSubscriber.h:43`.

**RT-safety: SOUND.** Drain apply + render change add only bounds-checked indexing + a multiply loop; no alloc/lock/syscall. The `x*1.0f==x` identity makes AC4 byte-exact for the default path. `snap_buf_` widening is a fixed O(MAX_OBJECTS) copy (+3 KB @128); no new false-sharing (writer/reader touch disjoint claimed buffers). **TSan claim is CORRECT**: both writer (drain) and reader (render) of the routing fields are the SAME audio thread; cross-thread exposure is only the existing `snap_buf_` copy, already gated by `soak_scene_save_race`. No new TSan gate required; the optional concurrent-`/obj/input` extension is belt-and-suspenders only.

**Steelman antithesis (echo/resync deferral):** The plan lumps "F-A3-resync" with echo and justifies deferral by the 8-address bitmask invasiveness. **That rationale is factually wrong for resync.** `emitStateDump` is decoupled from the bitmask — it emits straight from `ObjectSnapshot`. A3 adds two AUTHORITATIVE per-object fields to `obj_cache_` that the C6 resync dump will silently omit, **quietly breaking the documented C6 invariant "client reconciles EXACTLY to obj_cache_" (`:326`, `:409`)** — i.e. A3 reopens, for routing, exactly the grounding-drift class C6 just closed. Cost to preserve it is ~15 lines and touches NO bitmask. This is the sharpest hole.

**Required amendments:**
1. **Use `int32_t input_src_ch` in `ObjCache` (NOT `int16_t`); drop the `static_cast<int16_t>` in the drain.** `int32(4)+float(4)=8` aligned = the SAME 8-byte widening the plan already accepts, but kills the int16 wrap class: `src_ch=65536` casts to `0` → silent mis-route to ch0; `32768`→`-32768`→ treated as default. `QueuedCmd` already carries `int32_t`. Strictly better, zero cost. (If int16 is kept for any reason, instead reject `src_ch>32767` at decode.)
2. **Fix AC1/AC2 test assertions — `objDrySampleAt` reads dry_scratch_ POST-chain (`:817` overwrites it in place).** Absolute `objDrySampleAt(0,k)==input*gain` is unsound: at default `dist=1` the chain applies PropagationDelay (~140 samp @48k transient), DistanceLPF settling, and distance gain — so the equality fails for low `k` and is only approximately true in steady-state. Replace with: (a) a `objInputRouteAt(obj)` STATE assertion (proves decode→drain→cache), and (b) RELATIVE audio assertions that cancel the identical per-object chain: AC1 = two objects same `src`, gain 1.0 vs 2.0 → output **ratio ≈ 2.0** at matched indices; AC2 fan-out = two objects same src/gain → outputs **equal sample-for-sample**; AC3 = routed-out-of-range object's output is the (non-constant) sine, ≠ a constant-fed reference. Keep AC4/AC5 but assert via `objInputRouteAt` + the ratio method.
3. **Correct the resync deferral and preserve the C6 "reconciles EXACTLY" invariant.** Strongly recommend doing F-A3-resync NOW (cheap, bitmask-independent): `ObjectSnapshot += {input_src_ch, input_gain}`, add routing to the touched predicate (`:411-418`), and one `buildAndSend("/adm/obj/%u/input", ",if", ...)` in `emitStateDump`. If still deferred, the plan MUST (a) delete the false "8-address bitmask" rationale for resync, (b) explicitly document that after A3 the resync dump no longer reconciles exactly to `obj_cache_` (a NAMED regression of the C6 invariant), and (c) bundle the touched-predicate fix with the resync follow-up.

**Recommended (non-blocking) amendments:**
4. **Live echo deferral is fine** (genuinely invasive; no current subscriber consumes routing). Prefer the "fold into Dsp echo" shape over a 9th `EchoAddr` — but note it still needs `dsp_param_dirty` widened past 8 bits (p7=width reserved), so it is cheaper-than-a-9th-address but not free. Document the chosen F-A3-echo shape.
5. **Document `input_gain` as a block-stepped trim, consistent with `gain_lin`.** `PerObjectChain.h:77` sets the gain ramp with `ramp_samples=0` → the EXISTING per-object gain already steps at block boundaries. So the plan's tradeoff (ii) "input_gain could click" is true in absolute terms but introduces NO new click class. Do NOT ramp input_gain (it would be MORE elaborate than, and inconsistent with, gain_lin). Just document block-step semantics. Resolves (ii).
6. **Sine fallback for an EXPLICIT out-of-range route: keep it, but document precisely.** `input_channel_count` is FIXED at prepare/start for all backends → the plan's "input-count shrink → stale route" scenario is effectively unreachable in-session; the realistic case is a STATIC misconfig (`src=99`, 8 ch). Keep the single sine path (one fallback, not two) for backward-compat simplicity, but document that an out-of-range route emits the object's sine test tone AND silently ignores input_gain. AC3 must assert sine specifically. Resolves (i) — sine OK on simplicity grounds, not silence.

**Other design points ratified:** combined `,iif` (atomic route+gain) is acceptable — the split-`/obj/input/gain` alternative only helps if input_gain is automated/swept, which is not a stated use; note it but no change required. `-1` sentinel is idiomatic (unambiguous with int32). Many-to-one fan-out correct by construction (independent reads of one pointer).

**Single biggest risk:** Amendment 3 — A3 silently breaks the C6 "resync reconciles EXACTLY to obj_cache_" invariant by adding an authoritative field the resync dump omits, and the plan's stated reason to defer the fix (echo bitmask cost) does not actually apply to resync (which is bitmask-decoupled). Secondary: Amendment 2 — the unsound absolute test assertion would either fail spuriously or get "fixed" into a tautology.

**Deferral ruling:**
- **Echo (live dirty-mask): OK to defer.** Genuinely invasive; no consumer. Prefer Dsp-pseudo-param shape; name the follow-up.
- **Resync (`/sys/state_request`): NOT OK to defer on the plan's stated rationale.** Either do it now (recommended, ~15 lines, bitmask-independent) OR defer only with corrected rationale + an explicitly documented C6-invariant regression + the touched-predicate fix bundled.
- **Persistence (scene `toJson/fromJson`): OK to defer.** Consistent with C6's own deferral of `toJson/fromJson` for its additive fields; no in-session correctness impact.

With amendments 1-3 applied (4-6 are documentation/follow-up hardening), the design is RT-sound, backward-compatible, and ready for autopilot.

### Planner (REV2)

All six amendments applied in place. Summary of edits:

1. **[amend 1 — int32_t] APPLIED.** `ObjCache.input_src_ch` is now `int32_t` (Decision 1, Option-2 blurb, Step 4, ADR, Step 9). Drain apply is a direct `int32 ← int32` assign with the `static_cast<int16_t>` removed — kills the wrap mis-route class at zero cost (`QueuedCmd` already int32).
2. **[amend 2 — unsound test] APPLIED.** `objDrySampleAt` retracted as the primary hook (dry_scratch_ is overwritten post-chain at `:817`). Step 7 now proves routing via (a) `objInputRouteAt(obj)` STATE accessor `{src_ch,gain}`, and (b) RELATIVE-audio assertions that cancel the identical per-object chain: AC1b = ratio ≈ 2.0 (gain 1.0 vs 2.0, same src/position), AC2 = fan-out outputs EQUAL, AC3 = out-of-range → non-constant sine ≠ constant-fed reference, AC4 = default route ratio (ch0 vs ch1) + `objInputRouteAt=={-1,1.0}`, AC5 = reset via `objInputRouteAt`. Output is read from the test-owned `block.output_channels` (no mislabeled "dry" accessor). Decision-2 and Step-5 acceptance lines updated to match.
3. **[amend 3 — resync invariant, THE biggest risk] APPLIED — doing F-A3-resync NOW.** The false "8-address bitmask invasiveness" rationale for resync is retracted (Decision 6). New **Step 6** extends `ObjectSnapshot` (additive trailing fields, NOT in `toJson/fromJson`), adds routing to the `include_dsp_only` touched predicate (`:411-418`), and emits one `/adm/obj/N/input ,if <src_ch:int> <gain:float>` per non-default object in `emitStateDump` (slot mapping mirrors the `dsp` emit exactly; see REV3 NIT 1 for the corrected wire order). T-resync added (Step 7). This keeps the C6 "reconciles EXACTLY to obj_cache_" invariant literally true. Pre-mortem #4 + R3 updated.
4. **[amend 4 — echo shape] APPLIED (doc).** LIVE per-change echo stays deferred as F-A3-echo with the **chosen shape = fold into the `Dsp` echo** (not a 9th `EchoAddr`), noting it still needs `dsp_param_dirty` widened past 8 bits. Distinct from the resync dump (amend 3, done now).
5. **[amend 5 — block-stepped gain] APPLIED (doc).** Decision 4 now documents `input_gain` as a per-block scalar, consistent with `gain_lin` (ramp `ramp_samples=0`, `PerObjectChain.h:77`) → no new click class; explicitly NOT ramped.
6. **[amend 6 — sine fallback] APPLIED (doc).** Decision 2 + pre-mortem #3 document `input_channel_count` is FIXED at prepare/start (shrink scenario unreachable; realistic case = static misconfig), keep the single sine fallback path, and note `input_gain` is ignored on the out-of-range branch. AC3 asserts sine.

Also folded in Architect's verified grounding: `block.input_channel_count` is signed `int` (`AudioCallback.h:24`) → clean signed bounds compare (noted in Step 5); `0x0A` confirmed free; variant list at `Command.h:318-363`.

Core design unchanged (Option 2). Steps renumbered: resync inserted as Step 6; tests→7, docs→8, optional-TSan→9. Ready for Critic.

### Critic (REV2)

**Verdict: APPROVE** (Critic gate iteration 1/5 for A3). Consensus reached: Planner REV1 → Architect REV1 (SOUND-with-amendments) → Planner REV2 (all 6 applied) → Critic REV2 APPROVE. Ready for autopilot. Non-blocking nits below.

Grounding independently re-verified against HEAD (1306409):
- **RT render change (SpatialEngine.cpp:760-791).** `use_input` selector `:760-763`; hardcoded 1:1 `std::copy` `:772-778`; in-place chain overwrite `:817`. The scaled-copy `in[n]*g` adds only a bounds-checked index + multiply — no alloc/lock/syscall inside `SPE_RT_NO_ALLOC_SCOPE()` (`:571`). `block.input_channel_count` is signed `int` (`AudioCallback.h:24`) → `src < count` is a clean signed compare, no signed/unsigned UB. Default path `src=-1→i`, `gain=1.0`: `in[n]*1.0f` is IEEE-exact for finite audio ⇒ byte-identical to today's `std::copy`. **SOUND.**
- **Drain (`:602-728`).** `ObjInput` case mirrors `ObjGain` `:611`; `int32←int32` direct assign, no narrowing (amend 1 correct — `QueuedCmd` carries int32, `CommandFifo.h:39` precedent). OOB obj_id guard `:604`, `cache_dirty` every pop `:603` (F4b publish rides along). `SysReset` `:621` `obj_cache_.fill(ObjCache{})` auto-clears the new fields via their default member-inits (`-1`/`1.f`) — correct, though plan doesn't call it out (nit 3).
- **Amend 3 (resync) — verified COMPLETE & correct.** `emitStateDump` (`EchoSubscriber.cpp:274-344`) emits straight from `ObjectSnapshot`, fully decoupled from the dirty bitmask — the deferral rationale was indeed wrong and is correctly retracted. (a) `ObjectSnapshot` (`SceneSnapshot.h:10-27`) gains two additive trailing fields NOT in toJson/fromJson → scene byte-identical (exactly the C6 `k_hf`/`user_delay_ms`/`eq_gain_db` pattern). (b) Touched predicate (`SpatialEngine.cpp:411-418`) routing clause is gated on `include_dsp_only` ⇒ pure-routing object dumped on resync, scene-save (`include_dsp_only=false`) unchanged. (c) Aggregate-init `:420-425` append order (after eq array) matches the new struct trailing order. The "reconciles EXACTLY to obj_cache_" C6 invariant is literally restored for routing.
- **Amend 2 (tests) — sound, non-hollow.** `objDrySampleAt` correctly retracted (`:817` overwrites `dry_scratch_` in place — PropDelay transient/DistanceLPF make an absolute equality unsound). `objInputRouteAt` STATE accessor proves decode→drain→cache directly (cannot pass a broken decoder/drain). RELATIVE ratio/equal-sample audio assertions cancel the identical per-object chain ⇒ independent of absolute post-chain values, and the test reads test-owned `block.output_channels` (right tap, no mislabeled "dry" accessor). AC3 sine, AC4 default route + `{-1,1.0}`, AC5 reset — none pass vacuously against a broken impl.
- **Tag/decoder/variant.** `0x0A` confirmed free (`Command.h`: ObjName=0x09 → next used SysHandshake=0x10). `/obj/input ,iif` mirrors `/obj/dsp` decode (`CommandDecoder.cpp:557-571`: getInt(0)/getInt(1)/getFloat(0)); `src_ch≥-1` validation + unclamped gain (consistent with ObjGain) sound. Variant list `:318-363` must gain `PayloadObjInput` (plan says so). `QueuedCmd` stays trivially copyable.
- **Amend 5 (block-step gain).** `PerObjectChain` `applyParams` sets `gain_ramp_.setTarget(p.gain_lin, 0)` (ramp_samples=0) → existing per-object gain already steps at block boundaries; `input_gain` introduces no new click class. Correct.
- **Backward-compat / RT gate / TSan.** Routing fields are written in the drain and read in render — SAME audio thread; only the `snap_buf_` F4b copy crosses threads and is already gated by `soak_scene_save_race`. No other writer of these fields exists. No new TSan gate required (Architect ratified) — confirmed.
- **Gates.** `-DCMAKE_BUILD_TYPE=Release` is HARMLESS: it matches the repo default (`CMakeLists.txt:15`) and the core_unit tests explicitly `add_compile_options(-UNDEBUG)` (`core/tests/core_unit/CMakeLists.txt:6`, with a comment about exactly the Release-strips-assert hazard), so `assert()`-based checks stay live. New test must be registered there to inherit `-UNDEBUG` (plan Step 7 says so).
- **Spot-check (does widening ObjectSnapshot break existing tests?).** NO. `test_state_resync.cpp:361` asserts `sentinel_count == 3`; it sets no routing, so the new gated clause is false for all objects ⇒ count stays 3, no spurious `/adm/obj/N/input` packets. The untouched-object exclusion test (`:420-421`) likewise unaffected (default routing). No test does a positional `ObjectSnapshot{...}` aggregate-init (grep clean) ⇒ trailing-field widening is safe. toJson/fromJson untouched ⇒ scene round-trip + scene tests byte-identical.

**Non-blocking nits:**
1. **Wire-order prose mislabel (fix during Step 6/8 docs).** The emit CODE `buildAndSend(addr, ",if", &gain, 1, &src, 1, ...)` is CORRECT (type-tag `,if` ⇒ wire = int `src` first, float `gain` second — identical mapping to the dsp emit `:317`). But the plan PROSE/comment ("Wire format: `,if <gain:float> <src_ch:int>`", "value-then-id", T-resync "`,if <gain> <src>`") describes the wire as gain-then-src, which is backwards. Types disambiguate so no runtime swap is possible, but the Step-8 CHANGELOG/OSC-reference must publish the ACTUAL wire order `/adm/obj/N/input ,if <src_ch:int> <gain:float>`, and the T-resync assertion must read int-slot=src, float-slot=gain. Correct the prose to avoid shipping a wrong public spec.
2. **Touched predicate (style).** The added `|| (include_dsp_only && (c.input_src_ch != -1 || c.input_gain != 1.f))` is correct; folding it into the EXISTING `include_dsp_only` paren is cleaner but functionally identical. Optional.
3. **SysReset coverage (gap, optional).** `/sys/reset` already clears routing via `obj_cache_.fill(ObjCache{})` (new fields default `-1`/`1.f`). Worth a one-line note in Decision 1 and optionally a sub-assertion in AC5/T-resync that post-reset `objInputRouteAt(i)=={-1,1.0}`. Not blocking.

**Realist check:** No finding involves data loss/security/financial impact; all three nits are documentation/style with fast detection (the wire mislabel would surface immediately in a manual OSC client test). No severity inflation. No escalation to ADVERSARIAL — zero CRITICAL/MAJOR, no systemic pattern; the plan already absorbed the Architect's 6 amendments faithfully and each is source-verified.

Ralplan gates: Principle/Option Consistency = Pass (Option 2 invalidations sound, principles map to decisions). Alternatives Depth = Pass (3 options, bounded pros/cons, explicit invalidation). Risk/Verification Rigor = Pass (pre-mortem 4 scenarios with mitigations, expanded unit/integration/resync/regression/RT matrix, pinned no-hardware gate commands at 64 AND 128). Deliberate Additions = Pass (pre-mortem present incl. the sharpest C6-invariant hole now closed; expanded test plan covers unit/integration/e2e/RT-observability).

### Planner (REV3)

Critic APPROVE accepted; consensus reached. All 3 non-blocking nits applied; Status set to **APPROVED (execution-ready)**. Edits:

1. **[NIT 1 — wire-order prose, real spec correctness] APPLIED everywhere.** The emit CODE was already correct; only the PROSE was backwards. Authoritative outbound wire order pinned as **`/adm/obj/N/input ,if <src_ch:int> <gain:float>`** (type-tag `,if` ⇒ int slot = src_ch, float slot = gain — same slot mapping as the dsp emit). Corrected at: the Step-6 emit comment (now explains the C++-param-order vs type-tag-wire-order distinction), the Step-6 "Authoritative wire format" line, the Step-7 T-resync assertion (int-slot==src_ch, float-slot==gain), the Step-8 CHANGELOG/OSC-reference spec, and the REV2-log mention. Inbound `/obj/input ,iif <obj_id:int> <src_ch:int> <gain:float>` confirmed correct and unchanged.
2. **[NIT 2 — touched predicate style] APPLIED.** Step 6 now folds the routing terms INTO the existing `include_dsp_only` paren (`|| c.input_src_ch != -1 || c.input_gain != 1.f` inside `(include_dsp_only && ( … ))`) instead of a separate gated term — functionally identical, cleaner.
3. **[NIT 3 — SysReset coverage] APPLIED.** Decision 1 now notes `/sys/reset` clears routing for free via `obj_cache_.fill(ObjCache{})` (`SpatialEngine.cpp:620-622`) + the new members' default inits. AC5 gains a post-`/sys/reset` sub-assertion (`objInputRouteAt(i)=={-1,1.0}` for all `i`), and T-resync asserts a reset object is no longer dumped.

Design unchanged (Option 2). No code changed in this pass — documentation/spec corrections only. **Plan is execution-ready; hand off to `/oh-my-claudecode:autopilot`.**
