# Plan — v0.9 Lane G / ADR 0019 PR6: 60 s cross-process C++↔Python shm **drop-free streaming + lifecycle + leak soak**

> **REV2 reframe (Architect ACCEPT-WITH-MOD)**: PR6 is an empirical **drop-free streaming + lifecycle + leak soak on x86-64**, NOT a memory-ordering proof. On x86-64 TSO the gentle wall-clock-paced synthetic producer cannot exercise the acquire/release race on `write_idx` (a plain aligned u64 store is already release-ordered, the load already acquire-ordered) — a passing soak is by construction indistinguishable from a no-op *for the ordering question*. What PR6 genuinely proves: two real processes stream drop-free over real `/dev/shm` with a clean producer lifecycle and no leak. The **ARM64 weak-memory ordering proof** (where `os.sched_yield()` ≠ release fence) is the genuinely hard case and is **explicitly PR7**.
>
> **REV2 changelog (Architect)**: (1) wire-readback mechanism changed from `multiprocessing.shared_memory` → `os.open`+`mmap` read-only [BLOCKING — resource_tracker unlinks producer's live region]; (2) anchor paths corrected `core/src/core/` → `core/src/audio_io/` (+`/shm/`) [BLOCKING fact-error]; (3) deliberate reframe away from "memory-ordering proof" [BLOCKING overclaim]; (4) `read_idx→write_idx` elevated to PRIMARY consumer-side witness, `seq` demoted to producer-write witness [SHOULD]; (5) payload-content ramp assertion added [SHOULD]; (6) engine-outlives-producer slack made explicit [SHOULD].
>
> **REV3 changelog (Critic ITERATE)**: (7) assertion 3c reframed from "consumer torn-read detector" → **producer-side ramp-integrity witness** — the harness reads only the producer's ring (`--backend null`, private `staging_flat_`), so a consumer torn read is OUT of PR6 scope (→PR7 consumer-side exposure path) [MAJOR overclaim]; (8) **snapshot-vs-unlink race** fixed — the read-only mapping is acquired right after the readiness gate and HELD across the producer's `unlink()` (POSIX mapping survives unlink), so the teardown read can't `FileNotFoundError` [MAJOR bug]; (9) assertion 5 de-flaked — smoke `drain_dwell_s ≥ 2.0` + slack ≥ 3 s; smoke gates only "reaches 3", the `2→3` sequence is full-mode-only [MAJOR flake]; (10) AC8 grep scope narrowed to the orchestrator/reader (producer's `IpcRingSink` region-creation exempt) [MINOR]; (11) §9 sentinel CLI flags verified present (`--threshold-fd`, `--threshold-mb-h`) [MINOR].

- **Status**: APPROVED (ralplan consensus reached 2026-06-07 — Architect ACCEPT + Critic APPROVE @ REV4; ready for autopilot)
- **Mode**: SHORT (default). Promote to DELIBERATE only if Critic flags the memory-ordering claim as high-risk.
- **Scope**: PR6 = soak harness + Linux x86-64 run. PR7 (cross-platform CI: Windows `CreateFileMappingW`, macOS POSIX, **ARM64 enablement**) is OUT.
- **Repos touched**: `spatial_engine` only (test harness + helpers). `adm_player` is a runtime dependency, not edited.
- **Driver doc**: `spatial_engine/docs/adr/0019-phase-c-pcm-ipc-shm-ring.md` §7.2 (soak), §4.2 (xrun), §2.3 (header table), §6 (telemetry).

---

## 1. Context (verified against source)

PR1–PR5 shipped the full SPSC ring: the C++ consumer (`SharedRingBackend`) and the Python producer (`IpcRingSink`). PR5 **explicitly deferred** the memory-ordering proof to PR6 — the producer's publish path comments this directly:

> `ipc_sink.py:274-276` — "Best-effort scheduler nudge — NOT a memory fence. On x86-64 (TSO) the prior sample/pts stores are visible-before the write_idx store below; ARM64 weak-memory ordering is **PR6's concurrent-soak proof** (P3)."

PR6's job is the TRUE cross-language proof: run the real two-process pipeline (C++ engine consuming `shm:<name>`, Python producer feeding it) under load for ~60 s and assert **drop-free** streaming, racing a C++ `load-acquire` on `write_idx` against the Python `store-release` (a plain aligned 64-bit store + `os.sched_yield()`) under real scheduling.

### Verified anchors

