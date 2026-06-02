# Plan — v0.9 Lane F4 (REV5): Scene save→load→cue round-trip of per-object state (incl. `width_rad` + `reverb_send`)

**Status:** DRAFT REV5 (ralplan consensus — Critic returned ITERATE-with-conditional-pre-approval on REV4: "once C1+M1 corrected and the 4 items folded in, APPROVE-ready for autopilot." REV5 folds in those EXACT fixes. Snippet-level, NO redesign. Final iteration before consensus.).
**Mode:** SHORT for F4a; **DELIBERATE for F4b** — F4b is a concurrency change to audio-thread-owned state and carries a TSan-gated handshake.
**Author:** Planner
**Scene format anchor:** `core/src/ipc/SceneSnapshot.{h,cpp}` (hand-rolled JSON, no external dep)
**Lane decomposition:** **F4a** (serialization + cue-emit boundary — accepted) + **F4b** (save-side object-state capture — the part that closes the user round-trip; concurrency via the accepted 2d publish-on-dirty snapshot).

> REV5 change log (vs REV4): **(C1, CRITICAL)** the reader's single bounded re-check was still racy — there was no re-check after the *second* copy, so a reader descheduled during that copy while the writer fires 2+ more publishes can be lapped (the same "publish twice during my copy" class). Replaced with a **retry-until-stable seqlock-reader loop** and a **convergence/liveness bound** (reader copy « 2 audio-block periods, so the writer cannot lap the held buffer faster than the reader re-checks). Wording corrected from "single re-check sufficient / race-free by construction" to "correct by construction **with a seqlock retry whose liveness rests on reader-copy « 2 block periods**." Kept 3 buffers (retry loop + 3 buffers suffices; 4 not needed). **(M1, MAJOR)** AC9's "az from N / dist from ≤N" invariant is not mechanically checkable (ObjCache has no gen/seq field, fact #16); rewritten to a **correlated-field-pair invariant** the writer maintains and the reader asserts every snapshot — structurally detects cross-buffer tearing — alongside TSan 0-races. **(M2)** bounded the mute-omission corner: `ObjMove` sets `active=true` (`SpatialEngine.cpp:495-496`), so any positioned object is `touched` via `active`; the corner is strictly objects driven to FULL defaults then deactivated. **(4 items)** convergence sentence; `published_index_` lifecycle across prepare/stop-restart; explicit `bool cache_dirty=false;` instruction (no assumed `popped_any` — verified it does NOT exist at `SpatialEngine.cpp:488-491`); note on the two width write paths (ObjWidth verb apply `:161/:605-607` vs the formerly-dead ObjDsp `case 7` at `:579`).
>
> REV4 change log (vs REV3): 2d ACCEPTED. **(A1)** two-buffer even/odd seqlock → THREE-buffer rotation; **(A2)** RT-assert corrected to `SPE_RT_NO_ALLOC_SCOPE()` (existing `audioBlock`-entry guard covers the publish); **(A3)** coarse `dirty` documented as deliberate (dangerous mode is UNDER-publish). F5 precedent reframed as weaker (one-shot flip). (Retained below.)

---

## Problem (the real, user-visible F4 bug)

The production `/scene/save` path **persists no object state at all**. Three defects compound:

1. **Payload is name-only.** `PayloadSceneSave` carries `char name[64]` and nothing else (`Command.h:196`).
2. **The handler writes an empty objects vector.** `SceneController::handleCommand`'s `SceneSave` arm (`core/src/ipc/SceneController.cpp:348-362`) builds `SceneSnapshot snap; snap.name = p.name;` and calls `snap.saveToDisk(...)` — never populating `snap.objects`. `SceneController` has no engine pointer / object source (`SceneController.h:32-88`).
3. **The serialization struct cannot carry the two fields.** `ObjectSnapshot` (`SceneSnapshot.h:10-18`) has no `width_rad`/`reverb_send`; `CueEngine::snapshotToFrames` hard-codes both to 0 (`CueEngine.cpp:48-50`); `CueEngine::emitObject` never emits them (`CueEngine.cpp:55-106`).

The only writers that ever populate `snap.objects` are test/soak scaffolding (`test_p_scene_cue_e2e.cpp:81`, `test_p_cue_go_advance.cpp:61`, `soak_cue_cmdfifo_race.cpp:81`) — never the production handler. So a user who drives objects over OSC then sends `/scene/save` gets a scene with no objects; reload + cue restores nothing. **F4b is required** to close the round-trip.

### The authoritative object-state source (RESOLVED — accepted by Architect)

`StateModel` is **not** authoritative. It is a member of `SpatialEngine` (`SpatialEngine.h:363`), only handles ObjMove/Gain/Active/Algo (`StateModel.cpp:30-108`), lacks width/reverb fields (`StateModel.h:20-29`), and is **deliberately bypassed** for ADM-OSC object traffic (`CommandDecoder.cpp:600-601`, ADR 0006: seq=0 would be dropped).

The authoritative store is **`SpatialEngine::obj_cache_`** (`SpatialEngine.h:386-399`) — an `std::array<ObjCache, MAX_OBJECTS>` carrying every field incl. `reverb_send` (`:392`) and `width_rad` (`:397`), written **exclusively on the audio thread** in the `audioBlock` drain loop (`SpatialEngine.cpp:487-616`).

### Threading reality (CORRECTED in REV3 — this is the crux the Architect flagged)

**REV2 was wrong** to call the cross-thread read "an already-accepted idiom, risk not new." Honest statement:

- `SceneController` + `CueEngine` run on the **control loop** (`spatial_engine_core.cpp:487,496,673-690`). `snapshotObjects()` would be called from there, inside the `scene_ctrl.handleCommand(inc)` dispatch.
- Production backends run `audioBlock` on a **separate real-time worker thread**: `NullBackend.h:4,65` ("real-time-priority `std::thread`"), `SharedRingBackend.h:230-231` (`worker_` thread). That thread writes `obj_cache_` **every block** — e.g. ObjMove multi-field write (`SpatialEngine.cpp:495-496`), `reverb_send` (`:578`), `width_rad` (`:606`).
- The two precedents REV2 cited do **NOT** cover this case:
  - `objCacheActiveAt` (`SpatialEngine.h:332-338`) is explicitly stamped **"Not RT-safe"** and is **test-introspection only**.
  - The prepare-time `obj_cache_` read (`SpatialEngine.cpp:354-357`) is safe **only because audio is stopped during prepare**.
- Therefore a `/scene/save` during live playback is a **genuine concurrent read/write of non-atomic scalars = formal data race / UB**. No lifetime hazard (`obj_cache_` is a fixed `std::array`, never reallocated), worst realistic outcome is **cross-field staleness** (e.g. new `az` with old `dist`), but the project's **TSan + Relacy zero-races gate will flag it** — correctly.

This project enforces a **zero-races standard with no suppressions** (F5/ADR 0021 used an allocate-then-publish atomic handshake gated by `soak_wfs_algoswap_race` at 0 races/150 rounds; Lane B used a Relacy-proven double-buffer; Lane E asserted TSan 0 new races). F4b must therefore introduce a **real synchronization mechanism**, not a documented "benign" race.

---

## Verified facts (file:line — current `main`)

| # | Fact | Evidence |
|---|------|----------|
| 1 | `ObjectSnapshot{id,az_rad,el_rad,dist_m,algorithm(int),gain_linear,muted}` — `algorithm` is "Algorithm enum cast to int" | `SceneSnapshot.h:10-18` |
| 2 | `toJson` emits `"algorithm": std::to_string(o.algorithm)`; `fromJson` reads it as int (`getI`) | `SceneSnapshot.cpp:65,111` |
| 3 | `fromJson` is already missing-key tolerant (per-field defaults) | `SceneSnapshot.cpp:47-50,103-113` |
| 4 | `CueEngine::snapshotToFrames` forces width/reverb to 0 | `CueEngine.cpp:48-50` |
| 5 | `SceneCrossfade` already lerps both; `ObjectFrame` has the fields | `SceneCrossfade.cpp:84-85`; `:30-31` |
| 6 | `reverb_send` via `ObjDsp` param 6: decode `:555`, encode `:1010-1012`, apply `case 6: c.reverb_send` | `CommandDecoder.cpp`; `SpatialEngine.cpp:578` |
| 7 | `width_rad` via `ObjWidth` (0x08): decode `:262`, encode `:744`, apply `case ObjWidth: c.width_rad` | `SpatialEngine.cpp:605-607` |
| 8 | **Param-7 mis-route:** `/obj/dsp` decoder accepts `0..6` (`CommandDecoder.cpp:555`); on `param==7` the `if` is false so `p.param` stays default `Param::EqLow` (`Command.h:258`) → silently writes EQ band 0, not width. No reject else-arm. | `CommandDecoder.cpp:550-559`; `Command.h:246-259` |
| 9 | CueEngine `emitFn` round-trips OSC encode→decode→fifo; never touches the audio fifo directly | `spatial_engine_core.cpp:497-501` |
| 10 | No scene-file format-version field; `SCHEMA_VERSION` is the unrelated wire schema | `Command.h:14`; `SceneSnapshot.h:20-33` |
| 11 | SceneSave handler persists NO objects; `SceneController` has no engine/state source | `SceneController.cpp:348-362`; `SceneController.h:32-88` |
| 12 | Authoritative state = `obj_cache_` (audio-thread-written, all fields). StateModel bypassed for object traffic. | `SpatialEngine.h:363,386-399`; `SpatialEngine.cpp:487-616`; `CommandDecoder.cpp:600-601` |
| 13 | Control-loop SceneSave dispatch site has `engine`, `scene_ctrl`, `inc` in scope | `spatial_engine_core.cpp:673-690` |
| 14 | **`objCacheActiveAt` is "Not RT-safe" / test-only**; prepare read is safe only because audio is stopped | `SpatialEngine.h:332-338`; `SpatialEngine.cpp:354-357` |
| 15 | `audioBlock` runs on a separate RT worker thread in production | `NullBackend.h:4,65`; `SharedRingBackend.h:230-231` |
| 16 | `ObjCache` has **no `valid` flag** — only `active` (+ az/el/dist/algo/gain_lin/reverb_send/k_hf/user_delay_ms/eq/width_rad) | `SpatialEngine.h:387-398` |
| 17 | `obj_cache_` fields are read on the audio thread in MANY hot loops (chain build `:647-659`, render `:701-703`, ambisonic `:849-853`, binaural `:918-934`) | `SpatialEngine.cpp` as cited |
| 18 | Project handshake precedent: allocate-then-publish + `soak_wfs_algoswap_race` TSan gate, 0 races/150 rounds | `core/tests/perf/soak_wfs_algoswap_race.cpp`; ADR 0021 |

---

## Principles

1. **Backward-compat is sacred.** Old scenes (no width/reverb keys) load unchanged → 0; new files parse on the same hand-rolled, no-escape parser; no external JSON dep.
2. **Fix the real user workflow.** Acceptance is OSC→save→reload-from-disk→cue-fire preserving non-zero per-object state — not a struct unit round-trip.
3. **Use the authoritative source.** Save reads `obj_cache_`, never a bypassed StateModel.
4. **Zero races, no suppressions.** Cross-thread access to audio-thread-owned state must be synchronized to pass TSan + Relacy clean, matching the project bar (Principle elevated to MUST in REV3).
5. **Protect the RT hot path.** The mechanism must add no allocation, no lock, and (ideally) no per-field changes to the dozens of renderer read sites (fact #17). Steady-state (no OSC traffic) cost ≈ 0.

## Decision Drivers (top 3)

1. **Backward-compat (MUST).**
2. **Real end-to-end fix from the authoritative source (MUST).**
3. **Zero-races RT/threading safety with NO suppression (MUST)** — the REV3 crux.

---

## Options

### F4a (serialization/emit) — accepted, retained
Add `width_rad`+`reverb_send` to `ObjectSnapshot`+codec; copy through `snapshotToFrames`; emit width via `ObjWidth`, reverb_send via `ObjDsp` param 6. (Width-via-ObjDsp-7 rejected — fact #8; new ObjReverbSend verb rejected — pure surface area.)

### F4b save-source — settled in REV2: **snapshot `obj_cache_`**, NOT StateModel (B1 invalidated: bypassed source, missing fields, reviving it reintroduces the ADR-0006 seq=0 drop bug).

### F4b CONCURRENCY MECHANISM — the REV3 decision (NOT deferred)

The reader needs a **consistent per-object snapshot** of audio-thread-owned `obj_cache_` taken from the control thread during live audio, TSan-clean, no suppression.

**Option (2a) — documented benign torn-read + TSan suppression entry + comment.**
- **REJECTED against the project bar.** A standalone TSan suppression would be the **only** suppression in a codebase that proves 0 races (F5/ADR 0021, Lane B Relacy, Lane E). It does not make the gate genuinely green — it hides a real race. Violates Principle 4. The Architect explicitly asked to evaluate (2a) against this bar and pick the genuinely-green option; (2a) is not it.

**Option (2b) — atomicize the captured `ObjCache` scalars (`std::atomic<float>/<bool>/<uint8_t>`, relaxed).**
- TSan-clean and zero-cost on x86/ARM64. **But:** `ObjCache` fields are read on the audio thread at **dozens of hot sites** (fact #17: chain build, VBAP/DBAP/WFS render via `render::ObjectState{c.az,c.el,c.dist,c.active,c.width_rad}` at `:702`, ambisonic `:849-853`, binaural `:918-934`). Atomicizing the fields forces a `.load(relaxed)` rewrite at **every** one of those RT read sites, plus the aggregate construction at `:702` — a wide, error-prone ripple into the hottest DSP code. It also gives only **per-field** atomicity, so a save can still observe new `az` + old `dist` (cross-field tearing). Violates Principle 5 (blast radius into RT path). **Considered, rejected as primary.**

**Option (2c) — per-object seqlock guarding the live cache.** Audio writer bumps an odd→even generation around each object's field writes; reader retries on odd/changed generation.
- Gives consistent reads, but adds writer overhead on **every** ObjMove/Gain/Dsp on the hot ADM path and still wraps the live cache the renderers read. Overkill for a save. **Rejected** (matches Architect's seqlock-overkill note).

**Option (2d) — RECOMMENDED: publish-on-dirty THREE-buffer rotated snapshot.**
The audio thread keeps writing `obj_cache_` exactly as today (plain scalars — **zero change to the renderer read path**). At the **end of the drain block**, *iff the drain popped anything this block* (coarse `dirty`, see A3), it copies `obj_cache_` into a rotating buffer that is not the currently-published one, then **release-stores** `published_index_`. The control-thread reader runs a **retry-until-stable seqlock loop** (C1): acquire-load the index, copy that buffer, re-load the index, and `break` only when it was unchanged across the whole copy — otherwise retry from the new index. Full mechanism + writer/reader protocol + liveness bound in the Threading-safety argument above.
- **Pros:**
  - **One** audio-thread touchpoint (post-drain publish), not 6 write sites + dozens of read sites. Renderer hot path is **completely untouched** (Principle 5).
  - **Consistent per-object** snapshot — no cross-field tearing (beats 2b).
  - **Correct by construction with a seqlock retry** (three buffers + retry-until-stable reader); liveness rests on reader-copy « 2 block periods (the writer can't lap a tiny copy faster than the reader re-checks). Uses the project's acquire/release handshake culture (ADR 0021; Lane B double-buffer) — but see the corrected precedent framing (A1): the `soak_wfs_algoswap_race` one-shot flip is a *weaker* precedent; correctness here rests on the construction + retry, **confirmed** by AC9 under TSan. Passes TSan + Relacy **genuinely green, no suppression** (Principle 4).
  - **Steady-state cost = 0:** publish only fires on a `dirty` block (a command was popped). With no inbound OSC, no copy happens.
  - **No allocation, no lock:** three fixed-size `std::array` buffers (member, allocated at construction), plain assignment, one atomic index. The publish runs inside `audioBlock`'s existing `SPE_RT_NO_ALLOC_SCOPE()` (`SpatialEngine.cpp:464`) — no-alloc CI-verified, no extra macro (A2).
- **Cons:**
  - One extra `sizeof(obj_cache_)` copy (~ObjCache≈64 B × MAX_OBJECTS ⇒ ~4 KB @64 / ~8 KB @128) per *dirty* block; bounded, branch-predictable, far under the per-block budget, only on blocks that already did OSC work. Documented; measured in the perf check.
  - Three buffers (~24 KB @128) + one atomic index + one private cursor added to `SpatialEngine`. Trivial footprint, no RT cost.

**Decision: F4a (accepted) + F4b = `obj_cache_` source (B2) + Option (2d) publish-on-dirty THREE-buffer rotation + retry-until-stable seqlock reader.** Fold in the param-7 decoder reject (F4b-T0). This is the only option that keeps the TSan gate genuinely green (no suppression), gives a consistent snapshot, leaves the RT renderer path untouched, and is correct by construction (with a converging seqlock retry) for an unbounded-rate writer.

---

## Threading-safety argument (Option 2d — THREE-buffer rotation + seqlock-retry reader, with a liveness bound)

**A1 — Why three buffers, not two (the REV3 fix).** A two-buffer even/odd seqlock is incorrect for an **unbounded-rate writer**: while a slow reader is copying the published buffer, the audio thread can publish *twice*, and the second publish targets the very buffer the reader still holds — corrupting the read unless an *unbounded* retry loop saves it (livelock-prone). A **three-buffer rotation** plus a **retry-until-stable seqlock reader** removes this: the writer never overwrites the *currently-published* buffer, so a reader that re-checks the index and finds it *unchanged* across its copy is guaranteed to have read a buffer the writer did not touch. (C1 note: a *single* re-check is NOT enough — see the reader below; the read must loop until the index is stable across a full copy.)

**Storage (member, allocated at engine construction — NOT on the audio thread):**
```cpp
    std::array<std::array<ObjCache, MAX_OBJECTS>, 3> snap_buf_{}; // ~24 KB @128, trivial
    std::atomic<int> published_index_{-1};   // index last published by audio thread (-1 = none yet)
    int              writer_next_ = 0;        // audio-thread-private rotation cursor (no sync needed)
```
`snap_buf_` is a value `std::array` member, constructed with the engine (`SpatialEngine::SpatialEngine`, `SpatialEngine.cpp:17`) — same lifecycle as `obj_cache_`; the heavy per-object DSP allocation `chains_.resize(MAX_OBJECTS)` happens in `prepareToPlay` (`:417`), confirming construction-time allocation is off the audio thread.

**Writer (audio thread, post-drain, only when `dirty`):**
```cpp
    // pick a buffer that is NEITHER the published index NOR (implicitly) the one a
    // reader could currently hold. With 3 buffers, advancing writer_next_ past the
    // published index guarantees a free third buffer.
    int pub = published_index_.load(std::memory_order_relaxed);
    if (writer_next_ == pub) writer_next_ = (writer_next_ + 1) % 3;  // skip the published buffer
    const int w = writer_next_;
    snap_buf_[w] = obj_cache_;                                   // full consistent copy
    published_index_.store(w, std::memory_order_release);        // publish (release pairs reader acquire)
    writer_next_ = (writer_next_ + 1) % 3;                       // advance for next publish
```
The release-store ensures the copied bytes happen-before the index the reader observes. Because the writer skips the *published* index and then advances, two consecutive publishes use two *different* non-published buffers — the buffer a reader grabbed (the previously-published one) is never overwritten until at least two further publishes, by which point the reader's bounded re-check has already completed.

**Reader (control thread, at the SceneSave dispatch site `spatial_engine_core.cpp:673-690`) — retry-until-stable seqlock loop (C1 fix):**
```cpp
    out.clear();
    int idx = published_index_.load(std::memory_order_acquire);
    if (idx < 0) return;                          // nothing published yet → no objects driven
    std::array<ObjCache, MAX_OBJECTS> local;
    for (;;) {
        idx   = published_index_.load(std::memory_order_acquire);
        local = snap_buf_[idx];                   // copy the currently-published buffer
        if (published_index_.load(std::memory_order_acquire) == idx)
            break;                                // index stable across the whole copy → consistent
        // index moved during the copy → a publish happened; retry from the new index
    }
    // ... convert `local` → out (only active/touched entries)
```
The reader **never touches the live `obj_cache_`** — there is no concurrent access to the renderer-read scalars at all. **Why retry-until-stable, not a single re-check (C1):** the prior "copy → re-check once → re-copy → use" was racy because there is *no* re-check after the *second* copy; a reader descheduled during that second copy while the writer fires 2+ more publishes can be lapped — the exact "publish twice during my copy" class the design must eliminate. The loop above re-checks after *every* copy and only `break`s when the index was unchanged across a complete copy, so the buffer it returns was provably not overwritten while being read (the writer never overwrites the published index).

**Liveness / convergence bound (the 4-item C1 tie-in):** the loop terminates in practice because the reader's copy is tiny and far faster than the writer's publish cadence. The copy is `sizeof(obj_cache_)` ≈ 4 KB @64 / ~8 KB @128 — a single memcpy completing in well under a microsecond — whereas the writer publishes **at most once per audio block** (≈ `block/SR`; e.g. 512/48000 ≈ 10.7 ms, or 64/48000 ≈ 1.33 ms). The reader thus finishes a copy in « one block period, so the writer **cannot lap the held buffer faster than the reader re-checks**; the probability of N consecutive losing races decays geometrically and the loop converges in 1-2 iterations in practice. This is a **liveness argument** (not a hard bound): correctness is by construction (a stable-index copy is consistent regardless of iteration count); liveness rests on reader-copy « 2 block periods.

**Precedent framing (corrected, A1):** REV3 wrongly called this "the exact structure proven by `soak_wfs_algoswap_race`." That precedent is a **ONE-SHOT monotonic `ready_` false→true flip** (`soak_wfs_algoswap_race.cpp:9-11`) — a strictly *weaker and different* guarantee than a *repeating* publish. So we do **not** lean on it as proof. The three-buffer rotation + seqlock-retry reader above is **correct by construction with a seqlock retry whose liveness rests on reader-copy « 2 block periods**; **AC9 (`soak_scene_save_race`) CONFIRMS** both the safety (TSan 0 races) and the no-tearing invariant empirically rather than being the sole guarantee.

**No precedent abuse:** we explicitly do NOT reuse `objCacheActiveAt` (test-only, "Not RT-safe") or the prepare-time read (audio-stopped). The new accessor is its own synchronized path.

**RT contract:** publish is one `O(MAX_OBJECTS)` copy + two relaxed/release atomic stores — no alloc/lock/syscall — and only on dirty blocks. It runs inside `audioBlock`, which already opens an `SPE_RT_NO_ALLOC_SCOPE()` guard at entry (`SpatialEngine.cpp:464`), so the no-alloc property is CI-verified whenever `-DSPATIAL_ENGINE_RT_ASSERTS` is set (and it is set on the RT-assert/TSan build). The reader runs off the RT thread.

**`published_index_` lifecycle across prepare / stop-restart (4-item):** `snap_buf_`, `published_index_`, and `writer_next_` are plain `SpatialEngine` members, so they **persist** across `prepareToPlay`/`releaseResources` (they are NOT reset there, matching how `obj_cache_` itself persists — see `SpatialEngine.cpp:352-356` noting "obj_cache_ persists across prepare"). Consequences, all benign: (i) after a re-prepare, a stale `published_index_ >= 0` simply points at a previously-published buffer whose contents are still valid object state — a save before any new OSC traffic returns the last snapshot, which is correct (the engine has not been told otherwise). (ii) A `/sys/reset` (SysReset) clears the live cache via `obj_cache_.fill()` (`:508`) and, being a popped command, marks the block dirty → the **post-drain publish then captures the cleared cache**, so a subsequent save correctly yields empty objects. We therefore do NOT special-case reset for the snapshot; the dirty-publish handles it. (No reset of `published_index_` to -1 is required; if a future change wants "empty until first post-reset block," that is a one-line Follow-up, not needed for correctness.)

---

## Backward-Compatibility Strategy

1. **Old files:** `fromJson` defaults missing keys (`SceneSnapshot.cpp:103-113`); width/reverb default `0.f`. Two `getF` lines added, no structural change.
2. **New files:** append `"width_rad"`/`"reverb_send"` after `"muted"` in `toJson`; parser is keyed/order-independent; old binaries ignore extra keys.
3. **No format-version field** (fact #10); `SCHEMA_VERSION` untouched.
4. **Numeric format:** reuse `ftos` (`%.6g`) for both.
5. **Empty-objects regression note:** prior behavior (SceneSave writes empty objects) is the bug; AC4 is its explicit regression guard.

---

## Decisions on the secondary defects (Architect items 4 & 5)

### Mute persistence (item 4) — `ObjCache` has no `valid` flag, only `active`
A naive "emit only `active` entries, `muted=!active`" loses mute state and drops muted-but-configured objects. REV3 decision:

- **Distinguish "ever configured" from "never touched" without adding a `valid` flag, using existing data.** A `never-touched` object retains ObjCache defaults: `active=false`, `dist=1.f`, `gain_lin=1.f`, `az=el=0`, `reverb_send=0`, `width_rad=0`, `algo=VBAP`. We emit an object into the snapshot iff it is **non-default** in any of the captured fields (i.e. it has been driven at least once), OR `active==true`. Concretely: `bool touched = active || az!=0 || el!=0 || dist!=1.f || gain_lin!=1.f || reverb_send!=0 || width_rad!=0 || algo!=VBAP;`. This emits a muted-but-configured object (so mute survives) and still omits pristine slots.
- **`ObjectSnapshot.muted` mapping:** `muted = !active`. On reload + cue-fire, an emitted muted object will carry `active=false`; cue emit drives `ObjActive(false)` (the existing `emitObject` path already emits ObjActive), restoring mute.
- **Acceptance reflects this (AC7):** a muted-but-positioned object round-trips with its position/width/reverb preserved and `muted==true`.
- **Documented limitation (bounded — M2):** the omission corner is **strictly limited** to an object driven to FULL defaults (az=0, el=0, dist=1, gain=1, reverb_send=0, width_rad=0, VBAP) **AND** then deactivated/muted. It cannot be hit by a normally-placed object: verified against the drain arms, **`ObjMove` sets `c.active = true`** (`SpatialEngine.cpp:495-496`) and so does `ObjXYZ` (`:602`), so **any object that ever received a position is `touched` via `active`** and is emitted (with `muted=!active`) regardless of its field values. To land in the corner an object must be positioned, then moved exactly to the origin/unit defaults, then muted — at which point it is semantically indistinguishable from "never configured." This is an acceptable, vanishingly-rare corner, noted in the ADR Consequences. (A future `valid`/touched flag on ObjCache would remove it — Follow-up a.)

### Algo enum mapping (item 5) — make it an explicit T1 check
`ObjCache.algo` is `ipc::Algorithm` (`uint8_t` enum, `Command.h:17`); `ObjectSnapshot.algorithm` is an `int` documented "Algorithm enum cast to int" (fact #1), round-tripped by `toJson`/`fromJson` as a plain integer (fact #2). Mapping is therefore **`o.algorithm = static_cast<int>(c.algo)`** on capture, and the cue path already reconstructs `Algorithm` from the int via the existing snapshot→frame mapping. F4b-T1 includes a **static_assert / explicit unit check** that `static_cast<int>(ipc::Algorithm::WFS)` (and each variant) round-trips through `ObjectSnapshot.algorithm` and back, so this is a compile/test-time guarantee, not a runtime surprise.

---

## Step-by-step Implementation

### F4a — serialization + cue-emit (accepted)
- **F4a-T1** `SceneSnapshot.h:17` — add `float width_rad=0.f; float reverb_send=0.f;` to `ObjectSnapshot`.
- **F4a-T2** `SceneSnapshot.cpp:67` (`toJson`) — comma on `muted` line, append `"width_rad"`/`"reverb_send"` via `ftos`.
- **F4a-T3** `SceneSnapshot.cpp:113` (`fromJson`) — `o.width_rad=getF("width_rad",o.width_rad); o.reverb_send=getF("reverb_send",o.reverb_send);`.
- **F4a-T4** `CueEngine.cpp:48-50` (`snapshotToFrames`) — `f.width_rad=o.width_rad; f.reverb_send=o.reverb_send;`.
- **F4a-T5** `CueEngine.cpp:106` (`emitObject`) — emit `ObjWidth{width_rad}` + `ObjDsp{param=6,value=reverb_send}` via the existing `post(...)` drop-and-count lambda (`:59-61`).

> **Width write-path note (4-item, executor heads-up):** there are **two** code paths that can set `obj_cache_[i].width_rad`: (1) the **`ObjWidth` verb** — control-thread decode (`SpatialEngine.cpp:158-163`, sets `qc.width_rad`) applied on the audio thread at `case ObjWidth: c.width_rad = qc.width_rad;` (`:605-607`); this is the path F4a-T5 emits and the path F4b's e2e drives. (2) the **`ObjDsp` `case 7`** apply arm (`:579`), which is **currently DEAD** because the decoder clamps param ≤6 (fact #8) so an `ObjDsp` with `param==7` is never produced. F4b-T0 makes `case 7` reachable. Both ultimately write the same `c.width_rad`, so the snapshot captures width regardless of which path set it — do not be surprised by the duplicate sink; no dedup is needed.

### F4b — save-side capture with the 2d publish handshake

- **F4b-T0 — Decoder reject (fact #8).** `CommandDecoder.cpp:555`:
```cpp
        if (param_int >= 0 && param_int <= 7) {
            p.param = static_cast<PayloadObjDsp::Param>(param_int);
            p.value = getFloat(0);
            cmd.payload = p;
        } else {
            makeUnknown();   // reject; do NOT silently default-route to Param::EqLow
        }
```
+ a focused decoder unit test (AC5).

- **F4b-T1 — THREE-buffer published snapshot + accessor in `SpatialEngine`.** In `SpatialEngine.h` (members near `obj_cache_`):
```cpp
    // F4b: control-thread-readable, audio-thread-published CONSISTENT snapshot of
    // obj_cache_. THREE-buffer rotation (race-free by construction for an
    // unbounded-rate writer — see Threading-safety argument). The live obj_cache_
    // and the renderer read path are UNCHANGED; only this snapshot is shared
    // cross-thread. Allocated at engine construction (NOT on the audio thread).
    std::array<std::array<ObjCache, MAX_OBJECTS>, 3> snap_buf_{}; // ~24 KB @128
    std::atomic<int> published_index_{-1};  // last index published; -1 = nothing yet
    int              writer_next_ = 0;       // audio-thread-private rotation cursor
```
Public accessor (declared near the other read-only accessors; do NOT reuse the "Not RT-safe" objCacheActiveAt):
```cpp
    // F4b: consistent control-thread snapshot of authoritative object state into
    // scene ObjectSnapshots. Synchronized via the three-buffer published_index_
    // handshake; safe to call from the control loop concurrently with the RT
    // audioBlock. Emits objects driven at least once (touched heuristic). NOT RT.
    void snapshotObjects(std::vector<ipc::ObjectSnapshot>& out) const;
```
`snapshotObjects` impl (reader protocol, retry-until-stable seqlock loop — C1):
```cpp
    out.clear();
    int idx = published_index_.load(std::memory_order_acquire);
    if (idx < 0) return;                                  // nothing published → no objects driven
    std::array<ObjCache, MAX_OBJECTS> local;
    for (;;) {
        idx   = published_index_.load(std::memory_order_acquire);
        local = snap_buf_[idx];                           // copy currently-published buffer
        if (published_index_.load(std::memory_order_acquire) == idx) break; // stable across copy → consistent
        // index moved mid-copy → a publish happened; retry from the new index
        // (converges: copy « 2 block periods, see Threading-safety liveness bound)
    }
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        const ObjCache& c = local[i];
        const bool touched = c.active || c.az != 0.f || c.el != 0.f || c.dist != 1.f ||
                             c.gain_lin != 1.f || c.reverb_send != 0.f || c.width_rad != 0.f ||
                             c.algo != ipc::Algorithm::VBAP;
        if (!touched) continue;
        out.push_back(ObjectSnapshot{ /*id*/ i, c.az, c.el, c.dist,
                                      static_cast<int>(c.algo), c.gain_lin, /*muted*/ !c.active,
                                      c.width_rad, c.reverb_send });
    }
```
Add the algo round-trip `static_assert`/check here (item 5): assert `static_cast<int>(ipc::Algorithm::WFS)` etc. round-trip through `ObjectSnapshot.algorithm`.

- **F4b-T2 — Audio-thread publish (one touchpoint).** In the `audioBlock` drain block (`SpatialEngine.cpp:487-616`): the executor **declares a local `bool cache_dirty = false;` immediately before the `while (cmd_fifo_.pop(qc))` loop** (verified there is NO existing `popped_any`/dirty flag at `:488-491` — the loop body is just `qc` + the OOB `continue` + the tag switch), and sets `cache_dirty = true;` on **any** successful pop. Put the assignment at the **top of the loop body, before the `obj_id` OOB `continue` at `:491`**, so even an out-of-range pop marks the block dirty (harmless over-publish per A3, and avoids missing a mutation). **After the loop** (post-drain, so SysReset's `obj_cache_.fill(ObjCache{})` at `:508` is captured), iff `cache_dirty`:
```cpp
        // F4b publish — RT-safe: one fixed copy + atomic stores, no alloc/lock; only
        // on dirty blocks. No new RT-assert needed: audioBlock already opened
        // SPE_RT_NO_ALLOC_SCOPE() at entry (SpatialEngine.cpp:464), so this is
        // CI-verified no-alloc under -DSPATIAL_ENGINE_RT_ASSERTS.
        int pub = published_index_.load(std::memory_order_relaxed);
        if (writer_next_ == pub) writer_next_ = (writer_next_ + 1) % 3; // skip published buffer
        const int w = writer_next_;
        snap_buf_[w] = obj_cache_;                                   // full consistent copy
        published_index_.store(w, std::memory_order_release);        // publish
        writer_next_ = (writer_next_ + 1) % 3;                       // advance rotation
```
**A2 — RT-assert correction:** the real macro is `SPE_RT_NO_ALLOC_SCOPE()` (`RtAssertNoAlloc.h:54`), a scope guard already declared at `audioBlock` entry (`SpatialEngine.cpp:464`). The publish runs inside that scope, so **no additional macro call is added** — the existing guard covers it. The RT-assert/TSan build sets `-DSPATIAL_ENGINE_RT_ASSERTS`, so the no-alloc claim is CI-verified.

**A3 — coarse `dirty` (set true on ANY successful pop) is DELIBERATE.** The drain `continue`s on out-of-range `obj_id` before mutating (`SpatialEngine.cpp:490-491`), and the SysReset/Noise/Output/Transport/Reverb arms (`:507-561`) pop without touching object fields — so a few publishes will republish unchanged data. This is intentional: the **dangerous** failure mode is **UNDER-publish** (missing a real mutation → stale save), which coarse dirty eliminates outright; **over-publish is harmless** (an extra ~8 KB copy on a block that already did OSC work). We accept the harmless over-publish to guarantee no under-publish. SysReset's `obj_cache_.fill()` is a mutation captured by the post-drain publish.

- **F4b-T3 — Provider seam on `SceneController`** (engine-agnostic):
```cpp
    using ObjectStateProvider = std::function<void(std::vector<ObjectSnapshot>&)>;
    void setObjectStateProvider(ObjectStateProvider p) { objStateProvider_ = std::move(p); }
private:
    ObjectStateProvider objStateProvider_;
```

- **F4b-T4 — Populate objects in SceneSave handler.** `SceneController.cpp:348-362`, before `saveToDisk`:
```cpp
            SceneSnapshot snap;
            snap.name = p.name;
            if (objStateProvider_) objStateProvider_(snap.objects);  // F4b
            if (!snap.saveToDisk(scenesDir_)) return true;
```

- **F4b-T5 — Wire the provider in the daemon.** `spatial_engine_core.cpp` after `:496`:
```cpp
    scene_ctrl.setObjectStateProvider(
        [&engine](std::vector<spe::ipc::ObjectSnapshot>& out) { engine.snapshotObjects(out); });
```

**No changes to:** renderers, audio-thread DSP read sites, `StateModel`, the cmd_fifo producer count, the OSC verb set (beyond F4b-T0).

---

## Testable Acceptance Criteria

**F4a:**
1. **Struct round-trip:** `toJson`→`fromJson` preserves width/reverb (1.2 / 0.35) within 1e-4 for ≥2 objects.
2. **Backward-compat:** JSON lacking both keys parses with both = `0.f`, object loaded, no throw.
3. **Cue-fire emit:** after `cue.go(0,0)`, the emit capture has `ObjWidth{width_rad≈W}` AND `ObjDsp{param=ReverbSend(6),value≈R}` for the obj.

**F4b — real round-trip + concurrency:**
4. **End-to-end OSC→save→reload→cue:** drive `ObjWidth`+`ObjDsp` 6 + `ObjMove`+`ObjGain`; **(after the audio thread has published — spin until a block drains)**; `/scene/save`; reload from disk; assert the saved object has non-zero `width_rad` AND `reverb_send` (and matching az/el/dist/gain); fire cue; assert engine receives non-zero. Includes a **non-empty `objects` regression assertion** (guards fact #11).
5. **Decoder (F4b-T0):** `/obj/dsp <id> 7 <v>` → `ObjDsp{param=Width(7)}`; out-of-range (e.g. 9) → `Unknown` (no silent EqLow write).
6. **MAX_OBJECTS, both builds:** snapshot/emit skip ids `>=MAX_OBJECTS`; `snapshotObjects` emits only `touched` entries; passes under **64 and 128**.
7. **Mute persistence (item 4):** a muted-but-positioned object (active=false, non-default az/dist/width/reverb) round-trips with position/width/reverb preserved and `muted==true`; a pristine slot is omitted. Documented corner (defaults-then-mute) noted.
8. **Algo mapping (item 5):** static_assert/unit check that each `ipc::Algorithm` value round-trips through `ObjectSnapshot.algorithm` (int) and back.
9. **CONCURRENT TSan gate + correlated-pair tearing check (M1 — the proof of production safety):** a new `soak_scene_save_race` test (modeled on `soak_wfs_algoswap_race.cpp`) spins one thread driving `obj_cache_` writes (via the FIFO drain / `audioBlock`, or a thin harness that exercises the same drain-then-publish path) while a second thread repeatedly calls `engine.snapshotObjects(out)` mid-flight, ≥150 rounds.
   - **Mechanically-checkable no-tearing invariant (replaces the unprovable "az from N / dist from ≤N"):** the writer maintains a **deterministic correlated pair across two captured fields** using ONLY existing `ObjCache` fields (no new gen/seq field added to the live cache — fact #16). Concretely, on each update the writer sets a monotonically-stepped value into one field and a derived value into another that the reader can recompute — e.g. write `az_rad = k` and `width_rad = encode(k)` for a strictly-increasing sequence `k`, where `encode` is a fixed pure function (e.g. `width_rad = k * 0.001f`). The reader asserts **`local[i].width_rad == encode(local[i].az_rad)`** for every emitted object in every snapshot. A cross-buffer tear (one field from publish N, the other from publish M≠N) breaks the equality and fails the test. Pick a second independent pair (e.g. `dist_m` vs `gain_lin`) to also catch tears that happen to preserve the first pair.
   - **Invariant summary:** TSan reports **0 data races** AND the correlated-pair equality holds in **every** snapshot across ≥150 rounds. AC9 thus proves **both** no-races (TSan) **and** no-cross-buffer-tearing (the correlated-pair check) — the latter is what the retry-until-stable reader (C1) must guarantee.
   - Note: **AC4's "spin until published" is a determinism device, not a safety proof — AC9 is what proves production concurrent safety.**

**Gates:**
10. `NO_JUCE` ctest green (incl. new tests); `python3 -m pytest` scene/cue e2e green; **TSan build green with 0 new races incl. `soak_scene_save_race`**; both 64/128 caps; no new warnings; publish copy stays within the per-block budget (spot perf check).

---

## Verification Steps (commands)

```bash
# 1. Build (NO_JUCE canonical)
cmake -S core -B core/build -DSPATIAL_ENGINE_NO_JUCE=ON >/dev/null
cmake --build core/build -j"$(nproc)"

# 2. C++ unit suite (scene / cue / crossfade / decoder)
ctest --test-dir core/build --output-on-failure -R 'scene|cue|crossfade|decoder'

# 3. TSan build + the new concurrent gate (matches the F5 gate pattern;
#    on ASLR/TSan mmap conflict use `setarch -R`).
cmake -S core -B core/build_tsan -DSPATIAL_ENGINE_NO_JUCE=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" >/dev/null
cmake --build core/build_tsan -j"$(nproc)" --target soak_scene_save_race
setarch -R ./core/build_tsan/tests/perf/soak_scene_save_race   # expect 0 races, ALL PASS
ctest --test-dir core/build_tsan --output-on-failure -R 'scene|cue|race'

# 4. MAX_OBJECTS=128 variant
cmake -S core -B core/build_obj128 -DSPATIAL_ENGINE_NO_JUCE=ON -DMAX_OBJECTS=128 >/dev/null
cmake --build core/build_obj128 -j"$(nproc)"
ctest --test-dir core/build_obj128 --output-on-failure -R 'scene|cue|decoder'

# 5. Python e2e
python3 -m pytest ui/webgui/tests/test_scene_e2e.py ui/webgui/tests/test_cue_scene_e2e.py -q

# 6. Confirm objects appear in a freshly saved scene
grep -c 'width_rad' <scenes_dir>/<name>.json   # expect >=1 per touched object
```

---

## Pre-mortem (3 scenarios)

1. **The publish handshake still races / TSan flags the buffer copy / the reader gets lapped.** Mitigation: the three-buffer rotation + **retry-until-stable seqlock reader** is correct **by construction** — the writer never overwrites the currently-published buffer, and the reader only accepts a copy taken across an *unchanged* index, so a lapped copy is rejected and retried (C1 fixed the prior single-re-check hole). Liveness rests on reader-copy « 2 block periods (the tiny ~8 KB copy finishes far faster than the ≤1-publish-per-block writer cadence), so the loop converges in 1-2 iterations. We do NOT lean on the `soak_wfs_algoswap_race` one-shot-flip precedent (A1). AC9 (`soak_scene_save_race`, ≥150 rounds) **confirms** both 0 TSan races AND the correlated-pair no-tearing invariant; if the rotation/retry is mis-ordered, AC9's correlated-pair check fails fast (it is a gate, not an afterthought).
2. **Publish copy blows the per-block budget at MAX_OBJECTS=128.** Mitigation: publish only on `dirty` blocks (no OSC ⇒ no copy); ~8 KB contiguous copy is well under budget; AC10 spot-checks it; if ever an issue, narrow the copy to the 9 captured fields (not the full ObjCache).
3. **A second `"objects":[` scene-JSON writer exists and is not updated.** Mitigation: confirmed the only `ObjectSnapshot` JSON writer is C++ `SceneSnapshot::toJson` (Python `scene_io.py` is a separate YAML format). Architect to re-confirm before merge.

---

## ADR (finalized)

- **Decision:** Fix F4 in two parts. **F4a:** add `width_rad`+`reverb_send` to `ObjectSnapshot`+codec; route through `snapshotToFrames`; emit width via `ObjWidth`, reverb_send via `ObjDsp` param 6. **F4b:** capture authoritative per-object state on `/scene/save` by publishing a **consistent THREE-buffer rotated snapshot of `SpatialEngine::obj_cache_`** from the audio thread (acquire/release `published_index_` handshake) and reading it on the control thread via a **retry-until-stable seqlock reader** in a new `snapshotObjects()` accessor injected into `SceneController` as an `ObjectStateProvider`. Fold in the `/obj/dsp` param-7 decoder reject.
- **Drivers:** (1) backward-compat; (2) real end-to-end fix from the authoritative source; (3) **zero-races RT safety with NO TSan suppression** — the deciding driver.
- **Alternatives considered:** F4b source — StateModel/B1 *invalidated* (bypassed for ADM-OSC, missing fields, revives ADR-0006 seq=0 bug). Concurrency — (2a) benign-race + TSan suppression *rejected* (would be the only suppression in a 0-races project; doesn't make the gate genuinely green); (2b) atomicize ObjCache scalars *rejected as primary* (forces `.load()` across dozens of RT renderer read sites — fact #17 — and only gives per-field, not per-object, consistency); (2c) live-cache seqlock *rejected* (per-command writer overhead on the hot ADM path; overkill). F4a width-via-ObjDsp-7 and new ObjReverbSend verb — rejected as before.
- **Why chosen (2d):** consistent per-object snapshot; single audio-thread touchpoint; renderer hot path untouched; steady-state cost 0 (publish only on dirty blocks); **correct by construction with a seqlock retry** (three-buffer rotation + retry-until-stable reader; the two-buffer single-re-check was racy/livelock-prone — A1/C1) whose **liveness rests on reader-copy « 2 block periods**, confirmed (not merely asserted) by AC9 (TSan 0 races + correlated-pair no-tearing invariant — M1). NOTE the corrected precedent framing: `soak_wfs_algoswap_race` is a one-shot monotonic flip, a *weaker* precedent than this repeating publish — correctness rests on the construction + retry, not the precedent.
- **Consequences:** ~8 KB (@128) contiguous copy per *dirty* audio block (coarse `dirty` (any pop) deliberately over-publishes to guarantee no UNDER-publish — A3); three fixed-size buffers (~24 KB @128) + one atomic index + a private rotation cursor added to `SpatialEngine` (allocated at construction, persist across prepare/stop-restart — stale `published_index_` is benign, points at still-valid object state; no RT alloc — the existing `SPE_RT_NO_ALLOC_SCOPE()` at `audioBlock` entry covers the publish, A2); a control-thread reader that retries on concurrent publish (converges in 1-2 iterations, liveness by reader-copy « block period); asymmetric cue-emit verbs (documented); mute persistence via a `touched` heuristic has one **bounded** corner (object driven to FULL defaults THEN muted — unreachable for a normally-placed object since `ObjMove` sets `active=true` — M2) due to the absence of a `valid` flag on `ObjCache`.
- **Follow-ups:** (a) add a `touched`/`valid` flag to `ObjCache` to remove the mute corner and simplify the heuristic; (b) persist per-object EQ/delay/k_hf via the same provider (now trivially reachable) — out of scope for F4; (c) surface `ObjName` when ObjCache gains a name field.

---

## Open Questions for Architect / Critic

1. Confirm concurrency **Option 2d (publish-on-dirty THREE-buffer rotation + retry-until-stable seqlock reader)** over 2b (atomicize scalars) — agree the renderer-read-site ripple (fact #17) + per-field-only consistency makes 2b worse here, that 2a (suppression) is off the table under the project's 0-races bar, and that the **retry-until-stable** reader (not the prior single re-check) is required for an unbounded-rate writer (C1), with liveness resting on reader-copy « 2 block periods.
2. Confirm the **`touched` heuristic** for mute persistence (item 4) is acceptable with its one documented corner, vs adding a `valid` flag to `ObjCache` now (Follow-up a).
3. Confirm folding the `/obj/dsp` param-7 reject (F4b-T0) into this lane.
4. Re-confirm no second `"objects":[` JSON writer exists (pre-mortem #3).
