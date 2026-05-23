# ADR 0019 — Phase C PCM IPC: sample-accurate shared-memory ring backend

- **Status**: Proposed (2026-05-19)
- **Driver**: M4 in `adm_player/.omc/plans/adm_player_integration.md` — eliminate OS-device routing dependency between adm_player and spatial_engine, achieve sample-accurate sync. Today (Phase A+B) audio crosses the boundary via JACK / BlackHole / ASIO Link, which is correct-but-not-sample-locked and adds per-host setup pain.
- **Related**: ADR 0002 (native core C++/JUCE), ADR 0006 (ADM-OSC v1.0), ADR 0018 (Phase B sync handlers, this PR), ADR 0013 (WebGUI producer-mode toggle).

---

## 1. Context

### 1.1 Current state

`core/src/audio_io/AudioBackend.h` already abstracts the device cleanly. Two impls today:

- `NullBackend` — CI / no audio
- `DanteBackend` — JUCE → JACK → Dante PCIe (the production path)

`adm_player` (Python) plays a BWF via `sounddevice`, drops OSC at 9100, and trusts the OS audio router to land the multichannel signal at the engine's input device. For demos this is **fine**; for sample-accurate distance-of-arrival or LTC-locked content it is **not**.

### 1.2 What "Phase C" needs to provide

1. A new `AudioBackend` impl — call it `SharedRingBackend` — that pulls PCM from a producer (adm_player or any future sidecar) via a lock-free shared-memory ring.
2. A wire format the producer can hit from Python without writing C extension.
3. Sample-accurate alignment between OSC object positions and the PCM block they belong to.
4. A degradation path: producer crashes → engine continues to render silence on those channels (no audio thread stall).
5. Multi-OS — Linux POSIX shm at minimum for v0.8; macOS / Windows are stretch.

### 1.3 Why a ring buffer, not sockets

Unix domain sockets + recvmmsg() were the obvious alternative. We picked shared memory because:

- **Audio thread cannot do syscalls** (RT contract — see `D-M2 vDSO probe` in v0.6.x). A read() syscall per block is exactly what we forbid.
- A ring + a per-side atomic head/tail index requires zero syscalls in the steady state. The engine reads, the producer writes; both touch only mapped memory.
- The producer (Python) syscalls when it writes — that's fine; the Python side is not RT.
- Cross-process atomic semantics on x86-64 / ARM64 with mapped pages are well-defined. We don't need POSIX shm semantics for *correctness*, only for *namespace* — `mmap(MAP_SHARED)` over a file descriptor that both processes inherit/open is the actual primitive.

---

## 2. Wire format

### 2.1 Topology

**SPSC** (single producer, single consumer). One ring per session. Multiple sessions = multiple rings, identified by `--sink ipc://<path>` URI on the player and `--input-backend shm:<path>` on the engine.

We deliberately reject MPSC for v0.8. Multi-producer mixing belongs in the engine (it already has 64 object slots); a single producer per ring keeps the lock-free invariants trivial and lets the producer side stay in CPython without writing atomics carefully.

### 2.2 Memory layout

```
+----------------------------------------+ 0x0000
|  HEADER  (4096 bytes, page-aligned)    |
+----------------------------------------+ 0x1000
|  CHANNEL 0 ring  (CAPACITY * f32)      |
+----------------------------------------+
|  CHANNEL 1 ring  (CAPACITY * f32)      |
+----------------------------------------+
|  ...                                   |
+----------------------------------------+
|  CHANNEL N-1 ring (CAPACITY * f32)     |
+----------------------------------------+
```

Channels are **planar, not interleaved**. The engine's `AudioCallback` already expects planar `float**`; this avoids a per-block deinterleave.

### 2.3 HEADER fields (4 KiB)

All multi-byte fields are little-endian. All atomic fields are `_Atomic uint64_t` on the C side, written/read as 64-bit aligned. The Python producer maps the same offsets via `multiprocessing.shared_memory` + `struct`.