| Fact | Evidence (file:line) |
|------|----------------------|
| Producer publishes `write_idx` LAST, after samples + pts + `sched_yield` | `adm_player/dreamscape/adm_player/ipc_sink.py:271-285` |
| Producer drop-newest xrun policy (drop INCOMING block, bump `xrun_count`, never advance `write_idx`) | `ipc_sink.py:247-253` (matches ADR §4.2 / line 190) |
| `seq` bumped every successful block write (drop-free witness) | `ipc_sink.py:283-284`; ADR header table `seq`@0x0050 (ADR line 85) |
| `producer_heartbeat_ms` = unix-epoch ms, 10 Hz daemon + per-write stamp | `ipc_sink.py:99-107, 215-220, 285` |
| Header fields: `write_idx`@0x0020, `xrun_count`@0x003C, `producer_heartbeat_ms`@0x0034, `producer_state`@0x004C, `seq`@0x0050 | `ipc_sink.py:59,63,64,66,67`; C++ side `core/src/audio_io/shm/RingHeader.h:86,89-93` + `static_assert`s :108,111-115 (byte-for-byte match, verified) |
| **`seq` is PRODUCER-ONLY** — the C++ consumer never reads it (grep of `core/src/audio_io/` + `ShmTelemetryEmitter.h` = ∅). `seq` witnesses producer writes, NOT consumer consumption. | `RingHeader.h:93` (offset only; no consumer read site) |
| **`driver->xrunCount()` is CONSUMER-side device underrun** (`xruns_.total()`), distinct from wire `xrun_count`@0x003C which is PRODUCER-side drop-newest. Two opposite-end counters, not a cross-check of one value. | `core/src/audio_io/SharedRingBackend.h:127,233` ("backend-private, NOT header xrun_count"); producer drop `ipc_sink.py:251-252` |
| Producer CLI: `--sink ipc://NAME` (POSIX shm name), `--block-size` (default 256), `--ring-frames` (default 8192) | `adm_player/dreamscape/adm_player/__main__.py:41-64, 112-127, 254-272` |
| `ipc://NAME` ↔ engine `--input-backend shm:/NAME` (leading slash, no second slash) | ADR §4.1 PR5-Q3 reconciliation, line 183 |
| Engine consumes via `--input-backend shm:<path>` → `SharedRingBackend::attach(...)` | `core/src/bin/spatial_engine_core.cpp:521,530-540` |
| Engine headless output: `--input-backend shm:` pairs with default output; `--backend null` for no device | `spatial_engine_core.cpp:546-561`; ADR line 160 |
| Engine runs `--seconds N` then deadline-exits (`driver->stop()`) | `spatial_engine_core.cpp:351,614,660,795` |
| **`attach` uses `OpenExisting` and reads geometry from the header** → producer MUST create the ring before the engine attaches | `core/src/audio_io/SharedRingBackend.cpp:69` (`region_.attach(path, OpenExisting, full_bytes)`); `spatial_engine_core.cpp:533-534` |
| shm telemetry tick runs 1 Hz, ONLY when `input_is_shm` | `spatial_engine_core.cpp:743-753` |
| `/sys/warning ,iis 0 0 "<code>" "<detail>"` for `shm_underrun` / `shm_producer_stale` / `shm_producer_pacing` / `shm_attached_no_data` | `core/src/bin/ShmTelemetryEmitter.h:110-175` |
| `/sys/state ,s` keys: `shm_producer_alive`, `shm_producer_state`, `shm_consumer_locked` (on-change, latched) | `ShmTelemetryEmitter.h:207-218` |
| `attached_no_data` is once-on-attach while `write_idx==read_idx==0` — emitted before the producer's first write (this is the EXPECTED case, not an anomaly: readiness gate waits for header magic, not first `write_idx` advance) | `core/src/audio_io/SharedRingBackend.cpp:514-521` |
| POSIX shm name reconciliation: producer bare-name `SharedMemory(name="NAME")`→`/dev/shm/NAME`; engine wire `shm:/NAME`→`is_posix_shm_name("/NAME")`→`shm_open("/NAME")`→same `/dev/shm/NAME` (verified Py 3.12) | `core/src/audio_io/shm/SharedMemoryRegion.cpp:32-39`; `adm_player/.../__main__.py:116` |
| Engine binary built, fresh, x86-64 | `core/build/spatial_engine_core` (Jun 2 13:40); `uname -m`=x86_64 |
| Existing soak idioms: spawn engine on free port, OSC loopback listener, drain packets, terminate | `tests/soak_harness/test_phase_b_player_handshake.py:112-150`, `test_osc_warning_channel.py:129-185` |
| RSS-slope / fd-leak sentinels (consume JSON `rss_series`/`fd_series`) | `tests/soak_harness/rss_slope.py:93-135`, `check_fd_leak.py:43-55` |

### Cross-repo dependency topology (verified, this is the one genuine wrinkle)

- The producer package's import name is **`adm_player`** (pyproject `name="adm-player"`, `packages.find where=["."] include=["adm_player*"]`), and it physically lives at **`/home/seung/mmhoa/adm_player/dreamscape/adm_player/`**.
- It is **NOT installed** in the active environment: `importlib.util.find_spec('dreamscape')` → `None`; no egg-link / site-packages entry.
- It IS runnable today by putting the package root on `PYTHONPATH`:
  - `PYTHONPATH=/home/seung/mmhoa/adm_player/dreamscape python3 -m adm_player <wav> --sink ipc://<name> --block-size 256 --ring-frames 8192`  (verified: `dreamscape.adm_player.__main__` spec resolves; `import adm_player.ipc_sink` resolves with that root on the path).
