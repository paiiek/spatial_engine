# Plan — v0.9 Lane 6 / Gap C7: outbound echo for `/obj/dsp` (ObjDsp)

**Status:** ✅ APPROVED (ralplan consensus reached iter 3/5 — Critic REV4 APPROVE 2026-06-08). Ready for autopilot/executor.
**Mode:** SHORT (observability gap, additive, no DSP/audio-path change, no hardware)
**Plan file (durable artifact):** `.omc/plans/spatial-engine-v0.9-lane6-c7-objdsp-echo.md`
**Repo root:** `/home/seung/mmhoa/spatial_engine`

---

## 1. Problem statement

When a client sends `/obj/dsp ,iif obj_id param value` (per-object DSP: EQ bands, user
delay, HF rolloff, reverb send, width), the engine applies it to the audio-thread object
cache but emits **no outbound echo**. Every other object verb already echoes on the
ADM-OSC echo plane (port 9102): ObjMove→`/adm/obj/N/aed`, ObjGain→`/adm/obj/N/gain`,
ObjMute→`/adm/obj/N/mute`, ObjActiveAdm→`/adm/obj/N/active`, ObjWidth→`/adm/obj/N/width`,
ObjXYZ→`/adm/obj/N/xyz`, ObjName→`/adm/obj/N/name`. Because ObjDsp does not, a subscriber
**cannot verify** that an EQ/DSP change was applied. C7 closes this observability gap by
adding an ObjDsp echo that mirrors the existing `EchoPlane` `mark*()`→`flush()` idiom.

### Verified grounding (re-read confirmed during this pass)
- **Inbound decode:** `core/src/ipc/CommandDecoder.cpp:550-564` — `/obj/dsp` → `CommandTag::ObjDsp`
  (tag `0x60`, `core/src/ipc/Command.h:89`), payload `PayloadObjDsp{obj_id, param, value}`
  (`core/src/ipc/Command.h:246-260`). Param enum (uint8): EqLow=0, EqLowMid=1, EqHighMid=2,
  EqHigh=3, DelayMs=4, KHF=5, ReverbSend=6, Width=7. Decoder accepts param 0..7, rejects
  out-of-range as Unknown.
- **Gap site (mark must be added here):** `core/src/core/SpatialEngine.cpp:87-93` — the ObjDsp
  control-thread case queues to the audio FIFO (`qc.dsp_param`, `qc.dsp_value`) but makes
  **no** `echoPlane().mark*()` call. This is the same OSC/control (non-RT) thread on which
  `markWidth` is called for ObjWidth at `SpatialEngine.cpp:163` and `markGain` at `:46`.
- **Audio-thread apply (unchanged by this plan):** `core/src/core/SpatialEngine.cpp:626-638`
  switches on `qc.dsp_param` 0..7 into `ObjCache` fields. We touch only the echo (control)
  side, never this path.
- **Echo infra:** `core/src/ipc/EchoSubscriber.h` / `.cpp`. `EchoAddr` enum has 7 entries
  (Aed/Xyz/Gain/Mute/Active/Width/Name), `kEchoAddrCount=7` (`.h:41`). Dirty map is
  `kEchoMaxObjects × kEchoAddrCount` bits (`.h:74-102`). `EchoObjCache` holds per-object
  cached inbound values (`.h:61-71`). `mark*()` store value + set dirty bit; `flush(now_ms,
  send_fd)` iterates objects, and per dirty bit calls `buildAndSend(addr, types, floats,
  n_floats, ints, n_ints, str, now_ms, send_fd)` (`.cpp:190-256`). Width echo idiom:
  `/adm/obj/N/width ,f` at `.cpp:232-237`. **No `markDsp` and no DSP slot exist yet.**
- **Reconciliation facts:** ObjWidth (tag `0x08`) HAS a dedicated verb + echo (markWidth→
  `/adm/obj/N/width`). ReverbSend (param 6) has **no** dedicated OSC verb or echo — grep
  confirms `reverb_send` exists only as an internal engine/scene field and is set solely via
  ObjDsp param 6 (and `CueEngine.cpp:116-123`). So param 7 (Width) is the only overlap with
  an existing dedicated echo.
- **Tests:** `core/tests/core_unit/test_echo_plane.cpp` (10 conformance tests; pattern
  `ep.mark*()`→`ep.flush(now_ms, -1)`; test 6 coalesces all addrs in one tick; test 7 uses a
  real UDP fd to exercise the rate guard). Registered at
  `core/tests/core_unit/CMakeLists.txt:305-307` (`add_test(NAME echo_plane ...)`).
  `core/tests/core_unit/test_osc_outbound_reply_smoke.cpp` shows the loopback bind +
  `recvfrom` + OSC-parse idiom we reuse to assert wire content.

---

## 2. RALPLAN-DR summary

### Principles
1. **Mirror the established echo idiom exactly** — same `mark*()`→`flush()` lifecycle, same
   `buildAndSend` packet path, same thread model (OSC/control thread only). No new mechanism.
2. **One authoritative address per logical parameter** — a client must never receive two
   conflicting echoes for the same underlying state (width).
3. **Preserve coalesce semantics** — distinct parameters set within one tick must each echo;
   the same parameter set twice in one tick echoes once with the latest value.
4. **Audio path is sacrosanct** — zero changes to the audio-thread apply path; mark site must
   be no-alloc / no-lock, identical in cost profile to `markWidth`.
5. **Additive & backward compatible** — a new outbound address must not alter or break any
   existing echo, subscriber, or test.

### Top-3 decision drivers
1. **Coalescing correctness across 8 params** — EqLow then EqHigh in one flush tick must yield
   two echoes, not one. A single coarse dirty bit per object cannot express this.
2. **Wire-contract clarity** — the address scheme must be unambiguous and round-trippable, and
   must reconcile the param 7 / Width overlap so subscribers see exactly one width source.
3. **Cost & blast radius** — per-object cache growth and any change to `kEchoAddrCount` /
   dirty-map sizing must stay trivial and provably non-breaking.

### Design decision 1 — DSP echo address scheme

| Option | Sketch | Pros | Cons |
|---|---|---|---|
| **(a)** single `/adm/obj/N/dsp ,if` (param,value), coarse 1-bit dirty | minimal cache | smallest delta | **breaks coalesce**: EqLow then EqHigh in one tick → only the last param echoes. Violates Principle 3. ❌ |
| **(b)** per-param address `/adm/obj/N/dsp/<param> ,f` | 7 addresses | natural per-param coalesce | 7 new wire addresses to document/parse; larger dirty-map growth; diverges from the flat `/adm/obj/N/<x>` family. |
| **(c) CHOSEN** single `/adm/obj/N/dsp ,if` + per-object 8-slot value array + 8-bit param dirty mask, gated by one coarse `EchoAddr::Dsp` bit | flush emits one packet **per dirty param** | one clean address; full per-param coalesce; one new EchoAddr bit; tiny cache delta (~33 B/obj) | flush has a small inner loop over the param mask; `kEchoAddrCount` 7→8 (internal only). |

**Chosen: (c).** It is the only option that keeps a single, flat, round-trippable address AND
preserves per-param coalesce. (a) is invalidated by the coalesce requirement.

**(REV2 — Architect amendment 4) The (c)-over-(b) tiebreaker is verb↔echo symmetry, not "wire
bloat".** Every existing echo lifts `obj_id` into the address and mirrors exactly **one inbound
verb 1:1**. There is a single inbound verb `/obj/dsp ,iif obj_id param value`; the family
convention says it must mirror to a single outbound `/adm/obj/N/dsp ,if param value` (obj_id
lifted into the address). Option (b) fragments that one verb into 6 sibling addresses
(`/adm/obj/N/dsp/<param>`), breaking the verb↔echo symmetry and the round-trip clarity that
lets a client mechanically pair its write with the echo it should expect. That symmetry — not
documentation cost — is the deciding principle; (b)'s wire surface is machine-generated/parsed
and was overstated in the original draft.

