# Plan — ADR 0019 Phase C PCM IPC, PR3 (engine wiring)

**Mode:** RALPLAN-DR / **DELIBERATE** (RT audio worker thread + cross-process SPSC shm + CLI/engine pairing)
**Iteration:** 2 (iter-1 Architect APPROVE-WITH-CONDITIONS, Critic ITERATE; all Critic items C1/C2/M1–M5 + gaps applied)
**Authoritative design:** `docs/adr/0019-phase-c-pcm-ipc-shm-ring.md` (§2.3 header, §2.5 sync, §3 integration, §7.1 cases 9/10, §10 non-goals)
**Predecessor (FROZEN):** `.omc/plans/spatial-engine-adr0019-pr2-shared-ring-backend.md` (PR1+PR2 DONE)

**Target files:**
- `core/src/audio_io/SharedRingBackend.h` — add worker-thread members + (Decision 3) output-staging API + output-staging test accessors + (Decision 2) consumer-attach-lock state + atomic `running_`. **(M4)** Amend the thread-model contract comment at `SharedRingBackend.h:23-27` ("control thread … writes ONLY the four warning counters … NEVER writes a header atomic") to note the consumer-lock is the ONE exception: a control-thread-only CAS in `start()`/`stop()`, NEVER on the RT path.
- `core/src/audio_io/SharedRingBackend.cpp` — implement `thread_loop()`, start/stop join, (D2) consumer-lock acquire/release + stale reclaim, (D1) resync gating in `prepare()`, (D3) output-staging fill in `pump_block`; (M2) the single unified `prepare(...)` signature.
- `core/src/audio_io/AudioBackend.h` — (D2) add `BackendError::ConsumerAlreadyAttached` enumerator.
- `core/src/audio_io/AudioBackend.cpp` — (D2) add matching `case` to the exhaustive `describe()` switch (no `default:`; `-Werror=switch`).
- `core/src/audio_io/shm/RingHeader.h` — (D2/**M1**) add the named consumer-lock offset constant + an **inline `consumer_lock_atomic(RingHeader*)` helper, UNCONDITIONALLY compiled** (production, so tests reach it in BOTH the regular and RT-sentinel builds). NO layout/size change (the slot already exists as `_reserved` zero-init bytes).
- `docs/adr/0019-phase-c-pcm-ipc-shm-ring.md` — **(M4)** amend the §2.3 `_reserved` row Producer/Consumer columns to document the consumer-lock sub-field at offset 0x0058 + the producer's zero-init obligation (PR5 Python-producer compatibility).
- `core/src/bin/spatial_engine_core.cpp` — (D3) `--input-backend shm:<path>|null|dante` CLI parse, backend pairing, output composition via backend-owned staging + the existing `WavCapture` tap, banner/`--help`.
- `core/tests/core_unit/test_shared_ring_backend.cpp` — cases 9 + 10, registered in BOTH `main()`s (regular line ~943, RT-sentinel line ~933 must still compile).
- `core/tests/core_unit/CMakeLists.txt` — no new target needed (cases 9/10 live in the already-registered `test_shared_ring_backend` + `_rt_sentinel`); confirm both still build.

**Scope boundary (PR3 ONLY):** worker-thread cadence + start/stop join; case 9 resync; case 10 multi-attach reject; `--input-backend shm:` CLI + backend pairing + output composition in `spatial_engine_core.cpp`; integration ctests 9/10. **OUT:** PR4 (real `/sys/warning shm_*` + `/sys/state`), PR5 (`dreamscape/adm_player/ipc_sink.py`), PR6 (60 s soak), PR7 (cross-platform CI). Decisions here MUST NOT break those seams (esp. the `_reserved` consumer-lock offset = PR5 producer compat; the output API = PR4/future Dante).

---

## §0 — RALPLAN-DR Summary

### Principles (P)
- **P1 — Audio thread is sacred.** `pump_block` stays alloc/syscall/clock-free. The worker reads `steady_clock::now()` BEFORE `pump_block` (preserving the PR2 "hw_ts is a param" contract); the cadence `sleep_until` lives only in `thread_loop`, never in `pump_block`. The test path stays thread-free and sleep-free (`pump_synchronous`).
- **P2 — Honor PR2 frozen contracts.** `attach()` = mmap-only, no validation. ALL validation in `start()→BackendError` gates 1–6 IN ORDER. Masking capacity is ALWAYS `header.capacity_frames`. The 4 warning counters map 1:1 to PR4 OSC. `BackendError` switch is exhaustive (no `default:`). PR3 ADDS, never reorders or removes.
- **P3 — Do not regress PR2 cases 1–8 + 4 security cases.** Case 3 (verbatim seed at empty ring) and case 4 (drain: producer wrote 2 blocks, b0 delivered FIRST then b1) lock `prepare()`'s `read_idx_local_ = hdr_ri` behavior. Any resync (case 9) MUST be gated so cases 3/4 keep the verbatim-seed path unchanged.
- **P4 — Producer owns the shm lifecycle.** Consumer never `shm_unlink`s, never `ftruncate`s, never reorders the wire. A 2nd-consumer guard must keep version=1 and stay Python-producer-compatible (PR5 zero-inits `_reserved`).
- **P5 — Engine renders into OUTPUT buffers; shm supplies INPUT.** Confirmed: `SpatialEngine::audioBlock` writes `block.output_channels` (render guarded by `render_ready_ && block.output_channel_count > 0` at **`SpatialEngine.cpp:646`**; clamp `std::min(block.output_channel_count, n_spk)` at **`:703`**; every `output_channels[spk]` access null-checked at **`:705/727/750`**). It does NOT read `input_channels` for rendering (input ch0 only optionally tapped for LTC chase). Therefore an input-only shm backend that leaves `output_channel_count=0` produces NO audio output (the render block is skipped entirely). PR3's central wiring problem is getting output buffers into the AudioBlock on the shm-driven path.

### Decision Drivers (top 3)
- **D1 — No PR2 regression.** Cases 3/4/security must stay byte-identical green. This is the hard constraint that eliminates an unconditional resync (Decision 1) and forces additive-only changes.
- **D2 — Cross-process SPSC fidelity vs test-fidelity vs wire-compat.** Case 10 must enforce "second consumer refused." The mechanism must (a) keep wire version=1, (b) be PR5-Python-compatible, (c) be testable WITHOUT hardware, and (d) not corrupt the producer-owned ring.
- **D3 — RT-safety + build-config invariance.** The worker thread, the output staging, and the consumer-lock writes must not introduce alloc/syscall on the RT path, must build under `SPE_HAVE_JUCE=OFF` (the CI path), and must keep the RT-sentinel target compiling.

---

## Decision 1 — Case 9 resync semantics & placement

**ADR §7.1 case 9:** `attach_to_pre_existing_ring_resyncs_read_idx_to_write_idx_minus_block`. A consumer attaching to a ring the producer has been filling for a while should NOT replay the entire backlog from `read_idx`; it should jump near the write head so it plays *current* audio.

**The tension (verified):** `prepare()` seeds `read_idx_local_ = hdr_ri` verbatim (`SharedRingBackend.cpp:176`). An UNCONDITIONAL resync to `write_idx - block` would break:
- **Case 4** (`test_shared_ring_backend.cpp:402-442`): producer writes 2 blocks (`write=2*64=128, read=0`), test asserts pump#1 delivers **b0 first** then pump#2 delivers b1. An unconditional resync would set `read = write - block = 64`, skipping b0 → assertion fail.
- **Case 3** (`:316-326`): empty ring `write=read=0`; resync `write - block` would underflow.

### Viable Options

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **(a) Separate code path / explicit flag** — add `AttachMode::ResyncToHead` or a `bool resync_on_start` param; cases 3/4 keep the verbatim-seed path, case 9 opts in. | Surgically additive; cases 3/4 literally unchanged; intent explicit at the call site (PR3 CLI always wants head-resync for a live producer). | New public surface; must thread the flag through `attach`/`start`/`prepare`; two seed paths to maintain. |
| **(b) Gap-gated clamp** — in `prepare()`, after Gate 6, clamp `read_idx_local_` forward to `write_idx - block` ONLY when the backlog `(write_idx - hdr_ri) > capacity_frames` (**STRICT `> C`**; consumer is MORE than a full ring behind = the oldest unread frame has been physically overwritten). | Zero new API; physically motivated by the **static-overwrite predicate** (derivation below). Cases 3/4 untouched: 128 « 8192, so the gate is false. Case 9 sets up a `> C` backlog so the gate fires. | The resync trigger is implicit (backlog `> C`), not "always on attach"; a live producer ≤ one full ring ahead is NOT resynced (correct — those frames are still resident and should play). Subtle boundary (`== C` does NOT fire) to document + test. |
| **(c) Predicate on AttachMode + producer-ahead** — resync when `mode == OpenExisting && write_idx > 0 && hdr_ri < write_idx - block`. | Captures "pre-existing & producer-ahead." | Fires for case 4 (`OpenExisting`, `write=128`, `ri=0 < 64`) → **breaks case 4**. Rejected. |

### Recommended: **(b) Gap-gated clamp**, with the STRICT static-overwrite trigger `backlog > capacity_frames`.

**Rationale + derivation (C1 — the central decision).** The ONLY situation where the verbatim seed is *wrong* (not a stylistic choice) is when the oldest frame the first pump would read — index `hdr_ri` — has been **physically overwritten** by the producer's ring wrap (capacity is a sliding window; the producer keeps only the most-recent `capacity_frames` frames per `prefill_ring`'s comment at `test:856-859`). The resident window is exactly `[write_idx - capacity, write_idx)`. So:

> `hdr_ri` is overwritten ⟺ `hdr_ri < write_idx - capacity` ⟺ `write_idx - hdr_ri > capacity` ⟺ **`backlog > C`** (strict).

**Why STRICT `> C`, and why NOT the concurrency-margin variant `> C - block`:**
- At `backlog == C` exactly, `hdr_ri == write_idx - capacity` = the **oldest STILL-RESIDENT** frame. It is valid data and MUST be played, NOT resynced. A `>= C` test (iter-1, wrong) or a `> C - block` test (Architect's suggestion, also wrong) would resync at or before this boundary and **discard a legitimately-resident block** — regressing the ADR §6 "play buffered audio" intent and the case-4 contract class.
- The `> C - block` concurrency-margin variant is NOT needed here: the resync runs **single-threaded in `prepare()` before the worker thread is spawned** (Decision 3(i)). After resync, the first pump reads `[write_idx - block, write_idx)` — the NEWEST block. For the producer to lap (overwrite) that block it must advance a full `capacity_frames`, so the read window is bounded with no concurrent-producer race during the resync decision itself. There is no in-flight producer margin to reserve; `> C` is exact. (Steady-state pacing slack is a separate, already-handled concern — the pump's `available >= block` check, ADR §6 back-block tolerance.)

The chosen invariant, stated explicitly: **static-overwrite (`backlog > C`)** — resync iff the oldest unread frame is already gone. This precisely matches case 9's `> C` setup (AC-1: `backlog = 8448 > 8192`) while leaving cases 3 (gap 0), 4 (gap 128 « 8192), and the `== C` boundary on the verbatim path. Option (a) is the "always resync" reading of the ADR §7.1 title, but the ADR's own §6 ("Producer late by < 1 block → plays back-block") shows the design *wants* to play buffered audio when the data is resident — an unconditional resync would discard legitimately-resident blocks. Reject (c) (breaks case 4) and (a) (more API surface than the self-documenting predicate needs; the CLI needs no knob).

**Exact placement & sub-specs (resolve every sub-question the task raised):**

1. **Placement:** in `prepare()`, immediately AFTER Gate 6 (`hdr_ri > hdr_wi` reject, `:164`) and BEFORE `read_idx_local_ = hdr_ri` (`:176`). The store-back (sub-spec 3) happens only AFTER Gate 7 succeeds (Decision 2 step 5 ordering), so compute the resync value here and store it after the lock acquire. Replace line 176 with the gated computation — note the **STRICT `>`** (C1):
   ```
   const std::uint64_t backlog = hdr_wi - hdr_ri;   // hdr_wi >= hdr_ri (Gate 6 passed)
   const std::uint32_t block   = static_cast<std::uint32_t>(hdr_block);
   if (backlog > hdr_cap) {
       // Static-overwrite: hdr_ri < write_idx - capacity, i.e. the oldest unread
       // frame is already overwritten. Resync to the newest block's start.
       read_idx_local_ = (hdr_wi >= block) ? (hdr_wi - block) : 0u;   // underflow clamp
       resynced = true;   // store-back to header.read_idx happens after Gate 7
   } else {
       read_idx_local_ = hdr_ri;   // PR2 verbatim seed (cases 3/4 + the == C boundary).
   }
   ```
2. **Underflow clamp when `write_idx < block`:** set `read_idx_local_ = 0` (NOT `write_idx`). Rationale: `write_idx < block` with `backlog > capacity` is only reachable if `capacity < block`, which Gate 3 already rejects (`hdr_block > hdr_cap → BlockConfigMismatch`). So this branch is defensively dead in practice; clamping to 0 (rather than `write_idx`, which would make `available=0` and force a needless underrun on the first pump) is the safe floor and keeps `read_idx_local_ <= write_idx` (the pump_block availability invariant).
3. **Store-release the resync'd value into `header.read_idx`? → YES (it is the PR5 producer-handshake contract, NOT a frozen-code fact).** **(Gap-a / PM4)** The producer's ring-full / drop-oldest math (ADR §4.2: "if the ring has `< block_size` free slots, drop the new block … computed from `write_idx - read_idx`") is a **PR5 producer-model assumption**, not something the current C++ tree enforces — PR5's `IpcRingSink` will read `read_idx` to decide when to drop. If we resync `read_idx_local_` forward but leave `header.read_idx` stale-low, that future producer would believe the consumer is `> capacity` behind indefinitely and could drop-spuriously. So `prepare()` stores the resync'd value back: `region_.header()->read_idx.store(read_idx_local_, std::memory_order_release)` on the resync branch, AFTER Gate 7 succeeds (so a rejected consumer never mutates the header). **This store-back is the explicit handshake the PR5 producer relies on** (anchor: ADR §4.2 producer xrun policy). It is a CONTROL-THREAD write in `prepare()` before any worker spawns, so it does NOT violate the `pump_block`/`poll_diagnostics` header-write contract (those run after the worker exists; `prepare()` is single-threaded at start). On the non-resync branch, leave `header.read_idx` untouched (PR2 behavior preserved). Add a private `bool resynced = false;` local in `prepare()` to defer the store past Gate 7.
4. **Interaction with Gate 6 (`hdr_ri > hdr_wi` reject):** Gate 6 runs FIRST and unchanged. The resync block only runs when `hdr_wi >= hdr_ri`, so `backlog = hdr_wi - hdr_ri` cannot underflow.
5. **Interaction with the `attached_no_data` latch:** unchanged. `poll_diagnostics` latches `attached_no_data` only while `write_idx == read_idx == 0` (`:341-348`). After a resync, `write_idx != 0`, so the latch correctly does NOT fire (the producer HAS sent data). Case 9 must NOT assert `attachedNoDataWarningCount`.

---

## Decision 2 — Case 10 `multi_attach_rejected` (SPSC invariant)

**ADR §7.1 case 10:** a second consumer attaching to the same ring must be refused (SPSC = single consumer). `RingHeader` (`RingHeader.h:75-92`) has `producer_pid` (0x0030) but NO consumer field; `_reserved[0x0FA8]` spans 0x0058→0x1000.

### Viable Options

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **(b1) Consumer attach-lock in `_reserved`** — a fixed-offset `std::atomic<uint32_t>` carved from `_reserved`; CAS 0→`getpid()` on consumer start; `kill(pid,0)` to reclaim a stale lock from a dead consumer; store 0 on stop/detach. | Keeps wire version=1 (producer zero-inits reserved → lock starts unlocked, PR5-compatible). Cross-process real (the lock lives in shared memory both consumers map). No extra fd/file. Same-process double-attach in one test process also works (two backends, same shm). | Adds a consumer WRITE to the header (previously consumer wrote only `read_idx`). Needs a documented offset + stale-reclaim logic. Two consumers racing the CAS is handled by CAS atomicity. |
| **(b2) `flock`/sidecar lockfile** | Crash-safe auto-release (kernel drops the lock when the process dies). | `SharedMemoryRegion` CLOSES the fd after mmap (PR1 contract) → no fd to flock; needs a SEPARATE fd kept open or a sidecar `<path>.consumer.lock` file, adding a new artifact to the lifecycle + a new cleanup path + a new failure mode (lockfile left behind on `kill -9` for the *file* variant; `flock` needs the held fd). More moving parts; PR1 region wrapper would need a new "keep fd" mode. |
| **(b3) In-process static registry of attached paths** | Trivial; a same-process unit test passes. | Does NOT enforce cross-process SPSC — two engine processes would both attach. Fails the real invariant the ADR wants. Rejected as the production mechanism (acceptable only as a test shortcut, which we do NOT need given b1 is cross-process-real). |

### Recommended: **(b1) Consumer attach-lock in `_reserved`** with `kill(pid,0)` stale reclaim.

**Cross-process-real vs in-process-sufficient:** the chosen mechanism IS cross-process-real (the lock word lives in the shared region both processes map), AND it is in-process-testable (the PR3 ctest does a same-process double-attach — two `SharedRingBackend` instances on one ring — which exercises the exact CAS-rejection path without `fork()`). **`fork()`-based ctest is OUT of scope** for PR3: same-process double-attach is sufficient because the lock is a shared-memory CAS, not a process-local registry — the rejection logic is identical whether the second attach comes from this process or another. (A real cross-process soak belongs in PR6.)

**Exact specification:**

1. **Offset.** Define in `RingHeader.h` a named constant inside the reserved span:
   ```
   // Consumer attach-lock (ADR 0019 PR3). Carved from _reserved; wire version
   // stays 1 because the producer zero-inits _reserved (PR5), so the lock reads
   // 0 == "no consumer" on a fresh ring. 8-byte aligned within the reserved area.
   constexpr std::size_t kConsumerLockOffset = 0x0058;  // first reserved byte, 8-aligned
   ```
   Add a `static_assert(kConsumerLockOffset >= offsetof(RingHeader, _reserved))` and `static_assert(kConsumerLockOffset + sizeof(std::atomic<uint32_t>) <= sizeof(RingHeader))`. Do NOT add a struct field (that would change the type layout / break the `#pragma pack` offset asserts); instead access it via an **inline helper `consumer_lock_atomic(RingHeader*)` in `RingHeader.h` that is UNCONDITIONALLY compiled (M1)** — it is production code, NOT test-only, so both the regular `test_shared_ring_backend` build and the RT-sentinel build (`SRB_RT_SENTINEL`) can reach it:
   ```
   inline std::atomic<std::uint32_t>* consumer_lock_atomic(RingHeader* h) noexcept {
       return reinterpret_cast<std::atomic<std::uint32_t>*>(
           reinterpret_cast<char*>(h) + kConsumerLockOffset);
   }
   ```
   `_reserved` is `uint8_t[0x0FA8]` starting at 0x0058 (verified `:91`), and 0x0058 is 8-byte aligned, so the atomic is well-aligned and lock-free (`RingHeader.h:42-43` already `static_assert`s `atomic<uint32_t>` lock-free). The helper sits inside the existing `#if defined(__linux__)||defined(__APPLE__)` guard but is otherwise unconditional.
   **PR5 compat:** documented in the ADR §2.3 `_reserved`-row Producer/Consumer columns (M4) and the `RingHeader.h` comment that the producer MUST continue to zero-init `_reserved` (it already does per §2.3) — PR5's Python producer needs NO change; a zero lock word = unlocked.
2. **Where in the flow.** Per PR2 frozen contract, `attach()` is mmap-only (no validation, no side effects). So the consumer-lock CAS happens in **`start()` (via `prepare()`)**, NOT in `attach()`. Specifically: add a Gate **7** at the END of `prepare()`'s gate sequence (after Gate 6, after the Decision-1 resync, after `allocate_staging`, just before `return Ok`). This keeps the "attach = pure mmap" contract and the "all validation/side-effects in start()" contract intact.
3. **Gate 7 logic (CAS + stale reclaim):**
   ```
   std::atomic<uint32_t>* lock = consumer_lock_atomic(region_.header());
   const uint32_t me = static_cast<uint32_t>(::getpid());
   uint32_t expected = 0;
   if (!lock->compare_exchange_strong(expected, me, std::memory_order_acq_rel)) {
       // Lock held. Reclaim only if the holder is dead (kill(pid,0) == -1 && errno==ESRCH).
       if (::kill(static_cast<pid_t>(expected), 0) == -1 && errno == ESRCH) {
           // Stale holder dead → try to steal: CAS expected→me.
           if (lock->compare_exchange_strong(expected, me, std::memory_order_acq_rel)) {
               // stole it
           } else {
               return BackendError::ConsumerAlreadyAttached;   // someone else won the race
           }
       } else if (expected == me) {
           // Re-entrant same-pid: this process already holds it (e.g. our own
           // prior start without stop). Treat as already attached — refuse.
           return BackendError::ConsumerAlreadyAttached;
       } else {
           return BackendError::ConsumerAlreadyAttached;   // live foreign holder
       }
   }
   ```
   `::kill` and `::getpid` are CONTROL-THREAD calls in `start()` — NOT on the RT path (no contract violation). On any gate-7 failure, `prepare()` must NOT have allocated (so order matters — see step 5).
4. **Release (Minor — corrected rationale).** In `stop()` (and `~SharedRingBackend()` which calls `stop()`), release ONLY if `holds_consumer_lock_ == true`. The real guard against a double-release / wrong-clear is the **`holds_consumer_lock_` flag** (private `bool holds_consumer_lock_ = false`, set true ONLY on a successful gate-7 acquisition). Use a plain `lock->store(0, std::memory_order_release)` to clear (we hold it, so a bare store is correct — the earlier "CAS so we never clobber a holder that stole it after our death-detection" rationale was vacuous: if WE hold it, nobody else has it). **Reset `holds_consumer_lock_ = false` immediately after the store**, so a `~SharedRingBackend()` that runs after an explicit `stop()` does NOT re-store/re-clear (idempotent stop). `pump_synchronous` (which does NOT acquire — see step 6) leaves `holds_consumer_lock_ == false`, so it never releases.
5. **Allocation ordering.** Gate 7 must run BEFORE `allocate_staging()` so a rejected second consumer allocates nothing (preserves the PR2 "on gate failure, allocate nothing" invariant). Revised `prepare()` order: Gates 1–6 → Decision-1 resync **compute** (no alloc, sets `resynced` local) → **Gate 7 consumer-lock CAS** (sets `holds_consumer_lock_` on success) → if `resynced` then store-release the resync'd `read_idx` to the header (Decision 1 step 3) → commit geometry (set `out_channels_` etc.) → `allocate_staging()` (sizes input AND output staging) → set `callback_`. A rejected consumer (gate-7 fail) never reaches the store-back, the geometry commit, or the alloc.
6. **Test-hook (`pump_synchronous`) must NOT acquire the lock — via the unified `prepare` signature (M2).** `pump_synchronous` calls `prepare()` too (`cpp:313`). If it acquired the consumer lock, a test attaching twice to one ring via `pump_synchronous` would falsely reject. Keep the hooks orthogonal by routing through the single unified **private** signature (M2): `BackendError prepare(AudioCallback* callback, int engine_block_size, int out_channels, bool acquire_consumer_lock)`. `start()` passes `acquire_consumer_lock = true`; `pump_synchronous` passes `false`. (This is a private method, not a public/ABI surface.) Case 10 drives the real `start()` path (which acquires) to test rejection; cases 3–8 use `pump_synchronous` (no acquire) so they never touch the lock.

### Error code: **new `BackendError::ConsumerAlreadyAttached`** (NOT reuse `DeviceOpenFailed`).

**Rationale.** `DeviceOpenFailed` is the §5 OS-fallback / magic-mismatch code; overloading it for "ring is healthy but already has a consumer" hides a distinct, actionable operator error (you started two engines on one ring). Add the enumerator to `AudioBackend.h` and the matching `case BackendError::ConsumerAlreadyAttached: return "consumer_already_attached";` to the exhaustive `describe()` switch in `AudioBackend.cpp` (no `default:` → omitting it is a `-Werror=switch` break, so this edit is MANDATORY and lands in step 0 of the checklist, like PR2's `BlockConfigMismatch`). This keeps the switch exhaustive (P2).

### (M4) Frozen-contract amendment — the consumer-lock is the ONE consumer header write.

PR2's `SharedRingBackend.h:23-27` documents the control thread as writing "ONLY the four warning counters … NEVER writes a header atomic," and the consumer side historically wrote ONLY `read_idx` (in `pump_block`'s consume branch + the Decision-1 resync store-back). The consumer-lock adds exactly ONE more header write. PR3 MUST amend that comment to record the single exception explicitly:
> *Exception (PR3, ADR 0019): the consumer-attach-lock word at `kConsumerLockOffset` is CAS'd 0→pid in `start()` (gate 7) and stored→0 in `stop()`. This is the ONLY header write outside `read_idx`, is CONTROL-THREAD-ONLY (`start`/`stop`), and is NEVER touched on the RT path (`pump_block`/`thread_loop`).*

Mirror the same note in the ADR §2.3 `_reserved` row (M4): set Producer column = "rw (zero-init only)", Consumer column = "rw (attach-lock at 0x0058)", and add a footnote: the producer zero-inits the whole `_reserved` span (which leaves the lock unlocked); the consumer CAS-locks the first 4 bytes; PR5's Python producer needs no change.

**Seam note (PR5/PR7):** the `_reserved` consumer-lock offset (0x0058) is now load-bearing. PR5's Python producer must keep zero-initing `_reserved` (it does). PR7 cross-platform: the atomic-at-offset + `kill(pid,0)` is POSIX (Linux/macOS) — already inside the `#if defined(__linux__)||defined(__APPLE__)` guard; Windows (stretch, §5) would need `OpenProcess`-based liveness, documented as a follow-up but not implemented here.

---

## Decision 3 — CLI + backend pairing + worker thread + output-buffer composition

This is two coupled sub-decisions: **(i)** the worker thread (PR2 deferred it; `start()` only sets `running_=true` at `cpp:207-213`), and **(ii)** how output buffers reach the AudioBlock on the shm-driven path (the confirmed core gap).

### 3(i) — Worker thread

**Verified gap:** PR2 `start()` does NOT spawn a thread (`cpp:208-213` explicitly defers cadence to PR3). `pump_synchronous` is the only driver today. PR3 must add the production worker.

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **(a) Mirror `NullBackend::thread_loop`** — a `std::thread worker_`, `sleep_until(next_deadline)` cadence at `block_size/sample_rate`, read `steady_clock::now()` BEFORE `pump_block`, pass it as `hw_ts_ns`. | Byte-for-byte parallels the proven, in-tree `NullBackend::thread_loop` (`NullBackend.cpp:95-132`); reviewers trust the shape; clock read stays OUT of `pump_block` (preserves PR2 no-clock contract); `sleep_until` is the established cadence. | `running_` must become atomic (see below). |
| **(b) Busy-spin** | No sleep syscall. | Burns a core; no in-tree precedent; rejected (matches PR2 Decision-2 rejection of busy-spin). |

**Recommended: (a).** Spawn `worker_ = std::thread(&SharedRingBackend::thread_loop, this)` at the end of the **3-arg `start(cb,eb,out)`** path ONLY (after gates + alloc + `prepareToPlay`). **(Impl note / PR3-Q6, ratified at implementation):** the worker is gated by a private `start(cb,eb,out,bool spawn_worker)` so ONLY the 3-arg public form (CLI + AC-5) spawns it; the 1-/2-arg forms set `running_`+acquire the consumer lock but spawn no worker — because every frozen PR2 case drives the 2-arg `start()` + manual `pump_block`, which a 2-arg-spawned worker would race. Do NOT "fix" the 2-arg form to spawn a worker. `thread_loop` mirrors `NullBackend.cpp:95-132` exactly: compute `block_period = 1e9 * block_size_ / sample_rate_`; `next_deadline = now + block_period`; loop while `running_.load(acquire)`: read `now = steady_clock::now()`, call `pump_block(callback_, now_ns)`, then the late/sleep_until cadence (record_underrun on late wake — but note this is the *backend-private* XrunCounter, consistent with PR2). Join in `stop()`.

**`running_` MUST become `std::atomic<bool>` (was plain `bool` at `SharedRingBackend.h:186`).** Now that a worker thread reads `running_` while the control thread writes it in `stop()`, a plain `bool` is a data race (UB). Change `bool running_ = false;` → `std::atomic<bool> running_{false};` and update `isRunning()` (`.h:114`), `prepare()`'s `if (running_)` guard (`cpp:102`), `start()`'s `running_ = true` (`cpp:207`), and `stop()`'s `running_` read/write (`cpp:217-218`) to `.load()/.store()`. This mirrors `NullBackend`'s `std::atomic<bool> running_{false}` exactly (`NullBackend.h:64`). **`pump_synchronous` does NOT set `running_`** (it never has — it drives `pump_block` directly without the worker), so the test path stays thread-free.

**Start/stop join semantics:** `stop()` does `running_.store(false, release); if (worker_.joinable()) worker_.join();` BEFORE `callback_->releaseResources()` and BEFORE releasing the consumer lock (Decision 2 step 4) — so no `pump_block` runs after `releaseResources`. `~SharedRingBackend()` calls `stop()` (already does, `cpp:88-90`); the join there is safe because the dtor runs on the control thread.

### 3(ii) — Output-buffer composition (THE core gap)

**Verified:** `pump_block` builds AudioBlock with `output_channels=nullptr, output_channel_count=0` (`cpp:300-302`). The engine renders into `output_channels` and skips rendering entirely when `output_channel_count==0` (render guard `SpatialEngine.cpp:646`; M5). So today the shm path would render silence. PR3 must supply output buffers.

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **(A) shm backend owns output staging** — `SharedRingBackend` allocates an output planar buffer (sized `out_channels × block`), zeroes it each block, sets `AudioBlock.output_channels`/`output_channel_count`, calls the callback (engine renders into it). | Simplest; one object drives one AudioBlock; mirrors `NullBackend` which owns BOTH input and output staging (`NullBackend.cpp:20-32` allocates `flat_buffer_` for output AND `in_flat_buffer_` for input). Contradicts the "input-only" *name* but matches the *NullBackend precedent* (NullBackend is "output+input" and drives the loop). | The class is named "input-only" with `outputChannelCount()==0`; we'd add output staging while keeping that accessor at 0 (the accessor describes the *device's* output, which is still none — the staging is an engine scratch buffer, not a device output). Needs a doc note. |
| **(B) Composition wrapper in `spatial_engine_core.cpp`** — a wrapper callback owns output buffers, calls engine, forwards to WAV; the driving backend provides input. | Clean separation. | **BLOCKED by the frozen `pump_block` API:** `pump_block` hardcodes `AudioBlock.output_channels=nullptr` (`cpp:300`). A wrapper sees the AudioBlock the backend built — it cannot inject output buffers without `pump_block` reading them from somewhere. Would require an API change anyway (back to A). The wrapper alone is insufficient. |
| **(C) Separate output sink object the backend holds** — backend holds an `OutputStaging` it fills into AudioBlock; `spatial_engine_core.cpp` reads it post-callback for WAV. | Same as (A) but with an extra indirection object. | More structure than needed; (A) folds the staging into the backend directly. |

**Recommended: (A) shm backend owns output staging, sized via a new `start()` parameter.**

**Rationale.** `NullBackend` — the proven CI driver — already owns BOTH output (`flat_buffer_`) and input (`in_flat_buffer_`) staging and is the loop driver. `SharedRingBackend` is *also* the loop driver (it has the worker thread), so it is the natural owner of the output scratch buffer the engine renders into. Option (B) is structurally impossible without an API change (the wrapper can't reach into `pump_block`'s hardcoded AudioBlock), and once you change the API you're back to (A). Option (C) adds an object for no gain. "Input-only" is about the *device* (there is no audio device output — the engine's output goes to a *paired* output backend or WAV, not to shm), so `outputChannelCount()` honestly stays 0; the output staging is engine render scratch, exactly like NullBackend's `flat_buffer_`.

**Exact API resolution:**

1. **Add an output-channel count carried through the unified `prepare` (M2) + new `start`/`pump_synchronous` overloads (M3).** The AudioBackend interface fixes `start(callback)`; PR2 added `start(callback, engine_block_size)` (`.h:111`). PR3 routes EVERYTHING through one private signature (M2):
   ```
   private:
     BackendError prepare(AudioCallback* callback, int engine_block_size,
                          int out_channels, bool acquire_consumer_lock);
   public:
     BackendError start(AudioCallback* cb) override               { return startImpl(cb, header_block, 0); }   // delegates, acquire=true
     BackendError start(AudioCallback* cb, int engine_block)       { return startImpl(cb, engine_block, 0); }   // 2-arg, acquire=true
     BackendError start(AudioCallback* cb, int engine_block, int out_channels);                                 // 3-arg, acquire=true (CLI path)
     BackendError pump_synchronous(AudioCallback* cb, int blocks, std::uint64_t hw_ts_base,
                                   int engine_block_size, int out_channels);   // M3: out_channels added; acquire=false
   ```
   - **(Gap-b) The 2-arg `start(cb, engine_block)` delegates with `out_channels=0` AND `acquire_consumer_lock=TRUE`.** So PR2 cases 1–8 — which call the 2-arg `start()` — DO exercise the consumer-lock acquire harmlessly (each PR2 case uses a fresh ring + single `start()`, so the CAS 0→pid always succeeds and never rejects). **Non-regression note:** no PR2 case double-`start`s one ring (verified: cases 3/4 each `attach`+`start` once then `stop`; case 8 structural uses `pump_synchronous`), so none can trip the lock. `pump_synchronous` passes `acquire_consumer_lock=false`, so the case-8 path stays lock-free.
   - **(M3) `pump_synchronous` gains an `out_channels` parameter** routed into the unified `prepare(..., out_channels, /*acquire=*/false)`. This is what lets AC-6 test the output path SYNCHRONOUSLY (no worker, deterministic) — the frozen PR2 `pump_synchronous` had no output path, so AC-6 was untestable as written in iter-1.
   - The CLI path calls the 3-arg `start(cb, engine_block, channels)`.
2. **Set `out_channels_` inside `prepare()` BEFORE `allocate_staging()` (M2);** `allocate_staging()` sizes BOTH input and output staging from members: input `staging_flat_` (`in_channels_ × block`) — unchanged — plus output `out_staging_flat_` (`out_channels_ × block` floats) + `out_staging_channels_` (`float*[out_channels_]`), mirroring `NullBackend.cpp:20-24`/`20-32`. Sized in `prepare()`/`allocate_staging` (control thread) — never on the RT path. When `out_channels_ == 0`, `out_staging_flat_` is empty and `out_staging_channels_` is empty (no alloc).
3. **`pump_block` fills the output side of the AudioBlock:**
   ```
   if (out_channels_ > 0)
       std::fill(out_staging_flat_.begin(), out_staging_flat_.end(), 0.0f);  // zero before engine renders (matches NullBackend.cpp:104)
   blk.output_channels      = (out_channels_ > 0) ? out_staging_channels_.data() : nullptr;
   blk.output_channel_count = out_channels_;
   ```
   When `out_channels_ == 0` (PR2 test path), this is byte-identical to today (`nullptr`/`0`, no fill) → cases 3–8 unaffected. `std::fill` over a pre-sized vector is alloc-free (no realloc) → RT-safe, mirrors NullBackend's per-block output zero (`NullBackend.cpp:104`).
4. **(M3) Output-staging test accessors** mirroring the PR2 input accessors (`stagingData()`/`stagingCapacity()`, `.h:160-161`): add `const float* outStagingData() const noexcept { return out_staging_flat_.data(); }` and `std::size_t outStagingCapacity() const noexcept { return out_staging_flat_.capacity(); }`. **RT-sentinel note:** case 8 invariant (iv) extends to output staging — `outStagingData()`/`outStagingCapacity()` stable across the steady run (a realloc would move them).
5. **WAV / output forwarding in `spatial_engine_core.cpp`:** the existing `WavCapture` wrapper (`spatial_engine_core.cpp:283-308`) reads `block.output_channels` post-`engine_.audioBlock`. Since the shm backend now populates `output_channels`, `WavCapture` works UNCHANGED on the shm path. For a paired live output (Dante), that is PR-future (the ADR §3.2 says shm-in pairs with Dante-out); PR3's no-hardware CI path uses `--input-backend shm:<path>` + WAV (or null) output, which only needs the backend-owned output staging + WavCapture. Confirm: the shm backend is the SOLE driver; there is no second output backend `start()` in PR3's CI path (the engine renders into the shm backend's output staging, and WavCapture taps it).

**CLI + pairing in `spatial_engine_core.cpp`:**

6. **Parse `--input-backend shm:<path>|null|dante`.** Add a new arg `--input-backend` (string, default `dante` to preserve current behavior) alongside the existing `--backend` (which stays for OUTPUT/device selection). Add to the arg loop (`:340-392`):
   ```
   else if (a == "--input-backend") input_backend = nexts("dante");
   ```
   Then after parsing, classify:
   - `input_backend.rfind("shm:", 0) == 0` → `shm_path = input_backend.substr(4)` → SharedRingBackend input (step 7).
   - **(Gap-c) `input_backend == "null"`** → a NULL INPUT, **distinct from `--backend null` (output)**: construct `make_null_backend(sr, channels, block_size, /*input_channels=*/channels)` as the driver so the engine sees a null input source (the existing NullBackend already supports an input fixture / input channels, `NullBackend.h:46`); this is the no-producer driver path. Banner prints `input=null` separately from `output=<--backend>`.
   - `input_backend == "dante"` (default) → current behavior unchanged (the existing `--backend` selection drives I/O; no separate input backend constructed).
   The banner change (input distinct from output) is exercised by AC-9.
7. **Backend construction (shm).** When `shm_path` is set: build `SharedRingBackend::attach(shm_path, AttachMode::OpenExisting)`; if null (mmap failed), print error + exit nonzero (no Dante fallback in PR3 — the operator explicitly asked for shm). Then `start(callback, engine_block_size, channels)` (3-arg, acquire=true) where `engine_block_size` = the engine's block (`block_size` CLI, default 64; the header `block_size` must divide it per Gate 3) and `channels` = speaker count for the engine's output render. Guard the whole shm branch with `#if defined(__linux__) || defined(__APPLE__)`; on other platforms print "shm input requires Linux/macOS" + exit nonzero. Wrap the include `#include "audio_io/SharedRingBackend.h"` under the same guard.
8. **engine block_size vs header block_size divisor wiring.** Use the 3-arg `start(callback, engine_block_size, output_channels)` form. `engine_block_size` is the engine's callback block (the `--block` CLI value, default 64). The header `block_size` (producer's write granularity) must divide it (Gate 3, PR2). PR3 does NOT change this gate — it just passes the real engine block instead of PR2's "default to header block_size" stub (`cpp:199`). For the CI test the producer writes `block_size = 64` and the engine block is `256` (4 producer blocks per engine block) — but note the **PR3 ctest cases 9/10 use `pump_synchronous`/direct `pump_block`, NOT the CLI**, so the CLI divisor wiring is exercised by manual/soak runs, not the unit ctest (acceptable for PR3; soak in PR6).
9. **Banner / `--help`.** Update `print_banner` (`:255-264`) to print the input backend (`shm:<path>` / `null` / `dante`) distinct from the output backend. Add `--input-backend shm:<path>|null|dante` to the `--help` text (`:374-389`) with a one-line note: "shm input (Linux/macOS): pulls PCM from a producer ring; pairs with --backend for output." Print the `SharedRingBackend::description()` after start (it already prints `backend->description()` at `:471`).
10. **`SPE_HAVE_JUCE`-OFF build.** The shm path is JUCE-free (depends only on `SharedRingBackend` + PR1 primitives, all `#if defined(__linux__)||defined(__APPLE__)`-guarded, NOT `SPE_HAVE_JUCE`-guarded — verified `SharedRingBackend.cpp:8`). So `--input-backend shm:<path>` + `--backend null` (or `--wav`) builds and runs under `-DSPATIAL_ENGINE_NO_JUCE=ON`. The Dante OUTPUT pairing is the only `SPE_HAVE_JUCE` part and is unchanged.