- The `--sink ipc://` end-to-end path **requires an ADM BWF input file** (`__main__.py:133-155` reads axml/chna via `soundfile`). There are **no demo wavs** in the adm_player root. This forces a topology decision (Option A vs B below).
- **NOT a blocker**: `IpcRingSink` itself imports cleanly and can be driven directly (no `soundfile`, no BWF) with a synthetic block generator. This is the recommended path (Option A) and removes the BWF-fixture and `soundfile` dependency from CI.

---

## 2. Principles (RALPLAN-DR)

1. **The proof is empirical, not formal.** On x86-64 TSO the pairing is expected clean by construction; PR6 produces the *evidence* (drop-free `seq` + zero xrun under real concurrent scheduling), it does not prove ARM64. ARM64 is named and deferred to PR7.
2. **Two real processes, real shm, real OS scheduler.** No in-process fake, no mocked ring. The whole point is to race a separate-process acquire-load against a separate-process release-store.
3. **Default CI stays fast and deterministic; the 60 s run is opt-in.** Mirror the existing harness split (`run_soak.py` `--dry-run` caps at 60 s; webgui harness has day-1/day-2 modes). Default `pytest` gets a 5–10 s smoke; the full 60 s soak is nightly/manual.
4. **Reuse existing idioms, add nothing the engine doesn't already expose.** OSC loopback capture + free-port spawn from `test_phase_b`/`test_osc_warning_channel`; sentinels `rss_slope.py`/`check_fd_leak.py`. No new engine code, no new wire surface.
5. **Drop-free is defined by the wire, cross-checked three ways.** `seq` monotonic with no gaps AND `xrun_count==0` (both sides) AND no `shm_*` `/sys/warning`. Any one alone is weaker; all three together is the pairing proof.

## 3. Decision Drivers (top 3)