| Offset | Field                          | Type        | Producer  | Consumer  | Purpose |
|--------|--------------------------------|-------------|-----------|-----------|---------|
| 0x0000 | `magic = 0x53504543484D4E47`  | u64         | r/o       | r/o       | "SPECHMNG" — sanity check on attach. |
| 0x0008 | `version`                      | u32         | r/o       | r/o       | Wire format version; **1** at first ship. |
| 0x000C | `header_size`                  | u32         | r/o       | r/o       | 4096; lets future versions extend. |
| 0x0010 | `sample_rate`                  | u32         | r/o       | r/o       | 48000 typical; engine validates. |
| 0x0014 | `block_size`                   | u32         | r/o       | r/o       | Producer's write granularity. Must divide engine's block. Typical 64/128/256. |
| 0x0018 | `channels`                     | u32         | r/o       | r/o       | Per-ring channel count (1..64). |
| 0x001C | `capacity_frames`              | u32         | r/o       | r/o       | Frames per channel ring (power of 2). Typical 8192 = ~170 ms @ 48 k. |
| 0x0020 | `write_idx`                    | atomic u64  | rw        | r/o       | Total frames written since session start (monotonic, no wrap). |
| 0x0028 | `read_idx`                     | atomic u64  | r/o       | rw        | Total frames consumed since session start. |
| 0x0030 | `producer_pid`                 | u32         | rw (init) | r/o       | For diagnostics; engine may check `kill(pid, 0)` periodically to detect death (control thread only). |
| 0x0034 | `producer_heartbeat_ms`        | atomic u64  | rw        | r/o       | Wall-clock ms of last successful write. Stale > 100 ms = producer died. |
| 0x003C | `xrun_count`                   | atomic u64  | rw        | rw        | Both sides increment on their own xrun (under/overrun). For telemetry only. |
| 0x0044 | `producer_meta_block_pts_ns`   | atomic u64  | rw        | r/o       | Monotonic ns timestamp of the **first sample** in the most recently completed write. Lets the engine align with OSC `start_unix_seconds` (ADR 0018 D-2). |
| 0x004C | `producer_state`               | atomic u32  | rw        | r/o       | 0=idle 1=streaming 2=draining 3=closed. Engine treats 2/3 as "play remaining frames, then silence." |
| 0x0050 | `seq`                          | atomic u64  | rw        | r/o       | Incremented every successful block write. Used by tests + soak harness to verify no drops. |
| 0x0058 | _reserved (zero-init)_         | bytes       | rw (zero-init only) | rw (attach-lock at 0x0058) | Padding to 0x1000. Future v2 fields land here. **First 4 bytes = consumer-attach-lock (PR3).**[^consumer-lock] |

[^consumer-lock]: **Consumer-attach-lock (PR3, ADR 0019 Decision 2).** The producer zero-inits the entire `_reserved` span (it already does), which leaves the lock word = 0 = "unlocked"; PR5's Python producer needs no change. The consumer CAS-locks the first 4 bytes (`kConsumerLockOffset = 0x0058`, an `atomic<u32>`) `0 → getpid()` in `start()` (SPSC single-consumer enforcement, with `kill(pid,0)`-ESRCH stale reclaim) and stores `→ 0` in `stop()`. This is the ONLY consumer header write beyond `read_idx`, is control-thread-only, and is never touched on the RT path. PR7 (Windows) would need `OpenProcess`-based liveness in place of `kill(pid,0)`.

Indices are **64-bit monotonic** so we never reason about wrap-around at the wire level; modular arithmetic happens only at memory-access time (`idx & (capacity - 1)`).

### 2.4 Frame layout

For channel `c`, the sample at monotonic frame index `i` lives at:

```
offset = HEADER_SIZE
       + c * (capacity_frames * 4)
       + (i & (capacity_frames - 1)) * 4
```

`capacity_frames` MUST be a power of two. We pad to the next power of two if the user requests `--ring-frames 6000` — the engine logs "padded to 8192" once at start.

### 2.5 Synchronization

- Producer writes `block_size` frames per channel, then publishes by `store-release` on `write_idx += block_size`.
- Consumer reads `block_size` frames per channel after `load-acquire` on `write_idx`, then publishes `read_idx += block_size` with `store-release`.
- The engine **never blocks**. If `available = write_idx - read_idx < block_size`, the engine fills its callback buffers with **silence**, increments `xrun_count`, and emits one `/sys/warning shm_underrun "<channels=…>"` rate-limited to once per second (ADR 0017 outbound channel).
- Producer **may block** (Python side, fine) when the ring is full. Recommended Python behaviour: drop the oldest block and increment `xrun_count` — see §4.