**Honest tradeoff (not a free win):** option (b) would reuse the *existing* coarse 1-bit-per-
`EchoAddr` dirty mechanism with **zero** new struct fields, which would **eliminate risk R2**
(the manual `dsp_param_dirty = 0` clear) and **eliminate risk R4** (the int-mixed `,if`
typetag — (b) is pure-float `,f`). Choosing (c) deliberately accepts R2 and R4 (both fully
mitigated below) in exchange for verb↔echo symmetry and round-trip clarity. It is a considered
tradeoff, not a strict dominance.

### Design decision 2 — param 7 (Width) overlap, and ReverbSend(6)

| Option | Behaviour | Verdict |
|---|---|---|
| route param 7 through the existing **markWidth → `/adm/obj/N/width ,f`** echo; do **not** emit a dsp echo for 7 | single authoritative width address | **CHOSEN** |
| emit `/adm/obj/N/dsp ,if` for param 7 too | width state echoed on two addresses | ❌ double-echo / client confusion; violates Principle 2 |

**Rule (explicit):** In the ObjDsp mark site, `param == 7` calls `markWidth(obj, value)` (same
echo the dedicated ObjWidth verb produces); `param` 0..6 calls `markDsp(obj, param, value)`.
`markDsp` itself ignores `param == 7` defensively (no-op) so policy holds regardless of caller.
**ReverbSend (param 6)** has no dedicated verb/echo (verified), so it echoes normally via the
dsp address — no reconciliation needed.

**(REV2 — Architect decisive point) Decision 2 is MANDATORY, not merely preferable.** The audio
apply path writes the **same `c.width_rad` field** for both verbs: ObjDsp param 7 at
`SpatialEngine.cpp:635` (`case 7: c.width_rad = qc.dsp_value;`) and the ObjWidth audio-apply case
body (`case ObjWidth: c.width_rad = qc.width_rad;`, near `:661-662`) both mutate one shared width
state. **(REV3 — Critic nit N2, citation precision)** `SpatialEngine.cpp:161` is the *ObjWidth
control-thread MARK* site (`qc.width_rad = p->width_rad;`), one line above the `markWidth` call at
`:163` — it is NOT the audio apply; the apply that writes `c.width_rad` is the separate case body
(~`:661-662`). The dsp p7 `value` is **already in radians** (`Command.h:245`, "param 7 → source
width in radians"), so `markWidth(obj, p->value)` is **units-correct** with no conversion. Therefore the canonical width address `/adm/obj/N/width`
reflects the live width *only if* p7 routes through `markWidth`. **If we did NOT route p7,
`/adm/obj/N/width` would go silently STALE** whenever width is driven via `/obj/dsp` param 7 —
a subscriber would observe a width value that no longer matches engine state. That staleness is
a correctness defect, which is why routing p7 → `markWidth` is required, not just the tidier
option. The cross-address ergonomic asymmetry (documented in the wire contract + mark-site
comment + `test_dsp_param7_routes_to_width`) is the accepted cost of this correctness guarantee.

### Design decision 3 — cache representation & coalescing

- **`dsp_value[8]` (values) lives in `EchoObjCache`** — like every other cached inbound value
  (idempotent re-emit source), reset is not required for correctness.
- **(REV3 — Critic MUST-FIX 2) The per-param DIRTY MASK lives in `EchoDirtyMap`, NOT
  `EchoObjCache`.** Add `uint8_t dsp_param_dirty[kEchoMaxObjects]` (one byte per object; bits
  0..6 used, bit 7 unused since width routes elsewhere) **inside `EchoDirtyMap`**, and have
  `EchoDirtyMap::clear()` `memset` it to 0 **in the same call** that already zeroes `obj_bits`
  and the transport flags (`EchoSubscriber.h:83-87`).
  *Rationale (robust single-reset):* the original draft placed the mask in `EchoObjCache`, but
  the two reset paths that fire `dirty_.clear()` — `flush()`'s no-subscriber early return
  (`EchoSubscriber.cpp:190-194`) and `close()` (`:126-130`) — only reset `EchoDirtyMap`, NOT
  `EchoObjCache`. A param marked while **no subscriber is attached** (a real lifecycle:
  subscribers are evicted after `kEchoSubscriberTtlMs`=30 s via `evictStale`, then re-subscribe)
  would be **retained and spuriously emitted on the first dsp flush after (re)attach**. Legacy
  addresses cannot leak (their only dirty state is the coarse `obj_bits` bit, fully reset by
  `clear()`), so an `EchoObjCache`-resident mask would break family parity (Principles 1/5).
  Co-locating the mask with the coarse bits makes `dirty_.clear()` the **single** reset site for
  ALL dirty state — eliminating the entire "missed a clear site" bug class (exactly what would
  have bitten us) rather than patching two call sites a future third reset path could
  re-introduce. The alternative (keep it in `EchoObjCache` and add explicit resets at BOTH the
  no-subscriber early return AND `close()`) is rejected as fragile.
- Add `EchoAddr::Dsp = 7`; bump `kEchoAddrCount` 7→8 (dirty-map `kBitsPerObj` 7→8). Internal
  only — no wire impact, no subscriber impact.
- `markDsp(obj, param, value)`: if `param > 6` return; else `obj_cache_[obj].dsp_value[param] =
  value`, `dirty_.dsp_param_dirty[obj] |= (1<<param)`, `dirty_.mark(obj, EchoAddr::Dsp)`.
- `flush()`: when `dirty_.test(obj, EchoAddr::Dsp)`, loop `p = 0..6`; for each set bit in
  `dirty_.dsp_param_dirty[obj]` emit `/adm/obj/<obj>/dsp ,if` with `ints={p}` (note `,if` order
  = int param then float value, matching the inbound `,iif` minus obj_id) and
  `floats={dsp_value[p]}`. **No per-object inline clear needed** — the `dirty_.clear()` at the
  end of `flush()` (and the early-return / `close()` paths) now zero the whole mask uniformly.
- **Coalesce semantics preserved:** EqLow then EqHigh in one tick → bits 0 and 3 set → two
  packets. EqLow twice → bit 0, latest value → one packet. Matches existing per-addr behaviour.
- **Memory delta:** `dsp_value[8]` = 32 B/obj in `EchoObjCache` + 1 B/obj mask in `EchoDirtyMap`
  = 33 B/obj → 64 obj ≈ 2.1 KB, 128 obj ≈ 4.2 KB. Trivial.

### Design decision 4 — RT-safety at the mark site

`markDsp` is called from `SpatialEngine.cpp:87-93`, the **OSC/control (non-RT) thread** — the
identical thread and call style as `markWidth` at `:163`. `markDsp` performs only an array
write + a bit-OR + an existing `dirty_.mark()` bit set: **no alloc, no lock, no syscall**,
same cost profile as `markGain`/`markWidth`. Echo emission happens later on the OSC IO thread
in `flush()`. The audio thread is never touched. Build is verified under
`-DSPATIAL_ENGINE_RT_ASSERTS=ON` to confirm no-alloc on the relevant paths.

### Address-scheme decision (wire contract, frozen)
```
/adm/obj/<N>/dsp ,if  <param 0..6>  <value>
```
`,if` = (int param, float value). One packet per dirty param per flush tick. Param 7 is NOT
emitted on this address; width is observed exclusively on `/adm/obj/<N>/width ,f`.

**(REV2 — Architect amendment 3a) Cross-address contract (client-facing, MUST be documented):**
A client that writes `/obj/dsp ,iif N 7 <w>` (param 7 = Width) **must subscribe to
`/adm/obj/N/width`, never `/adm/obj/N/dsp`** — param 7 produces **no** packet on the dsp
address. This is the one ergonomic footgun of decision 2: a verifier watching `/adm/obj/N/dsp`
after a param-7 write will see silence and could wrongly conclude the write was dropped. It is
intentional and state-correct (see Decision 2), and is pinned here, in a mark-site code comment
(amendment 3b, Step 3), and by `test_dsp_param7_routes_to_width` (amendment 3c).

---

## 3. Implementation plan (exact files / functions)

> Echo side only. **No change** to `Command.h`, `CommandDecoder.cpp`, `CommandFifo.h`, or the
> audio-thread apply path (`SpatialEngine.cpp:626-638`).

### Step 1 — Extend EchoPlane data model (`core/src/ipc/EchoSubscriber.h`)
1. Add `Dsp = 7` to `enum class EchoAddr`; change `kEchoAddrCount` 7→8 (`.h:41,43-51`).
2. In `struct EchoObjCache` (`.h:61-71`) add `float dsp_value[8] = {};` (values only).
2b. **(REV3 — Critic MUST-FIX 2) Put the per-param dirty mask in `EchoDirtyMap`, not
   `EchoObjCache`.** Add `uint8_t dsp_param_dirty[kEchoMaxObjects] = {};` to `struct
   EchoDirtyMap` (`.h:74-102`), and extend `EchoDirtyMap::clear()` (`.h:83-87`) to also
   `std::memset(dsp_param_dirty, 0, sizeof(dsp_param_dirty));`. This makes `clear()` the single
   reset site for all dirty state (see Decision 3 rationale).
3. Add `markDsp(uint32_t obj_id, uint8_t param, float value) noexcept` next to `markWidth`
   (`.h:176-180`): bounds-check `obj_id < kEchoMaxObjects` and `param <= 6`; store
   `obj_cache_[obj_id].dsp_value[param]`, set `dirty_.dsp_param_dirty[obj_id] |= (1<<param)`,
   `dirty_.mark(obj_id, EchoAddr::Dsp)`.
   **Acceptance:** header compiles; `EchoDirtyMap::kObjBytes` recomputes from
   `kEchoAddrCount=8` (64→512 bits, 128→1024 bits) with no other code hard-coding 7; `clear()`
   zeroes `dsp_param_dirty` (verified by the no-subscriber regression test, Step 4).
4. **(REV2 — Architect amendment 1) Update the stale header doc-comment block at
   `EchoSubscriber.h:9-14`.** It currently reads "Addresses echoed: /adm/obj/N/{aed,xyz,gain,
   mute,active,width,name}" and "kEchoMaxObjects objects x 7 addresses (… 64 → 448 bits / 128
   → 896 bits)". After adding `Dsp` these must read **8 addresses**, append `dsp` to the
   echoed-address list (→ `…,width,name,dsp}`), and the bit-count figures must become
   **64 → 512 bits / 128 → 1024 bits**. **Acceptance:** no `"7 addresses"`, `"448"`, or `"896"`
   string remains in `EchoSubscriber.h`; the address list and bit counts match
   `kEchoAddrCount=8` / `EchoDirtyMap::kObjBytes`. (Doc-only; the comment must not lie post-change.)

