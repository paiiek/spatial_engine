# Architecture — spatial_engine v0

## Two-Process Model (ADR 0001)

`spatial_engine` splits into two OS processes to isolate the audio real-time thread from
all UI jank, GIL pauses, and PySide6 event-loop latency.

| Process | Language | Role |
|---------|----------|------|
| **Process A — Core** | C++17, JUCE 7.x | Audio I/O, DSP, rendering, OSC server |
| **Process B — UI** | Python 3.12, PySide6 | Top-down view, object panels, OSC client |

Neither process shares memory. All state crosses via OSC/UDP (see IPC section).

## Audio Thread RT Discipline

The JUCE `processBlock()` callback runs on the audio thread. Rules (non-negotiable):

- **Alloc-free**: no `new`, `delete`, `malloc`, `free`, STL containers with heap growth.
- **Lock-free**: no mutexes. State crosses via `LockFreeFloatFifo` (P5 SPSC ring).
- **Syscall-free**: no file I/O, no `printf`, no socket calls on the audio thread.
- **Log-free**: logging is posted to a background thread queue; audio thread only writes
  to the SPSC ring.

Violations are caught by the `AudioThreadSanitizer` (P3) which instruments a flag set
on audio-thread entry and asserts in CI on any heap allocation during `processBlock`.

## IPC: OSC/UDP Loopback (ADR 0003)

```
Process B (UI)  ──cmd OSC──►  port 9100  ──►  Process A (Core)
Process B (UI)  ◄─state OSC──  port 9101  ◄──  Process A (Core)
```

- Transport: UDP unicast, loopback (`127.0.0.1`) or LAN (configurable).
- Schema versioning: every packet carries `schema_version` (u16). On mismatch, core
  sends `/sys/warning` and UI shows an error dialog; audio does not start.
- Startup handshake: UI sends `/sys/protocol_version ,i 1`; core echoes same; UI unblocks.
- Full command and state broadcast tables: see `docs/ipc_schema.md`.

## Per-Object DSP Chain

```
OSC /obj/{id}/pos
       │
       ▼
  ObjectState (lock-free update from OSC thread → audio thread via SPSC ring)
       │
       ▼
  PerObjectChain
   ├─ GainProcessor
   ├─ RenderingAlgorithm  ◄── selected per object (VBAP / WFS / DBAP)
   │       │
   │       ▼
   │   SpeakerArray  (N output channels, layout from YAML config)
   └─ (crossfade buffer during algorithm swap — ADR 0006)
       │
       ▼
  FDN Reverb (shared tail)
       │
       ▼
  BinauralMonitor (side-chain, optional, stereo headphone out)
       │
       ▼
  Dante PCIe / PipeWire-JACK output buffer
```

## FDN Reverb (ADR 0004)

16-line Feedback Delay Network with Hadamard mixing matrix.

- Matrix: normalized 16×16 Hadamard (lossless energy exchange between delay lines).
- Denormal guard: `+1e-25f` DC offset injected at FDN input to prevent denormal floats
  on idle channels (verified by `test_p7_fdn_denormal_guard`).
- IR metadata validation: before any IR convolution path, metadata (sample rate, length,
  channel count) is validated against the active config; mismatch gates audio start.

## BinauralMonitor

Simultaneous binaural side-output for headphone monitoring without affecting speaker feeds.

- HRTF: KEMAR SOFA dataset. SOFA az convention is LEFT=+az; engine pipeline is RIGHT=+az.
  Negation happens inside `pipeline_to_ambix()` in `Coords.h` (see `docs/coordinate_convention.md`).
- Convolution: partitioned overlap-save for low and stable latency.
- Known v0 behavior: binaural path latency asterisked at <10 ms (acceptance criterion A2);
  exact value depends on partition size and SOFA IR length.

## LayoutCompatibilityChecker

Before audio start, validates `(layout, algorithm)` pairs:

- VBAP requires ≥ 3 non-coplanar speakers.
- WFS requires linear/planar array with known inter-speaker spacing.
- DBAP has no minimum requirement but warns if speaker count < 4.
- On failure: emits `/sys/warning` with `type=layout_incompatible`; audio start blocked.

## Algorithm-Swap Crossfade (ADR 0005 / ADR 0006)

Runtime algorithm swap (e.g., VBAP → DBAP) uses a 256-sample crossfade window to prevent
clicks. The swap is dispatched via the D1 message path and uses D3-friendly SoA (Structure
of Arrays) scratch buffers to keep the crossfade alloc-free.

Sequence:
1. UI sends `/obj/{id}/algorithm ,iis 1 <seq> <algo>`.
2. Core enqueues swap into SPSC ring.
3. Audio thread on next `processBlock`: starts 256-sample linear crossfade old→new.
4. After crossfade completes, old algorithm instance is recycled to object pool.

## Known v0 Constraints

| Item | Detail |
|------|--------|
| Binaural latency | <10 ms target (asterisked at A2); exact measure in P12 latency harness |
| Algorithm-swap crossfade | 256-sample window (~5.3 ms @ 48 kHz) |
| Max objects | 64 simultaneous (object pool pre-allocated at startup) |
| Speaker layouts | `configs/lab_*.yaml`; max 64 channels (ALP-Dante PCIe limit) |
| No-JUCE build | `cmake -DSPATIAL_ENGINE_NO_JUCE=ON` builds stub core; CI-clean |

## ADR Index

| ADR | Decision |
|-----|---------|
| 0001 | Two-process model (JUCE core + PySide6 UI) |
| 0002 | Native C++ core with JUCE |
| 0003 | IPC via OSC/UDP |
| 0004 | FDN topology (16-line Hadamard) |
| 0005 | Algorithm dispatch architecture |
| 0006 | Runtime algorithm swap with crossfade |

Full text: `docs/adr/`.