---

## §0.5 — Deliberate-mode escalation rationale

PR3 is DELIBERATE because it adds the FIRST **production worker thread** to `SharedRingBackend` (a data-race surface: `running_` shared between worker and control thread; a missed atomic conversion is silent UB that passes casual tests), and it introduces a **cross-process shared-memory write** (the consumer lock) on a producer-owned region (a wrong offset or missing zero-init assumption silently breaks the PR5 Python producer or corrupts a live ring). Both failure classes degrade intermittently rather than crash — exactly what the pre-mortem + expanded test plan exist to catch.

---

## §Pre-mortem — 4 failure scenarios

- **PM1 — Resync clobbers a healthy still-resident stream (Decision 1 over-fires).** If the gap-gate fired at or below the resident boundary — e.g. the iter-1 `>= C`, or the Architect-suggested `> C - block` — a consumer attaching to a producer that is legitimately ≤ one full ring ahead would jump to the head and DISCARD still-resident buffered audio that should play (regressing the case-4 "play buffered then silence" contract and ADR §6 "late by < 1 block → plays back-block"). **The `== C` boundary is the trap:** at `backlog == C`, `hdr_ri == write_idx - capacity` = the OLDEST STILL-RESIDENT frame — valid data that must be played, NOT resynced. **Detection:** case 9 asserts the resync fires at `backlog > C` (AC-1, `8448`) AND two NEGATIVE sub-cases: (AC-2) a 2-block backlog (`512 « 8192`) does NOT resync, and (the C1 boundary AC) `write_idx=8192, read_idx=0` (`backlog == C` exactly) does NOT resync — `header.read_idx` stays 0, b0 delivered first. **Mitigation:** trigger is **STRICT `backlog > capacity_frames`** (C1 static-overwrite predicate); cases 3 (gap 0), 4 (gap 128 « 8192), and the `== C` boundary provably stay on the verbatim path; CI runs all PR2 cases.