### Step 2 — Emit DSP echo in flush (`core/src/ipc/EchoSubscriber.cpp`)
1. In `flush()` (`.cpp:190-256`), inside the per-object loop after the `EchoAddr::Name` block
   (`.cpp:238-243`), add an `if (dirty_.test(oid, EchoAddr::Dsp))` block: build
   `char addr[32]` = `"/adm/obj/%u/dsp"`, loop `p=0..6`, and for each set bit in
   `dirty_.dsp_param_dirty[oid]` call `buildAndSend(addr, ",if", &c.dsp_value[p], 1, &p_int, 1,
   nullptr, now_ms, send_fd)` (with `int p_int = p;`).
   **(REV3) No per-object inline clear** — `dsp_param_dirty` now lives in `EchoDirtyMap` and is
   zeroed by the existing `dirty_.clear()` at the end of `flush()` (`.cpp:255`), which ALSO
   covers the no-subscriber early return (`.cpp:190-194`) and `close()` (`.cpp:126-130`).
   **Acceptance:** each dirty param produces one `/adm/obj/N/dsp ,if` packet; after any
   `flush()` the whole `dsp_param_dirty` mask is zero; a second flush with no new marks emits
   nothing; a markDsp issued with no subscriber leaves nothing pending after its flush (Step 4
   regression test).

### Step 3 — Wire the mark at the gap site (`core/src/core/SpatialEngine.cpp:87-93`)
1. In the `case ipc::CommandTag::ObjDsp:` block, after setting `qc.dsp_param`/`qc.dsp_value`,
   add: `if (p->param == ipc::PayloadObjDsp::Param::Width) osc_backend_.echoPlane().markWidth(p->obj_id, p->value); else osc_backend_.echoPlane().markDsp(p->obj_id, static_cast<uint8_t>(p->param), p->value);`
   Mirror the comment style of the adjacent `// M5.1 echo.` lines.
2. **(REV2 — Architect amendment 3b) Mandatory code comment at this mark site.** Add an inline
   comment stating the cross-address routing contract, e.g.:
   `// C7 echo: param 7 (Width) shares c.width_rad with ObjWidth (apply case 7 == ObjWidth's`
   `// field), so route it to markWidth → /adm/obj/N/width (the canonical width address).`
   `// params 0..6 echo on /adm/obj/N/dsp. A param-7 write produces NO /adm/obj/N/dsp packet.`
   This prevents a future maintainer from "fixing" the perceived missing dsp echo for p7.
   **Acceptance:** ObjDsp params 0..6 mark the dsp echo; param 7 marks the width echo; no audio
   FIFO behaviour changes; the routing-contract comment is present at the mark site.

### Step 4 — Conformance + round-trip tests (`core/tests/core_unit/test_echo_plane.cpp`)
Add tests (and call them from `main()`), reusing the existing patterns plus a small loopback
receiver helper (modeled on `test_osc_outbound_reply_smoke.cpp:43-96`) so we can assert wire
content rather than only byte counts:
- `test_dsp_each_param_echoes` — markDsp params 0..6 with distinct values on one object, flush
  to a real loopback fd, recvfrom + parse: assert exactly 7 `/adm/obj/N/dsp ,if` packets with
  the correct (param,value) pairs.
- `test_dsp_multi_param_one_tick_coalesce` — markDsp(obj,0,a) then markDsp(obj,3,b) in one
  tick → assert BOTH echo (params 0 and 3); markDsp(obj,0,a) then markDsp(obj,0,a2) → one
  packet, value a2.
