# Lane 6 — C6: State Persistence / UDP-Loss Resync (`/sys/state_request`)

Status: **APPROVED** — REV4 (ralplan consensus reached: Architect SOUND + Critic APPROVE; REV4 applies 3 non-blocking cleanups — `include_dsp_only` flag scoping, scene-save byte-identical, perf test name pinned). EXECUTION-READY.
Plan file: `.omc/plans/spatial-engine-v0.9-lane6-c6-state-resync.md`
Gap: 2026-06-08 engine gap audit, category C, item C6. Never implemented.
Mode: SHORT consensus (not `--deliberate`; additive, bounded blast radius, no DSP-path change).

---

## 1. Problem statement (with verified grounding — re-confirmed against source)

The OSC control surface is plain UDP. On packet loss the client's view of object state and
the engine's authoritative `obj_cache_` silently diverge ("grounding drift") with **no
resync/reconcile path**. C6 adds a mechanism for a client to recover the full authoritative
state on demand after suspected loss.

Grounding re-read and confirmed at the lines below:

- **Inbound decode.** `CommandDecoder::parseOscPacket` (`core/src/ipc/CommandDecoder.cpp:52-140`)
  is an OSC 1.1 subset parser with no loss detection. `CommandDecoder::buildCommand`
  (`CommandDecoder.cpp:284-...`) maps address → `CommandTag`; the `else if (addr == ...)`
  chain begins at line 320. ADM addresses are handled in `decodeAdmAddress`
  (`CommandDecoder.cpp:184-282`). `Command` carries a per-command `seq` (`Command.h:360`, used
  by `StateModel` reorder-drop) and an **unused** `id` field (`Command.h:361`).
  `SCHEMA_VERSION = 1` (`Command.h:14`).
- **CommandTag enum** (`Command.h:25-93`). System block tops out at
  `SysBinauralSofaSelect = 0x1B` (`Command.h:52`). **`0x1C` is free** (verified: no
  `state_request`/`SysStateRequest`/`0x1C` anywhere in `core/src/`).
- **Authoritative state.** `SpatialEngine::ObjCache` (`SpatialEngine.h:404-417`, audio thread):
  `az/el/dist/active/algo/gain_lin/reverb_send/k_hf/user_delay_ms/eq_gain_db[4]/width_rad` =
  full runtime state. `obj_cache_` is `std::array<ObjCache, MAX_OBJECTS>` (`SpatialEngine.h:417`).
- **`StateModel`** (`StateModel.h:20-29`) is a control-thread mirror but **INCOMPLETE**
  (`ObjectEntry` lacks reverb_send/width/EQ/delay/k_hf) — it is a reorder defense only, not a
  reconcile source. The `/sys/state` placeholder at `StateModel.h:80-84` (`setPendingSofaName`)
  is unrelated to full-state reflection.
- **KEY REUSABLE ASSET — `snapshotObjects()`** (`SpatialEngine.cpp:338-383`). F4b lock-free
  3-buffer reader-claim handshake reading live `obj_cache_`; race-free vs the audio writer; no
  locks; explicitly **"NOT RT-safe; called from the control loop only."** Currently used only
  for scene-save via `ObjectStateProvider` (`SceneController.h:78`, wired at
  `spatial_engine_core.cpp:547-550`). It emits only **"touched"** objects (`SpatialEngine.cpp:372-375`:
  `active || az!=0 || el!=0 || dist!=1 || gain_lin!=1 || reverb_send!=0 || width_rad!=0 ||
  algo!=VBAP`) into `ObjectSnapshot`.
- **`ObjectSnapshot`** (`SceneSnapshot.h:10-20`): `id, az_rad, el_rad, dist_m, algorithm,
  gain_linear, muted, width_rad, reverb_send`. **CRITICAL GAP:** it does **NOT** carry
  `k_hf`, `user_delay_ms`, or `eq_gain_db[4]` — so the existing snapshot loses the per-object
  DSP detail that C7's `/adm/obj/N/dsp` echoes carry (see Decision 4).
- **Echo plane — `EchoPlane`** (`EchoSubscriber.h/.cpp`). Emits OSC to ≤4 subscribers
  (`kMaxEchoSubscribers=4`) on the echo port, address family
  `/adm/obj/N/{aed,xyz,gain,mute,active,width,name,dsp}` (`EchoAddr`, `EchoSubscriber.h:43-52`).
  `buildAndSend` (private, `EchoSubscriber.cpp:147-186`) builds one packet and `sendto()`s each
  rate-allowed subscriber. Rate guard `allowSend` (`EchoSubscriber.cpp:101-143`):
  `kEchoRateLimit = 5000` pkts/s/subscriber (`EchoSubscriber.h:58`), excess → drop + one
  `/sys/warning` per second. 30 s TTL (`kEchoSubscriberTtlMs`). `kEchoMaxObjects` derives from
  `spe::MAX_OBJECTS` (`EchoSubscriber.h:56-57`). C7 added `EchoAddr::Dsp` + per-param dsp echo
  (`markDsp`, `EchoSubscriber.cpp:248-257`); main `5585a80`.
- **Sink / integration point.** The OSC command sink is the lambda in the `SpatialEngine`
  constructor (`SpatialEngine.cpp:19-323`). It runs **on the OSC IO thread** (the `udp_thread_`
  recvfrom loop in `OSCBackend::start` calls `sink_(cmd)`, `OSCBackend.cpp:335-336`). It already
  calls `osc_backend_.echoPlane().markXxx(...)` per command and `flush(now_ms, udpFdForEcho())`
  once per packet (`SpatialEngine.cpp:311-321`). Echo subscriber registration is done in the
  `SysHandshake` case (`SpatialEngine.cpp:240-271`) when the subscriber tag matches
  `EchoPlane::kEchoSubscriberTag`. **This lambda is the natural and only home for the C6 handler**
  — it has `snapshotObjects()` (member), `osc_backend_.echoPlane()`, and `udpFdForEcho()` in scope,
  and already runs on the correct (non-audio control/IO) thread.
- **`ReplyTag::StateUpdate = 0x03 → /sys/state`** (`Command.h:369`) is DEFINED but NOT
  implemented. ADR0020 states "no full-state `/sys/state` emitter exists." Python client mirror
  already declares `ADDR_STATE = "/sys/state"` (`ui/spatial_engine_ui/ipc/protocol.py:7`) as a
  bare constant with no live consumer — so a `/sys/state` sentinel has a reserved home on both
  ends.
- **Prior art.** ADR0003 10 Hz heartbeat = aliveness only. ADR0020 1 Hz `/sys/metrics` = telemetry,
  no full state. Neither reconciles object state.

**Conclusion from grounding:** C6 is best implemented as a **client-driven full-state snapshot
on request**: a new inbound `/sys/state_request` command triggers, on the OSC-IO thread,
`snapshotObjects()` → a re-emission of every touched object on the **existing C7 echo addresses**
(the same addresses the client already listens on), terminated by a single `/sys/state` completion
sentinel (finally giving `ReplyTag::StateUpdate=0x03` a concrete, lightweight meaning).

---

## 2. RALPLAN-DR summary

### Principles
1. **Reuse over invention.** Reconcile on the addresses the client already subscribes to
   (`/adm/obj/N/*`, post-C7) and through the existing `buildAndSend` + rate-guard fan-out. No new
   client parser, no new wire dialect.
2. **Verb ↔ echo symmetry.** A `state_request` produces exactly the echo packets a fresh inbound
   command would — the client's existing reconcile code path handles them with zero new logic.
3. **Authoritative source only.** Reconcile from `snapshotObjects()` (the live `obj_cache_` via the
   race-free F4b handshake), never from the incomplete `StateModel`.
4. **RT inviolate.** Zero new work on the audio thread; zero locks/allocs added to any hot path.
   Allocation (the snapshot vector) is amortized to a pre-reserved member.
5. **Additive & backward compatible.** New tag/decoder case/echo method only; no existing echo,
   command, scene, or test behavior changes.

### Decision drivers (top 3)
1. **Correctness of reconcile** — after the dump, the client's object model must equal `obj_cache_`
   for every object the engine knows (including inactive-but-touched → `active=0`).
2. **Bounded burst vs the 5000 pkt/s rate guard** — a full dump must not silently drop packets nor
   starve concurrent normal echoes.
3. **Minimal blast radius / RT safety** — touch the IPC + echo seam only; do not perturb the DSP or
   audio path; do not change scene serialization semantics.

### Options
- **A) Full-state snapshot-on-request (RECOMMENDED).** New inbound `/sys/state_request` →
  `snapshotObjects()` on the IO thread → re-emit every touched object on the existing
  `/adm/obj/N/{aed,gain,active,width,dsp}` echo addresses, then one `/sys/state` sentinel.
  - Pros: minimal blast radius; reuses `buildAndSend`/rate-guard/subscriber registry verbatim;
    client reuses its existing echo-reconcile path; client-driven (client retries on its own
    timer); idempotent; gives `ReplyTag 0x03` a concrete purpose.
  - Cons: needs the snapshot to carry full DSP detail (Decision 4); broadcast to all subscribers
    (not just requester) unless a targeted variant is added; large dumps interact with the rate
    guard (Decision 5).
- **B) Per-command seq + ack/nack.** Emit `/sys/ack obj_id seq` so clients detect gaps.
  - Pros: proactive gap detection; finer-grained.
  - Cons: medium blast (touches every command's payload + a new reply per command — bandwidth and
    code); detecting a gap still requires a resync emit, which **is option A**. So B is strictly
    additive cost on top of A's primitive. **INVALIDATED as the C6 primitive** (may be a future
    enhancement layered on A; not needed for recovery).
- **C) Periodic full-state heartbeat (1 Hz push).** Continuously push full state.
  - Pros: no client trigger needed.
  - Cons: continuous bandwidth even when idle; permanent rate-guard pressure (a 1 Hz × N-object
    dump competes with live echoes every second); wasteful when nothing changed; still races a
    loss that happens between pushes. **INVALIDATED** on cost/benefit — C6 asks for a *recovery*
    path, not continuous mirroring; ADR0003/ADR0020 already own the periodic-telemetry niche.

**Chosen: A.** It is the actual recovery primitive that B and C both ultimately depend on, with the
smallest blast radius and maximal reuse of the just-shipped C7 echo family.