- **PM2 — Consumer lock leaks on crash → ring permanently un-attachable.** If the engine is `kill -9`'d, `stop()` never runs, so the lock word keeps the dead PID. A new consumer must reclaim it. If the `kill(pid,0)` stale-reclaim is wrong (e.g. checks the wrong errno, or a PID has been recycled to a live unrelated process), the ring is either permanently locked (false "alive") or wrongly stolen (false "dead"). **Detection:** case 10 includes a stale-lock sub-case: write a definitely-dead PID (e.g. a PID we just `fork()`+`waitpid`'d to reap, OR PID 0x7FFFFFFF which `kill(pid,0)` returns ESRCH for on Linux) into the lock word, then assert a fresh `start()` SUCCEEDS (reclaims). **Mitigation:** reclaim only on `kill(pid,0)==-1 && errno==ESRCH`; PID-recycle false-negative is an accepted same-host risk (ADR §10: rings are file-permission-controlled, not authenticated) and is bounded — at worst a live unrelated process holds the lock and the second engine refuses (fail-safe, not data-corrupting). Document this in the gate-7 comment.

- **PM3 — `running_` left non-atomic → torn read/UB on the worker join.** If the `bool running_`→`atomic<bool>` conversion is missed in any of the 4 sites (`isRunning`, `prepare` guard, `start`, `stop`), the worker thread's `running_.load()` races the control thread's `store(false)`, and the worker may spin forever (missed stop) or read a torn value → the `stop()` join hangs. **Detection:** a dedicated start-then-stop ctest (AC-5): `start()` the backend with the worker thread (NOT `pump_synchronous`), let it run a few blocks against a prefilled ring, `stop()`, and assert `stop()` returns `Ok` and `isRunning()==false` and the join completed (the test would hang/timeout if `running_` were non-atomic and the store were lost). Run under TSan-capable build if available; otherwise the join-completes assertion + ctest `--timeout` is the guard. **Mitigation:** convert all 4 sites; mirror `NullBackend`'s `std::atomic<bool> running_{false}` exactly; review gate confirms no plain-`bool` read of `running_` remains (`grep -n 'running_' SharedRingBackend.*`).