- `test_dsp_param7_routes_to_width` — markDsp(obj,7,w) → assert NO `/adm/obj/N/dsp` packet;
  markWidth(obj,w) (the engine's routing target) → assert `/adm/obj/N/width ,f` w. Documents
  the reconciliation at the EchoPlane unit boundary.
- `test_dsp_rate_guard_drops_excess` — mirror existing test 7 with markDsp; assert
  `dropped_count > 0`.
- **(REV3 — Critic MUST-FIX 2; tightened in REV4) `test_dsp_no_subscriber_does_not_leak`** —
  the test MUST **re-arm the coarse `EchoAddr::Dsp` bit after the no-subscriber flush**, otherwise
  it cannot distinguish the buggy vs fixed design. Emission is gated FIRST by
  `dirty_.test(obj, EchoAddr::Dsp)`; a naive sequence (markDsp → no-sub flush → attach → flush →
  assert no packet) clears the coarse bit in the no-sub flush, so the second flush never enters
  the dsp block and emits zero packets **regardless of where the per-param mask lives** — it
  passes on both designs and never observes the relocated mask. Required sequence:
  1. (no subscriber) `markDsp(obj, 2, x_old)`
  2. `flush(t0, -1)` — no-subscriber early return: clears the coarse bit (AND the per-param mask
     in the fixed `EchoDirtyMap` design; NOT the mask in the buggy `EchoObjCache` design)
  3. `addSubscriber(loopbackNet(), <bound ephemeral port>, kEchoSubscriberTag, t1)` — using the
     `getsockname` + `SO_RCVTIMEO` loopback receiver harness
  4. `markDsp(obj, 5, y_new)` — **re-arms** the coarse `Dsp` bit + mask bit 5
  5. `flush(t1, fd)`
  6. drain ALL packets via `recvfrom`; assert **EXACTLY ONE** `/adm/obj/N/dsp` packet with
     `param == 5, value == y_new`, and assert **NO** packet with `param == 2`.

  This emits **2 packets** (params 2 and 5) on the buggy `EchoObjCache` mask but **exactly 1**
  (param 5) on the fixed `EchoDirtyMap` mask — so it genuinely distinguishes the designs and
  proves `dirty_.clear()` zeroed the relocated mask on the no-subscriber path. (Lifecycle:
  subscribers TTL-evict after 30 s via `evictStale`, then re-subscribe.) Optionally also run the
  same sequence at `obj_id = kEchoMaxObjects - 1` (N1).
- **(REV2 — Architect amendment 2) `test_legacy_echoes_wire_compat_after_resize`** (new sibling
  of `test_coalesce_different_addresses_all_echo`, OR an upgrade of it): mark all 7 legacy
  addresses (aed/xyz/gain/mute/active/width/name) **plus** markDsp, then flush to a **real bound
  loopback fd** and `recvfrom`+parse, asserting each of the 7 legacy packets still emits the
  correct **address / typetags / args** after `kEchoAddrCount` 7→8, and that the dsp bit
  coexists with no aliasing. **Rationale:** the legacy `flush(now,-1)` build-but-don't-send path
  (`EchoSubscriber.cpp:176`) returns only a byte count and asserts "no crash" — it **cannot
  prove wire-level backward-compat** and would not catch a resize-induced bit-aliasing
  regression. AC5 must be backed by this receiver-parse harness, not the legacy `flush(-1)` path.
- **(REV3 — Critic nit N1) Exercise a HIGH object id, not just obj 0.** In both
  `test_legacy_echoes_wire_compat_after_resize` and the dsp parse tests, include at least one
  assertion at `obj_id = kEchoMaxObjects - 1` (the upper dirty-map region past the old `7×obj`
  stride, where a resize/aliasing regression actually surfaces) in addition to obj 0. Use the
  build-time `kEchoMaxObjects` so the same test source validates both Config A (63) and Config B
  (127).

**Round-trip:** because `/adm/obj/N/dsp` is **outbound-only** (no inbound decoder needed — do
not add one), the encode→parse round-trip is satisfied by the receiver-parse assertions above
(EchoPlane builds the wire bytes; the test parses them back to (param,value)). Add a short
comment in the test noting this is the round-trip coverage for the new address.

CMake: `test_echo_plane` already builds the whole file (`CMakeLists.txt:305-307`); no new
target unless we split the receiver test out — prefer keeping it in `test_echo_plane.cpp`.

**(REV2 — Architect amendment 5) Loopback-receiver hygiene (MANDATORY for every parse test).**
The existing tests hard-code `addSubscriber(loopbackNet(), 9102, …)` and rely on **no actual
delivery** (they only flush to `-1` or ignore `ECONNREFUSED`). For the new parse-based tests
the receiver must actually receive, so:
1. `bind()` a loopback UDP socket to an **ephemeral port** (port 0), then `getsockname()` to
   read back the **exact bound port** (idiom: `test_osc_outbound_reply_smoke.cpp:43-63`,
   incl. `SO_RCVTIMEO` so the test fails fast instead of hanging).
2. Register **that exact bound port** as the subscriber: `addSubscriber(loopbackNet(),
   <bound_port>, EchoPlane::kEchoSubscriberTag, now_ms)` — do **not** reuse the hard-coded 9102.
3. `flush(now_ms, recv_fd_or_sender_fd)` with a **real** fd so `buildAndSend` reaches the
   per-subscriber `sendto`, then `recvfrom` on the bound socket and parse the OSC bytes.
Failing to pin the bound port means `recvfrom` receives nothing and the test silently passes on
a false negative.

---

## 4. Test plan & gate commands (no hardware)

**(REV3 — Critic MUST-FIX 1) The validating axis is `-DSPATIAL_ENGINE_MAX_OBJECTS`, NOT audio
block size.** `kEchoMaxObjects = spe::MAX_OBJECTS = SPATIAL_ENGINE_MAX_OBJECTS`
(`core/CMakeLists.txt:75-81`; default 64, `FATAL_ERROR` unless 64 or 128), and that cap is
exactly what sizes the dirty-map (`kEchoMaxObjects × kEchoAddrCount` bits → 64→512 / 128→1024
**after** the 7→8 bump). Audio block size is the **unrelated** `MAX_BLOCK = 512` constant
(`core/src/core/Constants.h:25`) with zero bearing on EchoPlane. **CI builds only the default
`MAX_OBJECTS=64`** (`.github/workflows/cross-platform.yml` runs the full `ctest` suite on the
default cap only — no `-DSPATIAL_ENGINE_MAX_OBJECTS=128` anywhere); the 128 build is a **manual
local reconfigure** and is the config that actually exercises the dirty-map resize. Run both, in
**separate fresh build dirs**:

```bash
# ---- Config A: MAX_OBJECTS=64 (default; matches CI) ----
mkdir -p /home/seung/mmhoa/spatial_engine/core/build64 && cd /home/seung/mmhoa/spatial_engine/core/build64
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=64 && make -j"$(nproc)" && ctest --output-on-failure

# ---- Config B: MAX_OBJECTS=128 (manual; EXERCISES THE DIRTY-MAP RESIZE — validates R1/AC5) ----
mkdir -p /home/seung/mmhoa/spatial_engine/core/build128 && cd /home/seung/mmhoa/spatial_engine/core/build128
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=128 && make -j"$(nproc)" && ctest --output-on-failure

# ---- RT-assert build variant — confirm no-alloc on the mark/flush paths ----
mkdir -p /home/seung/mmhoa/spatial_engine/core/build_rt && cd /home/seung/mmhoa/spatial_engine/core/build_rt
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_RT_ASSERTS=ON && make -j"$(nproc)" && ctest --output-on-failure

# ---- Python suite ----
cd /home/seung/mmhoa/spatial_engine && python3 -m pytest
```
**Config B is the gate that proves the `kEchoAddrCount` 7→8 resize is non-aliasing at the larger
cap (R1/AC5);** without it the resize regression cannot surface in CI (which only builds 64).

**Acceptance criteria (testable):**
- AC1 — each of params 0..6 echoes once on `/adm/obj/N/dsp ,if` with correct (param,value).
- AC2 — multiple distinct params in one tick all echo (coalesce preserves each); same param
  twice → one echo with latest value.
- AC3 — param 7 emits **no** dsp echo; width is observed only on `/adm/obj/N/width` (decision 2).
- AC4 — rate guard still increments `dropped_count` past `kEchoRateLimit` for dsp echoes.
- AC5 — all 7 pre-existing `EchoAddr` echoes still emit correct address/typetags/args after
  `kEchoAddrCount` 7→8, **proven via the loopback-receiver `recvfrom`+parse harness** (NOT the
  legacy `flush(-1)` crash-only path, which cannot assert wire content). See
  `test_legacy_echoes_wire_compat_after_resize` in Step 4.
- AC6 — receiver-parse (round-trip) confirms address/types/args of the new packet.
- AC7 — NO_JUCE build clean; ctest green at **`SPATIAL_ENGINE_MAX_OBJECTS=64` (Config A) AND
  `=128` (Config B, manual reconfigure — exercises the dirty-map resize)**; RT-assert variant
  green; pytest green. (NOT "block size" — that is the unrelated `MAX_BLOCK=512`.)
- AC8 — backward compatibility: no existing echo/subscriber/test altered or broken.
- AC9 — **(REV3; tightened REV4)** no cross-lifecycle dirty-mask leak. MUST be proven by the
  **re-arm sequence** (the trivial form is INSUFFICIENT — it clears the coarse `EchoAddr::Dsp`
  gate in the no-subscriber flush and emits zero packets on both the buggy and fixed designs):
  markDsp(obj,2,x_old) → `flush(-1)` (no subscriber) → attach bound-port subscriber →
  **markDsp(obj,5,y_new) to re-arm the coarse bit** → `flush(fd)` → drain `recvfrom` and assert
  **exactly one** dsp packet `param==5,value==y_new` and **none** with `param==2`. A test that
  omits the post-attach re-arm does NOT satisfy AC9. See `test_dsp_no_subscriber_does_not_leak`.

---

## 5. Risks & mitigations
- **R1 `kEchoAddrCount` 7→8 ripples.** Some code may hard-code 7. *Mitigation:* grep
  `kEchoAddrCount` / literal-7 in `EchoSubscriber.*` and any consumer; rely on derived
  `kBitsPerObj`/`kObjBytes`; AC5 re-runs all existing echo assertions.
- **R2 stale dsp re-emit / cross-lifecycle leak.** A per-param dirty bit left set → repeated or
  spurious echoes, including the **no-subscriber→re-attach leak** the original draft would have
  had (mask in `EchoObjCache`, not reset by `dirty_.clear()`). *Mitigation (REV3, robust
  single-reset):* the mask lives in `EchoDirtyMap` and is zeroed by the **single** `dirty_.clear()`
  reset site that already covers flush-end, the no-subscriber early return, and `close()` — so
  no flush path can leave it dirty. Tests: second-flush-emits-nothing AND
  `test_dsp_no_subscriber_does_not_leak`.
- **R3 param 7 double-echo.** Routing miss → width on two addresses. *Mitigation:* route at
  mark site AND make `markDsp` ignore param 7; AC3 asserts it.
- **R4 `,if` arg order confusion.** Echo is `,if` (param,value); inbound is `,iif`
  (obj_id,param,value). *Mitigation:* frozen wire-contract section; receiver-parse test pins it.
- **R5 packet/addr buffer overflow.** `char addr[32]` / `buf[256]`. `"/adm/obj/127/dsp"` = 16
  chars, `,if` = 8 B payload — well within bounds. No change needed; covered by existing guards.
- **R6 RT regression.** *Mitigation:* RT-assert build variant gate (AC7); mark site mirrors
  `markWidth` exactly (no alloc/lock).

---

## 6. ADR — ObjDsp outbound echo (C7)
- **Decision:** Add an outbound ADM-OSC echo for ObjDsp on a single flat address
  `/adm/obj/<N>/dsp ,if <param 0..6> <value>`, backed by a per-object 8-slot value array + an
  8-bit param dirty mask gated by a new `EchoAddr::Dsp` bit (`kEchoAddrCount` 7→8). Param 7
  (Width) routes to the existing `markWidth`→`/adm/obj/N/width` echo and is never emitted on
  the dsp address.
- **Drivers:** per-param coalesce correctness; single authoritative width address; minimal
  blast radius and wire surface.
- **Alternatives considered:** (a) single address with coarse 1-bit dirty — rejected (loses
  multi-param coalesce); (b) per-param addresses `/adm/obj/N/dsp/<param>` — rejected on
  **verb↔echo symmetry** grounds: one inbound verb `/obj/dsp ,iif` must mirror to one outbound
  `/adm/obj/N/dsp ,if` (obj_id lifted), matching the family convention; (b) fragments one verb
  into 6 addresses, breaking that 1:1 symmetry and round-trip clarity. **Note (b) was NOT a
  free loss:** it would have reused the existing coarse 1-bit dirty mechanism and thereby
  **eliminated R2** (manual `dsp_param_dirty` clear) and **R4** (int-mixed typetag — (b) is pure
  `,f`). (c) deliberately accepts R2+R4 (both mitigated) to preserve symmetry — a considered
  tradeoff, not strict dominance. Separately, emitting a dsp echo for param 7 — rejected
  (double-echo; see Decision 2).
- **Why chosen:** (c) is the only design that preserves **verb↔echo symmetry + per-param
  coalesce** simultaneously, at trivial cost (~33 B/obj, one inner loop in flush, one new
  internal enum bit), satisfying all five principles.
- **Consequences:** clients gain verifiable confirmation of EQ/delay/k_hf/reverb_send changes;
  width remains single-sourced; internal dirty-map grows by one bit/obj. No wire-breaking change.
- **Follow-ups:** if future params exceed 7, widen `dsp_value`/mask; consider surfacing dsp
  state in `/sys/state` (out of scope for C7).

---

## 7. Consensus 진행 로그
<!-- Architect / Critic append below. -->

### Architect (REV1)

**Verdict: SOUND-with-amendments**

Grounding re-read and confirmed against actual code:
- `kEchoAddrCount` is referenced **only** in `EchoSubscriber.h` (grep over `core/src` + `core/tests`, excluding build dirs, returns zero external uses). `EchoDirtyMap::kBitsPerObj`/`kTotalObjBits`/`kObjBytes` all derive from it (`.h:75-77`). The 7→8 blast radius is genuinely contained; no literal-7 hard-coding anywhere that would silently break. R1 claim holds.
- Appending `Dsp = 7` is non-aliasing: `mark`/`test` both compute `obj*kBitsPerObj + addr` with the same runtime `kBitsPerObj`; bit positions for Aed..Name (0..6) stay consistent within a build, buffer just grows. No persisted/serialized dirty-map or `EchoObjCache` (it is a private `std::array`, never memcpy'd to wire/shm). Safe.
- `buildAndSend` emits args in **typetag order** (`.cpp:161` `for (t=types+1; …)`), so `",if"` yields int(param)→float(value) on the wire, matching the frozen contract. Proposed call `buildAndSend(addr, ",if", &c.dsp_value[p], 1, &p_int, 1, nullptr, now_ms, send_fd)` matches the signature/arg-order exactly (`.h:222-225`). Correct.
- Decision 2 is **semantically correct, not just policy**: audio apply case 7 writes `c.width_rad` (`SpatialEngine.cpp:635`) — the *same* field `ObjWidth` writes (`:161`). Both verbs mutate one width state, and the dsp p7 value is already in radians (`Command.h:245`), so `markWidth(obj, p->value)` is units-correct. Routing p7→`/width` is required to keep the canonical width address from going stale; NOT routing it would leave `/adm/obj/N/width` silently stale whenever width is driven via dsp p7. This is the decisive argument and the plan under-states it.
- Mark site / thread model: `markDsp` is added in the identical function/thread as `markWidth` (`SpatialEngine.cpp:163`). Header thread-model comment (`.h:23-25`) calls this the "OSC IO thread"; the plan calls it "control thread" — same thread, harmless terminology drift. No audio-thread access; no alloc/lock/syscall in `markDsp`. RT reasoning holds. Buffer bounds fine (`/adm/obj/127/dsp`=16<32; payload ~12 B in buf[256]).

**Steelman antithesis (decision 1c vs 1b):** Option (b) per-param addresses `/adm/obj/N/dsp/<param> ,f` is *mechanically simpler* than the plan admits: it reuses the **existing** coarse 1-bit-per-EchoAddr mechanism with zero new struct fields, **eliminates the manual `dsp_param_dirty=0` clear** (kills risk R2 entirely) and uses **pure-float typetags** (kills R4's int-mix concern). `kEchoAddrCount` would go 7→13 but is still derived → still zero blast radius. The plan's "wire bloat / documentation cost" is overstated (addresses are machine-generated/parsed). **Does (b) win? No — but for a reason the plan never states.** The deciding principle is **verb↔echo symmetry**: every existing echo lifts `obj_id` into the address and mirrors *one inbound verb* 1:1. The single inbound verb `/obj/dsp ,iif` must mirror to a single outbound `/adm/obj/N/dsp ,if` (obj_id lifted, exactly the family convention). (b) fragments one verb into 6 addresses, breaking that symmetry and round-trip clarity. (c) wins — but the plan should make verb↔echo symmetry the *explicit tiebreaker* rather than resting on "wire bloat," and should acknowledge (b) would remove R2+R4.

**Tradeoff tension the plan under-states (biggest risk):** The decision-2 routing creates a real **client-facing contract footgun**: a client writes `/obj/dsp ,iif N 7 w` but must **subscribe to a *different* address** (`/adm/obj/N/width`) than the dsp family it wrote to — and gets **no** packet on `/adm/obj/N/dsp` for param 7. State-wise correct, ergonomically surprising for a verifier. This is the single biggest risk and it is **documentation-shaped**, not code-shaped: it must be pinned in the wire contract, a mark-site code comment, and a test. Synthesis: **keep** decision 2 (the stale-`/width` argument makes it mandatory), but treat the asymmetry as a documented contract, not an implicit behavior.

**Test-plan gap:** the existing conformance tests (`test_echo_plane.cpp`) almost all use `flush(now,-1)` — the *build-but-don't-send* path (`.cpp:176`) — and assert only "no crash" / `dropped_count`. They never parse wire bytes. So AC5 ("all 7 pre-existing echoes still pass unchanged after 7→8") as currently backed by `test_coalesce_different_addresses_all_echo` (`:96-113`, uses `flush(-1)`) does **not actually assert wire content** and would not catch a resize-induced aliasing regression. The plan's new loopback-receiver harness must be the vehicle for AC5, not the legacy crash-only path.

**Required amendments (mechanical):**
1. **Update stale header comments** in `EchoSubscriber.h:9-14` (address list "7 addresses", "64 → 448 bits / 128 → 896 bits") to reflect 8 addresses / 512 / 1024 bits, and add `Dsp` to the echoed-address list. Non-breaking but the doc must not lie post-change.
2. **AC5 must use the new receiver-parse harness**: upgrade (or add alongside) `test_coalesce_different_addresses_all_echo` to flush to a **real bound loopback fd** and `recvfrom`+parse, asserting the 7 legacy addresses still emit correct address/types/args after `kEchoAddrCount` 7→8 — the current `flush(-1)` version cannot prove backward-compat at the wire level.
3. **Document the param-7 contract asymmetry** in three places: (a) the frozen wire-contract section already notes width is on `/width`, but add an explicit "client writing `/obj/dsp` param 7 must subscribe to `/adm/obj/N/width`, never `/adm/obj/N/dsp`" line; (b) a code comment at the `SpatialEngine.cpp:87-93` mark site; (c) keep `test_dsp_param7_routes_to_width` as the executable spec (already planned — good).
4. **State the (c)-over-(b) tiebreaker as verb↔echo symmetry** in §Decision 1 / ADR, and note (b) would have eliminated R2+R4 — so the choice is a deliberate symmetry/round-trip tradeoff, not a free win.
5. **Receiver test hygiene**: the new loopback receiver must `bind()` to a port and register that exact port as the subscriber `echo_port` (`addSubscriber(loopbackNet(), <bound_port>, …)`), since existing tests hard-code 9102 and rely on no delivery. Pin the port so `recvfrom` actually receives.

**Single biggest risk:** the param-7 → `/adm/obj/N/width` routing is state-correct but is an *undocumented cross-address contract* — a verifier that sets dsp param 7 and watches `/adm/obj/N/dsp` will see silence and wrongly conclude the write was dropped. Mitigated entirely by amendment 3 (doc + comment + the already-planned routing test). No design change required.

### Planner (REV2)

All 5 Architect amendments applied to the plan in place; **design unchanged** (option 1c +
param-7→markWidth). Changes are mechanical/documentation only:

1. **Header doc-comment fix** → new **Step 1.4**: explicit checklist item to update
   `EchoSubscriber.h:9-14` to "8 addresses", add `dsp` to the echoed-address list, and change
   bit counts to 64→512 / 128→1024, with an acceptance gate that no `"7 addresses"`/`"448"`/
   `"896"` string survives.
2. **AC5 wire-level harness** → Step 4 gains `test_legacy_echoes_wire_compat_after_resize`
   (real bound loopback fd + `recvfrom`+parse) and **AC5 reworded** to require the
   receiver-parse harness, explicitly stating the legacy `flush(-1)` build-but-don't-send path
   cannot prove wire-level backward-compat. (This is the only AC changed, per instruction.)
3. **Param-7 contract asymmetry documented in three places**: (a) new "Cross-address contract"
   block in the frozen wire-contract section ("client writing `/obj/dsp` param 7 must subscribe
   to `/adm/obj/N/width`, never `/adm/obj/N/dsp`"); (b) new **Step 3.2** requiring a mandatory
   mark-site code comment stating the routing; (c) `test_dsp_param7_routes_to_width` retained as
   the executable spec.
4. **(c)-over-(b) tiebreaker = verb↔echo symmetry**: rewrote the Decision-1 "Chosen" rationale
   and the ADR "Alternatives/Why chosen" to make verb↔echo symmetry the explicit decider, and
   to acknowledge that (b) would have **eliminated R2 (manual dirty clear) and R4 (int-mixed
   typetag)** — so (c) is a deliberate symmetry/round-trip tradeoff, not a free win.
5. **Receiver-test hygiene note** added to Step 4: bind ephemeral port → `getsockname()` →
   register THAT exact bound port via `addSubscriber(loopbackNet(), <bound_port>, …)` (not the
   hard-coded 9102) so `recvfrom` actually receives; with `SO_RCVTIMEO`.

Plus the **Decision-2 strengthening**: added the Architect's decisive staleness argument —
ObjDsp apply case 7 (`SpatialEngine.cpp:635`) writes the *same* `c.width_rad` as ObjWidth
(`:161`), and the p7 value is already in radians, so `markWidth(obj, value)` is units-correct
and routing p7 is **mandatory** (else `/adm/obj/N/width` goes silently stale when width is
driven via dsp p7), not merely preferable.

No design, no audio-path, and no ACs other than AC5's harness were changed. Ready for Critic.

### Critic (REV2)

**Verdict: ITERATE** — design is sound and well-grounded (consensus log is honest about the
(b)→R2+R4 tradeoff; Decision 2 staleness argument verified against `SpatialEngine.cpp:635`/`:161`;
`buildAndSend(addr, ",if", &c.dsp_value[p], 1, &p_int, 1, …)` arg/typetag order verified correct
at `EchoSubscriber.cpp:147-174`; outbound-only / no-inbound-decoder is correct; the
`getsockname`+`SO_RCVTIMEO`+`recvfrom` harness is real and reusable at
`test_osc_outbound_reply_smoke.cpp:43-63,84-89`). Two concrete must-fix items block APPROVE.

**MUST-FIX 1 (MAJOR) — §4/AC7 names a knob that does not exist; the real knob is unnamed and
deferred.** §4 says "at BOTH block sizes per the existing CI matrix knob (64 and 128)" and
"re-run with the project's established block-size configuration for 64 AND 128 (executor: …
confirm in core/CMakeLists.txt / CI yml)"; AC7 says "ctest green at block 64 and 128". This is
factually wrong on two counts, verified:
  - The 64/128 knob is **`-DSPATIAL_ENGINE_MAX_OBJECTS`** (object-count cap), NOT block size —
    `core/CMakeLists.txt:75-81` (`set(SPATIAL_ENGINE_MAX_OBJECTS 64)`, `FATAL_ERROR` unless 64
    or 128). Audio block size is the unrelated `MAX_BLOCK=512` (`core/src/core/Constants.h:25`)
    and has **zero** bearing on EchoPlane (no audio-block processing in the echo path).
  - There is **no CI matrix that builds 128**: `.github/workflows/cross-platform.yml:99-102,182-185`
    run `ctest` on the **default** build only (MAX_OBJECTS=64); grep for `MAX_OBJECTS`/`128`
    across `.github/workflows/` returns nothing. The 128 build is a manual local reconfigure
    (cf. `core/tests/core_unit/CMakeLists.txt:361-362` "obj 63 at the 64-build and obj 127 at
    the 128-build").
  This is load-bearing, not cosmetic: the dirty-map resize the plan worries about (R1/AC5,
  "64 → 512 bits / 128 → 1024 bits") **is** the `kEchoMaxObjects = MAX_OBJECTS` axis — so the
  second config run is exactly what validates R1, yet the plan mislabels it as "block size" and
  sends the executor hunting for a nonexistent CI knob. The flag is simple and must be pinned.
  *Fix:* replace the §4 comment and AC7 with the explicit two-config gate:
  `cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_MAX_OBJECTS=64 && make && ctest` and
  again with `-DSPATIAL_ENGINE_MAX_OBJECTS=128` (fresh build dir). Stop deferring; drop all
  "block-size"/"CI matrix knob" wording.

**MUST-FIX 2 (MAJOR) — `dsp_param_dirty` leaks across the no-subscriber flush path; R2 mitigation
does not cover it.** Design 3 / Step 1.2 put `dsp_param_dirty` in **`EchoObjCache`**, but
`flush()`'s no-subscriber early return only calls `dirty_.clear()` (`EchoSubscriber.cpp:190-194`),
which memsets `EchoDirtyMap::obj_bits` + transport flags — it does **not** touch
`EchoObjCache::dsp_param_dirty`. `markDsp` sets the param-mask bit unconditionally (like the
legacy marks). So any param marked while no subscriber is attached is **retained** and then
**spuriously emitted on the first dsp flush after a subscriber (re)attaches** — a real lifecycle,
since subscribers are evicted after 30 s TTL (`kEchoSubscriberTtlMs`, `evictStale`) and
re-subscribe. The legacy addresses do **not** have this leak (their only dirty state is the coarse
bit, fully reset by `dirty_.clear()`), so this breaks family parity (Principle 1/5) — a re-attached
subscriber gets dsp packets it would never get for aed/xyz/gain/etc. Plan R2 ("clear inline in
flush") only covers the subscribed path inside `if (dirty_.test(oid, Dsp))`; it misses both the
no-subscriber early return AND `close()` (`:126-130`, same `dirty_.clear()` reset). The Step-2
acceptance "second flush emits nothing" would still pass and never catch this.
  *Fix:* reset `dsp_param_dirty` wherever `dirty_.clear()` is the reset (no-subscriber early
  return + `close()`), OR move `dsp_param_dirty` into `EchoDirtyMap` so `clear()` resets it
  atomically with the coarse bits. Add a regression test: markDsp under no subscriber → attach
  subscriber → flush → assert NO dsp packet.

**Non-blocking nits:**
- N1: The resize-aliasing tests (`test_legacy_echoes_wire_compat_after_resize`, dsp tests) operate
  on obj 0, where the 7→8 bit-offset change is least likely to expose aliasing. Add at least one
  assertion at a high object id (`kEchoMaxObjects-1`) so the upper dirty-map region (bit offset
  past the old `7*obj` stride) is actually exercised — that is where a resize regression would
  surface. Strengthens R1/AC5; not required for correctness of the happy path.
- N2: Plan repeatedly cites `SpatialEngine.cpp:161` as the ObjWidth *apply* writing `c.width_rad`;
  `:161` is actually the control-thread mark site (`qc.width_rad = p->width_rad`). The audio apply
  that writes `c.width_rad` for ObjWidth is the case body, separate from line 161. Substance holds
  (case 7 at `:635` writes the same `c.width_rad`); fix the reference for precision.

**Mode:** THOROUGH (no CRITICAL; 2 MAJOR < 3-MAJOR adversarial threshold; both findings are
localized and the design is otherwise verified-correct). Upgrade path to APPROVE: apply MUST-FIX 1
(pin `-DSPATIAL_ENGINE_MAX_OBJECTS=64/128`, drop block-size wording) and MUST-FIX 2 (close the
`dsp_param_dirty` leak + add its regression test). N1/N2 optional.

### Planner (REV3)

Both MAJOR must-fix items and both nits applied in place; **design unchanged** (option 1c +
param-7→markWidth). Summary:

1. **MUST-FIX 1 — pinned the real knob, dropped "block size".** §4 gate commands rewritten: the
   validating axis is `-DSPATIAL_ENGINE_MAX_OBJECTS` (`core/CMakeLists.txt:75-81`, default 64,
   `FATAL_ERROR` unless 64/128), NOT audio block size (that's the unrelated `MAX_BLOCK=512`,
   `Constants.h:25`). Replaced the single-build-dir snippet with two **fresh-build-dir** configs —
   **Config A `=64`** (matches CI) and **Config B `=128`** (manual reconfigure; CI never builds it)
   plus the RT-assert variant and pytest. Stated explicitly that **Config B is the gate that
   exercises the dirty-map resize (R1/AC5)**. AC7 reworded to name `MAX_OBJECTS=64/128`, not "block".
   No more knob deferral to the executor.
2. **MUST-FIX 2 — closed the cross-lifecycle dirty-mask leak via the robust single-reset.** Chose
   to **relocate the per-param dirty mask from `EchoObjCache` into `EchoDirtyMap`**
   (`uint8_t dsp_param_dirty[kEchoMaxObjects]`) and extend `EchoDirtyMap::clear()` to `memset` it —
   so the **single** `dirty_.clear()` reset site covers flush-end, the no-subscriber early return
   (`:190-194`), AND `close()` (`:126-130`) uniformly. This eliminates the entire "missed a clear
   site" bug class rather than patching two call sites (rejected as fragile, with rationale stated
   in Decision 3). Updated Decision 3, Step 1.2/1.2b/1.3, Step 2 (removed the inline per-object
   clear), and R2. Added regression test `test_dsp_no_subscriber_does_not_leak` (markDsp with no
   subscriber → attach → flush → assert NO dsp packet) and new **AC9**.
3. **N1 — high-object-id coverage.** Both `test_legacy_echoes_wire_compat_after_resize` and the dsp
   parse tests now also assert at `obj_id = kEchoMaxObjects-1` (upper dirty-map region past the old
   `7×obj` stride, where a resize regression surfaces), using the build-time `kEchoMaxObjects` so one
   source validates both 63 (Config A) and 127 (Config B).
4. **N2 — citation precision.** Corrected the Decision-2 strengthening: `SpatialEngine.cpp:161` is
   the ObjWidth **control-thread mark** (`qc.width_rad = p->width_rad`, one line above the
   `markWidth` call at `:163`), not the audio apply; the apply writing `c.width_rad` for ObjWidth is
   the separate case body (~`:661-662`). Substance unchanged — case 7 at `:635` writes the same
   `c.width_rad`.

Only AC5's harness (REV2) and the new AC9 (REV3) touch the ACs; no other AC, the design, or the
audio path changed. Ready for Critic re-review.

### Critic (REV3)

**Verdict: ITERATE** (iteration 2/5) — one remaining must-fix. The **production fix is correct
and safe**; the blocker is that the regression test for MUST-FIX 2 does not actually exercise the
bug it claims to close.

**MUST-FIX 1 — CLOSED (verified).** §4 now pins `-DSPATIAL_ENGINE_MAX_OBJECTS=64` (Config A,
matches CI) and `=128` (Config B, manual reconfigure, fresh build dir), names Config B as the
R1/AC5 dirty-map-resize gate, and drops all "block size"/"CI matrix knob" wording; AC7 reworded
to `MAX_OBJECTS=64/128`. Matches `core/CMakeLists.txt:75-81` (default 64, `FATAL_ERROR` unless
64/128) and the fact that CI builds only the default 64. Correct and complete.

**MUST-FIX 2 — production fix CORRECT and safe; regression test INADEQUATE (must-fix).**
- *Relocation introduces no new problem (verified against source):* `uint8_t
  dsp_param_dirty[kEchoMaxObjects]` is sized by the **object** axis, independent of the
  `kEchoAddrCount` 7→8 bump — no aliasing with `obj_bits`, `kObjBytes` unaffected. `clear()`
  (`EchoSubscriber.h:83-87`) memsets all three members (order-independent); the end-of-`flush()`
  `dirty_.clear()` (`.cpp:255`) is the last statement and the mask is read earlier in the loop —
  no read-after-clear. `markDsp` bounds-checks `obj_id < kEchoMaxObjects` before indexing.
  Co-locating with the coarse bits genuinely eliminates the "missed a clear site" class across
  flush-end / no-subscriber early return (`.cpp:190-194`) / `close()` (`.cpp:126-130`). The fix
  is right.
- *But `test_dsp_no_subscriber_does_not_leak` as specified does NOT prove the leak is closed —
  it passes against the buggy `EchoObjCache` design too.* Trace: emission is gated FIRST by the
  coarse bit `dirty_.test(obj, EchoAddr::Dsp)`. The planned sequence is markDsp(no sub) →
  `flush(t,-1)` → addSubscriber → `flush(t2,fd)` → assert no packet. Step 2's no-subscriber
  `dirty_.clear()` clears the **coarse** Dsp bit, so step 4's `dirty_.test(obj,Dsp)` is false and
  the dsp block is never entered — **zero packets regardless of where the mask lives.** The test
  is satisfied by the coarse-bit gating alone and never observes the relocated mask. It would
  give a green check against the exact bug MUST-FIX 2 targets, making AC9's "proven by
  test_dsp_no_subscriber_does_not_leak" unsupported.
- *Why:* the real leak (REV2 finding) needs a **second markDsp AFTER the no-subscriber flush** to
  re-arm the coarse bit, at which point a surviving stale mask bit rides along. Without that
  re-arm step the two designs are indistinguishable.
- *Fix (concrete):* rewrite the test to:
  1. (no subscriber) `markDsp(obj, 2, x_old)`
  2. `flush(t0, -1)`   // no-sub early return: clears coarse (+ mask in the fixed design)
  3. `addSubscriber(<bound ephemeral port>, fd)`
  4. `markDsp(obj, 5, y_new)`   // RE-ARMS coarse Dsp bit + mask bit 5
  5. `flush(t1, fd)`
  6. drain ALL packets via `recvfrom`; assert **exactly one** `/adm/obj/N/dsp` packet with
     `param==5, value==y_new`, and assert **NO** packet with `param==2`.
  This sequence emits 2 packets (params 2 and 5) on the buggy `EchoObjCache` mask and exactly 1
  (param 5) on the fixed `EchoDirtyMap` mask — so it actually proves the relocation closed the
  leak. (Optionally also run it at `obj_id = kEchoMaxObjects-1` per N1.)

**N1 — CLOSED.** Tests now assert at `obj_id = kEchoMaxObjects-1` using build-time
`kEchoMaxObjects`. Good.
**N2 — CLOSED.** Citation corrected (`SpatialEngine.cpp:161` = control-thread mark; apply is the
separate case body; `:635` writes the same `c.width_rad`). Good.

**Non-blocking nit:** AC9's wording should be tightened to require the re-arm-after-no-sub-flush
sequence explicitly, so a future executor cannot satisfy it with the trivial (ineffective) form.

**Mode:** THOROUGH (no CRITICAL; the single remaining issue is a localized test-construction
defect, production code verified correct). Consensus NOT yet reached — one more pass to fix the
regression test. Upgrade path to APPROVE: make `test_dsp_no_subscriber_does_not_leak` re-arm the
coarse bit after the no-subscriber flush (steps above) so it distinguishes the buggy mask
placement from the fixed one; everything else is ready.

### Planner (REV4)

Single remaining must-fix applied; **nothing else touched** (production design and all other ACs
were approved by Critic REV3).

- **Fixed the regression test's coarse-bit blind spot.** Rewrote
  `test_dsp_no_subscriber_does_not_leak` (Step 4) to the **re-arm sequence**: markDsp(obj,2,x_old)
  → `flush(t0,-1)` (no subscriber) → attach a bound-ephemeral-port loopback subscriber →
  **markDsp(obj,5,y_new)** (re-arms the coarse `EchoAddr::Dsp` gate + mask bit 5) → `flush(t1,fd)`
  → drain `recvfrom` and assert **exactly one** dsp packet (`param==5,value==y_new`) and **none**
  with `param==2`. Rationale captured inline: emission is gated first by
  `dirty_.test(obj, EchoAddr::Dsp)`, so the prior sequence cleared that gate in the no-subscriber
  flush and emitted zero packets on **both** designs — never observing the relocated mask. The
  re-arm makes the test emit 2 packets on the buggy `EchoObjCache` mask vs exactly 1 on the fixed
  `EchoDirtyMap` mask, so it actually distinguishes them. Optional `obj_id = kEchoMaxObjects-1`
  variant noted (N1).
- **Tightened AC9** to mandate the re-arm-after-no-subscriber-flush sequence explicitly and to
  state that a test omitting the post-attach re-arm does NOT satisfy AC9 — so a future executor
  cannot discharge it with the trivial/ineffective form.

No production-code, design, audio-path, or other-AC changes. Ready for Critic re-review.

### Critic (REV4)

**Verdict: APPROVE — consensus reached (iteration 3/5).**

The single remaining must-fix is correctly applied. `test_dsp_no_subscriber_does_not_leak` now
uses the re-arm sequence and genuinely distinguishes the two mask placements (traced):
markDsp(obj,2,x_old) → `flush(t0,-1)` [no sub: `dirty_.clear()` drops the coarse `Dsp` gate; in
the fixed `EchoDirtyMap` design it also zeroes mask bit 2, in the buggy `EchoObjCache` design bit
2 survives] → attach bound-port subscriber → markDsp(obj,5,y_new) [re-arms coarse `Dsp` + mask bit
5] → `flush(t1,fd)`. Fixed design emits exactly ONE packet (param 5); buggy design emits TWO
(param 2 with the still-cached `dsp_value[2]=x_old`, plus param 5). The assertions ("exactly one
dsp packet, param==5,value==y_new; NONE with param==2", drained via `recvfrom` + `SO_RCVTIMEO`)
fail on the buggy mask and pass only on the fix — so it actually proves the relocation closed the
leak. `dsp_value` correctly stays in `EchoObjCache` in both designs, so the buggy re-emit of the
stale value is real and observable.

AC9 wording is airtight: it explicitly marks the trivial form INSUFFICIENT, spells out the
re-arm sequence, and states "A test that omits the post-attach re-arm does NOT satisfy AC9" — it
cannot be discharged with the ineffective form.

**Status of all prior items:** MUST-FIX 1 (MAX_OBJECTS=64/128 gate, not block size) — CLOSED;
MUST-FIX 2 (cross-lifecycle dirty-mask leak: `EchoDirtyMap` relocation + single-reset `clear()`,
verified introduces no new problem vs the 7→8 resize) — production fix CLOSED in REV3, regression
test now CLOSED in REV4; N1 (high-obj-id coverage) — CLOSED; N2 (citation) — CLOSED. No production
code, design, audio-path, or other-AC changes in REV4 (test + AC9 only), as stated by Planner REV4.

**Non-blocking nits (none block APPROVE):**
- The re-arm test asserts "exactly one" via drain-to-timeout; ensure the second `recvfrom` is
  actually issued and a timeout return (-1/EAGAIN) is asserted, not just that the first packet is
  param 5 — otherwise a spurious second packet could slip by unobserved. (Implementation detail;
  the AC9 "exactly one" + "none param==2" already mandate it.)
- Optional: also run the re-arm sequence at `obj_id = kEchoMaxObjects-1` (already offered in Step
  4 as optional) to fold leak-coverage into the upper dirty-map region under Config B.

**Mode:** THOROUGH throughout (never reached the CRITICAL / 3-MAJOR adversarial threshold). Design
was sound from REV2; the loop closed two MAJOR verification-gate defects and a hollow regression
test. Plan is now actionable end-to-end — an executor following it will succeed without hitting an
undocumented wall. Ready for autopilot/executor.