1. **Drop-free streaming confidence (x86-64)** — does the C++ consumer ever drop, skip, or fall behind under real cross-process load? Witnessed by: zero producer/consumer xruns (1a/1b), `read_idx` catching up to `write_idx` (3, primary), and producer-side write integrity (`seq` no-gap 3b + ramp placement 3c). (NOTE: on x86-64 TSO this is NOT a memory-ordering proof — the acquire/release pairing is unobservable by construction; and PR6's wire-only view never reaches the consumer's reads, so a consumer torn-read is out of scope too. Both belong to ARM64/PR7. PR6's confidence is in liveness + lifecycle + leak + producer write-integrity, which is real and shippable.)
2. **CI cost & determinism** — 60 s × every push is unacceptable; a flaky soak is worse than none. The default gate must be short, hermetic, and skip cleanly when the engine binary or `adm_player` is absent (the existing harness skip-gracefully pattern).
3. **Cross-repo coupling** — the producer lives in a sibling repo and is not installed. The harness must reference it without hard-coding a brittle path, and must degrade to skip (not fail) when it cannot find it. This is also where the PR6/PR7 boundary sits.

---

## 4. Viable Options — harness topology

### Option A (RECOMMENDED): pytest-orchestrated subprocess, producer = direct `IpcRingSink` synthetic driver

A small **producer script** (`tests/soak_harness/shm_producer_sine.py`, ~60 LoC) imports `IpcRingSink` (via discovered `adm_player` path), and writes a deterministic ramp/sine at 256-frame cadence for `--duration` seconds. The pytest harness:
1. discovers `adm_player` (env var `ADM_PLAYER_ROOT` → `/home/seung/mmhoa/adm_player/dreamscape` default → `importlib.util.find_spec`), `pytest.skip` if not found;
2. spawns the producer (creates + header-inits the ring);
3. polls `/dev/shm/<name>` (via `stat` + `os.open`/`mmap` read-only, NOT `multiprocessing.shared_memory`) until it exists + header magic is set (readiness gate — **producer must precede engine** per `core/src/audio_io/SharedRingBackend.cpp:69` `OpenExisting`);
4. spawns the engine `--input-backend shm:/<name> --backend null --seconds <dur+slack> --osc-port <free>`;
5. opens an OSC loopback listener, sends `/sys/handshake ,ii 1 <reply_port>` so the engine captures the peer (telemetry is `sendReply`-based — `ShmTelemetryEmitter` needs a peer);
6. drains `/sys/warning` + `/sys/state` for the run, sampling RSS/fd of both PIDs every ~1 s into `rss_series`/`fd_series`;
7. **acquire the read-only wire mapping EARLY** (right after the readiness gate, step 3) via **`os.open('/dev/shm/'+name, O_RDONLY)` + `mmap.mmap(fd, size, prot=PROT_READ)`** and **retain it for the whole run**. The final snapshot read (`xrun_count`/`seq`/`read_idx`/`write_idx`/`producer_state` + ramp sample via `struct.unpack_from`) happens on this held mapping at teardown — **a POSIX mapping survives `shm_unlink`** (unlink removes the NAME, not the mapping; pages stay valid until `munmap`), so the read still succeeds even after the producer's `close()` has unlinked the region. Teardown order: terminate engine FIRST, then producer `close()` (drain→closed→unlink), then `struct.unpack_from` the held mapping, then `munmap`/close fd.
   - **[BLOCKING — snapshot-vs-unlink race]**: do NOT defer the `os.open` to teardown. The producer self-drains and `unlink()`s at ~`dur+1 s` (its 1.0 s+ DRAINING dwell, `ipc_sink.py:319-344`), but the engine deadline-exits at ~`dur+slack`; a teardown-time `os.open('/dev/shm/<name>')` would hit `FileNotFoundError` because the name is already gone. Mapping early + holding across unlink is the fix.
   - **[BLOCKING — do NOT use `multiprocessing.shared_memory.SharedMemory(create=False)`]**: a separate process opening the producer's region via `multiprocessing.shared_memory` triggers CPython's `resource_tracker` to `shm_unlink` the producer's STILL-LIVE region at the reader's exit (bpo-38119 / gh-82300; reproduced by Architect — producer lost its region, its own `unlink()` raised `FileNotFoundError`, and `-W error` CI breaks on the `resource_tracker` UserWarning). This applies to **both** the orchestrator's readiness gate (step 3) AND this teardown snapshot. Use raw `os.open`+`mmap` (read-only, never creates/unlinks), or `resource_tracker.unregister(...)` immediately after attach. The readiness gate likewise `stat`s `/dev/shm/<name>` + reads the magic via `mmap`, never via `multiprocessing.shared_memory`.

- **Pros**: no BWF fixture, no `soundfile` dependency, fully deterministic payload (ramp → exact `seq` gap detection), smallest CI surface, still a genuine two-process real-shm race. Reads the wire `seq`/`xrun` directly as ground truth.
- **Cons**: does not exercise the *full* `adm_player` playback/OSC path (only `IpcRingSink`). Mitigation: that full path is PR7 / covered by `test_phase_b` already; PR6's contract is the *ring*, not BWF parsing.

### Option B: pytest-orchestrated subprocess, producer = full `python -m adm_player <wav> --sink ipc://`

Same orchestration, but the producer is the real player binary driving a generated 60 s ADM BWF fixture.

- **Pros**: true full-stack producer (BWF → de-interleave → ring), closest to production.
- **Cons**: needs a committed/generated multichannel ADM BWF fixture + `soundfile`/`numpy` in CI; player loop pacing is file-length-bound (less control over duration/short-mode); BWF parse failures become soak failures unrelated to the ring. Heavier, flakier, and overlaps PR7's "real player on CI" territory.

### Option C (REJECTED): standalone shell harness (bash spawns both, greps stderr)

- **Why invalidated**: no structured OSC capture, no `seq`/`xrun` wire read, can't reuse `rss_slope.py`/`check_fd_leak.py` JSON contract, and the existing harness is already pytest+Python (`test_phase_b`, `run_soak.py`). A shell layer would duplicate the OSC encode/decode helpers that already exist in `test_osc_warning_channel.py`. No upside over A.

**Decision**: **Option A** for the PR6 gate, with a `@pytest.mark.adm_player_full` **Option B variant kept as an opt-in nightly** (skipped by default, no fixture committed in PR6 — the BWF fixture + full-player run is explicitly PR7-adjacent). This satisfies the ≥2-viable-options requirement and records why B is deferred rather than invalidated.

---

## 5. Pass / Fail assertions (the actual proof)

Read from the wire (`/dev/shm/<name>` final snapshot) AND cross-checked against OSC `/sys/*`:

| # | Assertion | Source of truth | What it proves |
|---|-----------|-----------------|----------------|
| 1a | wire `xrun_count`@0x003C `== 0` over the full run | wire `xrun_count`@0x003C (final mmap read) | No **producer-side** drop (drop-newest never fired → ring never overran). |
| 1b | `driver->xrunCount() == 0` via `/sys/metrics` | `SharedRingBackend.h:127,233` (`xruns_.total()`, consumer-side) | No **consumer-side** device underrun. **NOTE: 1a and 1b are two distinct opposite-end counters, NOT a cross-check of one value** — both must be 0 independently. |
| 2 | No `/sys/warning shm_*` emitted (none of `shm_underrun`/`shm_producer_stale`/`shm_producer_pacing`); `shm_attached_no_data` count is **exactly 0 or 1** (the once-on-attach pre-first-write case is EXPECTED, not a tolerated anomaly) | OSC drain, `ShmTelemetryEmitter.h:110-175`; `SharedRingBackend.cpp:514-521` | Consumer never went stale/under/paced — it always saw fresh published data when data existed. |
| **3 (PRIMARY drop-free witness)** | `read_idx == write_idx ± (≤ one engine block)` at drain | wire `write_idx`@0x0020 / `read_idx` (final mmap read) | **The consumer-side drop-free witness**: the consumer caught up to the producer — every block published was observed and consumed, no torn/stale `write_idx` caused a skipped or re-read block. This is the load-bearing assertion (NOT `seq`). |
| 3b (producer-write witness) | `seq` advances monotonically with **no gaps**: final `seq == total successful producer writes` | wire `seq`@0x0050 | The **producer** wrote N blocks cleanly. **Caveat: `seq` is producer-only — a wedged consumer (reads 0) still yields a perfect monotonic `seq`. `seq` alone is a FALSE-PASS for drop-free; it must be paired with assertion 3 (`read_idx→write_idx`).** |
| **3c (producer-side ramp-integrity witness)** | Every wire ramp sample `== its expected global frame index`, slot value `f = (write_idx-1) - ((write_idx-1 - slot) mod cap)` (Step 1 deterministic ramp) | wire payload region (mmap read, mapping held across producer exit) | **Witnesses that the PRODUCER wrote a correct planar ramp at correct slots** — complements `seq` no-gap (3b) by checking the de-interleave/placement, not just the write count. **HONEST SCOPE (REV3): this does NOT witness a consumer torn/stale read.** The consumer copies into private `staging_flat_` (`SharedRingBackend.cpp:404-414`) and re-publishes nothing (output is `--backend null`), so the harness only ever reads the producer's ring. A true consumer-read integrity check needs a consumer-side exposure path (debug checksum OSC emit) — that is **PR7 scope, not PR6**. On x86-64 3c cannot fail for a torn-read reason any more than 1–3b can. |
| 4 | `producer_heartbeat_ms` stayed fresh (`now_unix_ms - hb ≤ 100 ms` throughout) → `/sys/state shm_producer_alive=1`, never 0 | OSC `/sys/state`, `ShmTelemetryEmitter.h:178-218` | Producer never stalled (so a clean `xrun==0` is real streaming, not a dead producer). |
| 5 | `/sys/state shm_producer_state` reaches `2` (draining) then `3` (closed) at teardown | OSC `/sys/state` | Clean lifecycle, not a crash. **De-flake (REV3)**: the engine telemetry tick is exactly 1 Hz (`spatial_engine_core.cpp:743`); a 1.0 s DRAINING dwell sampled by a 1.0 s-period tick is phase-dependent and can miss state `2` entirely (latched on-change emit would jump 1→3). So the smoke producer sets **`drain_dwell_s ≥ 2.0`** (Step 1 CLI) and engine **slack ≥ 3 s** so ≥2 ticks land inside DRAINING. **In smoke**, the hard gate is "reaches `3` (closed)"; the `2→3` *sequence* is gated only in **full** mode (where the longer run makes the sample reliable). |
| 6 | (warn-only in smoke; gate in full) RSS slope ≤ threshold via `rss_slope.py`; fd delta ≤ 5 via `check_fd_leak.py` | JSON `rss_series`/`fd_series` | No leak across the run. |

**What PR6 actually proves (REV3 honest framing)**: assertions **1a + 1b + 2 + 3 together** = empirical **drop-free streaming** on x86-64, with **3b/3c** as producer-side write-integrity witnesses (count + placement). Assertion **3 (`read_idx→write_idx`)** is the primary consumer-side witness (the consumer caught up). A producer-side drop manifests as `xrun_count`++/`shm_underrun`; a consumer that fell behind manifests as a `read_idx`/`write_idx` divergence. **No assertion in PR6 witnesses a consumer torn/stale READ** — the consumer's reads are never exposed on the wire (`--backend null`, private `staging_flat_`). On x86-64 TSO drop-free is expected **by construction** (plain aligned u64 store = release; load = acquire), so a passing soak is **not** a memory-ordering proof (it cannot fail for the ordering reason; passing ≈ no-op for that question). PR6 records the empirical drop-free/lifecycle/leak zero. **The genuine memory-ordering proof is ARM64** (weak memory, where `os.sched_yield()` is a scheduler hint, NOT a release fence — ADR line 189) and is **explicitly assigned to PR7** (Linux ARM CI, ADR §7.3 line 266), which must also add a consumer-side read-integrity exposure path (debug checksum emit) since PR6's wire-only view cannot reach the consumer's reads. No ARM hardware is exercised here, and no x86-64 result is claimed as ordering evidence.

---

## 6. Short-mode vs full-soak split

| Mode | Duration | Where | Gate |
|------|----------|-------|------|
| **smoke** (default) | 5–8 s (`SHM_SOAK_SECONDS` env, default 6) + slack ≥3 s | every `pytest` / CI push | HARD: assertions 1a/1b/2/3/3b/3c/4 + assertion 5 "reaches `3`" only (the `2→3` *sequence* is full-mode-only — 1 Hz tick vs DRAINING dwell is phase-marginal at smoke length, §5 row 5); RSS/fd warn-only (too few samples to regress). Skips cleanly if engine binary or `adm_player` absent. Deterministic, hermetic (free ports, `/dev/shm` temp name with PID suffix, unlink on teardown). |
| **full** (`@pytest.mark.soak`, `--run-soak` or `SHM_SOAK_FULL=1`) | 60 s (default), `--duration` override | nightly / manual | HARD: assertions 1a–6 incl. the assertion-5 `2→3` sequence + RSS-slope + fd-leak. |
| **nightly full-player** (`@pytest.mark.adm_player_full`, Option B) | 60 s | nightly only, deferred | OUT of PR6 default; stub + skip with a `# PR7` note. |

Mirrors `run_soak.py:205-207` (`--dry-run` 60 s cap) and the webgui day-1/day-2 pattern.

---

## 7. Step-by-step (3–6 actionable steps)

### Step 1 — Producer driver helper
Add `tests/soak_harness/shm_producer_sine.py`: discovers `adm_player` (env `ADM_PLAYER_ROOT`, default `/home/seung/mmhoa/adm_player/dreamscape`), imports `IpcRingSink`, writes a per-channel **deterministic ramp: `sample[frame, ch] = global_frame_index`** (identical across channels, so the wire reader at any slot can assert `value == frame_index` — this is the assertion-3c **producer-side write-integrity witness**: placement/de-interleave correctness, complementing `seq` no-gap. It does NOT witness a consumer torn/stale read — see §5 row 3c) at `--block-size 256` cadence pacing to wall-clock `block/sr`, for `--duration` s, then `close()`. CLI: `--name --channels --rate --block-size --ring-frames --duration --drain-dwell-s` (passes through to `IpcRingSink(drain_dwell_s=...)`; smoke uses ≥2.0 to de-flake assertion 5 per §5 row 5). The ramp value is reconstructible from `read_idx`/`write_idx` on the reader side with **no out-of-band state**: for a wire slot `s ∈ [0,cap)`, the most-recently-written frame index at that slot is `f = (write_idx-1) - ((write_idx-1 - s) mod cap)` (the executor implements exactly this formula; verified exact for float32 up to frame ~2^24 ≈ 349 s, well beyond the 60 s full run).
- **AC**: `PYTHONPATH=<root> python3 tests/soak_harness/shm_producer_sine.py --name spe-pr6-smoke --duration 2` creates `/dev/shm/spe-pr6-smoke`, advances `seq`, exits 0, unlinks on close.

### Step 2 — The soak test
Add `tests/soak_harness/test_phase_c_shm_loopback.py` implementing Option A orchestration (§4 steps 1–7): discover/skip, spawn producer, **readiness-gate via `stat` + `os.open`/`mmap` read-only on `/dev/shm/<name>` header magic** (NOT `multiprocessing.shared_memory` — §4 step 7 BLOCKING note), spawn engine `--input-backend shm:/<name> --backend null --seconds <dur + slack>` with **slack ≥ 3 s** and smoke producer **`--drain-dwell-s ≥ 2.0`** so the engine outlives the producer's drain and ≥2 of the 1 Hz ticks (`ipc_sink.py:319-344`; `spatial_engine_core.cpp:743`) land inside DRAINING — making the `producer_state` 2→3 sequence reliably sampled in full mode (smoke gates only "reaches 3", §5 row 5), **acquire the read-only `os.open`+`mmap` wire mapping right after the readiness gate and HOLD it for the whole run** (so the final read survives the producer's `unlink()` — §4 step 7 BLOCKING note), handshake, drain `/sys/warning`+`/sys/state`, sample RSS/fd, at teardown read the final wire snapshot from the HELD mapping (`xrun_count`/`seq`/`read_idx`/`write_idx`/`producer_state` + a ramp payload sample), assert §5 1a/1b/2/3/3b/3c/4/5, teardown order (terminate engine FIRST, then producer `close()`, then read held mapping, then `munmap`).
- **AC**: with the binary built, `pytest tests/soak_harness/test_phase_c_shm_loopback.py -q` runs the 6 s smoke and PASSES; asserts 1a–5 hold incl. the ramp payload-content check (3c); produces a JSON report under `soak_reports/`; emits zero `resource_tracker` UserWarnings (passes under `-W error`).

