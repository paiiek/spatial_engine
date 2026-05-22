# Plan — ADR 0019 Phase C PCM IPC, PR2 (backend)

**Mode:** RALPLAN-DR / **DELIBERATE** (RT audio thread + cross-process shared-memory atomics)
**Authoritative design:** `docs/adr/0019-phase-c-pcm-ipc-shm-ring.md` (§2 wire, §3 integration, §6 telemetry, §7.1 cases, §8 PR split)
**Target files:**
- `core/src/audio_io/SharedRingBackend.h`
- `core/src/audio_io/SharedRingBackend.cpp`
- `core/src/audio_io/AudioBackend.h` — **(iter-2 M-A)** add the new `BackendError::BlockConfigMismatch` enumerator (header `block_size` does not divide the engine block / `block_size > capacity_frames`), distinct from `BlockSizeExceedsMax`.
- `core/src/audio_io/AudioBackend.cpp` — **(iter-2 M-A)** add the matching `case BackendError::BlockConfigMismatch: return "block_config_mismatch";` to the exhaustive `describe()` switch (currently `AudioBackend.cpp:8-16`, no `default:` — adding the enumerator without the case is a `-Werror=switch` build break, so this edit is mandatory and lands BEFORE case 2).
- `core/tests/core_unit/test_shared_ring_backend.cpp`
- `core/CMakeLists.txt` (SPE_CORE_SOURCES wiring) + `core/tests/core_unit/CMakeLists.txt` (test registration; the RT case 8 target is guarded `if(SPATIAL_ENGINE_RT_ASSERTS)` per **iter-2 M2**)

**Scope boundary:** Input-only `AudioBackend` impl on the PR1 primitives + ctest cases **1–8** from ADR §7.1. **Deferred:** cases 9 (resync-on-attach) & 10 (multi-attach-reject) → **PR3**; real `/sys/warning` OSC codes + `/sys/state` → **PR4**; engine CLI wiring (`--input-backend shm:`) → **PR3**; adm_player sidecar → PR5; soak → PR6.

---

## §0 — RALPLAN-DR Summary

### Principles (P)
- **P1 — Audio thread is sacred.** No allocations, no syscalls, no locks, no logging on the polling loop. All allocation happens in `start()`/`prepareToPlay()`. (Mirrors `AudioCallback.h` Principle 1 and `NullBackend::thread_loop` discipline.)
- **P2 — Build on PR1, do not redesign.** `RingHeader`, `SharedMemoryRegion`, `channel_byte_offset()`, `total_region_bytes()` are frozen. Producer owns the shm lifecycle (we never `shm_unlink`). SPSC, planar f32.
- **P3 — Conform to the existing seam.** `SharedRingBackend` is a drop-in `AudioBackend`; `xrunCount()` is exposed but the backend **does not emit OSC** (per task note + ADR §3.1: warning emission lives in engine/IPC layer = PR4).
- **P4 — Deterministic, hardware-free tests.** Mirror `NullBackend`'s `pump_synchronous` + `set_input_fixture` test-hook pattern; producer side is faked in-test by writing the ring directly. Linux/macOS only (POSIX shm), guarded like `test_shared_memory_region.cpp`.
- **P5 — Forward-compatible telemetry seam.** Whatever PR2 ships for cases 5 & 6 must let PR4 wire real OSC **on top, without rework**.

### Decision Drivers (top 3)
- **D1 — RT-safety provability.** Case 8 must remain green and meaningful. Note the build reality (below): the existing alloc sentinel is gated by `SPATIAL_ENGINE_RT_ASSERTS`, which is **OFF** in `build_off`. The chosen design must give case 8 teeth in *both* build configs and must enforce the no-**syscall** half structurally (no syscall sentinel exists in the tree).
- **D2 — Control/audio thread separation.** Heartbeat-stale + pacing-drift detection (cases 5 & 6) must run on the control thread and never mutate audio-thread state; the audio thread's only failure mode is underrun→silence (ADR §3.3, §6).
- **D3 — PR4 wireability.** The observable mechanism for cases 5 & 6 must be the exact thing PR4's `/sys/warning shm_producer_stale` / `shm_producer_pacing` emitter reads, so PR4 is additive.

### Build reality discovered (load-bearing for D1) — (iter-2 M2)
`build_off/CMakeCache.txt`: `SPATIAL_ENGINE_RT_ASSERTS:BOOL=OFF`. Therefore in `build_off`:
- `p1_rt_no_alloc` is **not registered** (CMake guards it behind `if(SPATIAL_ENGINE_RT_ASSERTS)` at `core/tests/core_unit/CMakeLists.txt:106`).
- `SPE_RT_NO_ALLOC_SCOPE()` expands to `do {} while(0)` (`RtAssertNoAlloc.h:56`), AND — the load-bearing fact — the global `operator new`/`delete` override that actually *records* violations is `#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS`-compiled-out of the translation unit (`RtAssertNoAlloc.cpp:8`: "In Release builds this TU is not compiled"). So in `build_off`, `RtAssertNoAllocScope` increments a per-thread depth counter that **nothing reads**, and `rt_alloc_violations()` is therefore **inert/vacuously 0**. There is no honest way to "self-arm the sentinel regardless of build flag" — without the override TU there is no probe.
- **Consequence (corrected):** in `build_off` the ONLY meaningful alloc guard case 8 has is the **buffer pointer/capacity stability invariant (invariant iv)** — a realloc moves `.data()`. The `rt_alloc_violations()==0` assertion is real ONLY in a `-DSPATIAL_ENGINE_RT_ASSERTS=ON` build, where the override TU is compiled. PR2 therefore **registers the case-8 RT target behind `if(SPATIAL_ENGINE_RT_ASSERTS)`** (mirroring `core/tests/core_unit/CMakeLists.txt:106`) so a dedicated RT_ASSERTS=ON ctest run gives it real teeth — **as a registered, reproducible ctest target in PR2, not PR7, and not a manual step.** The structural invariants (ii/iii/iv) are asserted in BOTH build configs (they need no override). Detailed in Decision 2 + case 8 acceptance.