- **PM4 — First-post-resync pump laps the producer / the PR5 producer ignores the store-back (Gap-a seam).** Decision 1 resyncs `read_idx_local_ = write_idx - block` and stores it back to `header.read_idx`. Two coupled risks: (1) **lapping race** — between the resync decision in `prepare()` and the first `pump_block`, a fast producer keeps writing; if it advanced a full `capacity_frames` in that window the first pump's `[write_idx-block, write_idx)` read would itself be partially overwritten. This is bounded: the worker thread is NOT spawned until AFTER `prepare()` returns (Decision 3(i)), so the resync decision is single-threaded, and the first pump re-loads `write_idx` with acquire (PR2 `pump_block` step 1) and recomputes `available` — a lapped read shows as `available < block` → silence + xrun (safe degrade, not a torn read), and the producer would have to write an entire ring in a sub-millisecond `prepare()`→pump window (implausible at 48 k/256). (2) **PR5 producer-model seam** — the store-back assumes PR5's `IpcRingSink` READS `header.read_idx` to drive its drop-oldest policy (ADR §4.2). If PR5 ignores `read_idx` (writes blindly), the store-back is harmless but the consumer-lag signal is lost; this is a PR5 design obligation, NOT a PR3 frozen-code fact. **Detection:** PR3 cannot test the live producer; AC-1 asserts the store-back happened (`header.read_idx == write_idx - block`) so the handshake VALUE is correct; the lapping race is a PR6 soak item (cross-process, real producer cadence). **Mitigation:** document the store-back as the explicit PR5 handshake contract (Decision 1 step 3, anchored to ADR §4.2); the single-threaded resync + acquire-reload-in-pump bounds the race to a safe-degrade, never a torn read.