### Step 3 — Full-soak mode + sentinels
Gate the 60 s body behind `@pytest.mark.soak` + `--run-soak`/`SHM_SOAK_FULL`; emit `rss_series`/`fd_series` JSON; wire `rss_slope.py` + `check_fd_leak.py` as the full-mode gate (assertion 6). Add the Option-B `@pytest.mark.adm_player_full` stub that `pytest.skip`s with a `# PR7` note (no BWF fixture committed).
- **AC**: `pytest -m soak --run-soak tests/soak_harness/test_phase_c_shm_loopback.py` runs 60 s, PASSES, and `python rss_slope.py --report <json> --threshold-mb-h 50` + `python check_fd_leak.py --report <json>` both exit 0.

### Step 4 — pytest marker registration + skip-graceful + docs
Register `soak`/`adm_player_full` markers (`pyproject`/`pytest.ini`); confirm clean skips when engine binary or `adm_player` missing (CI-portable, mirrors `test_phase_b`). Add a short `# PR6` note to the ADR §7.2 reference (doc-only; no ADR rewrite) and a usage block at the top of the test file (verification commands of §9).
- **AC**: in an env with no `adm_player` on the path, `pytest` reports `skipped`, not `failed`. `ctest` and `pytest` default suites stay green.

---

## 8. Guardrails

