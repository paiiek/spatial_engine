# PROVENANCE — Ported DSP (Dreamscape Convergence)

**Status:** authoritative provenance & rights record for all third-party-derived
DSP in `spatial_engine`. Written for commercial due-diligence ("what is ported,
from where, under what right, and how is it isolated?").

**Last updated:** 2026-06-07 · **Branch:** `feat/dreamscape-convergence`

---

## 1. Summary

The Dreamscape Convergence effort makes `spatial_engine` a **superset** of:

1. the Dreamscape spatial-audio **xlsx specification** (11 sheets, 00–11), and
2. the reference engine **`immersive-audio-engine`**
   (`github.com/dreamscapeaudio2023-star/immersive-audio-engine`, = Dreamscape
   v0.2.1, JUCE 8.0.6 single-app), via the **Path A** strategy: the reference
   DSP is **ported into the mmhoa core**, not adopted as a base.

All ported reference DSP lives in a single isolated directory,
`core/src/render/ported/`, namespaced `iae`, and is **JUCE-free** (compiles
headless under `-DSPATIAL_ENGINE_NO_JUCE=ON`). Every ported file carries an
in-source `// === PORTED ===` header naming the upstream source file and commit.

## 2. Rights basis (Decision D3, 2026-06-03)

**Porting right = HELD.** Per convergence decision **D3** (recorded in
`.omc/plans/dreamscape-convergence-master-plan.md` and project memory):

> Direct source port of the reference engine is **authorized** — the author of
> this work holds rights to the reference material. A clean-room reimplementation
> is therefore **not required**.

Consequences enforced in-tree:

- Every ported file keeps an **origin header** (upstream path + commit `f2cb796`)
  and the note `Direct port authorized (convergence D3)`.
- Ported code is **isolated** under `ported/` and namespaced `iae` so it is
  trivially auditable / re-syncable and never entangled with mmhoa-original code.
- This rights basis is distinct from the **JUCE/VST3** licensing handled in
  `.omc/plans/c2-licensing.md` (GPLv3 fallback + BSD-3 helpers). See §5.

## 3. Upstream coordinate

| Field | Value |
|---|---|
| Upstream repo | `github.com/dreamscapeaudio2023-star/immersive-audio-engine` |
| Upstream version | v0.2.1 (`project(ImmersiveAudioRenderer VERSION 0.2.1)`) |
| Pinned commit | **`f2cb796`** (single commit referenced by all ports) |
| Local clone | `/home/seung/mmhoa/dreamscape_references/immersive-audio-engine/` |
| Frame note | ported kernels use `+X right / +Y front / +Z up`; mmhoa uses `+X right / +Y up / +Z front`. Adapter = **Y↔Z swap (involution)** in `core/src/coords/Coords.h`. Gain is frame-agnostic; only input geometry is converted. |

## 4. Ported file ↔ upstream source ↔ xlsx sheet

All ported files: upstream commit **`f2cb796`**, namespace `iae`, JUCE-free.

| Ported file (`core/src/render/ported/`) | Upstream source | xlsx sheet |
|---|---|---|
| `SpatialMath.h` | `Source/SpatialMath.h` | 04 패닝 / 09 좌표계 |
| `Vbap.{h,cpp}` | `Source/Vbap.{h,cpp}` | 04 패닝 (VBAP) |
| `VbapMask.{h,cpp}` | `Source/AudioEngine.cpp` (`fillVbapMaskForObject` + helpers) | 04 패닝 (VBAP 3D 고도 마스크) |
| `Vap.{h,cpp}` | `Source/Vap.{h,cpp}` | 04 패닝 (VAP) |
| `Wfs.{h,cpp}`, `WfsDrivingParams.h` | `Source/Wfs.{h,cpp}`, `Source/WfsDrivingParams.h` | 04 패닝 (WFS) |
| `SpeakerKind.h` | `Source/SpeakerKind.h` | 01 상수 / 04 패닝 |
| `RoomFdn.{h,cpp}` | `Source/RoomEngine.{h,cpp}` (late FDN) | 05 룸엔진 |
| `RoomEarly.{h,cpp}` | `Source/RoomEngine.cpp` (early reflections) | 05 룸엔진 |
| `RoomCluster.{h,cpp}` | `Source/RoomEngine.cpp` (cluster diffusion) | 05 룸엔진 |
| `RoomBiquad.{h,cpp}` | `Source/RoomEngine.cpp` (absorption EQ) + JUCE `juce_IIRFilter` coeff form (byte-faithful) | 05 룸엔진 |
| `RoomDistanceGain.h` | `Source/AudioEngine.cpp:29-62` (distance gain curve) | 05 룸엔진 / 거리모델 |
| `SpeakerDecorrelation.{h,cpp}` | `Source/SpeakerDecorrelation.*` (Schroeder allpass bank) | 07 디코릴 |

> MDAP source width is **native** (`AlgorithmAnalyticReference::vbap_mdap_gain_into`),
> not a direct port — it re-uses the ported VBAP mask kernel via cone/arc sampling.
> Documented here for completeness; it derives from the ported geometry but is
> mmhoa-original code.

## 5. Isolation & re-sync policy

- **Directory isolation:** all ported DSP under `core/src/render/ported/` only.
  No ported logic is inlined into mmhoa-original files; mmhoa code calls into
  `iae::` entry points.
- **JUCE-free:** `ported/` compiles with `-DSPATIAL_ENGINE_NO_JUCE=ON`. The one
  JUCE-derived numeric form (RoomBiquad RBJ/Q coefficients) was reproduced as
  plain float math, not by linking JUCE.
- **Do not hand-edit logic:** ports are byte-faithful; re-sync from upstream
  `f2cb796` (or a newer pinned commit) rather than diverging in-tree.
- **Adaptations allowed & recorded in each header:** include-path rewrites,
  namespace preservation, `std::vector`→fixed-buffer for RT no-alloc, and
  local constant definitions (e.g. `kPrototypeChannels`).

## 6. mmhoa-original assets (NOT ported — retained in superset)

These are mmhoa's own and are the reason for Path A (port-into-mmhoa):
NO_JUCE headless build, 5 Ambisonic decoders, Dante/LTC/Cue, SHM telemetry,
VST3 wrapper, CI / relacy / no-alloc gate, coordinate adapter + golden tests.

## 7. Cross-references

- License (JUCE/VST3): `.omc/plans/c2-licensing.md`
- Convergence decisions D1–D4: `.omc/plans/dreamscape-convergence-master-plan.md`
- v1.0 coverage plan: `.omc/plans/spatial-engine-v1-full-coverage-plan.md`