---

## §Expanded test plan

| Layer | What | Where | Assertion |
|---|---|---|---|
| **Unit** | case 9 resync (`backlog > C`) | PR3 `test_shared_ring_backend.cpp` | resync fires; see AC-1 |
| **Unit** | case 9 NEGATIVE (sub-capacity gap does NOT resync) | PR3 (PM1 guard) | AC-2: `header.read_idx == hdr_ri`, b0 first |
| **Unit** | case 9 BOUNDARY (`backlog == C` does NOT resync) | PR3 (C1 boundary, PM1 guard) | AC-2b: `write=8192,read=0` → `read_idx` stays 0, b0 first |
| **Unit** | case 10 multi-attach reject (independent ring) | PR3 (C2) | 2nd `start()` → `ConsumerAlreadyAttached`; see AC-3 |
| **Unit** | case 10 stale-lock reclaim (independent ring) | PR3 (PM2/C2 guard) | dead-PID lock → fresh `start()` succeeds, lock→0 on stop; see AC-4 |
| **Unit** | resync store-back value correctness (PM4 seam) | PR3 | AC-1: `header.read_idx == write_idx - block` (PR5 handshake) |
| **Integration (RT-safety)** | worker-thread start/stop/join (PM3) | PR3 | `start()` (real thread) → run → `stop()==Ok`, `isRunning()==false`, join completes (no hang) |
| **RT-safety** | output-staging stability under steady run | PR3 (extend case 8 invariant iv) | `out_staging_flat_.data()/.capacity()` stable; `rt_alloc_violations()==0` in RT_ASSERTS=ON |
| **No-regression** | PR2 cases 1–8 + 4 security cases | PR3 (re-run) | all green, byte-identical behavior (Decision 1 must not regress) |
| **CLI smoke (no hardware)** | `--input-backend shm:<path>` + `--wav`/null output builds & parses | PR3 (manual + build gate) | binary builds under `NO_JUCE=ON`; `--help` lists the flag; bad shm path exits nonzero |
| **Observability** | counters → `/sys/warning shm_*` | **PR4** | deferred (counters from PR2 are the hook) |
| **E2E / soak** | 60 s Python→shm→null, cross-process 2nd-attach reject, sample-exact | **PR6** | deferred |