**Must have**: two real OS processes; real `/dev/shm`; producer-before-engine ordering with a readiness gate; the three-way drop-free proof (`xrun==0` + no `shm_*` warning + `seq` no-gap); clean skip when deps absent; default-CI smoke ≤ ~8 s; teardown always unlinks the shm region.
**Must NOT have**: no engine source changes; no new OSC/wire surface; no BWF fixture or `soundfile` dependency in the default gate; no ARM run (documented for PR7); no 60 s on every push; no leaked `/dev/shm/spe-pr6-*` after a run; no in-process fake ring.

---

## 9. Verification commands (headless, here)

```bash
# Build (already built; rebuild if needed)
cd /home/seung/mmhoa/spatial_engine/core/build && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && make -j"$(nproc)" spatial_engine_core

# Producer helper smoke (Step 1)
PYTHONPATH=/home/seung/mmhoa/adm_player/dreamscape \
  python3 /home/seung/mmhoa/spatial_engine/tests/soak_harness/shm_producer_sine.py \
  --name spe-pr6-smoke --channels 8 --rate 48000 --block-size 256 --ring-frames 8192 --duration 2

# Default smoke gate (Step 2) — ADM_PLAYER_ROOT lets the test discover the producer
ADM_PLAYER_ROOT=/home/seung/mmhoa/adm_player/dreamscape \
  python3 -m pytest /home/seung/mmhoa/spatial_engine/tests/soak_harness/test_phase_c_shm_loopback.py -q

# Full 60 s soak (Step 3, manual/nightly)
ADM_PLAYER_ROOT=/home/seung/mmhoa/adm_player/dreamscape SHM_SOAK_FULL=1 \
  python3 -m pytest -m soak --run-soak \
  /home/seung/mmhoa/spatial_engine/tests/soak_harness/test_phase_c_shm_loopback.py -q

# Sentinels on the full-soak report
python3 /home/seung/mmhoa/spatial_engine/tests/soak_harness/rss_slope.py --report <report>.json --threshold-mb-h 50
python3 /home/seung/mmhoa/spatial_engine/tests/soak_harness/check_fd_leak.py --report <report>.json --threshold-fd 5

# Leak hygiene check after any run
ls /dev/shm/spe-pr6-* 2>/dev/null && echo "LEAK" || echo "clean"
```