---

### Decision 1 — Warning-emission seam for ctest cases 5 & 6

ADR §8 assigns the real `/sys/warning shm_*` OSC to PR4, but cases 5 (`producer_pid_dead_emits_stale_warning_once`) and 6 (`pacing_drift_emits_warning_rate_limited`) land in PR2. PR2 must ship an *observable* mechanism the tests assert against — without wiring OSC.

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **(a) Diagnostic event counters on backend** — `staleWarningCount()`, `pacingWarningCount()` (+ `attachedNoDataWarningCount()`, `underrunWarningCount()`), incremented by the control-thread poll under the same rate-limit the ADR specifies. | Trivial to assert (`== 1` / `== N`); zero new types; matches existing `xrunCount()` style; rate-limit logic lives where PR4 needs it. | Counters are not literally the OSC payload; PR4 must read them and translate to `/sys/warning` strings (small, additive). |
| **(b) Injectable `DiagnosticSink` observer** — backend calls `sink->onWarning(code, detail)`; tests inject a recording fake; PR4 injects an OSC-emitting impl. | Cleanest seam for PR4 (PR4 = one impl class); decouples policy from transport; test fake records full payload incl. rate-limit timing. | New interface + ownership/lifetime + null-sink default to design now; over-engineered for PR2's 2 tests; risks bikeshedding the `code` enum that PR4 actually owns. |
| **(c) Raw state + rate-limit timestamps exposed; test computes** — expose `producer_heartbeat_ms()`, `producer_state()`, last-warning timestamps; test does the staleness/drift math itself. | Smallest backend surface; no warning-policy in PR2 at all. | Pushes the *policy under test* (>100 ms stale, 1/30 s rate-limit, >1-block drift, 1/5 s) into the test, so the test no longer validates the backend's behavior — it validates its own copy of the spec. Defeats the purpose of cases 5 & 6. |

**Selected: (a) Diagnostic event counters on the backend.**

**Rationale.** Cases 5 & 6 are behavioral assertions about *rate-limited warning emission* — the counter is the minimal faithful observable: it proves the backend (i) detected the condition and (ii) honored the rate-limit ("once", "rate-limited"). The detection + rate-limit logic must exist somewhere for PR4 regardless; placing it behind named counters keeps it on the control thread (D2) and lets PR4 read the same internal trigger points to emit OSC — PR4 becomes "on each counter-increment edge, also call the OSC emitter," which is additive, not rework (D3). Option (b)'s observer is the *right* shape eventually but PR4 owns the warning-code vocabulary (`shm_underrun`/`shm_producer_stale`/`shm_producer_pacing`/`shm_attached_no_data` per §6); defining that enum now in PR2 pre-commits PR4's design and is rejected as premature. Option (c) is rejected because it moves the spec-under-test into the test.

**PR4 wireability note (must hold):** the four counters map 1:1 onto §6 warning codes — `underrunWarningCount→shm_underrun`, `staleWarningCount→shm_producer_stale`, `pacingWarningCount→shm_producer_pacing`, `attachedNoDataWarningCount→shm_attached_no_data`. PR4 hooks the same control-thread edges. Document this mapping in the header so PR4 doesn't re-derive it.

---

### Decision 2 — RT polling-loop construction (no-alloc/no-syscall) — case 8

Mirror `NullBackend::thread_loop` but read from the ring instead of a fixture, with provable RT-safety.

| Option | Construction | Pros | Cons |
|---|---|---|---|
| **(a) Pre-allocated planar staging + `sleep_until` cadence** (NullBackend-identical timing). All buffers (`in_flat_buffer_`, `in_channel_pointers_`) sized in `start()`/`prepareToPlay()`; per block: `load-acquire write_idx`, copy ≤`block_size` frames per channel via index-masked `memcpy` (or fill silence on underrun), `store-release read_idx`, hand `AudioBlock` to callback, then `sleep_until(next_deadline)`. | Byte-for-byte parallels the proven `NullBackend` loop (lines 95–132); reviewers already trust this shape; `sleep_until` is the established cadence in-tree; copy is bounded + alloc-free. | `sleep_until`/`nanosleep` **is** a syscall — see resolution below. |
| **(b) Busy-spin cadence** (no sleep; spin on `steady_clock::now()` until deadline). | Zero syscall in the wait. | Burns a core; not how any existing backend behaves; ADR never asks for it; jitter/heat regressions; rejected as gratuitous. |
| **(c) Polling driven only by a `pump_synchronous`-style test hook; the real `std::thread` loop is thin** (loop = `pump_one_block()` + cadence wait). Factor the per-block body into `pump_block(AudioCallback*)` that **both** the worker thread and the deterministic test pump call. | Tests drive `pump_block` with **no thread and no sleep at all** → the no-syscall question is moot *for the test*; production loop reuses identical body so behavior is verified; matches `NullBackend::pump_synchronous` precedent exactly. | Slightly more structure than inlining; must ensure the worker-thread cadence wait stays out of `pump_block`. |

**Selected: (c) factored `pump_block()` body + (a)-style `sleep_until` cadence in the worker thread only.**