---

## §Per-case Acceptance Criteria (each tied to a command)

**Build & test commands (no hardware, per project `.claude/CLAUDE.md`):**
- Build: `cd /home/seung/mmhoa/spatial_engine/core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j$(nproc)`
- Tests: `ctest --output-on-failure` (from `core/build`).
- RT-sentinel lane: separate `-DSPATIAL_ENGINE_RT_ASSERTS=ON` build dir → `ctest -R shared_ring_backend_rt_sentinel --output-on-failure`.

- **AC-1 — Case 9 resync (positive, `backlog > C`).** New test `test_attach_to_pre_existing_ring_resyncs_read_idx`. Setup a ring `cap=8192, block=256, ch=2`; producer writes so the backlog `write_idx - read_idx > 8192` STRICTLY (set `read_idx=0`, `write_idx = 8192 + 256 = 8448`, backlog `8448 > 8192`). `attach()` non-null; `start(cb, 256)` → `Ok`. **Assert:** `header.read_idx.load() == write_idx - block == 8448 - 256 == 8192` (resync stored head-minus-block back to the header — the PM4/PR5 handshake value, Decision 1 step 3); the first `pump_block` delivers the block at logical index `8192`, NOT index 0; `xrunCount()==0` after that pump (data is available). *Proves:* STRICT-`>` resync, header store-back value, no spurious underrun. **Command:** `ctest -R '^shared_ring_backend$' --output-on-failure`.
- **AC-2 — Case 9 resync (negative, sub-capacity / PM1 guard).** Same ring geometry; producer writes exactly 2 blocks (`write=512, read=0`, backlog `512 < 8192`). `start(cb,256)` → `Ok`. **Assert:** `header.read_idx` UNCHANGED at 0 (verbatim seed; no resync); pump#1 delivers b0 sample-exact, pump#2 delivers b1 (case-4 invariant holds under the new code). *Proves:* the gate does NOT over-fire below capacity. **Command:** same.
- **AC-2b — Case 9 BOUNDARY (`backlog == C` does NOT resync / C1 trap guard).** Same ring geometry; set `read_idx=0`, `write_idx = 8192` (backlog `== 8192 == capacity`, so `hdr_ri == write_idx - capacity` = the oldest still-resident frame). `start(cb,256)` → `Ok`. **Assert:** `header.read_idx` UNCHANGED at 0 (STRICT `>` means `== C` does NOT fire — the resident frame is played, not skipped); pump#1 delivers the block at logical index 0 (b0) first. *Proves:* the C1 strict-`>` boundary; a `>=` or `> C-block` predicate would FAIL this AC. **Command:** same.
- **AC-3 — Case 10 multi-attach reject (independent ring / C2).** New test `test_multi_attach_rejected` on its OWN shm name (distinct from AC-4). `be1 = attach(ring); be1->start(cb1, 256)` → `Ok` (acquires lock). Then `be2 = attach(ring)` → non-null (mmap-only succeeds); `be2->start(cb2, 256)` → **`BackendError::ConsumerAlreadyAttached`**, `be2->isRunning()==false`, `be2` allocated nothing. `be1->stop()` → **assert the lock word `consumer_lock_atomic(header)->load() == 0`** (release directly observable, C2). Then a fresh `be3->start(cb3,256)` → `Ok` (re-acquires the now-free lock). **Assert** the exact error code and `describe(ConsumerAlreadyAttached)=="consumer_already_attached"`. *Proves:* CAS rejection + observable release-on-stop. **Command:** same.
- **AC-4 — Case 10 stale-lock reclaim (independent ring / PM2+C2 guard).** New test `test_stale_consumer_lock_reclaimed` on its OWN shm name (distinct from AC-3, no order dependence). Pre-write a dead PID into the lock word (`consumer_lock_atomic(header)->store(0x7FFFFFFF)` — a PID `kill(pid,0)` reports ESRCH for on Linux). `be = attach(ring); be->start(cb,256)` → `Ok` (reclaimed). **Assert** the lock word now holds `getpid()`. Then `be->stop()` → **assert the lock word returns to 0** (release, C2). *Proves:* stale reclaim + release. **Command:** same.
- **AC-5 — Worker start/stop/join (PM3 guard).** New test `test_worker_thread_start_stop_join`. Prefill a ring; `start(cb, engine_block, /*out_channels=*/0)` (real worker thread via `start()`, NOT `pump_synchronous`); spin-wait until `cb.calls > 4` (bounded loop with a timeout, e.g. 2 s); `stop()` → **`Ok`**, `isRunning()==false`. **Assert** the test returns (a non-atomic `running_` lost-store would hang the join → ctest timeout = FAIL). This is the ONLY AC that uses the real worker thread (M3 — output/RT ACs use the synchronous path). *Proves:* atomic `running_` + clean join. **Command:** `ctest -R '^shared_ring_backend$' --timeout 30 --output-on-failure`.
- **AC-6 — Output staging populated & RT-stable (SYNCHRONOUS path / M3).** New test driving the **synchronous** hook (no worker, deterministic — the frozen `pump_synchronous` could not test output, so M3 adds `out_channels` to it). Prefill a ring `ch=2, block=64, cap=32768`; `pump_synchronous(cb, K=256, hw_ts_base=0, engine_block=256, /*out_channels=*/8)`; **assert** every AudioBlock seen by the callback had `output_channel_count==8` and `output_channels != nullptr`; the input invariants (ii) `read_idx` advanced `K*block`, (iii) `xrunCount()` unchanged, AND (iv) extended: input staging `stagingData()/stagingCapacity()` AND output staging `outStagingData()/outStagingCapacity()` identical before/after (no realloc on either). RT_ASSERTS=ON build: `rt_alloc_violations()==0` covers the per-block output `std::fill`. *Proves:* output composition is RT-safe and deterministic. **Command:** `ctest -R 'shared_ring_backend' --output-on-failure` + RT lane.
- **AC-7 — No PR2 regression (Gap-b).** Full `ctest --output-on-failure` from `core/build`: all PR2 cases 1–8 + the 4 security cases (`test_stale_read_idx_ahead_of_write_rejected`, `test_oversized_geometry_rejected`, `test_region_smaller_than_header_claims_rejected`, `test_zero_sample_rate_rejected`, per `main()` `:954-957`) GREEN. **Note (Gap-b):** PR2 cases that call 2-arg `start(cb,256)` now harmlessly acquire the consumer lock (acquire=true) — each uses a fresh ring + single `start()`, so the CAS 0→pid always succeeds; no PR2 case double-`start`s one ring, so none can trip the reject path. *Proves:* Decision 1/2/3 additive-only. **Command:** `ctest --output-on-failure` (known flake: `vst3_bind_collision` under `-j`; passes isolated — do NOT "fix").
- **AC-8 — RT-sentinel main compiles & passes (M1).** Cases 9/10 + ALL their helpers live entirely inside the `#else` (non-sentinel) block (`:941-960`) with ZERO references from the sentinel `main()` (`:933-939`). The `consumer_lock_atomic` helper is production (`RingHeader.h`, unconditionally compiled, M1) so the test reaches it in BOTH builds. **`-Wunused-function` guard:** if the RT_ASSERTS=ON build compiles the file with `SRB_RT_SENTINEL` defined, the case-9/10 functions are unreferenced → a `-Werror=unused-function` break. Place them inside `#if !defined(SRB_RT_SENTINEL)` (the existing `#else` block) so they are not even compiled in the sentinel build (mirrors how PR2 cases 1–7 already live there, `:850`). If a future refactor moves a shared helper out of that guard, mark it `[[maybe_unused]]`. **Command:** RT_ASSERTS=ON build → `ctest -R shared_ring_backend_rt_sentinel --output-on-failure` (compiles clean, sentinel passes).
- **AC-9 — CLI builds & parses, both input modes (no hardware / Gap-c).** `cd core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j$(nproc) spatial_engine_core` succeeds; then:
  - `./spatial_engine_core --help` lists `--input-backend shm:<path>|null|dante`.
  - `./spatial_engine_core --input-backend shm:/nonexistent.shm --backend null --seconds 1` exits NONZERO with a clear "attach failed" message (no crash).
  - **(Gap-c) `./spatial_engine_core --input-backend null --backend null --seconds 1`** runs and exits 0, and the banner shows `input=null` DISTINCT from `output=null` (i.e. `--input-backend null` selects a null INPUT source, not just the `--backend null` output). *Proves:* CLI wiring + JUCE-OFF build + graceful shm failure + distinct null-input parse + banner change. **Command:** the build + three binary invocations above.

