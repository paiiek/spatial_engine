# Latency Budget — Spatial Engine v0

## Stage Decomposition

The end-to-end latency from a Qt drag event to audible Dante output is split into two measurement groups:

- **Stage 1** (P12): Qt event-loop tail — from drag gesture to OSC packet send. Measured separately in P12 with Qt instrumentation.
- **Stages 2–6** (P10, this document): OSC packet receive at the native core through Dante PCIe write. Measured by the `run_latency.py` harness.

## Budget Table

| Stage | Description | Budget |
|-------|-------------|--------|
| 1 | Qt event-loop tail (drag → OSC send) | <1 ms |
| 2 | OSC network (localhost UDP) | <0.1 ms |
| 3 | Command FIFO crossing | <0.1 ms |
| 4 | Audio thread block processing | <1.33 ms (64fr/48kHz) |
| 5 | JACK/PipeWire buffer | <2 ms |
| 6 | Dante PCIe DMA | <0.5 ms |
| **Total** | | **<5 ms p99** |

## Instrumentation Pins (C3)

| Pin | Location | Clock |
|-----|----------|-------|
| **T0** | `juce::OSCReceiver` callback entry in native core; recorded per received packet and passed back to harness via `/sys/metrics` field `t0_us` | Microsecond monotonic (`std::chrono::steady_clock`) |
| **T1** | `DanteBackend::audioDeviceIOCallbackWithContext` just before kernel hand-off to ALSA period buffer; recorded per block with sample index | Same monotonic clock, hardware-timestamp probe |

The harness injects `/obj/{id}/pos` jumps at known wall-clock T0 (`time.perf_counter_ns()`), then collects `/sys/metrics` replies containing `t0_us` and `t1_us`. Delta `T1 − T0` represents the full stages 2–6 pipeline.

## BinauralMonitor Note (A2)

The binaural rendering path (BinauralMonitor) uses partitioned convolution HRTFs, which adds tail latency from block overlap. The expected p99 for the binaural path is **<10 ms** (relaxed from the 5 ms spatial output target). This is acceptable because binaural is a headphone-monitoring path, not a live loudspeaker output path.

## PREEMPT_RT Note (A3)

Per architectural decision A3 (P0), PREEMPT_RT is the default target kernel for the lab measurement system. Expected results:

- **With PREEMPT_RT**: p99 stages 2–6 target <5 ms; JACK/PipeWire jitter dominated by audio period (1.33 ms at 64fr/48kHz).
- **Without PREEMPT_RT (commodity kernel)**: p99 may degrade to 10–20 ms due to scheduler jitter and non-deterministic wake latency. Commodity-kernel results are recorded in `baseline.json` under `"preempt_rt": false` and are informational only — they do not constitute a pass against the 5 ms exit criterion.

The `baseline.json` `"pass"` field reflects whether `p99_us < threshold_us` (default 5000 µs). A commodity-kernel run is expected to report `"pass": false`.