---

## 3. AudioBackend integration

### 3.1 New impl: `SharedRingBackend`

```cpp
// core/src/audio_io/SharedRingBackend.h
class SharedRingBackend final : public AudioBackend {
public:
    static std::unique_ptr<SharedRingBackend> attach(
        const std::string& path,         // "/tmp/spe-session-1.shm" or POSIX shm name "/spe-1"
        AttachMode mode,                 // Open vs CreateOrOpen
        OptionalMeta expected);          // sr/block/channels override; validated against header

    // AudioBackend
    int          outputChannelCount() const noexcept override;  // 0 — this is an INPUT-only backend
    int          inputChannelCount()  const noexcept override;  // = header.channels
    double       sampleRate()         const noexcept override;  // = header.sample_rate
    int          blockSize()          const noexcept override;  // = engine's, validated against header.block_size
    std::string  description()        const override;

    BackendError start(AudioCallback*) override;
    BackendError stop() noexcept override;
    bool         isRunning() const noexcept override;
    unsigned long long xrunCount() const noexcept override;

    // Diagnostics (control-thread only, never audio):
    uint64_t producer_heartbeat_ms() const noexcept;
    uint32_t producer_state()        const noexcept;
};
```

**Key constraints**:
- `SharedRingBackend` is **input-only**. The engine still uses `DanteBackend` (or NullBackend in CI) for outputs. A single session is "shm in → Dante out", just like a normal mixer.
- The audio callback inside `SharedRingBackend::start` runs the same way `NullBackend::thread_loop` does — a high-priority `std::thread` that polls the ring at `block_size / sample_rate` cadence, hands the buffer to the `AudioCallback`, and never allocates.

### 3.2 Engine wiring

`SpatialEngine::SpatialEngine` already accepts an `AudioBackend*`. We add a CLI flag in `core/src/bin/spatial_engine_core.cpp`:

```
--input-backend shm:/tmp/spe-session-1.shm
--input-backend null
--input-backend dante     (default)
```

`--input-backend shm` constructs a `SharedRingBackend` for the input and pairs it with the default `DanteBackend` for the output. The two backends share the same `sample_rate`/`block_size`; the engine refuses to start on mismatch (the same gate `BlockSizeExceedsMax` already enforces).

For tests we expose a `NullBackend`-paired-with-shm-input variant: input PCM comes from the ring, output is null. This lets the soak harness verify drop-free streaming without a real audio device.

### 3.3 Audio thread invariants

- No allocations.
- No syscalls — only loads/stores into the mapped pages.
- `producer_heartbeat_ms`/`producer_state` reads happen on the **control thread** only (`HeartbeatPublisher` already runs there at 10 Hz). If the heartbeat goes stale > 100 ms, control thread emits `/sys/warning shm_producer_stale` and the audio thread continues to underrun-silently — no audio thread state change.
- Producer crash mid-block: the ring's write_idx is not advanced (the producer publishes write_idx after the write completes), so the consumer sees no partial frames. Worst case: consumer reads up to the last fully-published write, then underruns. Acceptable.

---

## 4. adm_player sidecar mode

### 4.1 CLI surface

```
adm_player demo.wav --sink ipc:///tmp/spe-session-1.shm
                    [--ring-frames 8192]
                    [--block-size 256]
```

`--sink ipc://<path>` switches the audio backend from `sounddevice` to a new `IpcRingSink` class. The OSC emitter path (`/adm/obj/N/aed`, etc.) is unchanged — it still hits 9100 over UDP. Phase B sync (`/sys/handshake`, `/hb/ping`, `/transport/play`) is unchanged.

### 4.2 Producer-side implementation outline

- Use `multiprocessing.shared_memory.SharedMemory(create=True, name='spe-session-1', size=4096 + channels*capacity*4)` for POSIX (Linux/macOS).
- For Windows, the same API maps to `CreateFileMappingW` — verified in CPython 3.12+.
- Use `struct.pack_into('<Q', buf, OFFSET_WRITE_IDX, new_idx)` for the atomic stores. Python doesn't have store-release semantics, but on x86-64 / ARM64 a 64-bit aligned write of a u64 is atomic at the hardware level. For ARM, the missing release fence is real — we add a `os.sched_yield()` after the write as a workaround, then revisit if we find drops in soak (almost certainly we won't, given the producer is a Python audio loop running at 256-frame cadence).
- Producer xrun policy: if the ring has `< block_size` free slots, **drop the new block** (overwrite the oldest) and `+= xrun_count`. The engine still reads valid data; the artefact is one block of stale audio. This matches PortAudio's drop-newest behaviour and is preferable to blocking the Python audio loop.

