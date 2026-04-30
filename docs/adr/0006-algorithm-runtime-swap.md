# ADR 0006 — Algorithm runtime swap mechanism (K=256 sample crossfade)

- **Status**: Accepted (P0 first-draft; finalized at P3)
- **Date**: 2026-04-28
- **Origin**: Architect R1 must-address A4 + Critic R1 must-address C4

## Context

Spec acceptance #2 makes algorithm selection **runtime-changeable** per object via the
`/obj/{id}/algorithm` OSC command. Principle 1 forbids allocation, locks, and clicks on the
audio thread. Algorithm-specific scratch state differs (e.g., WFS's per-speaker
propagation-delay tail vs VBAP's gain table), so an instantaneous pointer swap produces a
discontinuity that is *not* just a gain step — it's a filter-state mismatch with audible
click.

## Decision

Per-object algorithm runtime change executes via:

1. **Pre-allocation** — at scene-load (or whenever a new algorithm is chosen for an object),
   the **control thread** allocates the new algorithm's scratch state from the per-algorithm
   SoA arrays (ADR 0005 / M5). Pre-allocation happens in non-realtime context.

2. **Atomic publish** — `Object` carries `std::atomic<AlgorithmId> algorithm_id`. The control
   thread sets `pending_algorithm_id` and a swap-armed flag via single-writer atomic publish.

3. **Audio-thread crossfade** — at the next block boundary, the audio thread reads the
   swap-armed flag; if armed, it begins a **K=256-sample crossfade window (~5.33 ms @ 48 kHz)**
   during which **both** the old and the new algorithm run on that object in parallel. Old
   output crossfades 1→0 via linear ramp; new output crossfades 0→1. Both are backed by
   pre-allocated scratch (Principle 1 alloc-free preserved).

4. **Cleanup** — after K samples, the audio thread atomically sets
   `algorithm_id = pending_algorithm_id` and clears the swap-armed flag. The control thread
   observes the flag clear at the next safe window and frees the old algorithm's scratch
   slot back to its SoA pool.

## Drivers

- **A4**: RT-safety vs spec-required runtime selectability — Principle 1 vs spec ontology
  `/obj/{id}/algorithm` runtime command.
- **Spec acceptance #2**: per-object selectable, runtime-changeable.
- **C4**: audio-domain falsifiable click test (FFT spectral spike check).

## Alternatives considered

### A — R1 silent-gap: ramp object gain to 0 → swap pointer → ramp back

- **Why rejected**: ~2.7 ms audible dip on the object during swap; user-visible artifact unfit
  for performance / exhibition target.

### B — No crossfade, instantaneous swap

- **Why rejected**: discontinuity at swap sample → click; algorithm-specific scratch
  differences (WFS delay-line tail VBAP doesn't have) make the discontinuity not just a
  gain step.

### C — K=64 (one-block) crossfade

- **Why rejected**: too short for some perceptually significant filter-state differences
  (e.g., the WFS delay-line tail).

### D — K=512 (eight-block) crossfade

- **Why rejected**: runs both algorithms in parallel for 10.7 ms; at 8 objects all swapping
  simultaneously, 2× p99 spike risk at the P11 gate.

## Why chosen

K=256 balances **click-freeness** (5.33 ms is enough for typical filter-state differences
to be perceptually masked) against the **per-block CPU spike** (parallel-run cost on a
single object at a single swap event is well within budget; even all-8-swap-at-once produces
a brief 8× CPU spike during one window — documented as known v0 behavior).

## Consequences

- ✓ Click-free runtime swap.
- ✓ RT-safe (audio thread reads atomic, never allocates).
- ✓ Falsifiable test (C8 FFT spectral check around the swap window).
- − 2× scratch per object slot during swap (in budget; <50 KB extra).
- − Control thread is responsible for free-after-clear (standard SPSC pattern).
- − Operator-issued "swap all 8 objects simultaneously" command produces a brief 8× CPU spike
  during the K=256 window — documented as known v0 behavior in `docs/architecture.md`.

## Falsifier

Either of:

1. A profiled allocation or vtable churn observed inside the audio thread during an algorithm
   swap under `RT_ASSERT_NO_ALLOC` instrumentation.
2. A click event during the K=256 swap window detected via `ClickDetectorFFT` spectral spike
   >10 dB above neighboring frequencies (per C8).

Either failure mode → file `algorithm-swap-mechanism-redesign` issue.

## Follow-ups

- P3 implements the swap mechanism (`AlgorithmSwapState.h`, `Object.algorithm_id` atomic).
- P3 integration test (`tests/e2e/`) asserts the C8 three-part criterion during the swap.
- Risk #13 in register tracks `algorithm_swap_click`.