### The 6 design decisions — resolved
1. **Trigger contract.** New inbound command **`/sys/state_request`**, typetags **`,i`** (one int
   = optional client-chosen request token; absent/legacy → token 0). New
   `CommandTag::SysStateRequest = 0x1C` + `PayloadSysStateRequest { uint32_t token; }` +
   decoder case. Handled on the **OSC IO thread** in the sink lambda (early-return, never enqueued
   to the audio FIFO). The reserved `/sys/state` (`ReplyTag::StateUpdate=0x03`) is **not** used as
   the *inbound* trigger — it is used only as the **outbound completion sentinel** (below). The dump
   is emitted to **all registered echo subscribers** (broadcast, reusing `buildAndSend`'s fan-out),
   not a targeted unicast — see Decision 2 rationale. Requester must already be a registered echo
   subscriber (via `/sys/handshake` with `kEchoSubscriberTag`); a non-subscriber request is a no-op.
2. **Emit wire format.** **Reuse the existing per-object echo addresses** — re-mark every touched
   object on `/adm/obj/N/aed` (`,fff` az_deg, el_deg, dist_norm), `/adm/obj/N/gain` (`,f`),
   `/adm/obj/N/active` (`,i`), `/adm/obj/N/width` (`,f`), and `/adm/obj/N/dsp` (`,if` param value)
   for each set DSP param — i.e. the SAME bytes a normal echo emits. Then emit ONE
   **`/sys/state ,i <object_count>`** completion sentinel (UDP gives the client no other way to know
   the burst ended). `/adm/obj/N/mute` is **omitted** (active is the canonical liveness flag; mute is
   `!active`, redundant — halves ambiguity). `/adm/obj/N/xyz` and `/name` are omitted from the dump
   (position is reconciled via `aed`; name is not authoritative engine state and is not in
   `ObjectSnapshot`). **Rejected:** a single aggregated `/sys/state` blob — it would require a brand
   new client parser, breaks verb↔echo symmetry, and duplicates the C7 address family for no benefit.
3. **Concurrency / RT safety (CORRECTED in REV2 — original claim was factually wrong).**
   `snapshotObjects()` has **TWO genuinely independent non-RT readers on TWO DIFFERENT threads:**
   - **existing:** the `/scene/save` `ObjectStateProvider` (`spatial_engine_core.cpp:547-550`),
     invoked from the **MAIN control-loop thread** (`spatial_engine_core.cpp:717` loop →
     `drainInbound` → `scene_ctrl.handleCommand`);
   - **new (C6):** the `/sys/state_request` handler in the sink lambda, which runs on the **OSC IO
     thread** (`OSCBackend.cpp:333-337`).

   These two event sources are independent and **CAN overlap**. `snapshotObjects()` is
   **single-reader by design**: it stomps ONE `mutable std::atomic<int> snap_reader_busy_idx_`
   (`SpatialEngine.h:449`) to publish its reader-claim to the audio writer. Two concurrent readers
   both write that single slot → the F4b writer-avoidance invariant breaks → torn read vs the audio
   writer — the EXACT race `soak_scene_save_race` (AC9, `core/tests/perf/soak_scene_save_race.cpp`)
   was built to prove absent under TSan. **The original plan's claim that the new caller runs "on the
   same IO thread that calls flush()" was FALSE — it is a different thread from the scene-save
   caller**, and these sources are not timing-isolated the way the audio writer is.

   **Fix (BLOCKER, REV2):** add `std::mutex state_snapshot_mtx_` to `SpatialEngine` and lock it
   **inside** `snapshotObjects()`, wrapping the reader-claim body, so BOTH the scene-save caller and
   the new `state_request` caller are serialized through the single busy-claim slot. **The audio
   writer NEVER locks** — the RT publish path (two atomic loads + one release store, per the
   `SpatialEngine.h` comment) is untouched; the mutex orders the two non-RT readers against each
   other only, never against the writer, so RT-safety is preserved. A save+resync overlap briefly
   serializes; both are rare. **Rejected alternative** — routing `SysStateRequest` to the control
   loop to dodge the mutex — is **worse**: `EchoPlane` is documented IO-thread-only, so that would
   split echo ownership across two threads (the exact hazard the echo design avoids).

   Allocation: the only alloc is `out.clear()/push_back()` on a `std::vector<ObjectSnapshot>` — moved
   to a **pre-reserved member** (`state_dump_buf_`, `reserve(MAX_OBJECTS)` at construction) so
   steady-state requests allocate nothing. **No audio-thread code is touched.** Emission reuses
   `buildAndSend`, which respects the per-subscriber 5000 pkt/s guard (Decision 5).
4. **Scope of dump (DSP-detail gap RESOLVED; touched-predicate hole FIXED in REV3).** Dump =
   **touched objects only**. Never-touched objects are absent (the client never had state for them).
   Inactive-but-touched objects (e.g. previously moved, now `active=false`) ARE emitted with
   **`/adm/obj/N/active ,i 0`**, so the client drops them — satisfying "must they be told to drop
   them?" → yes, via `active=0`.
   **DSP-detail gap — RESOLVED (Architect REV1):** extend `ObjectSnapshot` **additively** with
   `k_hf`, `user_delay_ms`, `eq_gain_db[4]` (defaults preserve scene JSON — `SceneSnapshot::toJson` is
   hand-rolled fixed-key-order and ignores unknown fields, so adding struct fields is byte-safe for
   existing scenes), populate them in `snapshotObjects()` from `ObjCache`, and emit them on
   `/adm/obj/N/dsp` using the C7 param IDs.
   **TOUCHED-PREDICATE HOLE — MAJOR, fixed in REV3 (Critic MUST-FIX 1).** The existing predicate at
   `SpatialEngine.cpp:372-374` is
   `c.active || c.az!=0 || c.el!=0 || c.dist!=1 || c.gain_lin!=1 || c.reverb_send!=0 ||
   c.width_rad!=0 || c.algo!=VBAP` — it does **NOT** test `k_hf`, `user_delay_ms`, or `eq_gain_db[]`.
   So an **inactive object whose ONLY non-default state is an EQ band (param 0-3), user delay
   (param 4), or HF rolloff k_hf (param 5)** evaluates `touched==false` and is **omitted from the
   dump** → a client that set `/adm/obj/N/dsp` on such an object and lost the echo could **never**
   recover it. That silently violates the headline "reconcile EXACTLY to `obj_cache_`" guarantee
   (Decision-driver #1, the AC).
   **Fix (REV4 — scoped to the resync dump ONLY, `/scene/save` stays byte-identical):** add a
   `bool include_dsp_only = false` parameter to `snapshotObjects(std::vector<ObjectSnapshot>&,
   bool include_dsp_only=false)`. The extra predicate clause is **active only when
   `include_dsp_only==true`** (verified `ObjCache::k_hf` default is **0.5f**, `SpatialEngine.h:411`;
   the others default 0):
   ```
   const bool touched =
       c.active || c.az != 0.f || c.el != 0.f || c.dist != 1.f ||
       c.gain_lin != 1.f || c.reverb_send != 0.f || c.width_rad != 0.f ||
       c.algo != ipc::Algorithm::VBAP ||
       (include_dsp_only &&
        (c.k_hf != 0.5f || c.user_delay_ms != 0.f ||
         c.eq_gain_db[0] != 0.f || c.eq_gain_db[1] != 0.f ||
         c.eq_gain_db[2] != 0.f || c.eq_gain_db[3] != 0.f));
   ```
   The `/sys/state_request` caller passes **`true`** (so the dump reconciles **exactly** to
   `obj_cache_`, pure-DSP objects included); the `/scene/save` provider
   (`spatial_engine_core.cpp:549`) passes **`false`** (default) and is therefore **completely
   unchanged — scene-save output stays byte-identical, no phantom objects**. The mutex
   (`state_snapshot_mtx_`) already serializes both callers, so the shared method-with-flag is safe.
   This removes the cross-caller side effect entirely: the "no existing scene behavior changed"
   guarantee is literally true again.
5. **Rate-guard / large-burst interaction.** Worst case at `MAX_OBJECTS=128`, all touched, full DSP:
   per object ≤ aed(1)+gain(1)+active(1)+width(1)+dsp(≤7) = ≤11 packets + 1 sentinel ≈ **≤1409
   packets** per subscriber — **under the 5000/s guard** from a fresh window. So a single dump does
   not, by itself, trip the guard. Decision: **no chunking/pacing in v1** (KISS; fits budget; a
   `state_request` is rare/client-driven). If a concurrent echo burst has already consumed tokens,
   the existing guard degrades gracefully (drops + one `/sys/warning`), and the client simply
   re-requests — acceptable for a recovery primitive. The dump emits synchronously in the sink
   (one tick); the ≤1409 `sendto()`s briefly occupy the IO thread but do not touch audio.
   *(Open tension flagged for Architect: should the dump be paced across K IO ticks to avoid (a)
   IO-thread stall and (b) starving live echoes during the burst? Planner recommends NO for v1 and
   documents pacing as a follow-up gated on profiling; the test proves a fresh-window 128-object dump
   drops zero packets.)*
6. **Idempotency / repeated requests (debounce raised to 500 ms in REV2).** Add a lightweight
   **debounce**: ignore a `state_request` that arrives within `kStateRequestDebounceMs = 500` ms of
   the previous serviced one (single `int64_t last_state_request_ms_` on the IO thread — no locking
   needed, single-threaded). Rapid repeats collapse to one dump per window.
   **Why 500, not 250 (REV2 / Architect amendment #3):** 250 ms permits 4 dumps/s; at
   `MAX_OBJECTS=128` full-DSP a dump is ≤~1409 pkts, and 4 × 1409 ≈ 5636/s **> the 5000 pkt/s guard**
   — so sustained 250 ms-spaced requests would trip the guard and drop **live echoes for ALL
   subscribers** (the dump is broadcast). 500 ms caps it at ≤2 dumps/s → ≤~2818/s < 5000, leaving
   headroom for concurrent live echoes. The rate guard remains the secondary backstop.
   **Debounce ↔ broadcast coupling (REV2 / amendment #4) — load-bearing:** this **GLOBAL** debounce
   (one shared `last_state_request_ms_`) is correct **ONLY because the dump is broadcast** — any
   serviced request refreshes EVERY subscriber, so a request from subscriber Y suppressed within the
   window is still reconciled by X's broadcast dump. **If the targeted-unicast follow-up ever lands,
   the debounce MUST become per-subscriber**, otherwise X's request silently suppresses Y's resync —
   a cross-client resync-suppression bug. A guard comment must sit at the debounce site (see Step 5)
   and the coupling is recorded in the ADR.

---

## 3. Step-by-step implementation plan (exact files / functions)

### Step 1 — Wire the new inbound command (`core/src/ipc/Command.h`)
- Add to `enum class CommandTag` in the System block (after `SysBinauralSofaSelect = 0x1B`,
  `Command.h:52`):
  ```
  SysStateRequest = 0x1C, // /sys/state_request ,i token — client asks for full-state resync (C6)
  ```
- Add payload struct near the other `PayloadSys*` structs:
  ```
  struct PayloadSysStateRequest { uint32_t token = 0; };
  ```
- Add `PayloadSysStateRequest` to the `CommandPayload` variant (`Command.h:~316-354`, before
  `PayloadUnknown`).
- (Sentinel uses the already-reserved `ReplyTag::StateUpdate = 0x03` / `/sys/state` — no new ReplyTag.)
- **AC:** compiles; tag value `0x1C` unique.

### Step 2 — Decode `/sys/state_request` (`core/src/ipc/CommandDecoder.cpp`)
- In `buildCommand`, add to the `else if` address chain (alongside the other `/sys/*` cases):
  ```
  } else if (addr == "/sys/state_request") {
      cmd.tag = CommandTag::SysStateRequest;
      PayloadSysStateRequest p;
      p.token = static_cast<uint32_t>(getInt(0)); // 0 when no arg
      cmd.payload = p;
  ```
- Note `getInt`/`payload_int_offset` already handle the leading `,ii seq id` convention
  (`CommandDecoder.cpp:288-304`); a bare `,i token` (no seq/id prefix) decodes token as `ints[0]`.
- **AC:** a `/sys/state_request ,i 7` packet decodes to `tag==SysStateRequest`, `token==7`; a `,`
  (no-arg) variant decodes to `token==0`; unknown-address path unchanged.

### Step 3 — (Recommended, Decision 4) Extend the snapshot to carry full DSP detail
- `core/src/ipc/SceneSnapshot.h` — add additive fields to `ObjectSnapshot` (after `reverb_send`):
  ```
  float k_hf          = 0.5f;
  float user_delay_ms = 0.f;
  float eq_gain_db[4] = {0.f, 0.f, 0.f, 0.f};
  ```
  (Defaults match `ObjCache`; `SceneSnapshot::toJson`/`fromJson` are fixed-key-order and untouched,
  so scene files remain byte-identical — verify in the scene round-trip test still passes.)
- `core/src/core/SpatialEngine.cpp` `snapshotObjects()` (`:376-378`) — populate the new fields from
  `ObjCache`. **Aggregate-init care (REV2 / amendment #6):** `ObjectSnapshot` gains
  `float eq_gain_db[4]` (a **C array**) while `ObjCache::eq_gain_db` is `std::array<float,4>`, so the
  brace-init cannot pass the `std::array` whole — it must **enumerate** the four elements:
  ```
  out.push_back(ipc::ObjectSnapshot{ /*id*/ i, c.az, c.el, c.dist,
                                     static_cast<int>(c.algo), c.gain_lin,
                                     /*muted*/ !c.active, c.width_rad, c.reverb_send,
                                     c.k_hf, c.user_delay_ms,
                                     {c.eq_gain_db[0], c.eq_gain_db[1],
                                      c.eq_gain_db[2], c.eq_gain_db[3]} });
  ```
- **EXTEND the "touched" predicate behind the `include_dsp_only` flag (REV4 — scoped to the dump
  only).** Add the parameter `bool include_dsp_only = false` to the signature and make the DSP clause
  conditional on it (`k_hf` default is **0.5f** per `SpatialEngine.h:411` — guard that exact default):
  ```
  void SpatialEngine::snapshotObjects(std::vector<ipc::ObjectSnapshot>& out,
                                      bool include_dsp_only) const {
      ...
      const bool touched =
          c.active || c.az != 0.f || c.el != 0.f || c.dist != 1.f ||
          c.gain_lin != 1.f || c.reverb_send != 0.f || c.width_rad != 0.f ||
          c.algo != ipc::Algorithm::VBAP ||
          (include_dsp_only &&
           (c.k_hf != 0.5f || c.user_delay_ms != 0.f ||
            c.eq_gain_db[0] != 0.f || c.eq_gain_db[1] != 0.f ||
            c.eq_gain_db[2] != 0.f || c.eq_gain_db[3] != 0.f));
  ```
  Update the declaration in `SpatialEngine.h` to match the default-argument signature.
- **Caller wiring (REV4):**
  - `/sys/state_request` handler (Step 5) calls `snapshotObjects(state_dump_buf_, /*include_dsp_only=*/true)`
    → pure-DSP inactive objects ARE dumped, so the dump reconciles **exactly** to `obj_cache_`.
  - `/scene/save` provider (`spatial_engine_core.cpp:549`) keeps calling `snapshotObjects(out)` with
    the default `false` → **scene-save behavior and output bytes are completely unchanged** (no
    phantom objects). The mutex serializes both callers, so the shared method-with-flag is safe.
- **AC:** scene save/load tests stay **byte-identical** (default `false` path untouched); the resync
  snapshot carries DSP detail; a pure-EQ inactive object IS dumped via the `true` path (T10).

### Step 4 — New echo emit path (`core/src/ipc/EchoSubscriber.h` + `.cpp`)
- `EchoSubscriber.h`: add `#include "ipc/SceneSnapshot.h"` (for `ObjectSnapshot`) and a new **public**
  method:
  ```
  // C6 — full-state resync. Emits each touched object on the existing echo
  // addresses (aed/gain/active/width/dsp) to ALL active subscribers via the
  // existing rate guard, then a single /sys/state ,i <count> completion sentinel.
  // Does NOT touch the dirty map or the inbound echo obj_cache_ (no coalesce).
  void emitStateDump(const std::vector<ObjectSnapshot>& objs,
                     int64_t now_ms, int send_fd) noexcept;
  ```
- `EchoSubscriber.cpp`: implement using the private `buildAndSend` (rate guard included). For each
  `ObjectSnapshot o`:
  - `/adm/obj/o.id/aed ,fff` with `(az_rad*RAD2DEG, el_rad*RAD2DEG, dist_m/MAX_DIST)` — same
    conversion as the ObjMove echo (`SpatialEngine.cpp:32-38`; `MAX_DIST=20`).
  - `/adm/obj/o.id/gain ,f` = `o.gain_linear`.
  - `/adm/obj/o.id/active ,i` = `o.muted ? 0 : 1`.
  - `/adm/obj/o.id/width ,f` = `o.width_rad`.
  - **DSP params — emit on the SINGLE `/adm/obj/o.id/dsp` address, one packet per param (REV3 / N2).**
    Mirror the EXACT existing dsp-echo call at `EchoSubscriber.cpp:254`:
    `buildAndSend(addr, ",if", &value, 1, &param_id, 1, nullptr, now_ms, send_fd)` —
    **float arg = the value, int arg = the param-id** (do NOT swap operands). The param-id → field map
    (per `Command.h:244-248`, `PayloadObjDsp::Param`):
    `0..3 → eq_gain_db[0..3]`, `4 → user_delay_ms`, `5 → k_hf`, `6 → reverb_send`.
    **reverb_send is dsp param 6** — emit it on `/adm/obj/o.id/dsp` with `param_id=6`, **NOT** a
    separate address. (param 7 = width is NOT a dsp packet — width is single-sourced on
    `/adm/obj/o.id/width` above, matching the C7 mark-site routing.) Emit a `/dsp` packet for each of
    params 0-6 whose value is non-default (or, simpler and safe, all 0-6 — the client overwrites
    idempotently); recommended: emit only the params that differ from default to keep the dump small.
  - After the loop: `buildAndSend("/sys/state", ",i", nullptr,0, &count,1, nullptr, now_ms, send_fd)`.
  - Early-return no-op if `!hasSubscribers()` (mirrors `flush`).
- **AC:** unit test (send_fd=-1 byte-count path + the loopback recvfrom path) sees correct packets +
  sentinel.

### Step 5 — Handle the command in the sink (`core/src/core/SpatialEngine.cpp` + `SpatialEngine.h`)
- `SpatialEngine.h`: add members:
  ```
  std::mutex state_snapshot_mtx_;                     // REV2 amendment #1 — serialize the two
                                                      //   non-RT snapshotObjects() readers
                                                      //   (scene-save@control-loop + state_request@IO).
                                                      //   The audio WRITER never locks.
  std::vector<ipc::ObjectSnapshot> state_dump_buf_;   // pre-reserved in ctor (Decision 3)
  int64_t last_state_request_ms_ = 0;                 // debounce (Decision 6)
  static constexpr int64_t kStateRequestDebounceMs = 500;  // REV2 amendment #3 (was 250)
  ```
  and `state_dump_buf_.reserve(MAX_OBJECTS);` in the constructor body / member init.
  (`#include <mutex>` in `SpatialEngine.h`.)
- **`snapshotObjects()` body (REV2 amendment #1 + REV4 flag):** take `std::lock_guard<std::mutex>
  lk(state_snapshot_mtx_);` at the top of the function, wrapping the reader-claim handshake
  (`SpatialEngine.cpp:354-382`). Both callers (the scene-save provider passing `include_dsp_only=false`
  AND the new state_request handler passing `true`) go through this lock. The audio writer's publish
  path is NOT touched and NEVER takes the lock — RT-safety preserved.
- `SpatialEngine.cpp` sink lambda: add a `case ipc::CommandTag::SysStateRequest:` (in the early-return
  group, alongside `SysHandshake`/`HbPing`). Use the **inline chrono idiom** already at
  `SpatialEngine.cpp:314-318` — **NOT** `steadyNowMs()`, which lives in `OSCBackend.cpp`'s anon
  namespace (`:205`) and is not in `SpatialEngine` scope (REV2 amendment #5):
  ```
  case ipc::CommandTag::SysStateRequest: {
      if (!osc_backend_.echoPlane().hasSubscribers()) return;
      using clock = std::chrono::steady_clock;
      const int64_t now_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              clock::now().time_since_epoch()).count();
      // REV2 amendment #4: this GLOBAL debounce is correct ONLY while the dump is
      // BROADCAST (one serviced request refreshes every subscriber). If a targeted-
      // unicast emit is ever adopted, this MUST become per-subscriber or it silently
      // suppresses another client's resync.
      if (now_ms - last_state_request_ms_ < kStateRequestDebounceMs) return; // Decision 6
      last_state_request_ms_ = now_ms;
      // REV4: include_dsp_only=true → pure-DSP inactive objects are dumped so the
      // client reconciles EXACTLY to obj_cache_. /scene/save keeps the default
      // false, so scene-save output stays byte-identical. Mutex-serialized.
      snapshotObjects(state_dump_buf_, /*include_dsp_only=*/true);
      osc_backend_.echoPlane().emitStateDump(
          state_dump_buf_, now_ms, osc_backend_.udpFdForEcho());
      return;
  }
  ```
- **AC:** a `/sys/state_request` from a registered subscriber triggers exactly one dump; from a
  non-subscriber is a no-op; rapid repeats within 500 ms collapse to one; a concurrent `/scene/save`
  cannot interleave inside `snapshotObjects()` (mutex).

### Step 6 — Tests + registration (see §4).

### Step 7 — (Optional) Python mirror
- `ui/spatial_engine_ui/ipc/protocol.py`: add `ADDR_STATE_REQUEST = "/sys/state_request"` next to
  the existing `ADDR_STATE` constant so the Python client can issue the request and recognize the
  sentinel. Additive; only if a Python client path exercises it.

---

## 4. Test plan + pinned gate commands

### New C++ tests — `core/tests/core_unit/test_state_resync.cpp`
Use the bound-loopback-fd + `recvfrom` + `SO_RCVTIMEO` harness copied from
`test_echo_plane.cpp:26-70` (`bindRx()` / `OscMsg` parser). Register the bound port as the echo
subscriber so dump packets are delivered and parsed off the wire. Cases:
- **T1 (happy path):** register a subscriber; drive several objects (aed, gain, active, width, dsp
  via the sink or directly via `markXxx`+`apply`); send `/sys/state_request`; assert every set
  object's `aed/gain/active/width/dsp` packets arrive with correct values, plus exactly one
  `/sys/state ,i <count>` sentinel with the right count.
- **T2 (inactive drop, Decision 4):** an object that was moved then set inactive emits
  `/adm/obj/N/active ,i 0`.
- **T3 (never-touched absent):** untouched object ids produce no packets.
- **T4 (rate guard, single dump, Decision 5):** at the 128-object build, drive 128 touched objects,
  single `state_request` from a fresh rate window; assert zero drops (count delivered packets ≤ 5000
  and equal to expected; `entryAt().dropped_count == 0`). *(Proves a single dump fits; T9 proves
  sustained frequency.)*
- **T5 (debounce, Decision 6):** two `state_request`s within 500 ms → only one dump observed; a
  third after the window → a second dump.
- **T6 (decoder unit):** `/sys/state_request ,i 7` → `SysStateRequest`, token 7; no-arg → token 0.
- **T7 (no subscribers):** `emitStateDump` with zero subscribers is a no-op (no crash, no packets).
- **T8 (concurrent-reader race, REV2 amendment #7a — proves amendment #1):** under **TSan**, model on
  the existing `soak_scene_save_race` (AC9, `core/tests/perf/soak_scene_save_race.cpp`): one thread
  hammers `snapshotObjects()` via the scene-save path while a second thread hammers it via the
  `state_request` path, concurrently with the audio writer publishing. Assert **0 races / 0 tears**.
  Without the `state_snapshot_mtx_` this test must FAIL (demonstrating the bug the mutex fixes); with
  it, clean. Lives in `core/tests/perf/` alongside AC9 (or extend AC9 to add the second reader source).
- **T9 (sustained frequency vs guard, REV2 amendment #7b — proves amendment #3):** at the 128-object
  build, issue `state_request`s spaced at the debounce interval over ~2 s and assert **no live-echo
  drop** for any subscriber (`dropped_count == 0` across the run) — i.e. ≤2 dumps/s × ≤1409 stays
  under 5000/s. A control sub-case at the OLD 250 ms spacing should show the guard tripping (drops>0),
  pinning the 500 ms choice.
- **T10 (pure-DSP inactive object, REV3 / Critic MUST-FIX 1c — proves the touched-predicate fix):**
  set a SINGLE object's ONLY non-default state to an EQ band (e.g. `eq_gain_db[1] = +6 dB` via
  `/adm/obj/N/dsp ,if 1 6.0`), with **everything else default and the object INACTIVE** (no aed/gain/
  width/active driven). Send `/sys/state_request`; assert the object **IS** in the dump: it emits
  `/adm/obj/N/active ,i 0` plus the correct `/adm/obj/N/dsp ,if 1 6.0` packet. *(Note: T1 also drives
  aed/gain/active/width, so T1 does NOT exercise this path — T10 is deliberately pure-DSP. Repeat the
  assertion for a param-5 / k_hf-only object and a param-4 / delay-only object to cover the full
  predicate extension.)*
- **Test-file placement (REV3 / N1) — unambiguous:**
  - **core_unit (`core/tests/core_unit/test_state_resync.cpp`):** T1-T7, T9, T10. Register in
    `core/tests/core_unit/CMakeLists.txt` mirroring the `test_echo_plane` block (`:305-307`):
    `add_executable(test_state_resync test_state_resync.cpp)` +
    `target_link_libraries(test_state_resync PRIVATE spe_core)` + `add_test(NAME state_resync ...)`.
  - **perf/TSan (`core/tests/perf/state_resync_race.cpp`):** **T8 only** (the concurrent scene-save +
    state_request race). It lives in `core/tests/perf/CMakeLists.txt` **inside the
    `if(CMAKE_BUILD_TYPE STREQUAL "Release" OR SPATIAL_ENGINE_RUN_SOAK)` block**, mirroring
    `soak_scene_save_race` (`core/tests/perf/CMakeLists.txt:42-48`):
    ```
    add_executable(state_resync_race state_resync_race.cpp)
    target_link_libraries(state_resync_race PRIVATE spe_core Threads::Threads)
    add_test(NAME state_resync_race COMMAND state_resync_race CONFIGURATIONS Release)
    set_tests_properties(state_resync_race PROPERTIES TIMEOUT 120)
    ```
    **The `add_test` NAME MUST be exactly `state_resync_race`** (REV4 / cleanup #3) so Gate 5's
    `ctest -R 'scene_save_race|state_resync_race'` matches it deterministically.

### Scene regression (Decision 4 / Step 3 — REV4: scene-save byte-identical)
- Confirm `test_p_scene_obj_state_e2e.cpp` and any scene JSON round-trip test stay green. **With the
  `include_dsp_only` flag (REV4), `/scene/save` calls `snapshotObjects(out)` with the default
  `false`, so scene-save output is BYTE-IDENTICAL** — no phantom pure-DSP objects, no count change.
  The additive `ObjectSnapshot` DSP fields are not serialized by the hand-rolled fixed-key-order
  `toJson`, so saved scene bytes are unchanged regardless. The regression simply asserts existing
  scene round-trips are untouched.

### Pinned gate commands (NO hardware)
```bash
# Gate 1 — NO_JUCE clean build + ctest at default MAX_OBJECTS=64 (fresh dir)
cd /home/seung/mmhoa/spatial_engine
rm -rf core/build_c6_64 && cmake -S core -B core/build_c6_64 \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=64
cmake --build core/build_c6_64 -j"$(nproc)"
( cd core/build_c6_64 && ctest --output-on-failure )

# Gate 2 — ctest at MAX_OBJECTS=128 (manual reconfigure, fresh dir; CI only builds 64)
rm -rf core/build_c6_128 && cmake -S core -B core/build_c6_128 \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128
cmake --build core/build_c6_128 -j"$(nproc)"
( cd core/build_c6_128 && ctest --output-on-failure )

# Gate 3 — RT-asserts build variant green (proves no new audio-path alloc/assert)
rm -rf core/build_c6_rton && cmake -S core -B core/build_c6_rton \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON
cmake --build core/build_c6_rton -j"$(nproc)"
( cd core/build_c6_rton && ctest --output-on-failure )

# Gate 4 — Python suite
cd /home/seung/mmhoa/spatial_engine && python3 -m pytest

# Gate 5 — TSan concurrent-reader gate (REV2 amendment #1 / REV3 MUST-FIX 2):
# proves the scene-save + state_request serialization (T8 + the sibling AC9).
#
# CRITICAL (REV3): the entire perf/soak block — incl. soak_scene_save_race AND the
# new state_resync_race — is gated by
#   if(CMAKE_BUILD_TYPE STREQUAL "Release" OR SPATIAL_ENGINE_RUN_SOAK)
# (core/tests/perf/CMakeLists.txt:5), and each add_test carries CONFIGURATIONS
# Release (e.g. :45-47). So the targets DO NOT EXIST unless we set Release (or
# RUN_SOAK), and `ctest -R ...race` must run the Release config or it matches
# ZERO tests (false green / "No tests were found"). The earlier Gate 5 set NEITHER
# → it proved nothing. Corrected mechanism below.
rm -rf core/build_c6_tsan && cmake -S core -B core/build_c6_tsan \
  -DSPATIAL_ENGINE_NO_JUCE=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build core/build_c6_tsan -j"$(nproc)"
# Single-config (Makefile) generator: CMAKE_BUILD_TYPE=Release satisfies the
# CONFIGURATIONS Release filter when ctest is invoked with `-C Release`.
# `setarch -R` disables ASLR to avoid the TSan/ASLR mmap conflict noted for the
# sibling soak_cue_cmdfifo_race gate (core/tests/perf/CMakeLists.txt:16-18).
( cd core/build_c6_tsan && \
  setarch -R ctest -C Release --output-on-failure -R 'scene_save_race|state_resync_race' )
# Expect: BOTH tests found and run; 0 ThreadSanitizer reports. (Sanity-check the
# count > 0 — a "No tests were found" line is a FAILED gate, not a pass.)
```
(Cross-checked against `core/tests/perf/CMakeLists.txt`, the `relacy.yml` workflow, and
`docs/RT_BUDGET_MAX_OBJECTS.md` for the Release-only soak convention.)
(`SPATIAL_ENGINE_MAX_OBJECTS` is validated to be 64|128 at configure, `core/CMakeLists.txt:75-81`;
`SPATIAL_ENGINE_RT_ASSERTS` → `SPE_RT_ASSERTS=1`, `core/CMakeLists.txt:67-68`.)

### Acceptance criteria (restated, testable)
- A subscriber that missed an echo issues `/sys/state_request` and receives the full current state
  of every touched object on the existing echo addresses + a `/sys/state` sentinel, reconciling to
  `obj_cache_` for every dumped object — `aed/gain/active/width` **unconditionally**, and each DSP
  param (EQ/delay/k_hf/reverb_send) that **differs from its default**, and including **inactive
  objects whose only state is a DSP param** (the extended touched predicate, MUST-FIX 1 / T10).
  **Known residual (independent code-review MINOR, plan-sanctioned by Step 4):** a DSP param that
  was previously non-default and is now back at default is *skipped* (emit-only-non-default), so a
  client that lost the reset-to-default echo keeps a stale non-default value for that one param. The
  fixed fields (aed/gain/active/width) have NO such gap. → **Follow-up #5** (emit all params 0..6
  unconditionally for dumped objects — ≤7 extra pkts/obj, still < the 5000/s guard — for literally
  exact DSP reconcile). Deferred (not unilaterally overriding the APPROVED conditional-emit design).
- Inactive-but-touched objects arrive as `active=0`; never-touched objects are absent.
- A full 128-object dump drops zero packets from a fresh rate window (T4); sustained 500 ms-spaced
  requests at 128 objects drop zero live echoes (T9).
- Rapid repeated requests collapse per the 500 ms debounce (T5).
- Concurrent `/scene/save` + `/sys/state_request` produce 0 races / 0 tears under TSan (T8).
- The new inbound command decodes correctly.
- Backward compatible: no existing echo/command/scene/test behavior changed — **scene-save output is
  byte-identical** (the `include_dsp_only` flag defaults `false` for the `/scene/save` caller, REV4);
  all gates green (incl. the TSan gate).

---

## 5. Risks + mitigations
- **R1 — DSP-detail extension touches `SceneSnapshot.h`/`snapshotObjects` + the touched predicate.**
  Mitigation: additive fields with defaults; hand-rolled `toJson` is fixed-key-order so scene bytes
  are unchanged. **REV4:** the broadened predicate is scoped behind `include_dsp_only` (true only for
  the resync caller), so `/scene/save` is byte-identical — the prior cross-caller side effect is
  gone. **NOTE:** deferring the DSP extension is NOT an acceptable fallback — it would reintroduce the
  MUST-FIX-1 reconcile hole (inactive pure-DSP objects unrecoverable). The extension + the
  flag-scoped predicate are REQUIRED for the correctness guarantee.
- **R2 — Large dump starves live echoes / stalls IO thread.** Mitigation: worst case ≤~1409 pkts <
  5000/s guard; rare client-driven event; debounce caps frequency. Follow-up: pace across K ticks if
  profiling shows a stall.
- **R3 — Rate-guard drop during a concurrent echo burst → partial dump.** Mitigation: graceful
  degradation (existing `/sys/warning`); client re-requests; the sentinel count lets the client
  detect an incomplete burst (received < count) and retry.
- **R4 — Snapshot/echo unit mismatch (deg vs rad, dist_m vs norm).** Mitigation: reuse the exact
  conversions already in the ObjMove echo path; assert wire values in T1.
- **R5 — Request from a non-subscriber yields silence (confusing).** Mitigation: documented contract
  — `state_request` requires prior echo `/sys/handshake`; T7 covers the no-subscriber no-op.
- **R6 — Cross-thread concurrent-reader race on `snapshotObjects()` (CORRECTED in REV2 — was the
  single biggest risk).** The two non-RT callers run on DIFFERENT threads — the existing scene-save
  provider on the **main control-loop thread** (`spatial_engine_core.cpp:547-550,717`) and the new
  `state_request` handler on the **OSC IO thread**. `snapshotObjects()` is single-reader (one
  `snap_reader_busy_idx_`), so concurrent calls break the F4b writer-avoidance invariant → torn read
  vs the audio writer (the race `soak_scene_save_race`/AC9 proves absent). **The original "same IO
  thread / race-free" claim was false.** Mitigation: `std::mutex state_snapshot_mtx_` locked inside
  `snapshotObjects()` serializes the two non-RT readers (amendment #1); the audio writer never locks
  (RT path intact); proven by the TSan T8 gate (Gate 5) + the RT-asserts gate (Gate 3).
- **R7 — DoS / cross-client amplification under auth-off default (REV2 / Architect steelman).** OSC
  auth is OFF by default, and the per-peer 5000/s INBOUND cap is gated on auth being enabled
  (`OSCBackend.cpp:294` wraps the whole rate-limit block). So in the default config there is **no
  inbound rate limit**, and a buggy/malicious client can hammer `/sys/state_request`; because the
  dump is broadcast, it taxes ALL ≤4 subscribers' echo budgets and can starve their live echoes. The
  **only** limiter in the default config is the 500 ms global debounce. Mitigation for v1: the 500 ms
  debounce caps dump frequency (≤2/s, ≤~2818 pkts/s < 5000) regardless of inbound request rate, so the
  guard still has headroom; enabling OSC auth restores the per-peer inbound cap. **Stronger fix
  (follow-up, promoted to RECOMMENDED):** targeted-unicast emit (dump only to the requester) removes
  cross-client amplification and makes a hammering client self-limiting — see ADR follow-ups.

---

## 6. ADR — C6 State Resync via `/sys/state_request`

- **Decision.** Implement a client-driven full-state resync: a new inbound `/sys/state_request ,i
  token` command triggers, on the OSC IO thread, `snapshotObjects()` → re-emission of every touched
  object on the existing C7 `/adm/obj/N/{aed,gain,active,width,dsp}` echo addresses, terminated by a
  single `/sys/state ,i <count>` completion sentinel (`ReplyTag::StateUpdate=0x03`). Dump is
  broadcast to all registered echo subscribers via the existing `buildAndSend` rate-guarded fan-out.
- **Drivers.** Correct reconcile to `obj_cache_`; bounded burst under the 5000 pkt/s guard; minimal
  blast radius + RT inviolate; reuse of the just-shipped C7 echo family.
- **Alternatives considered.** (B) per-command seq + ack/nack — invalidated: recovery still needs
  A's emit, so B is additive cost. (C) periodic 1 Hz full-state push — invalidated: continuous
  bandwidth/rate-guard pressure for a recovery need; ADR0003/0020 own periodic telemetry. Aggregated
  `/sys/state` blob wire format — rejected: needs a new client parser, breaks verb↔echo symmetry.
- **Why chosen.** A is the underlying recovery primitive both B and C depend on, with the smallest
  change surface and the highest reuse; it gives the long-reserved `ReplyTag 0x03` a concrete,
  minimal meaning (a completion sentinel) without committing to a full `/sys/state` blob.
- **Consequences.** New `CommandTag 0x1C` + payload + decoder case; new public `EchoPlane::
  emitStateDump`; sink handler + `SpatialEngine` members (`state_snapshot_mtx_`, `state_dump_buf_`,
  `last_state_request_ms_`); `ObjectSnapshot` gains additive DSP fields (scene-safe). `snapshotObjects()`
  gains a `bool include_dsp_only=false` parameter — the resync caller passes `true`, `/scene/save`
  keeps the default `false`, so **scene-save output is byte-identical and the DSP-reconcile fix is
  scoped to the dump only** (REV4). **A new `std::mutex` now serializes the two non-RT
  `snapshotObjects()` readers (scene-save@control-loop + state_request@IO); the audio writer remains
  lock-free — RT path unchanged** (REV2 amendment #1).
  Debounce is **500 ms** (≤2 dumps/s) to stay under the 5000/s echo guard (REV2 amendment #3). No
  audio-path change. Client reconciles via its existing echo path; a sentinel count enables
  incomplete-burst detection. Broadcast means non-requesting subs get a harmless idempotent refresh —
  **and the global debounce is correct ONLY under broadcast** (REV2 amendment #4): a unicast variant
  would require a per-subscriber debounce.
- **DoS posture (REV2).** OSC auth is off by default and the per-peer inbound cap is auth-gated
  (`OSCBackend.cpp:294`), so the 500 ms global debounce is the only limiter against request-hammering
  in the default config; it bounds dump frequency regardless of inbound rate. Recorded as R7.
- **Follow-ups.** (1) **Targeted-unicast `emitStateDump` — RECOMMENDED for the next iteration**
  (promoted from "optional"): dump only to the requester to remove cross-client amplification and make
  a hammering client self-limiting under auth-off; this REQUIRES switching the debounce to
  per-subscriber and the sentinel to `,ii token count` (the three are one coupled axis). (2) If
  profiling shows IO-thread stall, pace the dump across K ticks. (3) Optional layer B (seq/ack gap
  detection) on top of A for proactive loss signaling. (4) If Step 3 deferred, schedule the
  EQ/delay/HF reconcile completion. (5) **Emit all DSP params 0..6 unconditionally for dumped
  objects** (independent review MINOR-1) so a lost reset-to-default-DSP echo also reconciles —
  ≤7 extra pkts/obj, within the worst-case budget the plan already assumed (≤1409 < 5000/s). Not
  done in this iteration to avoid unilaterally overriding the APPROVED emit-only-non-default design.

---

## 7. Open questions — RESOLVED by Architect (REV1), recorded here

These four are **one coupled axis** (broadcast ⇒ global debounce is fine ⇒ per-requester token in
the sentinel is meaningless; unicast ⇒ per-subscriber debounce ⇒ sentinel carries the token). Resolved:
- **DSP-detail scope: EXTEND `ObjectSnapshot` now.** Correctness + C7 symmetry; scene-byte-safe
  (hand-rolled `toJson` cannot serialize unknown fields). → Step 3 + amendment #6.
- **Pacing: NO pacing in v1.** A single dump (≤~1409) fits the budget; the real risk is FREQUENCY,
  bounded by the 500 ms debounce (amendment #3). Pacing stays a profiling-gated follow-up.
- **Sentinel/token: `/sys/state ,i count`, NO token.** Under broadcast one dump answers multiple
  requesters, so echoing one requester's token misleads the others. The future unicast variant
  switches to `,ii token count`.
- **Broadcast vs unicast: BROADCAST for v1** (with amendments #1/#3/#4); **targeted-unicast RECOMMENDED
  for the next iteration** given the auth-off default has no inbound rate cap (R7 / ADR follow-up #1).

(No remaining open questions for Critic on this axis; `.omc/plans/open-questions.md` updated to mark
them resolved.)

---

## Consensus 진행 로그
<!-- Architect / Critic append findings, decisions, and amendments below. -->

### Architect (REV1)

**VERDICT: SOUND-with-amendments.**

Grounding re-verified against source (all confirmed unless noted):
- `CommandTag 0x1C` free — `SysBinauralSofaSelect=0x1B` is the last System entry, next is `HbPing=0x20` (`Command.h:52,55`). OK.
- `CommandPayload` variant at `Command.h:310-354` (before `PayloadUnknown`). OK.
- `snapshotObjects(std::vector<ObjectSnapshot>&)` out-param; does `out.clear()` + `push_back` (`SpatialEngine.cpp:348,376`). Touched predicate at `:372-374`. OK. ObjCache has `k_hf/user_delay_ms/eq_gain_db[4]` (`SpatialEngine.h:411-414`) — extension is populatable. OK.
- `buildAndSend` signature + `allowSend` 5000/s/subscriber guard (`EchoSubscriber.cpp:147,101`). OK.
- ObjectSnapshot 9 fields (`SceneSnapshot.h:10-20`); toJson/fromJson hand-rolled (no reflection) so additive fields are scene-byte-safe by construction. OK.
- Dispatch: `SysStateRequest` is neither cue nor scene → routes to `sink_(cmd)` on the **OSC IO thread** (`OSCBackend.cpp:333-337`). Confirms the plan's integration point.

**CRITICAL CORRECTION (single biggest risk).** Decision 3 / R6 claim snapshotObjects already runs "on the same IO thread that already calls flush(), which IS the control loop." **This is factually wrong.** The ONLY current caller is the SceneController provider (`spatial_engine_core.cpp:547-550`), invoked from the **main control-loop thread** (`spatial_engine_core.cpp:717` loop → `drainInbound` → `scene_ctrl.handleCommand`). The proposed C6 caller runs on the **OSC IO thread** (sink). These are two DIFFERENT threads. `snapshotObjects` is **single-reader** by design: one `mutable std::atomic<int> snap_reader_busy_idx_` (`SpatialEngine.h:449`). A `/scene/save` (control loop) concurrent with a `/sys/state_request` (IO thread) → both stomp the single busy-claim slot → the writer-avoidance invariant breaks → torn read vs the audio writer — the EXACT race `soak_scene_save_race`/AC9 was built to prove absent. Unlike the audio-writer timing argument, these two event sources are genuinely independent and CAN overlap. The plan ships a latent data race while asserting safety.

**Amendments (mechanical where possible):**
1. **[BLOCKER] Serialize the two non-RT readers.** Add `std::mutex state_snapshot_mtx_` to SpatialEngine; lock it inside `snapshotObjects()` around the reader-claim body (both the scene-save and state_request callers go through it). Audio writer NEVER locks — RT path untouched. Cost: a save+resync overlap briefly serializes; both rare. (Rejected alt: routing SysStateRequest to the control loop — EchoPlane is documented IO-thread-only, would split echo ownership across threads. Worse.)
2. **[BLOCKER] Fix the rationale.** Rewrite Decision 3 + R6: the safety now rests on amendment #1's mutex, NOT on a (false) same-thread claim. State plainly that the existing caller is the main control-loop thread and the new caller is the IO thread.
3. **[HIGH] Debounce vs rate-guard headroom.** 250ms debounce permits 4 dumps/s. At MAX_OBJECTS=128 full-DSP, ≤1409 pkts × 4 ≈ 5636/s > the 5000/s guard → sustained 250ms-spaced requests DO trip the guard and drop **live echoes for ALL subscribers** (broadcast). The plan's "single dump < 5000, no trip" holds only for ≤1 dump/window. Fix: raise `kStateRequestDebounceMs` to **500** (≤2 dumps/s → ≤2818 < 5000), one line.
4. **[MED] Document the debounce↔broadcast coupling.** The GLOBAL debounce is correct ONLY because the dump is broadcast (any serviced request refreshes every subscriber, so subscriber Y is reconciled by X's dump). If follow-up #2 (targeted unicast) ever lands, the debounce MUST become per-subscriber or it becomes a cross-client resync-suppression bug. Add a guard comment at the debounce site + an ADR note.
5. **[MED] Sink pseudo-code uses `steadyNowMs()`** — that helper is in OSCBackend.cpp's anon namespace (`:205`), NOT in SpatialEngine scope. Use the inline chrono idiom already at `SpatialEngine.cpp:314-318`, or add a private member helper.
6. **[LOW] Aggregate-init care (Step 3).** `SpatialEngine.cpp:376-378`: ObjectSnapshot would gain `float eq_gain_db[4]` (C array) while ObjCache has `std::array<float,4>`. The brace-init must enumerate: `..., c.k_hf, c.user_delay_ms, {c.eq_gain_db[0],c.eq_gain_db[1],c.eq_gain_db[2],c.eq_gain_db[3]}`.
7. **[HIGH] Test gaps.** T1-T7 do NOT cover the two highest risks: (a) concurrent `/scene/save` + `/sys/state_request` under TSan (proves amendment #1) — ADD it; (b) sustained debounce-spaced requests vs the guard at 128 (proves amendment #3) — ADD it. T4 only proves a SINGLE fresh-window dump.

**Steelman antithesis (targeted unicast wins on the DoS axis).** Broadcasting a full dump to all ≤4 subscribers on one client's request taxes innocent subscribers' rate budgets and can starve their live echoes. Critically: OSC auth is **OFF by default**, and the per-peer 5000/s inbound cap (`OSCBackend.h:333`) is **only applied when auth is enabled** (`OSCBackend.cpp:294` gates the whole block) — so with the default config there is NO inbound rate limit and a buggy/malicious client can hammer state_request, with the 250ms global debounce as the ONLY limiter. Targeted unicast (dump only to the requester) makes such a client self-limiting and removes cross-client amplification entirely. Given this repo just shipped commit 1306409 (OSC auth gate + per-peer rate limit + audit), the DoS posture is a live concern. Unicast requires (a) a per-subscriber `buildAndSend` target and (b) per-subscriber debounce (else global debounce suppresses Y). **Decision: broadcast is ACCEPTABLE for v1** (max reuse of the fan-out, idempotent refresh) PROVIDED amendments #1+#3+#4 land; but **promote targeted-unicast from "optional follow-up" to RECOMMENDED next iteration**, especially for auth-off deployments. The steelman does not block v1 but it is real.

**Tradeoff tension the plan under-states.** Broadcast-vs-unicast, global-vs-per-subscriber debounce, and token-in-sentinel are NOT three independent open questions — they are one coupled axis. Broadcast ⇒ global debounce is fine ⇒ a per-requester token in the sentinel is meaningless (a single broadcast answers multiple requesters). Unicast ⇒ debounce must be per-subscriber ⇒ sentinel should carry the token. The plan lists them separately and risks resolving them inconsistently.

**§7 open questions — resolved:**
1. *DSP-detail scope:* **EXTEND now.** Correctness + C7 symmetry; scene-byte-safe (hand-rolled toJson cannot serialize unknown fields). Apply amendment #6.
2. *Pacing:* **No pacing in v1.** A single dump (≤1409) fits the budget; the real risk is FREQUENCY, bounded by amendment #3 (500ms debounce), not single-burst size. Pacing stays a profiling-gated follow-up.
3. *Sentinel/token:* **`/sys/state ,i count`, NO token** — under broadcast a single dump answers multiple requesters, so echoing one requester's token misleads the others. A future unicast variant should switch to `,ii token count`.
4. *Broadcast vs unicast:* **Broadcast for v1** (with amendments #1/#3/#4), **unicast RECOMMENDED for the next iteration** given auth-off default has no inbound rate cap.

**Single biggest risk:** the cross-thread single-reader race on `snapshotObjects` (amendment #1) — it silently breaks the F4b TSan-proven safety the entire snapshot mechanism rests on, while the plan asserts it is safe.

### Planner (REV2)

**All 7 Architect amendments + the 4 open-question resolutions applied in place.** Core design (Option A) unchanged.

- **#1 [BLOCKER] — concurrent-reader race accepted and fixed.** Re-verified against source: the only current `snapshotObjects()` caller is the scene-save provider on the **main control-loop thread** (`spatial_engine_core.cpp:547-550`, driven by the `:717` loop), while the C6 caller runs on the **OSC IO thread** — two different threads stomping the single `mutable std::atomic<int> snap_reader_busy_idx_` (`SpatialEngine.h:449`). Added `std::mutex state_snapshot_mtx_` locked **inside** `snapshotObjects()` around the reader-claim body; the audio writer never locks (RT path intact). Updated Decision 3 (full rewrite), Step 5 (member + `#include <mutex>` + lock note), R6 (rewritten from the false same-thread claim). Rejected the control-loop-routing alt (would split IO-thread-only `EchoPlane` ownership).
- **#2 [BLOCKER] — rationale corrected.** Decision 3 and R6 now state plainly: existing caller = control-loop thread, new caller = IO thread, mutex serializes them, writer is lock-free. No surviving "same-thread / race-free" assertion.
- **#3 [HIGH] — debounce 250 → 500 ms.** Confirmed the arithmetic (4 × ≤1409 ≈ 5636 > 5000 broadcast guard). Updated Decision 6, `kStateRequestDebounceMs`, T5, AC, ADR.
- **#4 [MED] — debounce↔broadcast coupling documented.** Guard comment added at the debounce site (Step 5) + Decision 6 note + ADR ("global debounce correct ONLY under broadcast; unicast ⇒ per-subscriber"). 
- **#5 [MED] — `steadyNowMs()` removed.** Step 5 now uses the inline `std::chrono::steady_clock` idiom already at `SpatialEngine.cpp:314-318` (the helper is anon-namespace in `OSCBackend.cpp:205`, out of scope).
- **#6 [LOW] — aggregate-init fixed.** Step 3 now enumerates the C-array: `{c.eq_gain_db[0..3]}` (ObjectSnapshot `float[4]` vs ObjCache `std::array<float,4>`).
- **#7 [HIGH] — test gaps closed.** Added **T8** (concurrent scene-save + state_request under TSan, modeled on `soak_scene_save_race`/AC9 in `core/tests/perf/`, must fail without the mutex) and **T9** (sustained 500 ms-spaced requests at 128 objects → zero live-echo drop, with a 250 ms control sub-case that trips the guard). Added **Gate 5** (TSan ctest) and updated the restated AC.
- **Open questions (one coupled axis) resolved:** EXTEND ObjectSnapshot now; NO pacing in v1; sentinel `,i count` NO token; BROADCAST for v1 with targeted-unicast **promoted to RECOMMENDED** next iteration. Captured the auth-off DoS posture (per-peer inbound cap is auth-gated at `OSCBackend.cpp:294`) as **R7** + ADR DoS-posture note + follow-up #1.

§7 rewritten to "RESOLVED"; `.omc/plans/open-questions.md` to be updated to mark them resolved. Ready for Critic.

### Critic (REV2)

**VERDICT: ITERATE** (Critic gate iteration 1/5). The BLOCKER race fix (amendment #1) is
CORRECT and verified against source. But the plan ships two real defects: (1) a reconcile
*completeness* hole that contradicts its own headline "reconcile exactly to obj_cache_"
guarantee, and (2) a TSan Gate 5 that will not build or run the very target that proves the
BLOCKER fix. Both are concrete and must be fixed before APPROVE.

**Verified SOUND (no change needed):**
- *Amendment #1 race fix — CORRECT.* Both readers route through `snapshotObjects()`: scene-save
  at `core/src/bin/spatial_engine_core.cpp:549` (`engine.snapshotObjects(out)`, main control-loop
  thread) + new state_request (IO thread) → a `std::mutex` inside `snapshotObjects()` serializes
  both. The audio WRITER (`SpatialEngine.cpp:700-705`: 2 atomic loads + buffer copy + release
  store) NEVER takes the mutex — RT path intact. Lock at top-of-function covers the full
  load-claim-confirm-read body incl. the `idx<0` early return. No reentrancy/lock-ordering hazard.
  **Position: the mutex is the RIGHT choice over a 2nd lock-free reader slot** — with only 3
  `snap_buf_` buffers, two concurrent reader claims + the published index would occupy all three,
  starving the writer of a free buffer; both readers are non-RT and rare, so serialization is free.
- *R7 auth-off DoS — verified ACCURATE and adequately mitigated for v1.* `OSCBackend.cpp` gates the
  ENTIRE per-peer rate block under `if (!auth_token_.empty())` (confirmed: `kRateMaxPerSec` lives
  inside that block). Default config = no inbound cap. BUT the 500ms global debounce checks BEFORE
  `snapshotObjects()`, so even an unauthenticated request flood collapses to ≤2 broadcast dumps/s
  (≤~2818 pkts/s < 5000) — each ignored request costs one chrono-compare-return. Amplification is
  bounded. Deferral of targeted-unicast is honestly justified and documented. Not a blocker.
- *Spot-checks PASS:* CommandTag `0x1C` free (`SysBinauralSofaSelect=0x1B` → `HbPing=0x20`).
  `buildAndSend` arg order matches the planned `/sys/state ,i count` sentinel call (cf.
  `EchoSubscriber.cpp:223`); non-`/adm` address works (`/transport/play`). ObjectSnapshot DSP-field
  additions are scene-byte-safe — `SceneSnapshot.cpp` toJson/fromJson are fixed-key-order and never
  touch unknown fields (verified :62-69, :110-118). Gates 1-3 valid: `SPATIAL_ENGINE_RT_ASSERTS`
  (`core/CMakeLists.txt:67-68`) + MAX_OBJECTS 64|128 configure validation (`:76-81`) confirmed.
  PayloadObjDsp::Param ids confirmed (EQ 0-3, delay 4, k_hf 5, reverb_send 6, `Command.h:242-247`).

**MUST-FIX (blocking):**

1. **[MAJOR — reconcile hole] The "touched" predicate excludes the very DSP fields Step 3 adds → a
   pure-DSP object is silently never dumped, falsifying the plan's "reconcile EXACTLY to obj_cache_"
   guarantee.** `SpatialEngine.cpp:372-374` is `c.active || az||el || dist!=1 || gain_lin!=1 ||
   reverb_send!=0 || width_rad!=0 || algo!=VBAP`. It does **NOT** test `k_hf`, `user_delay_ms`, or
   `eq_gain_db[]`. Decision 4 and Step 3 both say *"Touched predicate unchanged."* Consequence: an
   INACTIVE object whose only non-default state is an EQ band (param 0-3), user delay (4), or k_hf
   (5) — everything else default — returns `touched==false` and is omitted from the dump. A client
   that set `/adm/obj/N/dsp` (EQ/delay/HF) on such an object and lost the echo will **never** recover
   it. This directly contradicts Decision-driver #1, Decision 4 ("makes the dump reconcile **exactly**
   to obj_cache_"), and AC line 441. Fix: extend the touched predicate to also test
   `c.k_hf != 0.5f || c.user_delay_ms != 0.f || c.eq_gain_db[0..3] != 0.f` (guard the 0.5f k_hf
   default explicitly). **AND** add a test case: a single inactive object with ONLY an EQ band set,
   assert it is dumped with `active=0` + the correct `/dsp` packet. T1 as written drives objects that
   also set aed/gain/active/width, so it does NOT currently exercise this path — the hole passes the
   test plan silently.

2. **[MAJOR — verification rigor] Gate 5 (the TSan proof of the BLOCKER fix) is invoked with a flag
   combination that builds and runs NOTHING.** `core/tests/perf/CMakeLists.txt:5` gates the entire
   soak block (incl. `soak_scene_save_race`) under `if(CMAKE_BUILD_TYPE STREQUAL "Release" OR
   SPATIAL_ENGINE_RUN_SOAK)`, and each `add_test` carries `CONFIGURATIONS Release`. Plan Gate 5 sets
   neither `-DCMAKE_BUILD_TYPE=Release` nor `-DSPATIAL_ENGINE_RUN_SOAK=ON` → the soak targets
   (`soak_scene_save_race` and the new `state_resync_race`) are **never compiled**, and
   `ctest -R 'scene_save_race|state_resync_race'` matches **zero tests** → false green / "No tests
   were found", NOT a clean TSan run. Even if compiled, `CONFIGURATIONS Release` requires the ctest
   run to be in the Release config. Fix: mirror the real mechanism (cf. `.github/workflows/relacy.yml`
   and `docs/RT_BUDGET_MAX_OBJECTS.md:43` — soak gated on `Release OR SPATIAL_ENGINE_RUN_SOAK`):
   add `-DCMAKE_BUILD_TYPE=Release` (required for the soak block to exist) alongside the
   `-fsanitize=thread` flags, and ensure ctest runs the Release config (single-config Makefile gen:
   CMAKE_BUILD_TYPE=Release satisfies `CONFIGURATIONS Release`). Also document the `setarch -R`
   ASLR/TSan mmap workaround already noted at `core/tests/perf/CMakeLists.txt:16` for the sibling
   `soak_cue_cmdfifo_race` gate. Until Gate 5 actually builds+runs the race target, amendment #1
   (the single biggest risk) is UNPROVEN.

**Non-blocking nits:**
- §4 header lists T8 under `test_state_resync.cpp` (core_unit) then says it "belongs in
  core/tests/perf/". State unambiguously: T8 → `core/tests/perf/` (or extend AC9), registered in the
  `Release OR RUN_SOAK` block with `CONFIGURATIONS Release`; T1-T7,T9 → core_unit.
- Step 4 `/dsp` emit: mirror the EXACT arg order of the existing dsp echo call
  (`EchoSubscriber.cpp:254`: types `",if"`, float=value ptr, int=param-id ptr) to avoid the executor
  swapping the int/float operands; reverb_send is dsp param 6 (`Command.h:244`), so it can ride the
  same `/dsp` path rather than a separate address — pick one and pin it.

**What changes for APPROVE:** fix the touched predicate + add the pure-DSP reconcile test (#1), and
correct Gate 5 to the real Release/RUN_SOAK + Release-config mechanism so the BLOCKER's TSan proof
actually runs (#2). The design (Option A), the mutex fix, the 500ms debounce arithmetic, R7 honesty,
and the coupled-axis resolution are all sound — this is a targeted ITERATE, not a structural reject.

### Planner (REV3)

**Both MAJOR must-fixes + both nits applied in place. Design unchanged (Option A, mutex fix, 500 ms debounce, broadcast-v1).** Each fix was verified against source before editing.

- **MUST-FIX 1 [MAJOR — reconcile hole] — ACCEPTED & FIXED.** Confirmed at `SpatialEngine.cpp:372-374` the touched predicate tests only `active/az/el/dist/gain_lin/reverb_send/width_rad/algo` — it does NOT test `k_hf`, `user_delay_ms`, or `eq_gain_db[]`, so an inactive object whose only non-default state is an EQ band / user delay / k_hf returns `touched==false` and was silently omitted → unrecoverable, breaking the "reconcile EXACTLY to `obj_cache_`" headline guarantee.
  - **(1a)** Decision 4 + Step 3 now REQUIRE extending the predicate, with the exact added clause `|| c.k_hf != 0.5f || c.user_delay_ms != 0.f || c.eq_gain_db[0..3] != 0.f`. Verified `ObjCache::k_hf` default is **0.5f** (`SpatialEngine.h:411`) and explicitly guarded that default. Removed the stale "Touched predicate unchanged" line.
  - **(1b)** Called out the shared-caller consequence explicitly: `snapshotObjects()` is also the `/scene/save` source (`spatial_engine_core.cpp:549`), so the broader predicate now also persists pure-DSP objects in saved scenes (correct — a latent scene-save omission, but it changes scene-save content). Updated the scene-regression note to assert **correct values for now-included objects, NOT unchanged count**. Updated R1 to forbid the defer-DSP fallback (it would reintroduce the hole) and the restated AC to make EQ/delay/HF + pure-DSP-inactive reconcile unconditional.
  - **(1c)** Added **T10** (pure-DSP inactive object: only an EQ band set, object inactive → asserts it IS dumped with `/adm/obj/N/active ,i 0` + the correct `/dsp` packet; plus k_hf-only and delay-only sub-cases). Explicitly noted T1 does NOT cover this path (it drives aed/gain/active/width).

- **MUST-FIX 2 [MAJOR — verification rigor] — ACCEPTED & FIXED.** Confirmed the entire perf/soak block is gated by `if(CMAKE_BUILD_TYPE STREQUAL "Release" OR SPATIAL_ENGINE_RUN_SOAK)` (`core/tests/perf/CMakeLists.txt:5`) with `CONFIGURATIONS Release` on each `add_test` (`soak_scene_save_race` at `:42-48`). The old Gate 5 set NEITHER Release nor RUN_SOAK → targets never compiled, `ctest -R …race` matched zero tests → false green, leaving the BLOCKER fix UNPROVEN. Rewrote Gate 5: `-DCMAKE_BUILD_TYPE=Release` (so the soak block EXISTS) + `-fsanitize=thread` (compile+link), run via `setarch -R ctest -C Release -R 'scene_save_race|state_resync_race'`, with an explicit "count>0 or it's a FAILED gate" check. Documented the `setarch -R` ASLR/TSan-mmap workaround (per `core/tests/perf/CMakeLists.txt:16-18`). Cross-checked against `.github/workflows/relacy.yml` (uses `-DCMAKE_BUILD_TYPE=Release`) and `docs/RT_BUDGET_MAX_OBJECTS.md` (Release-OR-RUN_SOAK soak convention).

- **N1 [test placement] — FIXED.** §4 now states unambiguously: T1-T7, T9, T10 → `core/tests/core_unit/test_state_resync.cpp`; **T8 (the concurrent scene-save + state_request race) → `core/tests/perf/state_resync_race.cpp`**, registered INSIDE the `Release OR RUN_SOAK` block with `CONFIGURATIONS Release`, mirroring `soak_scene_save_race`.

- **N2 [dsp emit operand order] — FIXED.** Step 4 now pins the exact mirror of `EchoSubscriber.cpp:254`: `buildAndSend(addr, ",if", &value, 1, &param_id, 1, …)` (float=value, int=param-id, do not swap), with the param-id→field map `0..3→eq_gain_db`, `4→user_delay_ms`, `5→k_hf`, `6→reverb_send` (per `Command.h:244-248`). **reverb_send rides the SAME `/adm/obj/N/dsp` path as param 6**, not a separate address; width stays single-sourced on `/adm/obj/N/width` (param 7 is not a `/dsp` packet).

Status bumped to REV3. Critic-verified items (mutex BLOCKER fix, R7/auth-off, 500 ms arithmetic, scene-byte-safety) untouched. Ready for re-gate.

### Critic (REV3)

**VERDICT: APPROVE — consensus reached.** Both MAJOR must-fixes are verifiably resolved against
source; both nits applied. One residual is a real but cosmetic-only, acknowledged, gated side effect
plus a stale AC wording bullet — neither blocks execution. Non-blocking cleanups listed below.

**MUST-FIX 1 (reconcile hole) — CORRECTLY FIXED, verified.**
- Touched-predicate extension (Step 3, `SpatialEngine.cpp:372-374`) adds
  `|| c.k_hf != 0.5f || c.user_delay_ms != 0.f || c.eq_gain_db[0..3] != 0.f`. Each guarded default
  matches `ObjCache`: `k_hf=0.5f` (`SpatialEngine.h:411`), `user_delay_ms=0.f` (`:412`),
  `eq_gain_db{0,0,0,0}` (`:414`). The hole (inactive pure-DSP object → `touched==false` → omitted) is
  closed. "Touched predicate unchanged" removed.
- T10 is a REAL (non-hollow) test: sets ONE inactive object's only non-default state to `eq_gain_db[1]`
  via `/adm/obj/N/dsp ,if 1 6.0`, asserts it IS dumped with `/adm/obj/N/active ,i 0` + the matching
  `/dsp` packet, plus k_hf-only and delay-only sub-cases covering the full predicate extension. It
  exercises exactly the path T1 does not.

**MUST-FIX 2 (Gate 5 TSan) — CORRECTLY FIXED, verified against the real mechanism.**
- Rewritten Gate 5 adds `-DCMAKE_BUILD_TYPE=Release` (so the `if(CMAKE_BUILD_TYPE STREQUAL "Release"
  OR SPATIAL_ENGINE_RUN_SOAK)` block at `core/tests/perf/CMakeLists.txt:5` compiles the soak targets),
  `-fsanitize=thread` on BOTH `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`, and runs via
  `setarch -R ctest -C Release -R 'scene_save_race|state_resync_race'`. The `-C Release` satisfies the
  per-test `CONFIGURATIONS Release` filter; the regex substring-matches both `soak_scene_save_race`
  and the new `state_resync_race`; the explicit "count>0 or FAILED gate" check defeats the
  "No tests were found" false-green. `setarch -R` ASLR workaround documented per `CMakeLists.txt:16`.
  Cross-checked against `.github/workflows/relacy.yml` (Release) — sound.
- N1 (placement) fixed: T1-T7,T9,T10 → core_unit; T8 → `core/tests/perf/state_resync_race.cpp` inside
  the Release-OR-RUN_SOAK block with `CONFIGURATIONS Release`. N2 fixed: Step 4 pins
  `buildAndSend(addr, ",if", &value, 1, &param_id, 1, …)` (float=value, int=param-id, no swap) with the
  `0..3→eq, 4→delay, 5→k_hf, 6→reverb_send` map on the single `/dsp` address — matches `Command.h:244`.

**Scene-save side effect — VERIFIED real but cosmetic-only; acknowledged; NOT a hidden regression.**
Confirmed the provider feeds `snapshotObjects(out)` straight into the scene `objects` vector
(`spatial_engine_core.cpp:548-550` → `SceneController` toJson), so the broadened SHARED predicate does
add pure-DSP inactive objects to saved scenes. BUT this is functionally inert: `toJson` does not
serialize the DSP fields (fixed key order, `SceneSnapshot.cpp:62-69`), so the phantom serializes as a
muted/default object; on reload its `obj_cache_` slot lands at default-inactive — **identical to the
pre-REV3 outcome where the object was simply absent** (also default-inactive). The only observable
delta is the scene-file object *count*/listing, with zero audio/functional reload difference. The plan
acknowledges it (Step 3 1b, R1, scene-regression note) and instructs the correct test (assert values,
not count). So it is bounded, harmless, and gated — does not block.

**Non-blocking cleanups (do before execution; none alter the design):**
1. **AC self-contradiction.** Restated AC still says *"no existing echo/command/scene/test behavior
   changed"* (§4 AC, ~line 529) while Step 3 1b / R1 / the scene-regression note correctly acknowledge
   scene-save content now changes. Resolve the wording: either carve out the scene-content exception in
   that AC bullet, OR — cleaner, and preserves Principle 5 / minimal blast radius — **scope the
   broadened predicate to the resync dump only** (e.g. a `bool include_dsp_only=false` param on
   `snapshotObjects`, true only on the `state_request` path; the mutex already serializes both
   callers), keeping `/scene/save` byte-identical. Recommended: the scoped option; it removes the
   phantom-object side effect AND the AC contradiction in one move.
2. **Framing nit.** "fixes a latent scene-save omission" (Step 3 1b) overstates it — reloaded
   `obj_cache_` is functionally identical with or without the phantom; the change is cosmetic
   (scene-file object count). State it as a cosmetic consequence, not a fix.
3. **Pin the perf test NAME.** Ensure the `add_test(NAME …)` for the new perf race target contains
   `state_resync_race` so Gate 5's `-R 'scene_save_race|state_resync_race'` matches deterministically
   (the plan says "mirror soak_scene_save_race" — make the NAME explicit).

**Consensus:** the design (Option A), the mutex BLOCKER fix, the 500 ms debounce, broadcast-v1, R7
honesty, the reconcile-predicate fix, and the corrected TSan gate are all sound and source-verified.
APPROVED for autopilot execution; the three cleanups above are wording/optional-refactor only and do
not gate the build.

### Planner (REV4)

**Consensus reached (Architect SOUND + Critic APPROVE). Applied the 3 non-blocking cleanups; Status → APPROVED, execution-ready. Design unchanged (Option A, mutex BLOCKER fix, 500 ms debounce, broadcast-v1, T10, Gate 5).**

- **Cleanup #1 [refactor — scope predicate to dump only].** Added a `bool include_dsp_only=false` parameter to `snapshotObjects(std::vector<ObjectSnapshot>&, bool=false)`; the DSP clause (`k_hf!=0.5f || user_delay_ms!=0.f || eq_gain_db[0..3]!=0.f`) is gated on it. The `/sys/state_request` caller passes `true` (Step 5 call site updated to `snapshotObjects(state_dump_buf_, /*include_dsp_only=*/true)`); `/scene/save` (`spatial_engine_core.cpp:549`) keeps the default `false` → **scene-save is byte-identical, no phantom objects**. The existing mutex already serializes both callers, so the shared method-with-flag is safe. Updated Decision 4, Step 3 (predicate + caller-wiring bullets), Step 5 (call site + body note), R1, the ADR Consequences, the scene-regression note, and the restated AC. The "no existing scene behavior changed" guarantee is now literally true — carve-out deleted.
- **Cleanup #2 [wording].** Removed the "fixes a latent scene-save omission" framing from Step 3/1b (scene-save is now unchanged, so the claim no longer applies); the scene-regression section now simply asserts existing round-trips are byte-identical.
- **Cleanup #3 [naming].** Pinned the perf TSan target: `add_test(NAME state_resync_race COMMAND state_resync_race CONFIGURATIONS Release)` in `core/tests/perf/CMakeLists.txt` (full block spelled out), mirroring `soak_scene_save_race`, so Gate 5's `-R 'scene_save_race|state_resync_race'` matches deterministically.

This is the final pass. The plan is execution-ready for `/oh-my-claudecode:autopilot`.
