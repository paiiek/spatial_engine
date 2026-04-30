# ADR 0002 — Native audio core language: C++ + JUCE 7.x (reaffirmation)

- **Status**: Accepted (P0 first-draft; finalized at P5)
- **Date**: 2026-04-28
- **Supersedes**: archived `v1-archive/` Rust + cpal stack (invalidated by spec v2.1 R12)

## Context

Spec v2.1 R12 locks the native core to JUCE. The vendor-delta scope (R7–R10) demands mature
DSP primitives, Dante PCIe Linux audio I/O, KEMAR HRTF convolution, and a future-VST3 plugin
path. Spec is fresh-frozen 2026-04-28; UI is throw-away; audio core is preserved into v1+.

## Decision

The native audio core is **C++17/20** built on **JUCE 7.x** as a git submodule under
`core/JUCE/`. `juce::dsp` modules (Biquad/IIR, Convolution, SmoothedValue, AudioBuffer,
ProcessorChain) are used where applicable. License: GPL v3 for v0 (lab/research); commercial
license required at trigger T per ADR `docs/license_procurement_plan.md` (C5).

## Drivers

1. Spec v2.1 R12 lock — JUCE explicitly named.
2. v0→v1 transition cost — the audio core survives, so the production-grade choice must
   land in v0.
3. Latency budget — `juce::dsp` provides pre-tested partitioned-convolution-capable
   building blocks for BinauralMonitor (A2).

## Alternatives considered

### A — Rust + `cpal` + custom DSP + `nih-plug` (the archived v1 plan)

- **Steelman**: memory safety in real-time path; modern toolchain; `cpal` callback model
  + `crossbeam` SPSC matches Principle 1 by construction; reproducible cargo builds.
- **Why rejected**: spec v2.1 R12 explicitly locks JUCE; `nih-plug` is the only practical
  Rust path for v1+ VST3Control plugin and remains pre-1.0; JUCE has 20-year track record;
  `juce::dsp` accelerates the spec's vendor-delta scope; collaborator pool skews
  JUCE-fluent.

### B — C + PortAudio (raw)

- **Why rejected**: re-invents wheels; no DSP primitives; no VST3 path.

### C — Zig + raw audio APIs

- **Why rejected**: ecosystem too young for production lab use.

## Why chosen

Spec R12 lock + `juce::dsp` accelerator for the spec v2.1 vendor-delta scope.

## Consequences

- ✓ Pre-tested DSP primitives.
- ✓ Production-grade audio framework with 20+ year track record.
- ✓ Clean v1+ VST3Control path via `juce::audio_plugin_client_VST3`.
- − GPL v0 license requires commercial license at trigger T per C5.
- − C++ exposes RT path to UB risks Rust forecloses. Mitigation: `RT_ASSERT_NO_ALLOC` macro
  + `juce::ScopedNoDenormals` + clang-tidy rule set
  (`cppcoreguidelines-pro-type-*`, `bugprone-*`, `performance-*`).

## Falsifier

At P11, profile audio thread with `perf` for 60 s under 8-object full-chain load. If
`juce::*` frames dominate inclusive >50% AND the dominant function is not user-overrideable,
file `re-evaluate-juce-framework` issue.

## Follow-ups

- Pin JUCE submodule at P0 (target tag: 7.0.12).
- Track JUCE commercial license budget per C5 trigger.
- `bootstrap.sh` ensures `apt` deps for JUCE Linux build (libasound2-dev, libjack-jackd2-dev,
  libfreetype-dev, libfontconfig1-dev, libx11-dev, libxcomposite-dev, libxcursor-dev,
  libxext-dev, libxinerama-dev, libxrandr-dev, libxrender-dev, libwebkit2gtk-4.1-dev,
  libgtk-3-dev).
