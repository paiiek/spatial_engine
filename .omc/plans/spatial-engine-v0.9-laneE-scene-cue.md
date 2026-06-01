# Spatial Engine v0.9 — Lane E: Scene Library / Cue (Snapshot) Automation — REV 4 (consensus: Architect SOUND + Critic GO-WITH-CONDITIONS)

> Consensus (ralplan) draft. Lanes A & B complete. Outline source:
> `.omc/plans/spatial-engine-v0.9-feature-extension.md` §레인 E (lines 176–221).
> This plan grounds E-M1..E-M6 in the actual code at HEAD `d24b530`.
>
> **REV 2** incorporated the Architect "SOUND WITH AMENDMENTS" review. The
> load-bearing unwired claim was VERIFIED TRUE. REV 2 fixed a BLOCKER
> (second-producer race on the SPSC command FIFO) by adopting fix 1a:
> funnel CueEngine object updates through the existing UDP producer thread.
>
> **REV 3** applies the Architect re-review's 4 trivial factual corrections
> (no design change): mailboxes are SPSC `CommandFifo` reuse (mutex `deque`
> fallback only); the listener's existing 100 ms `SO_RCVTIMEO` already wakes
> it ~10 Hz (no new timeout/self-pipe); forwarded-update latency restated as
> ≈100 ms (1–3 steps for a 100–300 ms fade), with dwell checks still 50 ms;
> emitted tags named (`ObjMove`/`ObjGain`/`ObjActive`/`ObjAlgo`, existing sink,
> no new branch). See Changelog at end. All claims re-verified against code.

---

## 0. Grounding — what actually exists today (file:line evidence)

This is the load-bearing context. Read it before disputing any milestone scope.

### 0.1 Scene engine code exists but is NOT wired into the live daemon

- `core/src/ipc/SceneController.{h,cpp}` — handles `SceneSave`/`SceneLoad`/`SceneList`.
  - Header comment is explicit (`SceneController.h:2-3`): *"message-thread handler … Must NOT be called from the RT audio thread."*
  - `handleCommand` (`SceneController.cpp:12-32`) is a 3-case switch. `SceneSave` builds a `SceneSnapshot` and calls `snap.saveToDisk(scenesDir_)`; `SceneLoad` sets `lastLoaded_`; `SceneList` sets `lastSceneList_`. State held in members `lastSceneList_`/`lastLoaded_` (`SceneController.h:30-31`). No threading primitives — single-threaded by contract.
- Disk layout (`SceneSnapshot.cpp`): files at `scenesDir/<name>.json` (`:129`,`:141`). One file per scene. Manual JSON, fixed key order, no escape support (`SceneSnapshot.h:24`). `listScenes` (`:151-162`) is a `directory_iterator` over `*.json`, returning stems. Name guard `isSafeSceneName` (`:76-81`): non-empty, ≤63 chars, rejects `/ \ NUL .` — so scene names cannot contain dots or path separators.
- **CRITICAL FINDING**: `SceneController` is instantiated only in `core/tests/core_unit/test_p_scene.cpp:135`. It is **NOT** instantiated anywhere in `core/src/bin/spatial_engine_core.cpp` (grep for `SceneController`/`scene` in that file returns zero hits). The daemon's command loop does not handle scene commands at all today. Scene save/load is currently a **Python-side OSC wire feature** (the desktop UI / webgui send `/scene/*` datagrams; `test_scene_e2e.py` only verifies TX wire bytes — RX is explicitly out of scope, ADR-2 deferred per `test_scene_e2e.py:8`).

  **Consequence**: Lane E must *wire* `SceneController` (and `CueEngine`) into the live daemon control loop for the first time. This is additive and lives entirely on the control thread. It is the reason RT-invariance is structurally cheap — the audio engine does not consume scene snapshots live today, so there is no audio-path scene application to perturb.

### 0.2 SceneCrossfade — allocation-free, audio-thread-steppable, but currently unused

- `core/src/scene/SceneCrossfade.{h,cpp}`. Header contract (`SceneCrossfade.h:11-12`): *"plain POD-arithmetic and allocation-free after start(); safe to step from the audio thread."*
- Thread split:
  - **Control thread** calls `start(from, to, duration_ms, sample_rate)` (`SceneCrossfade.cpp:28-44`) — assigns `from_`/`to_` by value (POD `std::array<ObjectFrame,64>` copy, no heap), computes `total_samples_`. duration ≤ 0 → snap (active_=false).
  - **Audio thread** (would) call `advance(num_samples)` (`:46-54`) and `currentObject(id)` (`:65-87`) — pure arithmetic, no alloc, no lock. `progress()` is a divide (`:56-63`).
