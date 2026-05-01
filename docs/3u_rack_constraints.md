# 3U Rack Constraints

Design constraints for deploying the spatial engine in a 3U rack-mount compute unit
(no GPU, PREEMPT_RT kernel, ECC RAM, fanless or low-noise cooling).

---

## CPU-Only

The spatial engine is CPU-only DSP. There are **no runtime dependencies** on CUDA, Metal,
Vulkan, OpenCL, or any GPU/NPU compute framework. This is intentional:

- 3U rack hardware typically ships without discrete GPUs
- GPU driver stacks introduce non-deterministic preemption latency
- CPU SIMD (AVX2/NEON) provides sufficient throughput for 64-ch Ambisonic decode at 48 kHz

Any future ML-based processing (e.g. room classification) must remain on a **separate
inference host** connected via the IPC channel — never loaded into the audio process.

---

## Low-Noise Real-Time Operation

During real-time audio operation the engine **must not**:

- Spawn new processes or threads (no `fork`, `exec`, `std::thread` construction)
- Open or close file descriptors on the audio thread
- Perform blocking I/O of any kind on the audio thread
- Load plugins or shared libraries dynamically after `prepareToPlay`
- Display a GUI on the audio host in v1+ deployments

The UI process (if present) runs on a **separate host or separate process** connected via
OSC over loopback UDP. GUI code must never be linked into the audio process binary.

---

## ECC-Friendly Memory

To maximise the benefit of ECC RAM and to avoid memory-subsystem noise:

- **No growing containers in steady state** — `std::vector`, `std::deque`, and similar
  containers are sized once before `prepareToPlay` and never resized during audio callbacks
- **Pre-allocated SoA buffers** — all per-block working memory is allocated as
  Structure-of-Arrays at initialisation time (see `core/src/`)
- **No scattered allocations after `prepareToPlay`** — heap fragmentation is eliminated;
  the allocator is not called from the audio thread
- Memory pools for OSC packet queues are ring-allocated; no dynamic growth

---

## Memory Anchors (Steady-State RSS Targets)

| Process | Steady-State RSS | Max Slope |
|---------|-----------------|-----------|
| Core audio engine | ~80 MB | < 1 MB/h |
| UI / control surface | ~150 MB | < 5 MB/h |

These values are validated by the 12-hour soak harness (`tests/soak_harness/run_soak.py`).
A slope exceeding the limit triggers a gate failure in `soak_report.json`.

---

## PREEMPT_RT Kernel

`PREEMPT_RT` (or equivalent fully-preemptible kernel patch) is **required** per ADR A3,
P0 thread pinning:

- Audio thread runs at `SCHED_FIFO` priority 80 pinned to isolated core(s)
- Interrupt affinity for the audio interface is set away from the audio core
- `mlockall(MCL_CURRENT | MCL_FUTURE)` is called at startup to prevent page faults

A commodity `PREEMPT_VOLUNTARY` kernel is supported as a **fallback** for development
environments only. Expect occasional p99 spikes > 933 µs on non-RT kernels — this is
acceptable in development but must not occur in production soak runs.

---

## Per-Block Time Budget

| Parameter | Value |
|-----------|-------|
| Block size | 64 frames |
| Sample rate | 48 000 Hz |
| Block period | 1 333 µs |
| Safety factor | 0.70 |
| **p99 gate threshold** | **933 µs** |

Derivation:

```
threshold_us = floor(block_size / sample_rate * safety_factor * 1e6)
             = floor(64 / 48000 * 0.7 * 1e6)
             = floor(933.333...)
             = 933 µs
```

The p99 gate is enforced by the soak harness and exposed via `/sys/metrics` as
`per_block_time_p99_us`. Breaching 933 µs causes `gates.per_block_p99_ok = false`
and `pass = false` in the soak report.

---

## Heartbeat Watchdog

The control thread publishes a heartbeat at **10 Hz** on `/sys/heartbeat`.
The watchdog monitors the gap between consecutive publishes:

| Parameter | Value |
|-----------|-------|
| Publish rate | 10 Hz (every 100 ms) |
| Miss threshold | 300 ms (3 consecutive misses) |
| Miss event | `/sys/heartbeat_miss` OSC message |
| Gate counter | `heartbeat_miss_count` in `/sys/metrics` |

A single miss event causes `gates.heartbeat_miss_zero = false` and `pass = false`
in the soak report. The 300 ms threshold was chosen per constraint C6 to detect
a stalled control thread before the audio watchdog (500 ms) fires.