---

## 10. Testable acceptance criteria (executor exit gate)

- [ ] AC1 — `shm_producer_sine.py` creates the ring, advances `seq`, writes the deterministic ramp (`sample==frame_index`), unlinks on close (Step 1 AC).
- [ ] AC2 — default smoke `pytest` PASSES; asserts §5 1a/1b/2/3/3b/3c/4/5; writes a JSON report; zero `resource_tracker` UserWarnings (passes `-W error`).
- [ ] AC3 — wire `xrun_count==0` (1a) AND `driver->xrunCount()==0` (1b); no `shm_underrun`/`shm_producer_stale`/`shm_producer_pacing`; `shm_attached_no_data` ∈ {0,1}; `read_idx==write_idx±block` (3, PRIMARY); `seq` no-gap (3b); ramp payload `==frame_index` (3c) — over the smoke run.
- [ ] AC4 — full 60 s soak PASSES incl. `rss_slope.py`/`check_fd_leak.py` exit 0.
- [ ] AC5 — clean `pytest.skip` (not fail) when engine binary OR `adm_player` absent.
- [ ] AC6 — no `/dev/shm/spe-pr6-*` left behind after pass OR fail (and verified NOT via the resource_tracker early-unlink side effect — the producer's own `close()` does the unlink).
- [ ] AC7 — `ctest` + default `pytest` suites remain green; zero engine source edits.
- [ ] AC8 — the **orchestrator's reader + readiness-gate code path** uses `os.open`+`mmap` read-only ONLY; `multiprocessing.shared_memory` appears nowhere in the **orchestrator/reader** code. EXEMPT: the producer (`shm_producer_sine.py` via `IpcRingSink`) legitimately *creates* the region through `multiprocessing.shared_memory` (`ipc_sink.py:27,151`) — that is the producer's own per-process resource_tracker registration and is required/correct. Scope the grep to the test orchestrator + reader helpers, not the producer or imported `ipc_sink`.
- [ ] AC9 — the held read-only mapping is acquired right after the readiness gate and its final `struct.unpack_from` succeeds AFTER the engine has terminated/`munmap`ed AND the producer has `close()`/`unlink()`ed (mapping-survives-unlink; engine `munmap` does not free the object's pages), i.e. teardown wire read does NOT raise `FileNotFoundError` or SIGBUS.

---

## 11. ADR-style decision record

- **Decision**: Implement PR6 as a pytest-orchestrated two-process **drop-free streaming + lifecycle + leak soak** (Option A): a synthetic-ramp `IpcRingSink` producer + the real `spatial_engine_core --input-backend shm:/… --backend null` consumer, with a ≤8 s default smoke and a `@pytest.mark.soak` 60 s full run, all on Linux x86-64. **Not** framed as a memory-ordering proof (see Consequences). Wire snapshots and readiness gate use raw `os.open`+`mmap` read-only, never `multiprocessing.shared_memory`.
- **Drivers**: (1) cross-language memory-ordering confidence, (2) CI cost/determinism, (3) cross-repo coupling without install — see §3.
- **Alternatives considered**: Option B (full `python -m adm_player` + BWF fixture) — deferred to nightly/PR7 (needs a committed BWF + `soundfile`, file-length-bound pacing, parse-failure noise). Option C (shell harness) — invalidated (no structured OSC/wire capture, can't reuse JSON sentinels, duplicates existing Python OSC helpers).
- **Why chosen**: Option A is the smallest hermetic two-process real-shm race that yields the three-way drop-free proof, reuses every existing harness idiom, and adds zero engine code. The full producer path is already partly covered by `test_phase_b` and squarely belongs to PR7.
- **Consequences**: PR6 proves **drop-free streaming + clean producer lifecycle + no leak + producer write-integrity, empirically on x86-64 only**. It does **NOT** prove the cross-language memory-ordering pairing, nor even a consumer torn/stale read: on x86-64 TSO the gentle wall-clock-paced producer cannot exercise the acquire/release race on `write_idx` (store is already release-ordered, load already acquire-ordered), so a passing soak is by construction indistinguishable from a no-op *for the ordering question*; and the consumer's reads are never exposed on the wire (`--backend null`, private `staging_flat_`), so the harness's wire-only view cannot observe a consumer torn read. Both the ARM64 weak-memory case (`sched_yield` ≠ release fence, ADR line 189) AND a consumer-side read-integrity exposure path (debug checksum emit) are explicitly **PR7**. The assertion-3c ramp check is a **producer-side** write-integrity witness only. The default gate stays fast; the real evidence is in the opt-in 60 s run.
- **Follow-ups (→ open-questions.md)**:
  - PR7: enable this soak on Linux ARM64 CI to actually exercise the weak-memory pairing; decide whether the producer then needs a real release barrier (e.g. a tiny C-extension or `ctypes` fence) instead of `os.sched_yield()`.
  - PR7: promote the Option-B `adm_player_full` variant (generate/commit a 60 s multichannel ADM BWF fixture; add `soundfile` to the nightly env).
  - Decide whether to fold the final wire snapshot read into a reusable `tests/soak_harness/shm_wire.py` helper if other shm tests need it.

---

## RALPLAN-DR summary (for step-2 AskUserQuestion alignment)

- **Principles**: (1) empirical drop-free/lifecycle/leak evidence (NOT a memory-ordering proof on x86-64 — that's ARM64/PR7; and NOT a consumer torn-read check — wire-only view, also PR7); (2) two real processes + real shm; (3) fast deterministic default CI, opt-in 60 s; (4) reuse existing idioms, zero engine change; (5) drop-free = `read_idx→write_idx` (PRIMARY) ∧ `xrun==0` (both counters) ∧ no `shm_*` warning; `seq` no-gap (3b) + ramp placement (3c) are PRODUCER-side write-integrity witnesses (neither witnesses a consumer read; `seq` alone is a false-pass).
- **Decision Drivers (top 3)**: drop-free streaming + producer write-integrity confidence (x86-64); CI cost/determinism; cross-repo coupling (producer not installed).
- **Viable Options**: A = pytest-orchestrated subprocess with synthetic `IpcRingSink` producer (**chosen**); B = pytest-orchestrated with full `python -m adm_player` + BWF (**deferred to nightly/PR7**); C = shell harness (**invalidated**).
- **Mode**: SHORT.
- **Genuine blocker check**: NONE. `adm_player` is not installed but IS importable via `PYTHONPATH=/home/seung/mmhoa/adm_player/dreamscape` (verified). Engine binary is built (x86-64). The only real constraint is launch ordering: the **producer must create the ring before the engine attaches** (`core/src/audio_io/SharedRingBackend.cpp:69` uses `OpenExisting`) — handled by the readiness gate in §4 step 3. (REV2: the `multiprocessing.shared_memory` resource_tracker unlink hazard was the one latent bug found in Architect review — neutralized by switching the readback to `os.open`+`mmap`.)