---

## Verification gate

- **Build (default CI):** `cd /home/seung/mmhoa/spatial_engine/core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j$(nproc)` — clean.
- **Tests:** `ctest --output-on-failure` from `core/build` — all PR2 cases 1–8 + 4 security cases STILL green (AC-7) + new cases 9/10 + worker/output sub-cases green. `vst3_bind_collision` `-j` flake tolerated (passes isolated).
- **RT-sentinel lane:** separate `-DSPATIAL_ENGINE_RT_ASSERTS=ON` build → `ctest -R shared_ring_backend_rt_sentinel` green; `rt_alloc_violations()==0` covers the new output-staging path (AC-6/AC-8).
- **CLI smoke:** AC-9 commands.
- **Python:** `python3 -m pytest` — unchanged by PR3 (no Python touched until PR5); run to confirm no regression.
- **Commit policy (`.claude/CLAUDE.md`):** commit only when CI-clean; do NOT tag (`v*` only on explicit user request).

---

## §ADR-style footer

- **Decision.** PR3 wires `SharedRingBackend` into the engine: (1) **Decision 1** — case-9 resync is a **gap-gated clamp** in `prepare()` (resync `read_idx_local_ = write_idx - block` and store-release it to `header.read_idx` ONLY when `backlog > capacity_frames` STRICTLY — the static-overwrite predicate, C1; verbatim seed at `backlog <= C` incl. the `== C` resident boundary), preserving PR2 cases 3/4. (2) **Decision 2** — case-10 SPSC enforcement via a **consumer attach-lock atomic carved from `_reserved` at offset 0x0058** (production inline `consumer_lock_atomic` helper, M1), CAS'd in `start()` (Gate 7) with `kill(pid,0)` stale reclaim and `holds_consumer_lock_`-guarded release in `stop()`; new `BackendError::ConsumerAlreadyAttached`. (3) **Decision 3** — add a **worker `std::thread`** (NullBackend-shaped `sleep_until` cadence, clock read OUTSIDE `pump_block`), make `running_` atomic, and have the **backend own output staging** (filled into the AudioBlock via the unified `prepare(cb, eb, out_channels, acquire_lock)` reached by new `start(cb,eb,out)` + `pump_synchronous(...,out_channels)` overloads, M2/M3) so the engine renders; `--input-backend shm:<path>|null|dante` CLI (incl. distinct null-input, Gap-c) + `WavCapture` output tap in `spatial_engine_core.cpp`.
- **Drivers.** D1 no PR2 regression; D2 cross-process SPSC fidelity + wire/PR5 compat + hardware-free testability; D3 RT-safety + JUCE-OFF build invariance.
- **Alternatives considered.** D1: unconditional resync (rejected — breaks case 4) / AttachMode+producer-ahead predicate (rejected — fires on case 4) / explicit flag (rejected — more API than the self-documenting gap gate needs). D2: `flock`/sidecar lockfile (rejected — PR1 closes the fd; adds an artifact + new failure mode) / in-process registry (rejected — not cross-process-real). D3: busy-spin cadence (rejected — burns a core, no precedent) / composition wrapper alone (rejected — `pump_block` hardcodes output=nullptr, structurally impossible without the API change that collapses back to backend-owned staging).
- **Why chosen.** Each decision is the minimal additive change that honors the PR2 frozen contracts: the STRICT-`>` gap gate fires only when the oldest unread frame is physically overwritten (matching the case-9 title without touching cases 3/4 or the `== C` resident boundary); the `_reserved` CAS keeps wire version=1 and PR5 producer-compat while being both cross-process-real and same-process-testable (no `fork()` ctest needed); backend-owned output staging mirrors the proven `NullBackend` driver and is the only option compatible with the frozen `pump_block` AudioBlock construction.
- **Consequences.** `_reserved` offset 0x0058 is now load-bearing (PR5 must keep zero-initing it; PR7 Windows needs `OpenProcess` liveness). The PR2 thread-model contract comment (`SharedRingBackend.h:23-27`) + ADR §2.3 `_reserved` row are amended (M4) to record the consumer-lock as the ONE consumer header write beyond `read_idx`. `running_` is now atomic (worker thread exists). `pump_block` zeroes + populates output staging when `out_channels_ > 0` (no-op at 0 → PR2 cases unaffected). One unified private `prepare(cb, eb, out_channels, acquire_lock)` backs all entry points (M2); `pump_synchronous` gains `out_channels` (M3). The consumer lock is acquired on every `start()` path (incl. the 2-arg form PR2 cases use — harmless, fresh-ring single-attach, Gap-b) but never `pump_synchronous`. PID-recycle false-negative on stale reclaim is an accepted, fail-safe (refuse, never corrupt) same-host risk per ADR §10.
- **Follow-ups (PR4–PR7 seams).** **PR4:** hook real `/sys/warning shm_*` + `/sys/state` on the existing 4 counter edges + emit `/sys/state.shm_consumer_locked` from the new lock word (the engine control tick must call `poll_diagnostics`; PR3 leaves that wiring to PR4 since the standalone loop at `spatial_engine_core.cpp:499-537` does not yet tick diagnostics). **PR5:** `dreamscape/adm_player/ipc_sink.py` — producer keeps zero-initing `_reserved` (consumer-lock-compatible) and provides the store-release fence (PM2 of PR2). **PR6:** 60 s cross-process soak — real 2nd-process attach reject + resync-on-late-attach. **PR7:** cross-platform CI; Windows consumer-lock liveness via `OpenProcess`; document macOS gap.

