# spatial_engine

Real-time, object-based immersive audio rendering engine for performance / exhibition use.
C++ + JUCE native audio core (Process A) + PySide6 UI (Process B), OSC/UDP loopback IPC.

## Status

**v0.1.0** — 기준 태그 완료 (commit `19679c6`). v1 피처 진행 중.

| 단계 | 내용 | 상태 |
|------|------|------|
| v0 (P0–P12) | C++ 코어, PySide6 UI, OSC IPC, VBAP/WFS/DBAP, FDN Reverb, Latency/Soak 하네스 | ✅ 완료 |
| v1-F1 | ADM-OSC 수신 네임스페이스 (`/adm/obj/n/*`) | ✅ 완료 |
| v1-F2 | 스냅샷/씬 시스템 + `/scene/save\|load\|list` OSC | ✅ 완료 |
| v1-F3 | MAX_OBJECTS 16→64 | ✅ 완료 |
| v1-F4 | IR SOFA 로더 확장 | ✅ 완료 |
| v1-F5 | Elevation UI 슬라이더 (-90..+90°) | ✅ 완료 |
| v1-F6 | VBAP 3D elevation 수치 테스트 | ✅ 완료 (2D-only 한계 명시) |
| v1-F7 | MIDI PC 스냅샷 리콜 브리지 | ✅ 완료 |
| 하드웨어 실측 | P10 레이턴시, P11 소크, P12 퍼셉추얼 | ⏳ 하드웨어 도착 대기 |

CI: **30/30 ctest** | **71 passed 1 skipped pytest** (하드웨어 불필요 기준)

자세한 내용: [`docs/v0.1.0_report.md`](docs/v0.1.0_report.md)

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
