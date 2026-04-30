# ADR 0004 — FDN topology: 16-line Hadamard FDN with per-line tone control + frequency-dependent T60

- **Status**: Accepted (P0 first-draft; finalized at P7)
- **Date**: 2026-04-28
- **Related**: spec acceptance #4 + #5; Pre-mortem A (FDN denormal CPU spike)

## Context

Spec acceptance #4 demands FDN-based algorithmic reverb with **per-object send modulation**
and audibly increasing distance-dependent reverb amount. Spec acceptance #5 demands a
`ReverbEngine` interface that allows **future IR convolution drop-in** without bus / send /
routing code changes.

## Decision

`FDNReverb` implements a **16-line Feedback Delay Network** with:

- **16 × 16 Hadamard mixing matrix** (4 cascaded butterfly stages; branchless; SIMD-friendly).
- **16 mutually-prime delay-line lengths** at 48 kHz, e.g.:
  `{1499, 1693, 1801, 1933, 2069, 2207, 2347, 2477, 2647, 2789, 2917, 3079, 3203, 3361, 3491, 3613}` samples.
  Tunable from `configs/reverb_fdn.yaml`.
- **Per-line one-pole LPF** for HF damping → frequency-dependent T60.
- **Per-line gain** `g_i = 10^(-3 × m_i / (T60 × fs))` for global RT60 control.
- **Denormal handling (Pre-mortem A)**:
  - `juce::ScopedNoDenormals` on every audio-callback entry (FTZ + DAZ).
  - Per-line ±1e-20 DC offset injection to keep state away from denormal range.

## Drivers

1. Latency budget — FDN must fit per-block compute (≤500 µs at P11 gate).
2. Principle 1 — allocation-free, branchless, lock-free.
3. Spec acceptance #4 + #5.

## Alternatives considered

### A — 8-line FDN with Householder + shared global tone control

- **Pros**: half the delay memory; ~2× fewer FLOPs.
- **Why rejected**: sparse modal density at lab RT60 (0.2–2.0 s); tail flutter risk on
  transient sources.

### B — Configurable {8, 16}

- **Pros**: optionality; falsifiable in lab listening.
- **Why rejected**: doubles test surface; tone-control coefficients need separate tuning per N.

### C — Schroeder (4 series allpass + 4 parallel comb)

- **Why rejected**: metallic coloration on dense input; not state-of-the-art for v0.

### D — Moorer (early reflections + comb + allpass)

- **Why rejected**: requires geometry-aware early-reflection design; v1+ enhancement.

## Why chosen

16 lines + Hadamard is the modern algorithmic-reverb floor (Jot 1991 / Schroeder-derived).
SIMD-friendly Hadamard butterfly; mutually-prime delay set maximizes echo density; 4 v1+
hooks remain unchanged when swapping to `IRConvolution` reverb at P7+ / v1+.

## Consequences

- ✓ Dense modal coverage at lab RT60.
- ✓ Branchless Hadamard fits SIMD.
- ✓ 4 v1+ hooks (block-process, getLatencySamples, optional `SupportsIRLoading`,
  parameter-ID table) preserved.
- − ~192 KB delay memory in core RSS (16 × ~3,000 samples × 4 bytes).
- − Denormal hazard requires explicit FTZ/DAZ + DC offset injection; soak gated.

## Falsifier

Not applicable as a budget falsifier; perceptual at P12. If listeners report "metallic" /
"ringy" after the v0 sign-off listening test, re-tune the mutually-prime set and per-line
tone control before doubling line count.

## Follow-ups

- Ship `configs/reverb_fdn.yaml` at P7.
- P7 unit test fits Schroeder energy decay to verify configured RT60 ±10%.
- P11 idle-with-tails soak gates p99 per-block time < `block_size × 0.7 = 933 µs`
  (A1/C2 corrected; derivation: `64 / 48000 × 0.7 = 933 µs`).