---

## Execution checklist for autopilot (TDD order)

For each step, commit footer: `ADR 0019 PR3 / <step>` (CI-clean, no tag).

0. **(MANDATORY first — enum + switch, like PR2 step 0.)** Add `BackendError::ConsumerAlreadyAttached` to `core/src/audio_io/AudioBackend.h` and `case BackendError::ConsumerAlreadyAttached: return "consumer_already_attached";` to the exhaustive `describe()` switch in `core/src/audio_io/AudioBackend.cpp` (no `default:` → omitting = `-Werror=switch` break). Build green before touching the backend.
1. **(M2 — collapse ALL signature churn into ONE step, FIRST.)** Land the final method shapes up front so no later step re-touches signatures:
   - `running_` → `std::atomic<bool> running_{false}` (`SharedRingBackend.h:186`); update all 4 sites (`isRunning`, `prepare` guard, `start`, `stop`) to `.load()/.store()` (mirror `NullBackend.h:64`).
   - Replace the private `prepare(cb, eb)` with the unified `prepare(AudioCallback*, int engine_block_size, int out_channels, bool acquire_consumer_lock)`. Route: `start(cb)`→`(cb,header_block,0,true)`; `start(cb,eb)`→`(cb,eb,0,true)`; new `start(cb,eb,out)`→`(cb,eb,out,true)`; `pump_synchronous(cb,blocks,hw_ts_base,eb,out)`→`(cb,eb,out,false)` (M3 adds `out_channels` to `pump_synchronous`).
   - Add members: `int out_channels_`, `std::vector<float> out_staging_flat_`, `std::vector<float*> out_staging_channels_`, `std::thread worker_`, `bool holds_consumer_lock_`; add output test accessors `outStagingData()/outStagingCapacity()`. `allocate_staging()` sizes BOTH input and output from members (`out_channels_` set in `prepare` before the call).
   - Build green; full `ctest` green (PR2 cases use `pump_synchronous` with `out=0`, no worker — unaffected; the 2-arg `start` they use now acquires the lock harmlessly on a fresh ring, Gap-b/AC-7).
2. **Decision 1 — strict-`>` gap-gated resync.** Edit `prepare()` (`cpp:176`) per Decision 1 (compute `resynced`; store-back to `header.read_idx` after Gate 7). Add AC-1 (positive `>C`) + AC-2 (sub-capacity negative) + AC-2b (`==C` boundary negative) tests. TDD: failing tests → implement → green → confirm cases 3/4 still green.
3. **Decision 2 — consumer lock.** Add `kConsumerLockOffset` + the unconditional inline `consumer_lock_atomic()` to `RingHeader.h` (+ static_asserts, + `_reserved` zero-init doc note); amend the `SharedRingBackend.h:23-27` contract comment + ADR §2.3 `_reserved` row (M4). Add Gate 7 to `prepare()` (behind `acquire_consumer_lock` so `pump_synchronous` skips it; before `allocate_staging`; sets `holds_consumer_lock_`); `holds_consumer_lock_`-guarded release in `stop()` (reset flag after store). Add AC-3 (reject, independent ring) + AC-4 (stale reclaim, independent ring) tests. TDD as above.
4. **Decision 3(i) — worker thread.** Implement `thread_loop()` (mirror `NullBackend.cpp:95-132`, clock read before `pump_block`); spawn `worker_` in the **3-arg `start(cb,eb,out)` ONLY** (via the private `spawn_worker` flag — see PR3-Q6; after gates+alloc+`prepareToPlay`), join in `stop()` (before `releaseResources` + lock release). The 1-/2-arg `start()` forms acquire the lock but spawn no worker (frozen PR2 cases use 2-arg `start()`+manual `pump_block`). Add AC-5 (start/stop/join, 3-arg path). Build + ctest green.
5. **Decision 3(ii) — output staging fill.** Implement the `pump_block` output-zero + AudioBlock output population (members + signatures already exist from step 1). Add AC-6 (synchronous path, `out=8`, RT-stable). Confirm PR2 cases 3–8 (which use `out=0`) still green.
6. **Decision 3 — CLI.** Wire `--input-backend shm:<path>|null|dante` (incl. distinct null-input, Gap-c) + pairing + banner/`--help` in `spatial_engine_core.cpp` (shm branch guarded `#if defined(__linux__)||defined(__APPLE__)`). AC-9 build + 3 binary smokes.
7. **Final gate.** Full `ctest --output-on-failure` (AC-7) + RT_ASSERTS=ON lane (AC-8, confirm cases 9/10 compile under the RT-sentinel `main()` with no dangling refs / `-Wunused-function`) + `python3 -m pytest` + AC-9 CLI smoke.

---

## Open questions (persist to `.omc/plans/open-questions.md`)
- **PR3-Q1 (Decision 1 trigger boundary — RESOLVED iter-2 C1):** the resync gate is **STRICT `backlog > capacity_frames`** (static-overwrite predicate: `hdr_ri` overwritten ⟺ `hdr_ri < write_idx - capacity` ⟺ `backlog > C`). The `== C` boundary does NOT resync (oldest still-resident frame is played). Both the iter-1 `>= C` and the Architect-suggested `> C - block` are corrected to `> C`. Confirm with Architect this STRICT-`>` reading is the intended case-9 semantics (vs the literal "always resync on attach" reading of the §7.1 title); if always-resync is wanted, fall back to Decision-1 option (a) (`AttachMode::ResyncToHead`) with cases 3/4 opting into verbatim explicitly. — Why it matters: the one place the ADR title and the case-4 contract can read as conflicting; the `== C` trap regresses a resident block if mis-gated.
- **PR3-Q2 (Decision 2 offset bikeshed):** `kConsumerLockOffset = 0x0058` (first reserved byte). Confirm no future v2 header field is already informally promised that slot. If PR4's `/sys/state` wants a consumer-state byte, it can share the same word (lock PID doubles as "consumer alive"). — Why it matters: the offset is load-bearing for PR5/PR7; changing it later is a wire break.
- **PR3-Q3 (Decision 3 output channel source):** the CLI passes `channels` (the `--channels` speaker count) as the backend's `output_channels`. Confirm this equals the engine's speaker-layout channel count in all configs (the engine clamps to `min(output_channel_count, n_spk)` at `SpatialEngine.cpp:703` (M5), so an over-count is safe but an under-count truncates). — Why it matters: a mismatch silently truncates output channels.
- **PR3-Q4 (diagnostics tick deferred to PR4):** PR3 does NOT call `poll_diagnostics` from the standalone loop (`spatial_engine_core.cpp:499-537` has no control tick). Confirm Architect accepts deferring the diagnostics-tick wiring to PR4 (where the real `/sys/warning` emit lands), since PR3's cases 9/10 drive `poll_diagnostics` directly in-test. — Why it matters: without a tick, a live shm run emits no stale/pacing warnings until PR4; acceptable per ADR §8 PR split but worth ratifying.
