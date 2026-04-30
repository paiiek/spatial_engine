# spatial_engine

Real-time, object-based immersive audio rendering engine for performance / exhibition use.
C++ + JUCE native audio core (Process A) + PySide6 UI (Process B), OSC/UDP loopback IPC.

## Status

`v0` — under construction. Plan: `.omc/plans/spatial-engine-v0.md` (R2, READY-FOR-CONSENSUS).

## Foundational stack (locked by spec v2.1)

- **OS**: Linux (Ubuntu 22.04 LTS lab baseline) + PREEMPT_RT kernel (default disposition; commodity 6.x preserved as fallback).
- **Audio I/O**: Digigram ALP-Dante PCIe via PipeWire-JACK, 64 frames @ 48 kHz.
- **Native core**: C++17/20, JUCE 7.x.
- **UI**: PySide6 (Qt6) + python-osc.
- **IPC**: OSC over UDP (loopback or LAN), single `Command` schema, in-band `schema_version` u16.

## Acceptance summary (16 criteria, spec v2.1)

See `.omc/specs/deep-interview-spatial-engine-v2.md` and `.omc/plans/spatial-engine-v0.md` §Acceptance Criteria Mapping.

## Quick start

```bash
just bootstrap      # one-shot Ubuntu setup (apt + JUCE submodule + uv sync + pre-commit)
just build          # cmake build of core/
just run            # spawn core + UI for a manual smoke
just test           # unit + integration (NullBackend; CI-clean)
just sofa-inspect   # print KEMAR SOFA metadata; result feeds docs/lab_setup.md
```

## Layout

```
core/                 C++ JUCE audio core
ui/                   PySide6 UI (throw-away v0)
proto/                shared IPC schema artifacts
configs/              speaker layouts, matrix, reverb, noise gen
tests/                unit / e2e / latency / soak / accuracy / compat / perceptual
tools/                operational scripts (sofa_inspector, render_test_signals, osc_debug_console)
docs/                 architecture, IPC schema, lab setup, ADRs
```

## License

GPL v3 for v0 (JUCE 7.x is GPL-licensed). Commercial redistribution requires a JUCE Indie/Pro
license — see `docs/license_procurement_plan.md` for the trigger event and procurement plan (C5).
PRs must be GPL-compatible.