**Rationale.** The audio-thread RT body is **`pump_block(AudioCallback* cb, uint64_t hw_ts_ns)`** — **(iter-2 S1)** the hw timestamp is a PARAMETER, not read inside the body, so `pump_block` contains **zero clock reads and zero syscalls**. The worker thread supplies `hw_ts_ns` from a vDSO `steady_clock::now()` taken *before* entering `pump_block`; the deterministic test supplies a synthetic monotonic value (mirrors `NullBackend::pump_synchronous` passing `i*block_size` as `hw_timestamp_ns`, `NullBackend.cpp:151`). Body, in this exact order — **(iter-2 S1) SPSC ordering contract pinned**:
1. **single `write_idx.load(std::memory_order_acquire)`** at the very top (the acquire). No sample `memcpy` may be hoisted above this load — the acquire is the synchronizes-with edge that pairs with the producer's release store (PM2); reading samples before it is the torn-read bug.
2. `available = write_idx_snapshot - read_idx_local` (read_idx is consumer-owned; relaxed local read is fine — single consumer).
3. if `available >= block_size`: copy each channel's `block_size` frames from `base + channel_byte_offset(c, header.capacity_frames) + (idx & (header.capacity_frames - 1))*4` into the pre-allocated planar staging (handling the **capacity-wrap split** as two `memcpy`s — see Pre-mortem PM1); else `memset` silence + `xruns_.record_underrun()` (backend-private `XrunCounter`, see iter-2 S3) and DO NOT advance read_idx past write_idx.
4. **`read_idx.store(new_read_idx, std::memory_order_release)`** at the end (the release) — only after the copy completes, publishing the consumed range.
5. build `AudioBlock` (input_channels = staging pointers, output null/zeroed, num_frames = block_size, `hw_timestamp_ns = hw_ts_ns`); `SPE_RT_NO_ALLOC_SCOPE()`; call `cb->audioBlock(blk)`.

**Masking capacity is ALWAYS `header.capacity_frames` (iter-2 M1)** — never a consumer-rounded value (see revised case 7). **All allocation is in `start()`** (mirrors `NullBackend` ctor lines 20–32 + `start` lines 77–81): staging vectors + channel-pointer vector + region attach. `start()` also performs the **attach-time gate `block_size <= header.capacity_frames` (iter-2 S1)** (a block larger than the ring can never be satisfied) alongside the divisor/pow2/magic gates. `pump_block` touches only mapped pages and pre-sized vectors — **no alloc, no syscall, no clock read**. The header comment on `pump_block` documents the acquire/release pairing as a load-bearing contract.

The cadence wait (`sleep_until`) lives *only* in `thread_loop()`, outside `pump_block`. This resolves the syscall question precisely: **the deterministic ctests never start the thread and never sleep** — they call `pump_block`/`pump_synchronous` N times directly (exactly the `NullBackend::pump_synchronous` pattern, lines 134–158), so case 8 exercises the real RT body with **zero syscalls in the asserted path**. Production steady-state pacing uses `sleep_until` like every other backend in the tree (the ADR §3.1 says "polls at block_size/sample_rate cadence … like `NullBackend::thread_loop`"; NullBackend itself sleeps, so the in-tree RT contract already tolerates the cadence sleep — it is not in the per-block data path the no-syscall rule protects, and it is absent from the test path).

**Case-8 RT enforcement — honest accounting (iter-2 M2).** There is NO way to make the alloc *sentinel* fire in `build_off`: the recording override TU is `#if SPE_RT_ASSERTS`-compiled-out (`RtAssertNoAlloc.cpp:8`), so instantiating `RtAssertNoAllocScope` there only bumps a counter nothing reads — `rt_alloc_violations()` is inert/vacuously 0. The plan does not pretend otherwise. Split the enforcement explicitly:
- **Structural invariants — asserted in BOTH build configs** (they need no override): drive `pump_synchronous(cb, K=256, hw_ts_synthetic)` against a pre-filled ring and assert (ii) `read_idx` advanced by exactly `K*block_size`; (iii) `xrunCount()` unchanged across the steady run (no spurious underrun = no late-path branch taken); **(iv) the backend object's owned staging buffer `.data()` pointer and `.capacity()` are identical before and after the run** — a realloc would move them. **(iv) is the ONLY meaningful alloc guard in `build_off`.**
- **Sentinel invariant (i) `rt_alloc_violations()==0` — real ONLY under `-DSPATIAL_ENGINE_RT_ASSERTS=ON`**, where the override TU is compiled and `SPE_RT_NO_ALLOC_SCOPE()` arms a real probe inside `pump_block`. PR2 **registers the case-8 RT target behind `if(SPATIAL_ENGINE_RT_ASSERTS)`** (mirror `core/tests/core_unit/CMakeLists.txt:106`) so it is a reproducible ctest in an RT_ASSERTS=ON build (the dedicated lane noted in the verification gate). In `build_off` the RT case-8 target is simply not registered (same as `p1_rt_no_alloc` today); the structural invariants (ii/iii/iv) are exercised by the *non-RT* portion of `test_shared_ring_backend` that always runs.
- **No-syscall half:** enforced structurally — `pump_block` takes `hw_ts_ns` as a param (iter-2 S1) so its body has no clock read, and is documented + reviewed to contain only loads/stores/memcpy/memset. The tree has no syscall sentinel; this is an explicit review gate (ADR-style footer).

`xrunCount()` reads the backend-private `XrunCounter` (iter-2 S3), NOT the shared header `xrun_count`, so invariant (iii) is deterministic and immune to a concurrent producer.

---

### Decision 3 — Producer-death / pacing detection on the CONTROL thread only

Heartbeat-stale (>100 ms) and pacing-drift (>1 block period) detection must not mutate audio-thread state (ADR §3.3, §6).

