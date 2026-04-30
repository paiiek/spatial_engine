# ADR 0005 — Per-object algorithm dispatch: D1 polymorphic + D3-friendly invariants (M5 synthesis)

- **Status**: Accepted (P0 first-draft; finalized at P3)
- **Date**: 2026-04-28
- **Related**: ADR 0006 (algorithm runtime swap); spec acceptance #2

## Context

Spec ontology is per-object selectable algorithm (`Object.algorithm ∈ {VBAP, WFS, DBAP}`)
with runtime-changeable selection via `/obj/{id}/algorithm`. Three drivers compete:

- **Ergonomics**: each algorithm should own its own scratch state.
- **Forward-compat**: v1+ may want to scale to N≥32 objects, where SoA layout matters.
- **Principle 1**: allocation-free, branchless on the audio thread.

## Decision

**D1 dispatch surface + D3-friendly invariants.** Specifically:

- Each `Object` carries a `RenderingAlgorithm*` pointer (D1 dispatch surface).
- At `prepareToPlay`, **each `RenderingAlgorithm` impl pre-allocates SoA scratch arrays
  sized `MAX_OBJECTS=16`** (D3-friendly invariant per Architect Recommendation A / M5).
- `engine->getObjectsByAlgorithm(Algorithm)` returns spans (no allocation; used by debug
  tools in v0 and by D3 dispatch in v1+).
- Audio-thread loop is `for (auto& obj : objects) obj.algorithm->processBlock(...)` (D1).
- The v1+ migration to D3 is a **single-loop swap** to
  `for (auto algo : algorithms) for (auto& obj : objects_by_algo[algo]) algo.processBlock(...)`
  — no header change, no ownership-model rewrite.

## Drivers

1. **Principle 4** — `RenderingAlgorithm` is a stable abstraction.
2. **Spec ontology** — `Object.algorithm` per-object selectable.
3. **D2** — v1+ adds new algorithm impls without engine surgery.
4. **Forward-compatibility with v1+ N≥32-object scaling** (M5).

## Alternatives considered

### A — D2 (table-driven dispatch)

- **Pros**: no v-table; cache-friendly when all objects share an algorithm.
- **Why rejected**: scratch ownership becomes the engine's problem (Principle 4 violation);
  v-table cost dwarfed by FDN + LPF + delay anyway.

### B — D3 (bucket-by-algorithm) v0 directly

- **Pros**: SIMD-friendly within a bucket; minimizes branch mispredictions.
- **Why rejected for v0**: bucket bookkeeping; complexity not justified at 8 objects.
- **Migration target** with the SoA invariant pre-baked.

## Why chosen

D1 dispatch surface preserves Principle 4 ergonomics; the D3-friendly SoA invariant +
`getObjectsByAlgorithm` helper means the v1+ migration is "swap one for-loop", not "rewrite
ownership model". This is the Architect synthesis.

## Consequences

- ✓ Simple, idiomatic, encapsulated dispatch (D1).
- ✓ Each algorithm owns scratch (D1) AND scratch is SoA-laid-out for v1+ D3 migration (M5).
- ✓ ~3× over-allocation at MAX_OBJECTS=16 across all three algorithms is small (~few hundred
  KB; in budget).
- − Virtual-call overhead per object per block (~few ns × 8 = invisible at v0 scale).
- − SoA layout adds ~30 lines vs pure D1.

## Falsifier

At P11 soak, profile audio thread with `perf` for 60 s under 8-object full-chain
mixed-algorithm load (e.g., 4 VBAP + 2 WFS + 2 DBAP). If v-table dispatch + virtual-call
overhead is >5% of audio-thread CPU, file `eval-d3-bucket-dispatch` issue and re-evaluate
at v1+.

## Follow-ups

- P3 implements `RenderingAlgorithm` base + VBAP/WFS/DBAP impls + SoA scratch.
- P3 implements `engine->getObjectsByAlgorithm(Algorithm)` helper.
- At P11 record `perf` flame graph as baseline.
- Document v1+ D3 migration recipe in `docs/architecture.md`.