- Discrete fields (`active`, `algorithm`) snap at t=0.5 (`:73-79`); scalars lerp; angles use shortest-arc `lerpAngle` (`:18-26`).
- **CRITICAL FINDING**: `SceneCrossfade` is referenced only by `core/tests/core_unit/test_p_scene_crossfade.cpp`. It is **not** wired into `SpatialEngine` either — the only crossfade in `SpatialEngine.h` is the unrelated B1↔B2 binaural mode crossfade (`SpatialEngine.h:92,444`).

  **Consequence**: Lane E reuses `SceneCrossfade` as the *trigger primitive* — `CueEngine` (control loop) calls `SceneController` to load the target snapshot (with a units conversion — see E-M3 / fix #3), then arms a `SceneCrossfade` for that transition. Because no audio-path consumer exists yet, E does **not** add `advance()` calls to the audio callback in this lane. The crossfade clock advances on the control loop (50 ms cadence) but forwarded object updates land at ≈100 ms listener-wake granularity (§0.4), NOT by audio-thread sample accounting; interpolated object updates are routed back through the UDP producer thread (fix 1a, §0.4) so `cmd_fifo_` keeps a single producer and the audio path stays byte-identical.

### 0.3 OSC decode + routing pattern for /scene/*

- Decode: `CommandDecoder.cpp:472-484` — `/scene/save` → `CommandTag::SceneSave` + `PayloadSceneSave`, `/scene/load` → `SceneLoad`, `/scene/list` → `SceneList`. Name copied via `copySceneName(dst[64], src)` (`:156`). New addresses follow this exact `else if (addr == "...")` chain.
- Encode (round-trip for outbound/tests): `CommandDecoder.cpp:856-872`.
- Tags live in `core/src/ipc/Command.h:59-61` (`SceneSave=0x30`, `SceneLoad=0x31`, `SceneList=0x32`). Payloads `Command.h:186-188` (`PayloadSceneSave/Load{char name[64]}`, `PayloadSceneList{}`). New tags slot into the enum and the `CommandPayload` variant (`Command.h:284-320`).
- **Pattern to follow for new addresses** (`/scene/rename`, `/scene/duplicate`, `/scene/delete`, `/scene/meta`, `/cue/go`, `/cue/next`, `/cue/prev`, `/cue/stop`):
  1. Add `CommandTag` enum value + payload struct in `Command.h`; add payload to the variant.
  2. Add `else if (addr == "/...")` decode branch in `CommandDecoder.cpp` buildCommand (~`:472`).
  3. Add encode branch (~`:856`) — **mandatory** for any new tag exercised by a C++ round-trip or the `injectCommand` re-encode path (`OSCBackend.cpp:246`); Python-only TX-byte tests (vs `OscMessageBuilder`) may skip the C++ encoder. (REV 4 / Critic Major #2 — was "(Optional)".)
  4. Route in the daemon control loop (new — see §0.4).

### 0.4 The thread model — and where the cue timer/clock lives

This is the load-bearing correctness section (REV 2). Get the threads right or the
SPSC invariant breaks.

**Three threads touch commands today:**

1. **UDP listener thread** — `OSCBackend.cpp:169` `udp_thread_`. Loops on `recvfrom()`, calls `decoder_.decode(packet)`, and **inline on this same thread** calls `if (sink_) sink_(cmd)` (`OSCBackend.cpp:199`). The sink is wired to the engine, whose handler ultimately calls `cmd_fifo_.push(qc)` (`SpatialEngine.cpp:293`). **This is the SOLE physical producer to `cmd_fifo_` today.**
2. **Audio (RT) thread** — drains `cmd_fifo_` (the consumer). Untouched by Lane E.
3. **Daemon control loop** — `spatial_engine_core.cpp:609` `while (!g_quit && now < deadline)`, ending each iteration with `std::this_thread::sleep_for(50ms)` (`:713`). This is a **separate thread** from the UDP listener. It runs the gated sub-ticks: ambi-decoder/binaural-sofa apply (`:628`), Lane A metrics emit (`:644`), shm diag (`:666`). Those sub-ticks are gated to `>= seconds(1)` *inside* a loop that actually wakes ~every **50 ms**.

**Cadence — corrected (fix #4):** The "~1 Hz" figures at `:628/:644/:666` are *gated sub-ticks*, NOT the loop cadence. The real main-loop cadence is `sleep_for(50ms)` (`:713`) ≈ **20 Hz**. There is no finer-grained timer. Therefore:
- **Dwell auto-advance granularity ≈ 50 ms** (one loop iteration), driven by a `std::chrono::steady_clock` deadline comparison in the loop — same idiom as the gated sub-ticks but with a per-cue deadline rather than a fixed period. For cue dwell times (seconds), ±50 ms jitter is fine.
- **Dwell-deadline checking runs at the 50 ms control-loop cadence**, but **crossfade-step forwarding to `cmd_fifo_` is bounded by the UDP listener's wake ≈100 ms** — the listener drains the control→UDP mailbox when `recvfrom()` returns, gated by its existing 100 ms `SO_RCVTIMEO` (`OSCBackend.cpp:128`, see fix 1a below). So forwarded object-update / crossfade-step granularity is **≈100 ms** (listener-wake-bounded), not 50 ms. **Honest consequence:** a `crossfade_ms` in the **100–300 ms band yields only ~1–3 forwarded parameter steps** — a coarse staircase, NOT a click-free fade. Acceptable for cue automation (≥100 ms, where `/obj/move` is already command-rate) but sample-accurate / click-free morphs are explicitly **out of scope** (Follow-up F1, Option B).

**THE BLOCKER (fix #1, #2) — second-producer race:** A naive Option A would have the *control loop thread* call `engine.dispatchCommand(cmd)` (`SpatialEngine.h:257`) → `osc_backend_.injectCommand(cmd)` (`OSCBackend.h:88`) → `sink_(cmd)` → `cmd_fifo_.push()` (`SpatialEngine.cpp:293`) to apply crossfade object updates. But `cmd_fifo_` is a **single-producer** queue — `CommandFifo.h:2` states *"SPSC ring buffer (OSC thread → audio thread)"*. The UDP listener thread is already the sole producer. A control-loop producer running **concurrently** with the UDP listener = **two producers on an SPSC queue** → data race on `head_`, torn slots, ring corruption. `dispatchCommand`/`injectCommand` are **test-only today** (`SpatialEngine.h:255` comment "VST3 layer host→core control plane"; all current callers are single-threaded, which is why SPSC has held). Lane E would be the **first production caller concurrent with the UDP listener** — so this defect is net-new and must be designed out, not assumed away.

**Fix 1a (ADOPTED) — single physical producer preserved:** CueEngine does NOT push to `cmd_fifo_` from the control loop. Instead:
- All `/cue/*` and `Scene*` commands decoded on the UDP thread are **posted to a UDP→control mailbox** — **default: reuse the existing SPSC `CommandFifo` (`core/src/util/CommandFifo.h`)**, here UDP thread = sole producer, control loop = sole consumer (clean 1:1 SPSC). A mutex-guarded `std::deque<Command>` is a **fallback only**, not co-equal — the no-lock posture on the control loop is preserved. They are **NOT executed inline** in the decode callback.
- The **control loop drains** this mailbox each ~50 ms iteration and runs `CueEngine` logic there. `generation_`, the dwell/crossfade clocks, the index, and all file I/O are touched **only on the control loop** → Principle 4 (single writer) and the D2 generation-latch hold because `generation_` is never cross-thread shared mutable state.
- When CueEngine emits object updates (the interpolated `ObjectFrame`s), it does **not** call `cmd_fifo_.push()` itself. It posts object-update Commands onto a **control→UDP mailbox** — **default: a second SPSC `CommandFifo`**, control loop = sole producer, UDP thread = sole consumer (1:1 SPSC; mutex `deque` fallback only). The **UDP listener thread drains it and forwards each via the existing `sink_(cmd)`** — so `cmd_fifo_` retains its **single physical producer** (the UDP thread). No new socket timeout or self-pipe is added: the existing 100 ms `SO_RCVTIMEO` (`OSCBackend.cpp:128`, present "for clean shutdown") already makes `recvfrom()` return ~every 100 ms even at zero traffic, so the listener already wakes ~10 Hz and drains the mailbox on that existing wake.
- **Emitted tags:** CueEngine crossfade updates emit `ObjMove` / `ObjGain` / `ObjActive` / `ObjAlgo` commands, which the existing sink already handles (`SpatialEngine.cpp:24-60`) — **no new sink branch is needed.**
- **Net effect:** the audio callback is byte-identical (literally "no new audio-path sync"); the SPSC invariant on `cmd_fifo_` is preserved (still exactly one producer thread = UDP thread); CueEngine state stays single-threaded on the control loop.

> Rejected alternatives for this race (per Architect): upgrading `cmd_fifo_` to MPSC (changes the audio-consumer side's invariant + perf), or adding a *second* audio-path FIFO (adds audio-path sync). Both violate the lane constraint. **1a is the only fix that keeps BOTH the SPSC invariant AND a byte-identical audio callback.**

### 0.5 UI surfaces

- **WebGUI** (E-M4): `ui/webgui/server.py` exposes `/api/*` routes (mode, trajectory, vid2spatial, hrtf/catalog) and `/ws` (command dispatch) + `/ws/metrics`. The `_dispatch_to_osc` mapper (`server.py:268-327`) already handles `scene_save`/`scene_load`/`scene_list` message types → `osc_send_fn("/scene/...", ...)`. The `/dashboard` route is at `:594`; the A-M4 shell is `ui/webgui/static/dashboard.html` (panel CSS `.panel` at `:55`, panel sections at `:139-180`). New cue/scene panels reuse this shell + the `_dispatch_to_osc` extension pattern.
- **Desktop PySide6** (reference, not the E-M5 target): `ui/spatial_engine_ui/views/scene_panel.py` — Save/Load/List buttons → `osc_sender(addr, *args)`. Optional Qt import guard so headless tests import it.
- **MIDI** (E-M5): `ui/spatial_engine_ui/midi/midi_bridge.py`. Today `MidiBridge.handle_message` (`:44-50`) maps **every** Program Change → `/scene/load scene_{pc}` via `pc_to_scene_name` (`:40-42`). E-M5 extends this to map PC# → cue index → `/cue/go ,i <idx>` with a configurable mapping table (default identity), while preserving the existing scene-load behavior behind a mode flag.

### 0.6 Test conventions

- **C++ ctest**: each test is its own `add_executable` + `add_test(NAME … COMMAND …)` in `core/tests/core_unit/CMakeLists.txt` (e.g. `:185-187` for `test_p_sys_metrics_extended`). Tests use `assert()`/`CHECK` macros; `-UNDEBUG` is forced (`CMakeLists.txt:6`). `SceneController` round-trip precedent: `test_p_scene.cpp:125-160` (temp dir under `fs::temp_directory_path()`, `remove_all` + `create_directories`). Source compiled into `spe_core` (`core/CMakeLists.txt:110-114`).
- **pytest / webgui**: `ui/webgui/tests/test_scene_e2e.py` — raw-UDP wire-byte SHA256 capture vs `pythonosc.OscMessageBuilder` reference. `_dispatch_to_osc` driven over `/ws`. New cue TX tests follow this exact capture-and-compare fixture (`capture_and_client`, `_expected_bytes`, `_sha256`).
- **playwright**: `ui/webgui/tests/playwright/` (smoke at `test_dashboard_smoke.py`); fixtures in `conftest.py`/`_helpers.py`.

---

## RALPLAN-DR Summary (for Architect / Critic)

### Principles
1. **RT path is sacrosanct AND the SPSC producer invariant is preserved.** No new allocation, lock, or branch in the audio callback; AND `cmd_fifo_` keeps exactly one physical producer thread (the UDP listener). CueEngine never pushes to `cmd_fifo_` from the control loop — it routes object updates back through the UDP producer (fix 1a). (`CommandFifo.h:2` SPSC; `SceneController.h:2-3`; `SceneCrossfade.h:11-12`.)
2. **Scene files are ground truth; the index is a rebuildable cache.** Any index/disk divergence resolves by rescanning the directory (`listScenes` semantics, `SceneSnapshot.cpp:151`).
3. **Reuse before invent.** Cue firing reuses `SceneController` (load) + `SceneCrossfade` (transition primitive), entering the engine via the existing `dispatchCommand`/`injectCommand` control-plane (`SpatialEngine.h:257`, `OSCBackend.h:88`). No new DSP, no new snapshot format beyond the meta-in-index + cuelist.
4. **One writer per piece of state, on one thread.** The control loop owns the cue engine, dwell/crossfade clock, `generation_`, index, and all file I/O. `/cue/*` and `Scene*` commands are decoded on the UDP thread but QUEUED and APPLIED on the control loop — never executed inline in the decode callback — so `generation_` is never cross-thread shared mutable state.
5. **Additive wire protocol.** New OSC addresses extend the existing `else if` decode chain and the `CommandTag` enum; existing addresses and their wire bytes are unchanged (preserves `test_scene_e2e.py` hashes).

### Decision Drivers (top 3)
1. **D1 — No RT-alloc regression AND no producer-side data race.** Two-part: (a) the `core/build_rton` (`-DSPATIAL_ENGINE_RT_ASSERTS=ON`) build shows no new alloc on the audio path; (b) **`cmd_fifo_` keeps a single producer** — the second-producer race (control loop + UDP thread both pushing to the SPSC ring, `CommandFifo.h:2`) is the real correctness defect and is designed out by fix 1a. **RT-asserts is NECESSARY BUT INSUFFICIENT for (b)** — it guards audio-thread alloc, not producer-side races. Part (b) is verified by ThreadSanitizer / a concurrent stress test, not RT-asserts.
2. **D2 — Dwell auto-advance vs manual `/cue/go` race.** Resolved by a generation-counter latch — VALID only because (fix #5) all cue commands are applied on the single control loop, so `generation_` is never cross-thread shared. A manual go/stop bumps `generation_`; a stale dwell deadline whose tag mismatches is ignored (no double-fire, no late fire).
3. **D3 — Index corruption resilience.** A partially-written or hand-edited `index.json` must never make scenes unreachable; rescan fallback is mandatory.

### Viable Options

**Where does the crossfade timing live?** (the load-bearing architectural choice)

- **Option A + fix 1a — Control-loop crossfade, UDP-thread-funnelled updates (CHOSEN).** `CueEngine` runs on the daemon control loop (≈20 Hz / 50 ms cadence, §0.4). `/cue/*` + `Scene*` commands are decoded on the UDP thread but **queued** to the control loop, which drains them, loads the target snapshot via `SceneController`, and steps `SceneCrossfade` by elapsed wall-clock. The interpolated `ObjectFrame`s are posted back to the **UDP producer thread**, which forwards them via the existing `sink_`→`cmd_fifo_.push()` path — so `cmd_fifo_` keeps **one physical producer** and the audio callback is byte-identical. (The control-plane entry is `dispatchCommand` `SpatialEngine.h:257` / `injectCommand` `OSCBackend.h:88`, today test-only — first production use.)
  - Pros: Audio callback byte-identical → no RT-alloc regression. SPSC producer invariant preserved (no concurrent second producer). Single-threaded CueEngine state → D2 generation-latch and D3 simple. Matches reality that no live audio-path scene consumer exists (§0.1, §0.2).
  - Cons: Forwarded object updates land at ≈100 ms (UDP-listener-wake-bounded, `OSCBackend.cpp:128`), not per-sample. **Honest:** `crossfade_ms` in the 100–300 ms band yields only ~1–3 forwarded parameter steps (a coarse staircase, NOT click-free). Acceptable for cue-scale fades (≥100 ms); sample-accurate click-free morphs are out of scope (F1). Adds two internal SPSC `CommandFifo` mailboxes (control↔UDP, control-plane only).

- **Option B — Audio-thread-stepped crossfade.** Wire `SceneCrossfade` into `SpatialEngine`'s audio callback: control thread arms via a lock-free handoff (double-buffer like AmbiDecoder), audio thread calls `advance()`/`currentObject()` per block and applies to the live object cache.
  - Pros: Sample-accurate, click-free fades of any duration.
  - Cons: **Directly threatens D1** — adds a new lock-free handoff + per-block `advance()` + per-object `currentObject()` work to the audio path; every one of these must be proven alloc-free and the RT-asserts build re-validated. Far larger blast radius, and it requires building the live audio-path scene-application machinery that does not exist today (out of Lane E's stated scope — "NO new synchronization may be added to the audio path"). **Invalidated by the KEY CONSTRAINT.**

- **Option C — Dedicated cue thread with its own timer.** Spawn a `std::thread` running the dwell/crossfade clock independent of the daemon loop.
  - Pros: Crisp dwell timing independent of loop cadence.
  - Cons: Introduces a *second* writer to scene/cue state AND a *third* producer to `cmd_fifo_` → needs sync between cue thread and the rest, violating Principle 4 for sub-perceptual benefit. The ≈20 Hz / 50 ms main loop (§0.4) already gives adequate dwell precision. **Invalidated as over-engineering.**

- **Rejected race-fixes (for completeness, per Architect):** (i) upgrade `cmd_fifo_` to MPSC — changes the audio-consumer invariant + perf; (ii) add a second audio-path FIFO — adds audio-path sync. Both violate the lane constraint. **Fix 1a is the only option that keeps BOTH the SPSC invariant AND a byte-identical audio callback.**

**Conclusion:** Option A + fix 1a is chosen. Option B is invalid under the lane's KEY CONSTRAINT (no audio-path sync); Option C is invalid under Principle 4 (single writer). The second-producer race is the decisive defect and is designed out by funnelling CueEngine object updates back through the single UDP producer thread. Sample-accurate / sub-50 ms click-free cue fades are a clean future lane (Follow-up F1) — not in scope here.

Mode: **SHORT** (default). Lane E is additive, control-thread-only, with an existing tested substrate. No `--deliberate` signal given; pre-mortem and expanded test matrix are not required. (If the Critic flags D1 as high-risk, escalate to DELIBERATE and add the pre-mortem before final.)

---

## Milestones

> RT-safety invariant for **every** milestone: all new code runs on the message/control thread (daemon main loop in `spatial_engine_core.cpp`, or Python UI threads). The audio callback in `SpatialEngine` is **not touched**. Gate D1 is verified once at E-M3 and re-verified at E-M6 via the `core/build_rton` RT-asserts build.

### E-M1 — Scene library index + management ops (rename / duplicate / delete / meta)

**Files**
- Modify `core/src/ipc/SceneController.{h,cpp}` — add `rename(old,new)`, `duplicate(src,dst)`, `remove(name)`, `setMeta(name, metaJson)`, `rebuildIndex()`, and `const SceneIndex& index() const`.
- Modify `core/src/ipc/SceneSnapshot.{h,cpp}` — optionally factor a `sceneExists`/`scenePath` helper; reuse `isSafeSceneName` guard for all new ops (`SceneSnapshot.cpp:76`).
- New: index persisted at `scenesDir/index.json` — `{ "scenes": [ { "name", "created_unix", "tags":[...], "note" } ] }`. Meta sidecar policy: keep meta **inside** `index.json` only (scene `.json` stays the pure snapshot, preserving existing format and `test_scene_e2e` hashes).
- Modify `core/src/ipc/Command.h` — tags `SceneRename=0x33`, `SceneDuplicate=0x34`, `SceneDelete=0x35`, `SceneMeta=0x36`; payloads `PayloadSceneRename{char from[64]; char to[64];}`, `PayloadSceneDuplicate{from,to}`, `PayloadSceneDelete{char name[64];}`, `PayloadSceneMeta{char name[64]; std::string meta_json;}`; add to variant.
- Modify `core/src/ipc/CommandDecoder.cpp` — decode `/scene/rename ,ss`, `/scene/duplicate ,ss`, `/scene/delete ,s`, `/scene/meta ,ss` following the `:472-484` pattern.

**Precise change**
- `rename`: validate both names; `fs::rename(scenesDir/old.json, scenesDir/new.json)`; update index entry; persist index. Reject if dst exists or src missing.
- `duplicate`: copy file; clone index entry with new `created_unix`. Reject if dst exists.
- `remove`: delete file; drop index entry; persist.
- `setMeta`: parse the `,ss` (name, json-ish tags/note) into the index entry; persist.
- `rebuildIndex`: `listScenes()` → for each, if missing from index add a default entry (created_unix=mtime, empty tags/note); drop index entries with no backing file. This is the **D3 rescan fallback**.
- Index load on construction: try-parse `index.json`; on parse failure or missing → `rebuildIndex()`.

**RT-safety invariant**: control thread only; pure `std::filesystem` + string ops. Audio path untouched. (`SceneController.h:2-3` contract preserved.)

**Gate / test**: NO_JUCE ctest `test_p_scene_library_ops` (new `core/tests/core_unit/test_p_scene_library_ops.cpp` + `add_executable`/`add_test(NAME p_scene_library_ops …)` in `core/tests/core_unit/CMakeLists.txt`). Covers: save→rename→list (old gone, new present); duplicate (both present, distinct created_unix); delete (gone from disk + index); setMeta round-trip; **D3**: corrupt `index.json` (truncated/garbage) → construct controller → `rebuildIndex` recovers full list from disk files. Temp-dir pattern per `test_p_scene.cpp:125`.

**Acceptance**
- All four ops round-trip; index stays consistent with disk after each.
- Corrupt/missing index recovers by rescan; scene files are authoritative.
- `isSafeSceneName` rejects traversal on every op (no `/ \ . NUL`).
- Existing `/scene/save|load|list` wire bytes unchanged (existing tests green).

---

### E-M2 — Cue list model + serialization

**Files**
- New: `core/src/scene/CueList.{h,cpp}` — `struct Cue { std::string scene; float crossfade_ms = 0.f; std::optional<float> dwell_ms; };` and `class CueList { std::vector<Cue> cues; bool loadFromDisk(dir); bool saveToDisk(dir); std::string toJson(); static CueList fromJson(str); }`. Stored at `scenesDir/cuelist.json`.
- Modify `core/CMakeLists.txt:110-114` — add `src/scene/CueList.cpp` to the `spe_core` source list (next to `SceneCrossfade.cpp`).

**Precise change**
- Manual JSON in the same hand-rolled style as `SceneSnapshot.cpp` (no new dependency): `{"cues":[{"scene":"a","crossfade_ms":1000,"dwell_ms":5000}, …]}`. `dwell_ms` omitted/null = manual-advance-only cue.
- Validation on load: clamp negative times to 0; drop cues whose `scene` is empty; tolerate missing `dwell_ms` (→ nullopt). Malformed file → empty list (not a crash), mirroring `SceneSnapshot::fromJson` swallow-on-error policy (`SceneSnapshot.cpp:42-50`).

**RT-safety invariant**: pure data model + file I/O; never referenced from audio path.

**Gate / test**: NO_JUCE ctest `test_p_cuelist_serialize` (new file + CMake registration). Covers: JSON round-trip (with and without `dwell_ms`); boundary values (empty list, 1 cue, negative→0 clamp, missing scene dropped); malformed JSON → empty list.

**Acceptance**
- Save→load is lossless for all field combinations.
- Optional `dwell_ms` correctly distinguishes auto-advance from manual-only cues.
- Malformed input degrades to empty list, never throws across the C ABI boundary.

---

### E-M3 — Cue firing engine + dwell auto-advance + OSC routing

**Files**
- New: `core/src/scene/CueEngine.{h,cpp}` — owns a `CueList`, a `SceneController*` (load), a `SceneCrossfade` (transition primitive), current cue index, and dwell/crossfade deadlines. Add to `core/CMakeLists.txt` `spe_core` sources.
- Modify `core/src/ipc/Command.h` — tags `CueGo=0x37`, `CueNext=0x38`, `CuePrev=0x39`, `CueStop=0x3A`; payloads `PayloadCueGo{int32_t index;}`, `PayloadCueNext{}`, `PayloadCuePrev{}`, `PayloadCueStop{}`; add to variant.
- Modify `core/src/ipc/CommandDecoder.cpp` — decode `/cue/go ,i`, `/cue/next`, `/cue/prev`, `/cue/stop` (these decode on the UDP listener thread, `OSCBackend.cpp:192-199`).
- Modify `core/src/ipc/OSCBackend.{h,cpp}` — add the **fix-1a control-plane mailboxes** (see threading note), **each a reused SPSC `CommandFifo` (`core/src/util/CommandFifo.h`); mutex `std::deque` fallback only**: (a) a control→UDP outbound mailbox (control loop = producer, UDP thread = consumer) that the UDP thread drains on its **existing 100 ms `SO_RCVTIMEO` wake** (`OSCBackend.cpp:128` — no new timeout/self-pipe), forwarding each CueEngine update via the existing `sink_(cmd)`; (b) a UDP→control inbound mailbox (UDP thread = producer, control loop = consumer) carrying decoded `/cue/*` + `Scene*` Commands. Both are control-plane only; the audio `cmd_fifo_` and audio callback are untouched.
- Modify `core/src/bin/spatial_engine_core.cpp` — **first-ever wiring** of scene/cue into the daemon: instantiate `SceneController ctrl(scenesDir)` + `CueEngine cue(&ctrl, sampleRate, emitFn)` near engine setup, where `emitFn` posts an object-update `Command` to the control→UDP mailbox; in the ~50 ms main loop (`:609`), drain the UDP→control inbound mailbox into `ctrl`/`cue`, then call `cue.tick(now_ms)` (next to the `:628` control-tick block).

**Control-plane entry & threading (fix #1, #2, #5):** The engine's control-plane entry is `SpatialEngine::dispatchCommand` (`SpatialEngine.h:257`) → `OSCBackend::injectCommand` (`OSCBackend.h:88`) → `sink_(cmd)` → `cmd_fifo_.push()` (`SpatialEngine.cpp:293`). These are **test-only today** (single-threaded); Lane E is their first production use. To preserve the SPSC invariant, **CueEngine never calls `dispatchCommand` from the control loop**. Instead its object updates are posted to the control→UDP mailbox and **forwarded by the UDP thread**, which is the sole `cmd_fifo_` producer. Symmetrically, decoded `/cue/*` + `Scene*` commands are **queued (not applied inline)** on the UDP thread and **applied on the control loop** — so `generation_` and all CueEngine state are single-threaded (D2 valid; Principle 4 holds).

**Snapshot conversion (fix #3) — ObjectSnapshot → ObjectFrame:** `SceneController` load yields `SceneSnapshot` with `std::vector<ObjectSnapshot>{id, az_rad, el_rad, dist_m, algorithm, gain_linear, muted}` (`SceneSnapshot.h:10-18`). `SceneCrossfade::start` requires `Snapshot = std::array<ObjectFrame,64>` where `ObjectFrame{active, algorithm, az_rad, el_rad, dist_m, gain_db, width_rad, reverb_send}` (`SceneCrossfade.h:22-37`). These are **different structs** — CueEngine must convert per object indexed by `ObjectSnapshot.id` into the array slot:
- `az_rad`, `el_rad`, `dist_m`, `algorithm` → copy directly.
- `gain_linear` → **`gain_db = 20 * log10f(max(gain_linear, 1e-6f))`** (unit conversion; clamp to avoid `log10(0)`; `SceneCrossfade` lerps gain in dB per its header `:28`).
- `active` → **`active = !muted`** for any object the snapshot lists; **default `false`** for array slots with no matching `ObjectSnapshot.id`. (REV 4 / Critic Minor #3 — disambiguated: a listed-but-muted object is `active=false`, never `true`.)
- `width_rad`, `reverb_send` → **NO source in `ObjectSnapshot`** → default `0.f` (point source, no reverb send). Document this as a known v0.9 limitation (scenes do not yet round-trip width/reverb_send; Follow-up F4).

**REVERSE conversion (fix #7 / Critic Major #1) — `ObjectFrame.gain_db` → `PayloadObjGain.gain` (linear) on emit:** `SceneCrossfade` interpolates gain in **dB** (`SceneCrossfade.h:28`), but `PayloadObjGain.gain` is **linear** — decoded straight from the wire float (`CommandDecoder.cpp:332`) and applied as `c.gain_lin = qc.gain` (`SpatialEngine.cpp:485`). Therefore when `CueEngine::tick` emits an `ObjGain` command it MUST convert back: **`payload.gain = powf(10.f, frame.gain_db / 20.f)`**. Emitting the dB value directly into the linear field would send e.g. `-6.0` linear (garbage/negative gain) for a `gain_linear=0.5` scene object — every cue gain transition silently wrong. The units assertion in `test_p_cue_go_advance` MUST check the **emitted** `PayloadObjGain.gain ≈ 0.5` (linear), not only the intermediate `ObjectFrame.gain_db ≈ -6.02`.

**Algorithm range guard (Critic OQ):** when building `PayloadObjAlgo` from `ObjectFrame.algorithm` (int), validate the value is within the `Algorithm` enum range before cast; out-of-range scene data → clamp to a safe default (e.g. the current/first algorithm) rather than producing an invalid enum.

**Crossfade `from`/`current` snapshot source (fix #8 / Critic gap):** `SceneCrossfade::start(current, target, …)` needs a `from` snapshot, but the live `obj_cache_` lives on the **audio thread** and must not be read from the control loop. Therefore **CueEngine tracks its own `current_frame_` baseline** (`std::array<ObjectFrame,64>`, control-loop-owned): initialized to the engine's default/neutral frame at construction, and updated to the crossfade **target** when each crossfade completes (and to the latest interpolated frame mid-transition if a new `go()` interrupts). `go()` arms `start(current_frame_, target, …)` from this tracked baseline — never from the audio-thread cache. This keeps CueEngine single-threaded (Principle 4) and avoids a cross-thread read of `obj_cache_`.

**Mailbox-full policy (fix #9 / Critic gap):** both control↔UDP mailboxes are fixed-size SPSC `CommandFifo<N>` whose `push` returns `false` when full (`CommandFifo.h:56`). A fast crossfade can emit up to 64 object updates per step; if the UDP thread is briefly starved the outbound mailbox could fill. Policy: **drop-and-count** — on `push==false`, increment a dropped-update counter (surfaced via the existing diag/metrics path) and skip that update; the next `tick` re-emits the current interpolated state, so a dropped intermediate step is self-healing (the staircase just loses one tread). Do NOT block or spin on the control loop. Document this in `SCENE_AND_CUE_WORKFLOW.md`.

**SceneLoad failure policy (Critic gap):** if `SceneController::handleCommand(SceneLoad)` fails (missing/corrupt scene file), `CueEngine::go` **holds the current scene** (no crossfade armed, no `generation_` side effects beyond the latch bump already done), logs a warning, and leaves the cue index unchanged so a later valid `go`/`next` still works. Never crash, never apply a stale/empty `lastLoaded()`.

**CueEngine API (control loop only)**
- `void go(int index, int64_t now_ms)`: bump `uint64_t generation_++` (latch); clamp index to `[0, cues.size())`; load target via `SceneController::handleCommand(SceneLoad)` — **on load failure, hold current scene** (no crossfade armed, index unchanged, warn; see SceneLoad failure policy above); on success **convert** `SceneSnapshot`→`SceneCrossfade::Snapshot` (above); arm `SceneCrossfade::start(current_frame_, target, cue.crossfade_ms, sr)` where `current_frame_` is CueEngine's control-loop-owned baseline (see "Crossfade `from`/`current` snapshot source" above) — **never** the audio-thread `obj_cache_`; record crossfade-end deadline; if `cue.dwell_ms` set, record `dwell_deadline = crossfade_end + dwell_ms` tagged with the current `generation_`.
- `void next(now)` / `void prev(now)`: `go(cur±1)`; default end policy = clamp/stop at last (no wrap).
- `void stop(now)`: bump `generation_` (cancels pending dwell), freeze at current scene, clear deadlines.
- `void tick(int64_t now_ms)`: advance the control-side crossfade clock by elapsed wall-time (dwell deadlines checked at the 50 ms control-loop cadence; forwarded object updates land at ≈100 ms listener-wake granularity), post interpolated `ObjectFrame`s via `emitFn` as `ObjMove`/`ObjGain`/`ObjActive`/`ObjAlgo` commands (→ control→UDP mailbox → UDP thread → existing `sink_`, `SpatialEngine.cpp:24-60`, no new sink branch). **When emitting `ObjGain`, convert `frame.gain_db`→linear: `payload.gain = powf(10, gain_db/20)`** (fix #7 — `PayloadObjGain.gain` is linear). On mailbox-full (`push==false`), drop-and-count (fix #9). When the crossfade completes, snap to target and update `current_frame_` to the target. Then: if a dwell deadline exists, is in the past, and its tag == current `generation_`, fire `next()`. **D2 latch:** a manual `go`/`stop`/`next`/`prev` bumped `generation_`, so the stale dwell tag mismatches and is ignored → no double-fire, no late fire. **No audio-callback change.**

**RT-safety invariant (D1, two-part):** (a) every CueEngine method runs on the daemon control loop; `SceneCrossfade` is stepped by wall-clock there, not by audio-thread `advance()`. (b) `cmd_fifo_` keeps a **single producer** (the UDP thread) because CueEngine routes updates through the control→UDP mailbox — no second producer, no torn ring. Audio callback byte-identical.

**Gate / test**
- NO_JUCE ctest `test_p_cue_go_advance`: build a cuelist; `go(0)` → assert target scene loaded + crossfade armed; advance time → `tick` past dwell → assert auto-advance to cue 1 in order; **D2 race**: arm dwell on cue 0, then `go(2)` before dwell fires → advance past the *old* dwell deadline → assert it does NOT fire (generation mismatch), engine sits on cue 2; `stop()` cancels pending dwell. **Units assertion (fix #3 + fix #7):** a scene object with `gain_linear=0.5` produces an intermediate `ObjectFrame.gain_db ≈ -6.02` AND — load-bearing — the **emitted `PayloadObjGain.gain ≈ 0.5` (linear)** after the reverse conversion (assert the EMITTED linear value, not only the intermediate dB); `width_rad`/`reverb_send` default to `0.f`. **Failure-policy assertion:** `go()` to a non-existent scene holds the current scene (no crash, index unchanged).
- **Wire-path test (fix #5):** `test_p_cue_wire_dispatch` — fire `/cue/go ,i 1` as an actual **datagram** through `OSCBackend` (UDP/injectPacket path, `OSCBackend.cpp:192`/`:247`), then drain the inbound mailbox + `tick` on a simulated control loop, and assert the cue advanced. This proves the queued-from-UDP / applied-on-control-loop split, not just direct `CueEngine::go()`.
- **D1 gate (two-part, fix #6):**
  1. **Alloc:** `cmake -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON` (`core/build_rton`) builds; full ctest green (`p1_rt_no_alloc`, `CMakeLists.txt:109`, stays green); audio-path code unchanged.
  2. **Race (RT-asserts is NECESSARY BUT INSUFFICIENT here):** **ThreadSanitizer is the PRIMARY gate** (fix #10 / Critic Minor #1 — a stress test can pass by luck; TSan is higher-signal for the producer race). A concurrent **stress test** — UDP `/adm/obj/*` flood (reuse `core/tests/perf/soak_adm_osc_flood.cpp` pattern) running **concurrently** with active cue crossfades posting updates — is **supplementary**, not a substitute. Both must report **zero data races / no `cmd_fifo_` corruption**. This is the gate that actually catches the second-producer defect; RT-asserts does not. **Invariant re-check:** after E-M3, `grep -rn 'cmd_fifo_.push' core/src` must still yield exactly ONE call site, reached only by the UDP thread.

**Acceptance**
- `/cue/go i`, `/cue/next`, `/cue/prev`, `/cue/stop` decode on UDP, queue, and apply on the control loop.
- Dwell auto-advance fires in order; manual go/stop deterministically cancels pending dwell (single-thread generation latch).
- Snapshot→frame conversion correct: gain interpolated in dB BUT **emitted `PayloadObjGain.gain` is linear** (reverse conversion verified); width/reverb_send defaulted; `active=!muted`; algorithm range-guarded.
- Crossfade `from` baseline comes from CueEngine's own `current_frame_`, never the audio-thread `obj_cache_`.
- RT-asserts build green AND **TSan (primary) + stress (supplementary)** show no producer race; audio callback diff = ∅; `cmd_fifo_` single-producer preserved (one `push` call site, UDP-thread-only).
- Daemon applies loaded scenes live for the first time, with updates funnelled through the single UDP producer.

---

### E-M4 — WebGUI scene library + cue list panel (reuse A-M4 shell)

**Files**
- Modify `ui/webgui/static/dashboard.html` — add a `.panel` section for Scene Library (list + rename/duplicate/delete/meta buttons) and a Cue List panel (cue rows + Go/Next/Prev/Stop), reusing the existing panel CSS (`dashboard.html:55`) and section layout (`:139-180`).
- Modify `ui/webgui/static/js/` (the dashboard JS) — wire panel buttons to `/ws` dispatch messages and `/api/scenes` / `/api/cues` fetches.
- Modify `ui/webgui/server.py` — add `@app.get("/api/scenes")` (list + meta from index), `@app.post("/api/scenes/{op}")`, `@app.get("/api/cues")`, `@app.post("/api/cues")`; extend `_dispatch_to_osc` (`server.py:268-327`) with new message types `scene_rename`/`scene_duplicate`/`scene_delete`/`scene_meta`/`cue_go`/`cue_next`/`cue_prev`/`cue_stop` → `osc_send_fn("/scene/…"|"/cue/…", …)`, following the existing `scene_save` branch (`:317-327`).

**Precise change**: `/api/scenes` and `/api/cues` are read views (list scenes/meta and cues); mutations go through the `/ws` dispatch → OSC path so the C++ daemon (E-M1/E-M3) remains the single writer of index/cuelist files. The webgui reads files or queries via OSC reply (reuse whatever read path E-M1 exposes; if RX is still deferred, read `index.json`/`cuelist.json` directly from the shared scenes dir).

**RT-safety invariant**: Python UI thread only; no engine audio impact.

**Gate / test**: playwright `test_dashboard_scene_cue_smoke` (in `ui/webgui/tests/playwright/`) — load `/dashboard`, save a scene, add a cue, fire Go; assert the corresponding OSC dispatch occurred. Plus pytest TX-byte tests for the new `/cue/*` and `/scene/rename|duplicate|delete|meta` message types mirroring `test_scene_e2e.py` (SHA256 vs `OscMessageBuilder`).

**Acceptance**
- Dashboard shows scene library (with meta) and cue list, parity with desktop scene panel + new cue features.
- Buttons dispatch correct OSC; new `/cue/*` and `/scene/*` TX bytes match canonical encoding.
- Existing dashboard panels/tests unaffected.

---

### E-M5 — MIDI Program Change → cue index mapping

**Files**
- Modify `ui/spatial_engine_ui/midi/midi_bridge.py` — add a cue-mapping mode: `MidiBridge(osc_client, midi_port_name, mode="cue"|"scene", pc_to_cue: dict[int,int] | None)`. In `cue` mode, `handle_message` (`:44-50`) maps `msg.program` → cue index (default identity, or via `pc_to_cue` table) → `self._client.send_message("/cue/go", idx)`. Preserve the existing `scene` mode (default stays backward-compatible `/scene/load scene_{pc}`).

**Precise change**: add `OSC_CUE_GO = "/cue/go"`, a `pc_to_cue_index(pc)` helper (table lookup with identity fallback), and a `mode` switch in `handle_message`. Keep `iter_pending()` non-blocking poll contract (`:71-74`) intact.

**RT-safety invariant**: Python MIDI thread; no engine/audio coupling.

**Gate / test**: pytest `test_midi_cue_trigger` — feed a fake `program_change` message; assert in cue mode it dispatches `/cue/go <mapped idx>`; assert scene mode still dispatches `/scene/load scene_{pc}` (no regression). Mirror the existing midi_bridge test style (mock `osc_client`).

**Acceptance**
- PC# → cue index dispatch works with identity and custom table.
- Legacy scene-load mode unchanged (default or explicit).

---

### E-M6 — Regression suite + documentation

**Files**
- New: `ui/webgui/tests/test_scene_cue_e2e.py` (or core integration) — full flow: create scenes → build cuelist → fire `/cue/go` → dwell auto-advance order verified.
- New: `docs/SCENE_AND_CUE_WORKFLOW.md` — library management ops, cuelist format, OSC reference (`/scene/rename|duplicate|delete|meta`, `/cue/go|next|prev|stop`), dwell/crossfade semantics, the **threading model** (UDP-thread decode → control-loop apply → UDP-thread-funnelled updates, fix 1a), the **honest crossfade-quantization note** (100–300 ms fades yield ~1–3 forwarded steps at ≈100 ms listener-wake granularity), snapshot field gaps (width/reverb_send default to 0), and index-corruption recovery behavior.
- Modify `README` (pointer to the new doc).

**Gate / test**: NO_JUCE ctest (all `test_p_scene_library_ops`, `test_p_cuelist_serialize`, `test_p_cue_go_advance` incl. emitted-linear-gain assertion, `test_p_cue_wire_dispatch` green) + pytest (`test_scene_cue_e2e`, `test_midi_cue_trigger`, new TX-byte tests) + **D1 re-verify (two-part)**: (a) `core/build_rton` RT-asserts build green, audio-path diff = ∅; (b) **TSan (primary) + concurrent stress (supplementary)** (UDP `/adm/obj` flood + active cue crossfade) reports zero data races and no `cmd_fifo_` corruption; `grep cmd_fifo_.push` = one UDP-thread-only call site.

**Acceptance**
- Full library→cuelist→auto-advance flow passes end-to-end (incl. the wire-path dispatch test).
- Doc complete (incl. threading + quantization honesty); README links it.
- All gates green; RT-asserts build clean AND TSan/stress race-free.

---

## ADR — Lane E Scene Library / Cue Automation

**Decision**: Implement cue automation as a `CueEngine` whose state and clock live **solely on the daemon control loop** (`spatial_engine_core.cpp`, ≈20 Hz / 50 ms cadence), reusing `SceneController` (load) and `SceneCrossfade` (transition primitive). `/cue/*` and `Scene*` commands are decoded on the existing UDP listener thread but **queued and applied on the control loop** (not inline in decode). CueEngine's interpolated object updates are **routed back through the UDP listener thread** (fix 1a control→UDP mailbox) so that `cmd_fifo_` retains **exactly one physical producer** (the UDP thread) — preserving its SPSC invariant (`CommandFifo.h:2`) and leaving the audio callback byte-identical. Scene meta lives in `index.json` (rebuildable cache); cuelist in `cuelist.json`. Dwell auto-advance uses a generation-counter latch, valid because all CueEngine state is single-threaded.

**Drivers**: (D1) no RT-alloc regression AND no producer-side data race on `cmd_fifo_`; (D2) dwell vs manual-go race deterministic; (D3) index corruption self-heals by rescan.

**Alternatives considered**:
- *Naive Option A (control loop pushes to `cmd_fifo_` directly)* — would make the control loop a **second producer** to the SPSC `cmd_fifo_` concurrent with the UDP listener → data race / ring corruption (`CommandFifo.h:2`). **This was the decisive defect the Architect caught.** Rejected; superseded by fix 1a.
- *Audio-thread-stepped crossfade (Option B)* — sample-accurate but adds a lock-free handoff + per-block work to the audio path, violating the KEY CONSTRAINT. Rejected → Follow-up F1.
- *Dedicated cue thread (Option C)* — second writer + third `cmd_fifo_` producer for sub-perceptual benefit. Rejected (single-writer).
- *Upgrade `cmd_fifo_` to MPSC* / *second audio-path FIFO* — both change or add audio-path synchronization. Rejected.

**Why chosen**: Fix 1a uniquely keeps BOTH invariants true — the SPSC producer invariant on `cmd_fifo_` (single UDP producer) AND a byte-identical audio callback. CueEngine state is single-threaded on the control loop, so D2's generation latch and D3 are simple. It matches the verified codebase reality that no live audio-path scene consumer exists today — cue automation is a control-plane orchestration layer over already-tested primitives.

**Consequences**:
- (+) Audio callback byte-identical; `cmd_fifo_` SPSC preserved; reuses tested `SceneController`/`SceneCrossfade`/`SceneSnapshot`.
- (+) Daemon gains live scene application for the first time (latent capability becomes real).
- (−) Adds two SPSC `CommandFifo` mailboxes (control→UDP and UDP→control) to `OSCBackend` (control-plane only). No new socket timeout / self-pipe — the listener's existing 100 ms `SO_RCVTIMEO` (`OSCBackend.cpp:128`) already wakes it ~10 Hz to drain the mailbox.
- (−) Forwarded updates land at ≈100 ms (listener-wake-bounded): `crossfade_ms` in the 100–300 ms band produces ~1–3 **audible** parameter steps (coarse staircase, honestly NOT click-free). Adequate for cue-scale fades (≥100 ms); sub-50 ms click-free morphs out of scope. (Dwell-deadline checks still run at the 50 ms control-loop cadence.)
- (−) Scene snapshots have no `width_rad`/`reverb_send` field → those default to 0 across cue loads (F4).
- (−) New OSC tags expand the `CommandTag` enum and decode chain (additive, low risk).

**Follow-ups**:
- **F1**: Sample-accurate / sub-50 ms click-free cue fades — separate lane wiring `SceneCrossfade` into the audio path via a lock-free double-buffer (Option B), with its own RT-asserts + TSan gate.
- **F2**: Expose scene/cue index read-back over an OSC reply channel (RX deferred, ADR-2) so the webgui need not read shared files directly.
- **F3**: Cuelist wrap-around / loop and fade-curve (linear vs equal-power) options.
- **F4**: Extend `SceneSnapshot` (`ObjectSnapshot`) to persist `width_rad` and `reverb_send` so scenes/cues round-trip them instead of defaulting to 0; coordinate with the existing snapshot wire-byte tests.
- **F5** (surfaced by E-M3 TSan gate): **Pre-existing `udp_fd_` shutdown race** — `OSCBackend::stop()` (`OSCBackend.cpp:269` `close(udp_fd_); udp_fd_ = -1;`) races the UDP listener's `recvfrom(udp_fd_, …)` read (`:218`). TSan flags it under BOTH the new `soak_cue_cmdfifo_race` AND the pre-existing `soak_adm_osc_flood` (identical report) — so it is a v0.5.1-era benign shutdown race, **NOT a Lane E regression** (E-M3 did not touch `stop()` or `udp_fd_`). Fix out of Lane E scope: guard `udp_fd_` with an atomic or rely solely on `shutdown()`+`running_=false` without nulling the fd from `stop()`. Note: the soak's `xrunCount()==0` assertion also fails under TSan for both soaks (instrumentation slowdown artifact); the NON-TSan Release run reports `xruns: 0`. TSan is used here for race inspection; the functional/RT xrun gate is the Release run.

---

## Progress Tracker

- [ ] **E-M1** — Scene library index + rename/duplicate/delete/meta; `test_p_scene_library_ops` (incl. D3 rescan recovery) green; existing scene wire bytes unchanged.
- [ ] **E-M2** — `CueList.{h,cpp}` + `cuelist.json` serialize; `test_p_cuelist_serialize` green.
- [x] **E-M3** — `CueEngine` (control-loop, fix-1a UDP-funnelled updates) + `/cue/*` decode→queue→apply + dwell auto-advance + `OSCBackend` control-plane mailbox + snapshot→frame conversion; `test_p_cue_go_advance` (D2 latch + units incl. **emitted-linear gain fix #7**), `test_p_cue_wire_dispatch` (wire-path) green; full ctest **111/111**; **D1(a)** RT-asserts build green (18/18 RT); **D1(b)** TSan: zero NEW races, zero `cmd_fifo_`/mailbox races (only pre-existing `udp_fd_` shutdown race → F5); NON-TSan soak `xruns:0`, `inbound_drops:0`; audio diff ∅; `cmd_fifo_` single-producer preserved (one push site). DONE.
- [ ] **E-M4** — WebGUI scene library + cue list panels + `/api/scenes` `/api/cues` + `_dispatch_to_osc` extension; playwright `test_dashboard_scene_cue_smoke` + cue TX-byte tests green.
- [ ] **E-M5** — MIDI PC → cue index mode in `midi_bridge.py`; `test_midi_cue_trigger` green; scene mode regression-free.
- [ ] **E-M6** — `test_scene_cue_e2e` + `docs/SCENE_AND_CUE_WORKFLOW.md` (incl. threading + quantization honesty) + README pointer; full ctest + pytest + RT-asserts + TSan/stress re-verify green.

---

## Changelog — REV 1 → REV 2 (Architect amendments)

- **BLOCKER (fix #1, second-producer race):** Rewrote Option A to **fix 1a**. CueEngine no longer pushes to the SPSC `cmd_fifo_` from the control loop; object updates are funnelled back through the UDP listener thread (the sole producer) via a new `OSCBackend` control→UDP mailbox. `/cue/*` + `Scene*` commands are queued from the UDP thread and applied on the control loop. Updated §0.4 (new thread-model section), Principle 1 & 4, Driver D1, Options, ADR. Rejected MPSC-upgrade and second-FIFO explicitly.
- **fix #2 (name the mechanism):** E-M3 now cites `SpatialEngine::dispatchCommand` (`SpatialEngine.h:257`) / `OSCBackend::injectCommand` (`OSCBackend.h:88`) → `sink_` → `cmd_fifo_.push()` (`SpatialEngine.cpp:293`) as the control-plane entry, noting they are test-only today (first production use).
- **fix #3 (snapshot conversion):** Added explicit `ObjectSnapshot`→`ObjectFrame` mapping in E-M3 — `gain_linear→gain_db = 20·log10`, `active` from `!muted`, `width_rad`/`reverb_send` default 0 (no source; F4). Added a units assertion to `test_p_cue_go_advance`.
- **fix #4 (cadence):** Corrected "~1 Hz" → real loop cadence `sleep_for(50ms)` ≈ 20 Hz (`spatial_engine_core.cpp:713`); removed prior "inaudible" framing. (REV 3 further refines the forwarding granularity to ≈100 ms — see below.)
- **fix #5 (pin application thread):** Stated explicitly that `/cue/*` + `Scene*` decode on the UDP thread but are QUEUED and APPLIED on the control loop, so `generation_` is never cross-thread shared (Principle 4 + D2 hold). Added `test_p_cue_wire_dispatch` firing `/cue/go` over the wire (not direct `CueEngine::go()`).
- **fix #6 (D1 gate strengthening):** D1 is now two-part — alloc (RT-asserts) AND race (TSan or concurrent UDP-flood + cue-crossfade stress). Dropped all "RT-asserts green ⇒ safe" framing; RT-asserts is necessary-but-insufficient for the producer race. Added the TSan/stress gate to E-M3 and E-M6.
- **Confirmed SOUND (unchanged):** load-bearing unwired claim (re-verified true); D2 generation-latch logic; rejection of Option C; index.json rescan recovery.
- **Mode:** Remains SHORT. The BLOCKER was a localized correctness defect with a verified mechanical fix (1a), not a scope/risk explosion — no pre-mortem required. If the Critic deems the second-producer race residually high-risk, escalate to DELIBERATE and add a 3-scenario pre-mortem focused on the mailbox/listener-wake path.

## Changelog — REV 2 → REV 3 (Architect re-review — 4 trivial factual corrections, no design change)

- **C1 — no phantom listener wake:** Removed the proposed "add a short `SO_RCVTIMEO` / self-pipe wake". The UDP socket **already** sets a 100 ms `SO_RCVTIMEO` (`OSCBackend.cpp:126-128`, "for clean shutdown"), so `recvfrom()` returns ~every 100 ms (EAGAIN) even at zero traffic — the listener already wakes ~10 Hz and drains the mailbox on that existing wake. Updated §0.4 and the E-M3 `OSCBackend` file note and ADR consequence.
- **C2 — corrected forwarding latency:** The control→UDP mailbox is drained on the UDP listener's wake (bounded by the 100 ms `SO_RCVTIMEO`), **not** the 50 ms control loop. Restated crossfade-step / forwarded-update granularity as **≈100 ms** (was ≈50 ms) at §0.4, §0.2 consequence, Option A cons, E-M3 `tick`, ADR consequence, and the doc note; a 100–300 ms fade is now **~1–3** forwarded steps (was 3–6). Dwell-deadline checking remains correct at the 50 ms control-loop cadence — only `cmd_fifo_` forwarding is 100 ms-bounded. Kept the honest coarse/not-click-free framing.
- **C3 — SPSC mailbox pinned as default:** Both mailboxes are now specified as **reused SPSC `CommandFifo` (`core/src/util/CommandFifo.h`)** — control→UDP (control producer / UDP consumer) and UDP→control (UDP producer / control consumer), each a clean 1:1 SPSC. The mutex-guarded `std::deque<Command>` is a **fallback only**, not co-equal; the no-lock posture on the control loop is preserved. Updated §0.4 bullets, E-M3 file note, Option A cons, ADR.
- **C4 — emitted tags named:** Added that CueEngine crossfade updates emit `ObjMove`/`ObjGain`/`ObjActive`/`ObjAlgo` commands, which the **existing sink already handles** (`SpatialEngine.cpp:24-60`) — **no new sink branch needed.** Added to §0.4 and E-M3 `tick`.
- **Verification:** Both new code citations confirmed against source — `SO_RCVTIMEO` at `OSCBackend.cpp:126-128`; the four `Obj*` handlers at `SpatialEngine.cpp:24-60`.
- **No design change; no gate change.** Mode remains SHORT. Ready for Critic final pass.

## Changelog — REV 3 → REV 4 (Critic final pass — GO-WITH-CONDITIONS, conditions baked in)

Critic verdict: **GO-WITH-CONDITIONS**. The 6 load-bearing checks verified against source: §0.1/§0.2 unwired claim CONFIRMED (zero `Scene`/`Cue` refs in `spatial_engine_core.cpp`); SPSC second-producer race CONFIRMED (fix-1a sound); §0.4 cadence CONFIRMED exact (`sleep_for(50ms)` `:713`, `SO_RCVTIMEO {0,100000}` `:126-128`); tag range `0x33–0x3A` CONFIRMED no collision (next used = `NoiseType=0x40`); milestones CONFIRMED runnable. No design change; all conditions are localized, test-catchable additions.

- **fix #7 (Critic Major #1 — gain reverse conversion, BLOCKER for E-M3):** `SceneCrossfade` interpolates gain in **dB** but `PayloadObjGain.gain` is **linear** (`CommandDecoder.cpp:332`, `SpatialEngine.cpp:485`). Added mandatory reverse conversion `payload.gain = powf(10, gain_db/20)` on emit, and strengthened the `test_p_cue_go_advance` units assertion to check the **emitted linear value ≈ 0.5**, not only the intermediate dB.
- **fix #8 (Critic gap — crossfade `from` source):** CueEngine tracks its own control-loop-owned `current_frame_` baseline (init neutral; updated to target on completion); `go()` arms `start(current_frame_, …)` — never reads the audio-thread `obj_cache_`.
- **fix #9 (Critic gap — mailbox-full policy):** drop-and-count on `CommandFifo::push==false`; self-healing (next tick re-emits current state); never block the control loop. Documented in the workflow doc.
- **fix #10 (Critic Minor #1 — TSan primacy):** ThreadSanitizer is the PRIMARY producer-race gate; UDP-flood stress is supplementary, not a substitute. Added `cmd_fifo_.push` single-call-site invariant re-check after E-M3.
- **Critic Major #2 (encode mandatory):** §0.3 step 3 changed from "(Optional)" to mandatory for any new tag exercised by a C++ round-trip or `injectCommand` re-encode; Python-only TX tests may skip the C++ encoder.
- **Critic Minor #3 + OQ (clarifications):** `active = !muted` disambiguated (listed-but-muted → `active=false`); `PayloadObjAlgo` algorithm value range-guarded before enum cast; SceneLoad-failure policy = hold current scene (no crash).

**Execution conditions (must hold during autopilot):** (1) emitted `ObjGain` linear + assertion [E-M3]; (2) `current_frame_` baseline, no audio-cache read [E-M3]; (3) TSan primary race gate [E-M3/E-M6]; (4) mailbox-full drop-and-count [E-M3]; (5) C++ encode mandatory for round-tripped tags [E-M1/E-M3]; (6) single-producer invariant re-verify each milestone.

**Status: consensus reached (Architect SOUND + Critic GO-WITH-CONDITIONS, conditions incorporated). Cleared for autopilot.**