| Option | Where it lives | Pros | Cons |
|---|---|---|---|
| **(a) Control-thread poll method the test drives synchronously** — `poll_diagnostics(uint64_t now_ms, uint64_t now_monotonic_ns)` reads `producer_heartbeat_ms` / `producer_state` / `producer_meta_block_pts_ns` (all load-acquire), applies the >100 ms staleness + >1-block-drift rules + rate limits, bumps the Decision-1 counters. Tests inject `now_*` and write a stale heartbeat / drifted pts into the shm header directly, then call `poll_diagnostics`. | Fully deterministic (test owns the clock — no wall-clock flake); zero new threads; reads-only of header (never writes audio-thread state); maps exactly onto the existing 10 Hz `HeartbeatPublisher`/`HeartbeatMonitor` control-thread cadence (ADR §3.3) that PR4 will call it from. | Caller must drive it (in production: the engine's existing control-thread tick — PR3/PR4 wiring). For PR2 that's fine: only the test drives it. |
| **(b) Separate diagnostic `std::thread` inside the backend** that wakes ~10 Hz and self-checks against real `steady_clock`. | Self-contained; no external driver. | Wall-clock-dependent → flaky tests (must inject fake clock anyway, which collapses back to (a)); extra thread lifecycle to manage in `start()/stop()`; duplicates the control-thread cadence the engine already owns; harder to make case 5 ("once") and case 6 ("rate-limited") deterministic. |

**Selected: (a) control-thread `poll_diagnostics(now_ms, now_ns)` with injected clock.**

**Rationale.** D2 demands strict thread separation and determinism. A pure poll method with an injected clock makes cases 5 & 6 deterministic with **zero sleeps and zero real-clock dependence**. **(iter-2 C1)** This injected-clock pattern is the established in-tree precedent: `HeartbeatPublisher` (`core/src/ipc/HeartbeatPublisher.h:3` — "Time dependency is mockable — call tick(ms) directly in tests") exposes `tick(uint64_t delta_ms)` and tests drive the clock by hand. `poll_diagnostics(now_ms, now_ns)` follows the same shape (absolute injected clock rather than delta, since staleness is `producer_heartbeat_ms` vs `now_ms`), and is the exact method PR4 will call from that same control-thread tick — PR4 just adds the OSC emit on the counter edges.

**`poll_diagnostics` signature contract (iter-2 — Critic "what's missing"):**
```
void poll_diagnostics(uint64_t now_ms, uint64_t now_ns) noexcept;
```
- **READS only** header atomics (`producer_heartbeat_ms`, `producer_state`, `producer_meta_block_pts_ns`) via load-acquire, plus its own backend-private state.
- **WRITES only** backend-private fields: the four warning counters + the previous-pts cache + last-warning-timestamp-per-code (for the rate limits).
- **NEVER** advances `read_idx`, **NEVER** touches the audio staging buffers, **NEVER** writes any header atomic. Documented as a hard signature contract in the header so PR4's wiring cannot regress it. This satisfies "audio thread continues to underrun-silently — no audio thread state change" (ADR §3.3).

Option (b)'s self-clocking thread is rejected: it forces a fake-clock injection for testability anyway (collapsing to (a)) while adding thread-lifecycle risk.

---

## §0.5 — Deliberate-mode escalation rationale

This PR is escalated to DELIBERATE because it sits at the intersection of the two highest-risk surfaces in the codebase: (1) the **RT audio thread**, where a single hidden allocation or syscall silently violates Principle 1 and is undetectable in the default `build_off` config (RT_ASSERTS=OFF); and (2) **cross-process lock-free shared-memory atomics**, where an incorrect memory-ordering or capacity-wrap split produces *torn reads* that pass casual tests but corrupt audio under load. A torn read or a missed release fence does not crash — it degrades audibly and intermittently, exactly the failure class soak tests exist to catch but unit tests usually miss. The pre-mortem (below) and expanded test plan are therefore mandatory, not optional.

---

## Per-case acceptance criteria (cases 1–8)

### attach()/start() contract — RESOLVED toward Critic option (b) (iter-2 C2)
**CRITICAL fix.** `SharedRingBackend::attach(path, mode, expected)` returns `std::unique_ptr<SharedRingBackend>` (per ADR §3.1) and therefore **cannot return a `BackendError`**. Cases 1/2/7 previously asserted an impossible "`attach()` returns `BackendError`" contract. **Single resolved contract for the whole plan:**
- `attach()` performs only the OS-level mmap (via PR1 `SharedMemoryRegion::attach`). On an OS failure (region absent / mmap fail) it returns `nullptr`. It does **NOT** validate header semantics.
- **ALL header validation moves to `start(AudioCallback*)`**, which returns `BackendError` — matching the existing `NullBackend::start` gate shape (`NullBackend.cpp:73-75`). `start()` validates, in order: magic == `kSpeRingMagic` && version == `kRingHeaderVersion` → else `BackendError::DeviceOpenFailed`; `header.block_size > MAX_BLOCK(512)` → `BackendError::BlockSizeExceedsMax`; `header.block_size` does not divide engine block, OR `header.block_size > header.capacity_frames` → `BackendError::BlockConfigMismatch` (new, iter-2 M-A/S2); `header.capacity_frames` not a power of two → `BackendError::DeviceOpenFailed` (iter-2 M1, see case 7). All allocation also happens in `start()` (Decision 2). On any gate failure `start()` allocates nothing and leaves `isRunning()==false`.
- **Cases 1/2/7 assert `start()`'s `BackendError` return, not `attach()`'s** (which is just non-null for a successfully-mapped region). This closes open question PR2-Q-attach toward option (b).

All tests live in `test_shared_ring_backend.cpp`, guarded `#if defined(__linux__) || defined(__APPLE__)` with a SKIP `main()` otherwise (mirror `test_shared_memory_region.cpp` lines 26 / 136–143). Unique shm names via `"/spe-srb-" + getpid() + "-" + counter` (mirror lines 33–38). Each test creates the region itself (`AttachMode::CreateOrOpen`, `total_region_bytes(channels, capacity)`), writes a `RingHeader` (magic/version/sizes per sub-case), drives the backend, asserts, then `detach()` + `::shm_unlink()`. A small in-test `producer_write_block(header, base, channel_data, block_size, pts_ns)` helper publishes a block: copy planar samples into the ring at `(write_idx & (cap-1))`, store `producer_meta_block_pts_ns`, store-release `write_idx += block_size`, bump `seq`, update `producer_heartbeat_ms`. **Engine block passed to `start()` is ≤ MAX_BLOCK(512) in every case (iter-2 Critic "what's missing")** — concretely 256 — so case 2's divisor sub-case exercises the `BlockConfigMismatch` divisor gate, not the `BlockSizeExceedsMax` gate (which is exercised only by the explicit `header.block_size > 512` sub-case).

1. **`header_magic_and_version_validated`** — **(iter-2 C2)** `attach()` to a successfully-mapped region returns non-null even when the header is bad (it does not validate). Then: header with wrong `magic` (e.g. `0xDEAD…`) → **`start(cb) == BackendError::DeviceOpenFailed`** (the §5 fallback code), `isRunning()==false`, nothing allocated; repeat with `version != kRingHeaderVersion` → `start(cb) == BackendError::DeviceOpenFailed`. Valid magic+version (other gates passing) → `start(cb) == BackendError::Ok`, `isRunning()==true`. *Driven:* construct header bytes directly; assert `start()`'s return, not `attach()`'s.

2. **`block_size_divisor_and_max_gates`** — **(iter-2 S2/M-A/C2)** Engine block fixed at 256 (≤512). Sub-cases, each asserting `start(cb)`'s return:
   - header `block_size = 100` (does NOT divide 256) → **`BackendError::BlockConfigMismatch`** (new enumerator, distinct from `BlockSizeExceedsMax`; `describe()` → `"block_config_mismatch"`).
   - header `block_size = 64` (divides 256) and `block_size <= capacity_frames` → **`BackendError::Ok`**.
   - header `block_size = 1024` (> MAX_BLOCK 512) → **`BackendError::BlockSizeExceedsMax`** strictly (this is the ONLY gate that returns this code; iter-2 S2).
   - header `block_size = 512` but `capacity_frames = 256` (block > capacity) → **`BackendError::BlockConfigMismatch`** (iter-2 S1 attach-time `block_size <= capacity_frames` gate).
   *Driven:* vary header `block_size`/`capacity_frames` + the engine block arg to `start()`. Renamed from `block_size_must_divide_engine_block` to cover both gates explicitly while still satisfying ADR §7.1 case-2 intent.

3. **`underrun_fills_silence_and_increments_xrun`** — Attach + `start()` against a ring with `write_idx==read_idx==0` (no data). **(iter-2 S3)** `xrunCount()` reads the backend-private `XrunCounter` (NOT header `xrun_count`), so all assertions are deterministic vs any concurrent producer. Steps:
   - `pump_block(cb, hw_ts)` once → `AudioBlock.input_channels` all-zero for every channel (assert each sample `== 0.0f`), `xrunCount()` increased by exactly 1, `read_idx` NOT advanced (stays 0, never exceeds `write_idx`).
   - **(iter-2 Critic "what's missing" — 4th counter folded here):** with the ring still empty at attach, call `poll_diagnostics(now_ms, now_ns)` once → **`attachedNoDataWarningCount() == 1`** (ADR §6 "Producer not yet started → `shm_attached_no_data` once on attach"); a second `poll_diagnostics` while still `write_idx==0` → stays `== 1` (once-on-attach latch). This gives the 4th counter a real test instead of shipping it untested.
   - Then `producer_write_block(...)` writes one block; `pump_block` again → staged samples match producer data ±0, `xrunCount()` unchanged, `read_idx` advanced by exactly `block_size`.
   - **(iter-2 Critic "what's missing" — PM1 wrap sub-case, pinned):** set `read_idx`/`write_idx` so the next read straddles the capacity wrap (`(read_idx & (cap-1)) + block_size > cap`); producer fills the contiguous logical block; `pump_block` once → assert **`read_idx` advanced by exactly `block_size`**, **`xrunCount()` unchanged across the wrapped read**, and the two staged halves (pre-wrap tail + post-wrap head) **equal the producer's contiguous block ±0** (every sample). This proves the two-`memcpy` split (Decision 2 / PM1) is correct.
   *Driven:* control `available` and the wrap offset by ordering writes vs pumps and pre-seeding `read_idx`/`write_idx`.

4. **`producer_drain_state_plays_remaining_then_silence`** — **(iter-2 M-B)** Drain length is derived SOLELY from `available = write_idx - read_idx`, never from a count field — `ProducerState` is a bare enum (`RingHeader.h:31-36`) with NO embedded count. Explicit mechanism:
   - Producer writes 2 blocks → `write_idx == 2*block_size`, `read_idx == 0`; then stores `ProducerState::Draining` (enum value 2) into the header `producer_state` atomic.
   - `pump_block` ×3: block 1 (`available == 2*block_size >= block_size`) delivers buffered audio sample-exact, `read_idx → block_size`; block 2 (`available == block_size`) delivers buffered audio sample-exact, `read_idx → 2*block_size`; block 3 (`available == 0` **&&** `state == Draining`) → silence + `xrunCount()+1`, `read_idx` unchanged.
   - Producer stores `ProducerState::Closed` (enum value 3); `pump_block` → silence (`available == 0`), `read_idx` unchanged.
   - Assert the audio thread treats Draining/Closed as "play remaining frames (whatever `available` says), then silence" (ADR §2.3, §6) and **never blocks** (each `pump_block` returns immediately).
   The `Draining`/`Closed` parentheticals above are "(enum value)" annotations, not block counts. *Driven:* sequence `producer_write_block` calls + `producer_state` stores between pumps.

5. **`producer_pid_dead_emits_stale_warning_once`** — **(iter-2 C1)** Staleness predicate stated exactly: `stale := (now_ms - producer_heartbeat_ms) > 100`. Both sides of the boundary asserted with exact injected-clock values (no "≈"):
   - **Boundary low side:** `producer_heartbeat_ms = now_ms - 100` (age exactly 100 ms, NOT `> 100`) → `poll_diagnostics(now_ms, now_ns)` → **`staleWarningCount() == 0`**.
   - **Boundary high side:** `producer_heartbeat_ms = now_ms - 101` (age 101 ms) → `poll_diagnostics(now_ms, now_ns)` → **`staleWarningCount() == 1`**.
   - **Rate-limit (once / 30 s):** call again at `now_ms + 10` (still stale, within the 30 000 ms window) → **`staleWarningCount() == 1`** (suppressed).
   - **Re-arm:** call at `now_ms + 30_001` with still-stale heartbeat → **`staleWarningCount() == 2`**.
   - Assert `xrunCount()` and `read_idx` are **unchanged** by every `poll_diagnostics` call (signature contract; no audio-thread mutation).
   *Driven:* test-injected `now_ms`, direct header write; the mockable-clock pattern of `HeartbeatPublisher::tick` (iter-2 C1 citation).

6. **`pacing_drift_emits_warning_rate_limited`** — **(iter-2 C1)** Drift predicate stated exactly: `block_period_ns = 1e9 * header.block_size / header.sample_rate`; `drift := (pts_delta_ns) > block_period_ns`, where `pts_delta_ns` is the delta between successive completed-write `producer_meta_block_pts_ns` values the backend caches on the control thread. Both sides asserted:
   - **Seed baseline pts:** first `producer_meta_block_pts_ns = t0`; `poll_diagnostics` → caches `t0`, `pacingWarningCount() == 0`.
   - **Boundary low side:** next `producer_meta_block_pts_ns = t0 + block_period_ns` (delta == exactly one block period, NOT `>`) → `poll_diagnostics` → **`pacingWarningCount() == 0`**.
   - **Boundary high side:** next `producer_meta_block_pts_ns` advanced so the delta == `block_period_ns + 1` → `poll_diagnostics` → **`pacingWarningCount() == 1`**.
   - **Rate-limit (once / 5 s):** another drifted delta within the 5 000 ms window (injected `now_ns`/`now_ms`) → **`pacingWarningCount() == 1`** (suppressed); after `now` advanced > 5 000 ms with another drift → **`pacingWarningCount() == 2`**.
   - Assert `xrunCount()` and `read_idx` unchanged across all calls (no audio-thread mutation).
   *Driven:* test sets `producer_meta_block_pts_ns` + injected `now_ms`/`now_ns`; backend stores the previous pts + last-pacing-warning timestamp internally on the control thread.

7. **`ring_capacity_non_pow2_rejected_on_attach`** — **(iter-2 M1) — REWRITTEN.** The wire format mandates `capacity_frames` MUST be a power of two (ADR §2.4; §2.5 masking `idx & (cap-1)` is only correct for pow2). The consumer **NEVER** rounds the masking capacity up — masking capacity is **always** `header.capacity_frames` exactly. A consumer-side round-up to 8192 against a producer that mapped `total_region_bytes(channels, 6000)` would index past the producer's mapped region → **cross-process out-of-bounds read / SIGSEGV**. Padding to a power of two is a **producer-side obligation, OUT of PR2 scope**. Sub-cases (assert `start()`'s return):
   - header `capacity_frames = 6000` (non-pow2) → **`start(cb) == BackendError::DeviceOpenFailed`** (the §5 attach-fallback code), `isRunning()==false`, nothing allocated.
   - header `capacity_frames = 8192` (pow2) and other gates passing → **`start(cb) == BackendError::Ok`**; effective masking capacity == `header.capacity_frames` (8192) — assert via test accessor that it equals the header value verbatim, never a rounded value.
   *Driven:* construct headers with 6000 and 8192. The pow2 gate lives in `start()` (with magic/version/block gates). Renamed from `ring_capacity_padded_up_to_pow2`.