A reference implementation lives in `dreamscape/adm_player/ipc_sink.py` (~150 LoC) — `class IpcRingSink` with `write(block: np.ndarray[float32])` matching the existing `sounddevice` sink contract.

### 4.3 Time alignment with OSC

When the player writes a block at producer-side ns `t0` (CLOCK_MONOTONIC), it stamps `producer_meta_block_pts_ns = t0` **before** publishing `write_idx`. The engine, when it reads that block, knows: "this audio's first sample is anchored at producer-monotonic-ns t0".

To convert producer-monotonic-ns to engine-monotonic-ns we'd need a clock-sync handshake, **which we don't need at v0.8**. The engine uses the value only to detect **gaps** in the producer's pacing — if successive `producer_meta_block_pts_ns` deltas drift by > 1 block-period, the engine logs it via `/sys/warning shm_producer_pacing` (once per 5 s).

For real sample-accurate sync (LTC-locked, fade-in at exactly N samples), the engine's block boundary IS the alignment point — OSC commands queued for the same block all take effect on the first sample of that block (Phase A behaviour, unchanged).

---

## 5. OS abstraction

| OS              | Primitive                                            | Status in v0.8 |
|-----------------|------------------------------------------------------|-----------------|
| Linux (POSIX)   | `shm_open` + `ftruncate` + `mmap(MAP_SHARED)`        | **Required**    |
| Linux (memfd)   | `memfd_create` + `mmap(MAP_SHARED)` via inherited FD | Optional (sandbox-friendly future) |
| macOS           | `shm_open` (POSIX) + `mmap`                          | Stretch (v0.9)  |
| Windows         | `CreateFileMappingW` + `MapViewOfFile`               | Stretch (v0.9)  |

Engine side: a thin `core/src/audio_io/shm/SharedMemoryRegion.{h,cpp}` that hides the OS, defaulting to POSIX. CMake feature-detects `SPE_HAVE_POSIX_SHM` at configure; if absent (Windows MSVC), `SharedRingBackend::attach` returns `BackendError::DeviceOpenFailed` and the engine falls back to `DanteBackend`.

Python side: `multiprocessing.shared_memory` already abstracts these three OSes, which is why we picked it.

---

## 6. Failure modes & telemetry

| Scenario                                  | Audio path           | Telemetry                          |
|-------------------------------------------|----------------------|-------------------------------------|
| Producer not yet started                  | Silence              | `/sys/warning shm_attached_no_data` once on attach |
| Producer late by < 1 block                | Plays back-block     | `xrun_count += 1`, no warning      |
| Producer late by > 1 block                | Silence              | `/sys/warning shm_underrun` (1/s)  |
| Producer crashed (heartbeat stale > 100 ms) | Silence            | `/sys/warning shm_producer_stale` (1/30 s) + `/sys/state.shm_producer_alive=0` |
| Producer state == draining                | Plays then silence   | `/sys/state.shm_producer_state=2`  |
| Producer state == closed                  | Silence              | `/sys/state.shm_producer_state=3`; engine releases ring on stop |
| Ring overrun (producer-side full)         | Producer drops block | producer-side `xrun_count += 1`    |
| Header magic mismatch                     | Refuse to attach     | error log, exit nonzero            |
| `sample_rate` / `channels` mismatch       | Refuse to attach     | error log, exit nonzero            |
| `block_size` not a divisor of engine's    | Refuse to attach     | error log, exit nonzero            |

All warnings land on the existing `/sys/warning ,iis` channel (ADR 0017 telemetry rules — bounded outbound, peer-validated).

---

## 7. Tests

### 7.1 Unit (ctest)

`core/tests/core_unit/test_shared_ring_backend.cpp`:

1. `header_magic_and_version_validated`
2. `block_size_must_divide_engine_block`
3. `underrun_fills_silence_and_increments_xrun`
4. `producer_drain_state_plays_remaining_then_silence`
5. `producer_pid_dead_emits_stale_warning_once`
6. `pacing_drift_emits_warning_rate_limited`
7. `ring_capacity_padded_up_to_pow2`
8. `audio_thread_no_alloc_no_syscall` — extend the existing RT contract test
9. `attach_to_pre_existing_ring_resyncs_read_idx_to_write_idx_minus_block`
10. `multi_attach_rejected` — second consumer attach should refuse (SPSC invariant)

### 7.2 Soak (tests/soak_harness)

`test_phase_c_shm_loopback.py`:

- Python producer writes a 60 s 48-kHz 8-channel sine over shm.
- Engine renders to a `NullBackend` output that captures frames to a wav.
- Compare in: sample-accurate (every frame matches, ±0 tolerance — there is no DSP in this path).
- Assert `xrun_count == 0`, no `/sys/warning` for the full run.

### 7.3 Cross-platform (.github/workflows/cross-platform.yml)

- Linux x86-64 + ARM64: full ctest + soak.
- macOS arm64: ctest-only (no Dante, NullBackend output).
- Windows: skip in v0.8 (stretch).

---

## 8. Implementation steps

1. **PR1 — core wire format**: `SharedMemoryRegion.{h,cpp}` (POSIX), `RingHeader.h` (POD layout), header-only unit tests for offsets/alignment.
2. **PR2 — backend**: `SharedRingBackend.{h,cpp}` + ctest cases 1–8.
3. **PR3 — engine wiring**: `--input-backend shm:` CLI, backend-pairing in `spatial_engine_core.cpp`, integration ctest #9, #10.
4. **PR4 — telemetry**: `/sys/warning` codes (`shm_underrun`, `shm_producer_stale`, `shm_producer_pacing`, `shm_attached_no_data`), `/sys/state` additions.
5. **PR5 — adm_player sidecar**: `dreamscape/adm_player/ipc_sink.py` + `--sink ipc://…` CLI + pytest round-trip on a 1-second buffer.
6. **PR6 — soak harness**: §7.2.
7. **PR7 — cross-platform CI**: enable on Linux ARM; document macOS gap.

Estimated total: ~600 LoC engine, ~200 LoC player, ~400 LoC tests, ~50 LoC CI YAML. Three weeks for a single contributor; two if PR5 happens in parallel.

---

## 9. Open questions (NOT blocking this ADR)

- **Q1**: Should the engine offer a **second** ring direction (engine → player monitor) so the player can VU-meter the engine's output? Today the player doesn't need it — Phase A keeps the audio fully on the player side until SHM is in. Likely yes in M5 (recorder integration, ADR 0020).
- **Q2**: Do we want `mlock`-pinned pages on the ring? RT-best-practice says yes; the cost is a few KB of resident RAM. Defer until first observed jitter.
- **Q3**: How does a VST3 plugin host (which runs `SpatialEngineProcessor` in its own process) attach to a player's shm? It can't, by design — the VST3 host owns audio I/O. The shm path is specific to "standalone engine + sidecar player". If we ever want plug-in style "player → DAW VST3 → engine", we'd reuse this ring inside the VST3 process, but VST3 already gives us sample-accurate I/O via the host's buffer, so the question is moot.
- **Q4**: What's the contract if the producer sends `block_size = 64` and the engine's audio block is `256`? The engine accumulates 4 producer blocks per audio block. The first 3 increment `read_idx`; the 4th completes a callback. Drift between producer pacing (4 × 64 = 256 frames every audio period) and engine pacing is bounded by the producer's xrun policy.

---

## 10. Non-goals

- **Sub-block scheduling of OSC commands** — the alignment point is the engine audio block. We do NOT make OSC time-tags sample-accurate within a block. (D-2 in ADR 0018 already locked this.)
- **Multi-producer mixing** — see §2.1.
- **Encrypted/authenticated rings** — same-host, file-permission-controlled. Users who need cross-host want a network protocol, not shm. (PR3 note: the engine's `--input-backend shm:<path>` regular-file branch opens an operator-supplied path with `O_RDWR` and follows symlinks; this is explicitly inside the same-host operator trust boundary — the path comes from the operator's own argv, ring content is never disclosed, and write-back happens only after the header magic/geometry gates pass.)
- **Engine-as-producer / player-as-consumer** — that's M5 (recorder pickup), ADR 0020.