8. **`audio_thread_no_alloc_no_syscall`** — **(iter-2 M2) — honest split, no "self-arm regardless of flag" claim.** Mirror the `test_p1_rt_no_alloc.cpp` pattern.
   - **Always-on portion (registered in `build_off` and RT_ASSERTS=ON, no override needed):** pre-fill the ring with ≥K=256 blocks; capture staging buffer `.data()` ptr + `.capacity()`; drive `pump_synchronous(cb, 256, hw_ts_synthetic)`; assert **(ii)** `read_idx` advanced by exactly `256*block_size`; **(iii)** `xrunCount()` (backend-private `XrunCounter`, iter-2 S3) unchanged across the steady run (no late/underrun branch taken); **(iv)** staging buffer `.data()` ptr + `.capacity()` identical before/after (a realloc would move them — **this is the only meaningful alloc guard in `build_off`**).
   - **RT_ASSERTS=ON-only portion (registered behind `if(SPATIAL_ENGINE_RT_ASSERTS)`, mirror `CMakeLists.txt:106`):** `rt_alloc_violations_reset()`; drive the same `pump_synchronous`; assert **(i)** `rt_alloc_violations() == 0`. This is real ONLY when the override TU (`RtAssertNoAlloc.cpp`) is compiled; in `build_off` it would be inert/vacuous so it is NOT registered there.
   - **No-syscall half:** enforced structurally — `pump_block(cb, hw_ts_ns)` takes the timestamp as a param so its body has no clock read (iter-2 S1), and is documented + reviewed to contain only loads/stores/memcpy/memset. Review gate (ADR-style footer); the tree has no syscall sentinel.
   The case-8 RT target is a **registered, reproducible ctest** in an RT_ASSERTS=ON build (PR2, not PR7) — see verification gate.

---

## §Pre-mortem — 3 failure scenarios

- **PM1 — Torn read across the capacity wrap.** When `(read_idx & (cap-1)) + block_size > cap`, a single `memcpy(block_size)` reads past the channel's ring end into the next channel's region (or off the end). **Detection (iter-2):** the pinned wrap sub-case in **case 3** — straddle the wrap and assert `read_idx` advances by exactly `block_size`, `xrunCount()` unchanged, and the two halves equal the producer's contiguous block ±0. **Mitigation:** `pump_block` computes `first = min(block_size, cap - (idx & (cap-1)))` using `cap = header.capacity_frames` (always; iter-2 M1) and does up to two `memcpy`s (head at offset, tail at channel base) — both bounded, alloc-free; documented inline.

- **PM2 — Producer advances `write_idx` without a release fence (Python/ARM).** ADR §4.2 admits the Python producer lacks store-release on ARM (uses `sched_yield` workaround). If the consumer's `load-acquire write_idx` observes the new index before the sample stores are visible, it copies stale/garbage samples — no crash, intermittent audible corruption. **Detection:** PR2 cannot test the Python side; instead the C++ consumer is written to be *correct given a correct producer* (load-acquire write_idx pairs with the producer's release). Document the producer-side fence requirement as a hard contract in the header comment and flag it as a PR5/soak (PR6) verification item (sample-exact loopback, ±0 tolerance, ADR §7.2). **Mitigation:** strictly `memory_order_acquire` on `write_idx` read and `memory_order_release` on `read_idx` write in `pump_block`; never read samples before the acquire load of `write_idx`.

- **PM3 — `sleep_until` cadence jitter causes false underruns.** If the worker thread wakes late (scheduler jitter), `available` may momentarily be `< block_size` even though the producer is healthy, spuriously bumping `xrun_count`. **Detection:** this is exactly why case 8 asserts `xrunCount()` unchanged over a *pre-filled* steady run (no cadence in the test path → no jitter), isolating the bug to production pacing. **Mitigation:** the steady-state contract is "back-block tolerance" (ADR §6: "Producer late by < 1 block → plays back-block, no warning"); the worker keeps ≥1 block of slack by reading only when `available >= block_size`. Soak (PR6) is the real jitter gate; PR2 keeps the test path sleep-free.

---

## §Expanded test plan

| Layer | What | Where (PR) | Assertion |
|---|---|---|---|
| **Unit** | cases 1–8 above (case 8 always-on portion incl.) | PR2 (this) | ctest `shared_ring_backend` exits 0; per-case asserts as specified |
| **Unit (wrap)** | PM1 capacity-wrap split | PR2 (pinned sub-case in **case 3**, iter-2) | `read_idx += block_size` exact, `xrunCount()` unchanged, two halves == contiguous block ±0 |
| **Unit (RT sentinel)** | case 8 `rt_alloc_violations()==0` | PR2, registered behind `if(SPATIAL_ENGINE_RT_ASSERTS)` (iter-2 M2) | real ONLY in RT_ASSERTS=ON build; structural invariants (ii/iii/iv) also asserted in `build_off` |
| **Integration** | resync-on-attach (#9), multi-attach-reject (#10), `--input-backend shm:` CLI pairing with NullBackend output | **PR3** | deferred — out of scope here |
| **Observability** | counters → real `/sys/warning shm_*` + `/sys/state` | **PR4** | deferred — counters from Decision 1 are the hook |
| **E2E / soak** | 60 s 48 k 8-ch sine Python→shm→NullBackend, sample-exact ±0, `xrun_count==0`, no warnings | **PR6** (ADR §7.2) | deferred; PM2 producer-fence verified here |

---

## Verification gate

- Build: `cd /home/seung/mmhoa/spatial_engine/build_off && cmake --build . -j$(nproc) && ctest --output-on-failure`
- **Baseline:** `build_off` currently registers **122 tests, all pass.** Known flake: `vst3_bind_collision` only under `-j` (port-9100 contention); passes isolated — not introduced by PR2, do not "fix."
- **Target after PR2 (iter-2):** in `build_off` (RT_ASSERTS=OFF), **130 tests** — 122 prior + cases 1–7 (each a registered ctest) + case 8's always-on structural-invariant target (8 new targets total; the case-8 RT *sentinel* portion is NOT registered in build_off, see below). All 122 prior remain green; the 8 new pass. (If a case is split into multiple ctest targets the absolute count rises accordingly — the invariant is "122 prior all green + every new case green," not a magic number.)
- **RT_ASSERTS=ON lane (iter-2 M2 — in PR2, not PR7):** a `-DSPATIAL_ENGINE_RT_ASSERTS=ON` configure registers the case-8 RT-sentinel target (behind `if(SPATIAL_ENGINE_RT_ASSERTS)`, mirror `core/tests/core_unit/CMakeLists.txt:106`); `rt_alloc_violations()==0` is meaningful only there. Case 8 must pass in BOTH configs (structural invariants in build_off; sentinel + invariants in RT_ASSERTS=ON). This is a registered, reproducible ctest target — not a manual step.
- No hardware: input from the in-test producer writing the ring; output path unused (input-only backend; NullBackend pairing is PR3).
- POSIX-only: new test guarded `#if defined(__linux__) || defined(__APPLE__)` with a SKIP `main()` (mirror `test_shared_memory_region.cpp`).
- Python: `python3 -m pytest` (226 collected) — unchanged by PR2; run to confirm no regressions.
- Commit policy (`.claude/CLAUDE.md`): commit only when CI-clean; **do NOT tag**.

---

## §ADR-style footer

- **Decision.** Ship `SharedRingBackend` as an input-only `AudioBackend` over PR1 primitives, with: (1) named diagnostic counters (`staleWarningCount`/`pacingWarningCount`/`underrunWarningCount`/`attachedNoDataWarningCount`) as the cases-5/6 observable; (2) a factored `pump_block()` RT body reused by both the worker `thread_loop` (with `sleep_until` cadence) and a `pump_synchronous` test hook; (3) a control-thread `poll_diagnostics(now_ms, now_ns)` with injected clock for stale/pacing detection.
- **Drivers.** D1 RT-safety provability under RT_ASSERTS=OFF; D2 strict control/audio thread separation; D3 PR4 must wire OSC additively.
- **Alternatives considered.** Decision 1: `DiagnosticSink` observer (rejected — pre-commits PR4's warning-code enum); raw-state-only (rejected — moves spec into test). Decision 2: busy-spin (rejected — gratuitous). Decision 3: self-clocking diagnostic thread (rejected — forces fake-clock anyway + thread-lifecycle risk).
- **Why chosen.** Minimal faithful observables; the test path is sleep-/thread-/syscall-free yet exercises the real RT body; PR4 is purely additive (hook OSC on counter edges via the same `poll_diagnostics` it already calls).
- **Consequences.** PR2 owns the detection + rate-limit *policy* (stale `> 100` ms, 1/30 s; pacing-drift `> block_period_ns`, 1/5 s); PR4 owns *transport*. The no-syscall contract for `pump_block` (which takes `hw_ts_ns` as a param so its body has no clock read, iter-2 S1) is enforced by review — explicit review gate, no syscall sentinel in tree. **`capacity_frames` MUST be pow2; non-pow2 is REJECTED at `start()` (iter-2 M1) — the consumer NEVER rounds, masking capacity is always `header.capacity_frames`; producer-side padding is OUT of PR2 scope.** New enumerator `BackendError::BlockConfigMismatch` (iter-2 M-A) is distinct from `BlockSizeExceedsMax` (latter strictly for `block_size > 512`). `attach()` returns `unique_ptr` (non-null on mmap success, no header validation); ALL header validation is in `start()` returning `BackendError` (iter-2 C2). The case-8 alloc sentinel is meaningful only in RT_ASSERTS=ON (iter-2 M2).
- **Follow-ups (PR3+).** PR3: cases 9–10 (resync `read_idx = write_idx - block_size` on attach; reject 2nd consumer for SPSC invariant) + `--input-backend shm:` CLI in `spatial_engine_core.cpp`. PR4: real `/sys/warning shm_*` + `/sys/state` on counter edges. PR5: `dreamscape/adm_player/ipc_sink.py` (verify PM2 producer release fence). PR6: 60 s sample-exact soak. PR7: cross-platform CI (Linux ARM; macOS gap).

---

## Execution checklist for autopilot (TDD order)

For each case, commit footer: `ADR 0019 PR2 / case N` (CI-clean, no tag).

0. **(iter-2 M-A) — enum first, BEFORE case 2.** Add `BackendError::BlockConfigMismatch` to `core/src/audio_io/AudioBackend.h` and the matching `case ...: return "block_config_mismatch";` to the exhaustive `describe()` switch in `core/src/audio_io/AudioBackend.cpp` (no `default:` → omitting the case is a `-Werror=switch` build break). Build to confirm green before touching the backend.
1. Add `SharedRingBackend.h` skeleton: `attach()` returning `std::unique_ptr<SharedRingBackend>` (mmap only, no header validation — iter-2 C2); `start(AudioCallback*)` returning `BackendError` performing ALL gates (magic/version → `DeviceOpenFailed`; `block_size > 512` → `BlockSizeExceedsMax`; non-divisor or `block_size > capacity_frames` → `BlockConfigMismatch`; non-pow2 `capacity_frames` → `DeviceOpenFailed`) and all allocation; the 4 counters (`stale`/`pacing`/`underrun`/`attachedNoData`WarningCount); backend-private `XrunCounter` for `xrunCount()` (iter-2 S3); `producer_heartbeat_ms()`/`producer_state()` accessors; `poll_diagnostics(now_ms, now_ns)` with the documented signature contract (reads header atomics + private state; writes only private counters/caches; never `read_idx`/staging); `pump_block(AudioCallback*, uint64_t hw_ts_ns)` + `pump_synchronous(cb, blocks, hw_ts_base)` test hooks (iter-2 S1); test accessors for staging `.data()`/`.capacity()` (case 8) and effective masking capacity (case 7). Add `SharedRingBackend.cpp` to `core/CMakeLists.txt` SPE_CORE_SOURCES guarded `$<$<BOOL:${UNIX}>:...>` (mirror line 141). Register `test_shared_ring_backend` in `core/tests/core_unit/CMakeLists.txt` (link `spe_core`); register the case-8 RT-sentinel target behind `if(SPATIAL_ENGINE_RT_ASSERTS)` (iter-2 M2, mirror line 106).
2. **Write failing test → implement → pass → commit**, one case at a time, in order: 1 (magic/version, via `start()`) → 2 (`block_size_divisor_and_max_gates`) → 7 (`ring_capacity_non_pow2_rejected_on_attach`) → 3 (underrun silence + attachedNoData + PM1 wrap) → 4 (drain/closed) → 5 (stale warning) → 6 (pacing warning) → 8 (RT invariants + sentinel). (Ordering: attach/config gates first, then data path, then diagnostics, then RT contract last so the loop body is final.)
3. After each case: `cd build_off && cmake --build . -j$(nproc) && ctest --output-on-failure` — keep all prior green.
4. Final: confirm the 8 new cases pass in `build_off` (≥130 targets, all 122 prior green) + run `python3 -m pytest` (226) + a separate `-DSPATIAL_ENGINE_RT_ASSERTS=ON` configure-build-ctest so the case-8 RT sentinel (`rt_alloc_violations()==0`) is exercised with real teeth (iter-2 M2).

---

## Open questions (persist to `.omc/plans/open-questions.md`)
- Test-only accessors for case 8 (iv) buffer-pointer-stability and case 7 effective-capacity: expose `#ifndef NDEBUG`-style test hooks vs `friend` test? Decide at implementation; prefer minimal public test accessors mirroring `NullBackend`'s `set_input_fixture`/`pump_synchronous` precedent. (PR2-Q5)
- **(iter-2 M1) RESOLVED — ADR §2.4 padding semantics for case 7:** consumer-side round-up is **REJECTED as memory-unsafe** (would read past the producer's `total_region_bytes(channels, 6000)` mapping → cross-process SIGSEGV). Non-pow2 `capacity_frames` is rejected at `start()`. The "padded to 8192 … logs once" behavior in §2.4 is a **producer-side** obligation, out of PR2 scope. (PR2-Q4)
- **(iter-2 C2) RESOLVED — attach()/start() contract:** resolved toward option (b): `attach()` returns `unique_ptr` (mmap only, non-null on success, no header validation); ALL header validation in `start()` → `BackendError`. Cases 1/2/7 assert `start()`'s return. (PR2-Q-attach, recorded in open-questions.md)
