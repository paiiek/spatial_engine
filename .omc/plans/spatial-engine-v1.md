# Plan: spatial_engine v1 — F1~F4 (VBAP3D, Ambisonics encoder stub, ElevationView, MIDI auto-discovery)

## Header
- **Project**: `spatial_engine` (object-based immersive audio rendering engine; C++ JUCE core + PySide6 UI)
- **Plan ID**: `spatial-engine-v1`
- **Date**: 2026-05-01
- **Status**: `REVISED-AFTER-CONSENSUS` (RALPLAN-DR short mode; rev 2 — final fixes applied 2026-05-01)
- **Working dir**: `/home/seung/mmhoa/spatial_engine/`
- **Predecessor**: `.omc/plans/spatial-engine-v0.md` (P0~P12 완료, v0.1.0 태그 commit `19679c6`, 30/30 ctest + 71 passed/1 skipped pytest 그린 상태)
- **Mode**: SHORT (no pre-mortem; deliberate gating not required — 4 self-contained features, hardware-free CI, each independently committable, low integration risk vs v0 audio-thread/IPC core)
- **Revision log**:
  - rev 1 (2026-05-01): Applied Architect + Critic REVISE feedback. Critical: F2 W=1.0 (was 1/√2); F4 stop() race-free reorder; F4 rtmidi backend probe in `is_available()` and skipif. Major: F4 mido loopback test wrapped in try/except + Windows/rtmidi skips; F1 ADR coordinate-frame statement; F1 fallback tie-break documented. Medium: F1 minimum-spread heuristic disclaimer; `[KNOWN-2D]` removal made literal; F4 stop-without-start no-op + double-start guard; R2 RT-cost caveat. Tests: F1 dimensionality-probe boundary + fallback gain pattern; F2 NaN propagation.
  - rev 2 (2026-05-01): Final fixes. C1 (CRITICAL): F4 test snippets corrected to actual `MidiBridge(osc_client=..., midi_port_name=...)` signature and private `bridge._client.calls` field (matches `midi_bridge.py:19-20`). M1 (MAJOR): F4 pytest count summary rewritten to explicit 76+3 / 78+1 = 79 collected; Success Criteria split into ≥76 (no-mido) / ≥78 (full env); ambiguous "5 tests + v0 …" wording replaced with "All existing 6 tests". M3 (MAJOR): F4 worker loop now MUST use `iter_pending()` + `time.sleep(0.001)` (blocking `receive()` PROHIBITED with stated rationale); test stub spec gains explicit `import time` + `import sys` + `import pytest` + guarded `import mido`.

---

## Principles (5)

1. **No regression on v0 baseline** — 30/30 ctest + 71 passed pytest must remain green after every feature commit. Each feature is independently committable.
2. **Hardware-free CI parity** — every test must pass under `SPATIAL_ENGINE_NO_JUCE=ON` and headless pytest (`PySide6` stubbed out). No lab gear in the loop.
3. **Analytical reference first, engine reuse second** — F1 (VBAP 3D triplet) and F2 (Ambisonics encoder) live in the analytic-reference layer (`AlgorithmAnalyticReference` / new `AmbisonicEncoder`) so they are testable against closed-form math without DSP scaffolding (consistent with v0 ADR Critic-C7).
4. **Headless-stub pattern parity** — F3 (ElevationView) and F4 (MidiBridge.start) follow the same `try/except ImportError` stub pattern as `ElevationControl` / existing `MidiBridge` so headless tests run on hosts without PySide6 / mido.
5. **Per-feature commit boundary** — each of F1/F2/F3/F4 is a single commit with its own test gate; failures in one must not block the others.

## Decision Drivers (top 3)

1. **D1 — Falsifiability**: each feature has at least one test that **fails today** and **passes after the change**, so we can prove forward motion without manual inspection. (F1: `[KNOWN-2D]` removal + `CHECK(any_diff)`; F2/F3/F4: new test files added that didn't exist.)
2. **D2 — Hardware-free determinism**: F1 uses synthetic 8ch_3d layout already in the test file; F2 is pure math; F3 uses headless PySide6 stub; F4 uses mido virtual loopback (skipped when mido absent). Zero hardware dependency.
3. **D3 — Minimum API surface**: F2 ships an encoder stub (1st-order ambisonics only, no decoder, no rotation, no HOA-N≥2). F3 ships an ElevationView side-by-side with TopDownView (no zoom, no pan, no drag). F4 ships start()/stop() pair (no MIDI clock, no SysEx, no CC, only Program Change). Resist scope creep — full HOA / 3D scene editor / MIDI Learn are out of scope.

## Viable Options Considered

### Option A — All four features in one commit (rejected)
- Pros: single CI run, atomic milestone tag.
- Cons: violates Principle 5; if F1 (3D triplet algorithm) regresses 30/30 ctest, F2/F3/F4 are blocked from landing; review surface area is too large.

### Option B — Four independent commits, in dependency order F2 → F1 → F3 → F4 (chosen)
- Pros: each commit is rebase-friendly; F2 (lowest risk: pure math, new file) lands first to seed `core/src/ambi/` directory and prove CMakeLists registration mechanics; F1 (medium risk: changes existing analytic reference function signature semantics) lands second; F3/F4 are UI-side and parallel-safe.
- Cons: 4× CI runs vs 1.

### Option C — F1 only this milestone, defer F2/F3/F4 (rejected)
- Pros: smallest scope.
- Cons: F2/F3/F4 are independent and ready; deferring buys nothing. F2 in particular is foundational for any future HOA work and is cheap.

**Decision**: Option B. Order of execution: F2 → F1 → F3 → F4.

---

## Per-Feature Specification

### F2 — Ambisonics 1st-order encoder stub (executes first; lowest risk)

**Why first**: pure math, new directory, no existing-code-path semantics shift. Validates the ambi/ subdirectory wiring in CMakeLists for any future HOA work.

**Files**:
- `core/src/ambi/AmbisonicEncoder.h` (new) — header declaring `class AmbisonicEncoder` with static `encode_1st_order(float az_rad, float el_rad) -> std::array<float,4>` returning `{W, X, Y, Z}` in ACN order with W=1.0 (SN3D-consistent default).
- `core/src/ambi/AmbisonicEncoder.cpp` (new) — implementation: `W = 1.0`, `X = cos(el)*cos(az)`, `Y = cos(el)*sin(az)`, `Z = sin(el)`. Coordinate convention matches `Speaker` struct (x=right, y=up, z=front), but az/el are the engine convention used in `ObjectState` (az=0 → front, el=0 → horizontal plane).
- `core/CMakeLists.txt:75-110` — append `src/ambi/AmbisonicEncoder.cpp` to `SPE_CORE_SOURCES`. No new lib; reuses `spe_core`.
- `core/tests/core_unit/test_p_ambi.cpp` (new) — `CHECK_NEAR` based numerical test (same test harness style as `test_p_vbap3d.cpp`):
  - Case 1: az=0, el=0 → `{1.0, 1, 0, 0}` (cos(0)=1, sin(0)=0).
  - Case 2: az=π/2, el=0 → `{1.0, 0, 1, 0}`.
  - Case 3: az=π, el=0 → `{1.0, -1, 0, 0}`.
  - Case 4: az=0, el=+π/2 → `{1.0, 0, 0, 1}`.
  - Case 5: az=0, el=-π/2 → `{1.0, 0, 0, -1}`.
  - Tolerance 1e-6; `[RESULT] PASS`/`FAIL` printout + `return failures==0?0:1` exit code.
  - **Invalid input case**: `encode(NaN, 0.0)` and `encode(0.0, NaN)` — document behavior: NaN propagates through `cos/sin`, output `X/Y/Z` are NaN, `W = 1.0` is finite. Test asserts `std::isnan(out[1])` (i.e. NaN propagation, not exception). Rationale: encoder has no input validation; caller is responsible for sanitizing object state. Documented in header.
- `core/tests/core_unit/CMakeLists.txt` — append `add_executable(test_p_ambi ...)` + `target_link_libraries(test_p_ambi PRIVATE spe_core)` + `add_test(NAME p_ambi COMMAND test_p_ambi)`. Goes after the `# Feature 2: VBAP 3D ...` block at L141 (rename that comment to "VBAP 3D"; F2 gets its own block "# Feature: Ambisonics 1st-order encoder").

**Acceptance** (file:line precision):
- `core/build/test_p_ambi` exit 0 → `ctest -R p_ambi` PASS.
- All 5 closed-form cases match within 1e-6 — verified by `CHECK_NEAR(gains[i], expected[i], 1e-6f)` at each test function. W=1.0 in every case (not 1/√2).
- NaN propagation case: `encode(NaN, 0)` → `out[0] == 1.0` AND `std::isnan(out[1])` (NaN propagates through `cos/sin` to X; documented behavior, not assertion).
- 31/31 ctest after F2 (was 30/30; +1 = p_ambi). No skip, no flake.
- `core/src/ambi/AmbisonicEncoder.h` declares only the static method (no instance state). Header documents: ACN ordering, W=1.0 (SN3D-consistent default; downstream decoder applies its own W weighting), no input validation (caller responsible for finite az/el). NO_JUCE build does not regress.

**Out of scope (explicit)**:
- Higher-order (N≥2) ACN/SN3D coefficients.
- N3D normalization variant.
- Decoder (B-format → speaker) — that is a separate v2 feature.
- Rotation / Furse-Malham / FuMa.

---

### F1 — VBAP 3D triplet selection (executes second)

**Why second**: changes semantics of an existing function (`AlgorithmAnalyticReference::vbap_gain` el_rad param goes from ignored to load-bearing). Must not regress `test_p3_vbap` (4ch horizontal layout) which currently passes with 2D pair selection.

**Files**:
- `core/src/render/AlgorithmAnalyticReference.h:20-22` — keep signature `vbap_gain(layout, az_rad, el_rad=0.f)`; update doc comment from "2D: find the loudspeaker pair..." to "VBAP 2D pair (Pulkki 1997) when no upper-ring speakers; VBAP 3D triplet when layout has speakers above horizontal plane (any speaker.y > eps)."
- `core/src/render/AlgorithmAnalyticReference.cpp:16-98` — refactor `vbap_gain`:
  1. **Layout dimensionality probe** at function entry: scan `layout.speakers[].y`; if `max(|y|) < 1e-3` → fall through to existing 2D pair-search (no behavioral change for 4ch_horizontal; protects `test_p3_vbap`).
  2. **3D triplet path**: if any speaker has `|y| ≥ 1e-3`:
     - **Coordinate frame** (explicit): engine az convention is `atan2(x, z)` (az=0 → +z front, az=π/2 → +x right). Source unit vector: `s = (cos(el)·sin(az), sin(el), cos(el)·cos(az))`. Speaker unit vectors are computed from their stored `(x, y, z)` and renormalized to length 1.
     - Build candidate triangles via the convex-hull-of-speakers approach: for v1 simplicity, enumerate all C(N,3) triplets (N≤8 in v1 layout tests → ≤56 triplets, not RT-critical because this is the analytic reference, called once per object per block in `VBAPRenderer::processBlock` — same call frequency as 2D path).
     - For each triplet (i,j,k), compute matrix `L = [l_i; l_j; l_k]` (3×3, rows are speaker unit vectors). Solve `g = L^{-1} · s`. If all three `g_i, g_j, g_k ≥ 0` (point lies inside triangle), accept.
     - Among accepted triplets, pick the one with the smallest spread (max angular distance from speaker i,j,k to source) — minimum-spread heuristic (Pulkki-inspired, not Pulkki 1997's volume criterion).
     - Energy-normalize: `g_norm = g / sqrt(g_i² + g_j² + g_k²)`. Write into output gain vector at indices i,j,k; remaining gains stay 0.
  3. **Fallback** (no triplet contains source — outside convex hull of speaker layout): nearest-3-speakers by angular distance, with non-negative clamp + energy-normalize. **Tie-break**: among equidistant speakers (angular diff < 1e-6 rad), prefer ascending channel index. This avoids the "el extreme" crash path in `test2_extreme_elevation`.
- `core/src/render/VBAPRenderer.cpp:38-39` — already passes `objects[obj].el_rad` to `vbap_gain`; verify the call is unchanged. No edit needed; just confirm in commit.
- `core/tests/core_unit/test_p_vbap3d.cpp:189-194` — Replace lines 189-194 in test_p_vbap3d.cpp with:
  ```cpp
  CHECK(any_diff);
  printf("[PASS] test3: el=30 changes gain distribution\n");
  ```
  Drop the `if (any_diff)/else` branching entirely; no conditional printout.

- `core/tests/core_unit/test_p_vbap3d.cpp` — **Add new test4 (dimensionality probe boundary)**:
  - Construct two minimal layouts: `layout_a` with `max|y| = 5e-4` (below 1e-3 threshold → must take 2D pair path) and `layout_b` with `max|y| = 2e-3` (above threshold → must take 3D triplet path).
  - Assert: in `layout_a` with az=π/2, el=0, gains follow 2D pair-search semantics (only 2 non-zero gains, sum-of-squares == 1).
  - Assert: in `layout_b` with az=π/2, el=0.5, gains follow 3D triplet semantics (up to 3 non-zero gains, sum-of-squares == 1).
  - Print `[PASS] test4: dimensionality probe routes 5e-4→2D, 2e-3→3D`.

- `core/tests/core_unit/test_p_vbap3d.cpp` — **Extend test2 (extreme elevation)** with fallback gain pattern assertion:
  - After existing finite/non-negative checks, additionally assert: gains are non-zero only on the nearest 3 speakers (count of `|gain[i]| > 1e-6` is ≤ 3).
  - This proves the fallback path uses the documented nearest-3-speakers strategy, not a degenerate spread-everything pattern.

**Acceptance** (file:line precision):
- `test_p_vbap3d` test3 — `gains_el0` vs `gains_el30` on `make_8ch_3d` layout: at least one speaker gain differs by `> 1e-4` (the `any_diff` predicate at L179-184 must evaluate true). Hard `CHECK(any_diff)` (no `[KNOWN-2D]` branch).
- `test_p_vbap3d` test1 (4ch horizontal, el=0) — sum-of-squares == 1 (unchanged behavior).
- `test_p_vbap3d` test2 (extreme el ±π/2 on 4ch horizontal) — no crash, gains finite, non-negative; **additionally**: count of `|gain[i]| > 1e-6` must be ≤ 3 (proves nearest-3-speakers fallback pattern).
- `test_p_vbap3d` test4 (new — dimensionality probe boundary) — `layout_a` with max\|y\|=5e-4 takes 2D pair path (exactly 2 non-zero gains, sum-of-squares == 1); `layout_b` with max\|y\|=2e-3 takes 3D triplet path (≤ 3 non-zero gains, sum-of-squares == 1).
- `test_p3_vbap` (existing v0 test) — must still pass. The 2D dimensionality probe (`max(|y|) < 1e-3`) is the regression safeguard.
- 31/31 ctest after F1 (no count change vs F2, since `test_p_vbap3d` already exists and was already counted in v0 — it was just passing trivially via the `[KNOWN-2D]` branch).

**Out of scope**:
- Convex-hull pre-computation in `prepareToPlay` (premature optimization; 560-triplet brute-force at block rate is fine for v1).
- Subwoofer exclusion / virtual imaginary speaker (Pulkki/Lokki 2000 extension).
- Layout-validation refusal of degenerate triangles (collinear triplets) — they are filtered by the `det(L) < 1e-8` check.

---

### F3 — ElevationView side-panel (executes third; UI-side, parallel-safe with F4)

**Why third**: UI-only, no audio thread interaction. Independent of F1/F2.

**Files**:
- `ui/spatial_engine_ui/views/topdown.py:10-19` — keep existing `TopDownView`. Add new class `ElevationView(QWidget)` after `TopDownView` (still inside the `try` PySide6 branch):
  - Layout: vertical box. Header `QLabel("[Elevation View — el vs distance]")`. Body: a custom `QWidget` subclass (or reuse `QLabel`-based placeholder for v1) with `paintEvent` drawing scatter points at `(r, el)` for each object, where `r = sqrt(x² + z²)` (horizontal distance) and `el = elevation in degrees`.
  - Coordinate transform: `screen_x = margin + r * scale_r`, `screen_y = center_y - el_deg * scale_el` (positive elevation goes up on screen).
  - Refresh: `refresh()` method calls `self.update()`. Same pattern as `TopDownView.refresh`.
  - Constructor signature: `__init__(self, object_model: object = None, parent: QWidget | None = None)`.
- `ui/spatial_engine_ui/views/topdown.py:64-98` — extend the headless `except ImportError` branch with `class ElevationView` stub (same constructor signature, `refresh() → pass`, plus a pure-Python helper `to_screen_coords(self, x, y, z) -> tuple[int, int]` so the test can verify the coordinate transform without Qt).
- `ui/tests/test_elevation_view.py` (new) — headless pytest, same `_stub_pyside6()` pattern as `test_elevation_ui.py:8-20`:
  - Test 1: `ElevationView` is importable in headless mode (no PySide6).
  - Test 2: `ElevationView()` constructor accepts `object_model=None`.
  - Test 3: `to_screen_coords` maps `(x=1, y=0, z=0)` (az=90°, el=0°, r=1) → `(margin + scale_r, center_y)` (some pixel position with el=0 → screen vertical center). Use small fixed-point assertions: `assert sx > 0 and sy == center_y` style (don't assert exact pixel — use the transform's invariants: el=0 → on horizontal axis; r=0 → at left margin; positive el → smaller sy).
  - Test 4: `refresh()` is callable and returns `None`.

**Acceptance** (file:line precision):
- `python3 -m pytest ui/tests/test_elevation_view.py -q` → 4 passed.
- `python3 -m pytest ui/tests` → **75 passed, 1 skipped** (was 71+1; +4 = 75. The skip is the existing JUCE-host-required test).
- `from spatial_engine_ui.views.topdown import ElevationView` works in both PySide6-present and PySide6-absent environments.
- Existing `test_elevation_ui.py` (which tests `ElevationControl`, the slider) — must still pass unchanged.

**Out of scope**:
- Drag-to-edit elevation in the view (we already have `ElevationControl` slider; this view is read-only).
- 3D rendering / OpenGL.
- Object selection / hover.
- Wiring into `app.py` main window — leave widget standalone for v1; integration is a follow-up.

---

### F4 — MIDI port auto-discovery + start() implementation (executes fourth; UI-side)

**Why fourth**: tightest external dependency (`mido`). Must skip cleanly when `mido` absent.

**Files**:
- `ui/spatial_engine_ui/midi/midi_bridge.py:41-48` — replace the deferred `start()` body with real implementation:
  1. Guard: `if not _MIDO_AVAILABLE: return`.
  2. **Double-start guard**: `if self._thread is not None and self._thread.is_alive(): return` (idempotent start).
  3. Port discovery: `available = mido.get_input_names()`.
  4. Port selection:
     - If `self._port_name` is set and exists in `available` → use it.
     - Else if any name in `available` matches `"loopback"` (case-insensitive substring) → use that.
     - Else if `available` is non-empty → use `available[0]`.
     - Else → log `"[midi_bridge] no MIDI input ports available"` and return (no error, no exception).
  5. Open: `self._port = mido.open_input(selected)`.
  6. Clear stop flag: `self._stop_event.clear()`.
  7. Spawn worker thread (`threading.Thread(target=self._loop, daemon=True)`) that polls `self._port` for messages; for each, call `self.handle_message(msg)`. Loop terminates when `self._stop_event.is_set()`.
     - **REQUIRED**: worker loop MUST use `iter_pending()` (non-blocking) with `time.sleep(0.001)` between iterations. Blocking `receive()` is **PROHIBITED** because it prevents `_stop_event` from being checked and would cause the 1 s join timeout in `stop()` to expire, leaving zombie threads. Required loop body shape:
       ```python
       def _loop(self):
           while not self._stop_event.is_set():
               for msg in self._port.iter_pending():
                   self.handle_message(msg)
               time.sleep(0.001)
       ```
- `ui/spatial_engine_ui/midi/midi_bridge.py:49-51` — implement `stop()` with **race-free ordering**:
  1. **Stop-without-start safety**: `if self._thread is None: return` (no-op).
  2. Set stop flag first: `self._stop_event.set()`.
  3. Join the worker thread with 1 s timeout: `self._thread.join(timeout=1.0)`.
  4. Close port last: `if self._port is not None: self._port.close(); self._port = None`.
  5. Reset thread handle: `self._thread = None`.
  - Rationale: setting the flag before close lets the worker exit its `iter_pending` loop cleanly; closing after join prevents the worker thread from racing on a half-closed port.
- `ui/spatial_engine_ui/midi/midi_bridge.py:19-21` — extend `__init__` to allocate `self._stop_event = threading.Event()`, `self._port = None`, `self._thread = None`.
- `ui/spatial_engine_ui/midi/midi_bridge.py` — extend `is_available()` classmethod/staticmethod:
  ```python
  @staticmethod
  def is_available() -> bool:
      if not _MIDO_AVAILABLE:
          return False
      try:
          mido.get_input_names()  # probes rtmidi backend
          return True
      except Exception:
          return False
  ```
  Add module-level `_RTMIDI_AVAILABLE` probe at import time mirroring the same try/except, used by tests' skipif decorator.
- `ui/tests/test_midi_bridge.py` (existing, append at end) — required imports at top of file: `import time`, `import sys`, `import pytest`, `import mido` (under the same `try/except ImportError` guard already used by the module). Then add `test_start_loopback`:
  - Decorators (all of these):
    - `@pytest.mark.skipif(not MidiBridge.is_available(), reason="mido not available")`
    - `@pytest.mark.skipif(not _RTMIDI_AVAILABLE, reason="rtmidi backend unavailable")`
    - `@pytest.mark.skipif(sys.platform == 'win32', reason="virtual MIDI ports unsupported on Windows")`
  - Wrap entire body in `try/except`:
    ```python
    try:
        with mido.open_output('test_loopback', virtual=True) as out_port:
            bridge = MidiBridge(osc_client=MockClient(), midi_port_name='test_loopback')
            bridge.start()  # bridge calls mido.open_input('test_loopback') internally
            try:
                out_port.send(mido.Message('program_change', program=7))
                # Wait up to 500 ms for _client.calls to contain the OSC message
                deadline = time.monotonic() + 0.5
                while time.monotonic() < deadline:
                    if len(bridge._client.calls) >= 1:
                        break
                    time.sleep(0.02)
            finally:
                bridge.stop()
            assert len(bridge._client.calls) == 1
            assert bridge._client.calls[0] == ('/scene/load', 'scene_7')
            assert bridge._thread is None or not bridge._thread.is_alive()
    except (IOError, OSError, RuntimeError) as e:
        pytest.skip(f"MIDI loopback unavailable: {e}")
    ```
  - Test side opens virtual **output** port; bridge's `start()` opens an **input** port with the same name (Linux RtMidi backend pairs them via the virtual=True mechanism).
- `ui/tests/test_midi_bridge.py` — add `test_stop_without_start`:
  - Construct bridge, call `bridge.stop()` without prior `start()`. Assert: no exception raised, `bridge._thread is None`.
  - No skipif required (pure Python; mido stub is sufficient).
- `ui/tests/test_midi_bridge.py` — add `test_double_start_idempotent` (skipif decorators identical to `test_start_loopback`, wrapped in same try/except):
  - `bridge.start()`, capture `t1 = bridge._thread`.
  - Call `bridge.start()` again (immediate, before stop).
  - Assert `bridge._thread is t1` (no new thread spawned).
  - `bridge.stop()`.

**Acceptance** (file:line precision):
- `python3 -m pytest ui/tests/test_midi_bridge.py -q`:
  - Without mido installed → 7 passed (incl. `test_stop_without_start`), 2 skipped (`test_start_loopback`, `test_double_start_idempotent`).
  - With mido installed but no rtmidi backend / Windows → 7 passed, 2 skipped.
  - With mido installed + rtmidi backend on Linux/macOS → 9 passed.
- `python3 -m pytest ui/tests` total:
  - **Without mido/rtmidi**: 76 passed + 3 skipped (1 v0 juce-host skip + 2 mido loopback skips) = 79 collected.
  - **With mido + rtmidi on Linux/macOS**: 78 passed + 1 skipped (v0 juce-host only) = 79 collected.
  - The hard requirement: zero failures.
- **Race-free stop**: `bridge.stop()` after `bridge.start()` sets stop flag → joins thread (1 s timeout) → closes port → resets `_thread = None`. No zombie threads (verified by `bridge._thread is None or not bridge._thread.is_alive()` after stop).
- **Stop-without-start safety**: `bridge.stop()` called without prior `start()` is a no-op (verified by new test: `MidiBridge(); bridge.stop()` does not raise).
- **Double-start guard**: `bridge.start(); bridge.start()` does not spawn a second worker thread (verified: `_thread` identity unchanged on second call when first thread is alive).
- All existing 6 `test_midi_bridge.py` tests — unchanged, still pass.

**Out of scope**:
- MIDI Learn / dynamic remap.
- CC, SysEx, MIDI Clock.
- Multi-port aggregation.
- Reconnection on port disappear.

---

## Task Flow

```
[v0.1.0 / commit 19679c6 baseline: 30/30 ctest, 71+1 pytest]
            │
            ▼
F2 ─ Ambisonics 1st-order encoder stub
   │   - core/src/ambi/AmbisonicEncoder.{h,cpp}  (new)
   │   - core/CMakeLists.txt          (append source)
   │   - core/tests/core_unit/test_p_ambi.cpp  (new, 5 cases)
   │   - core/tests/core_unit/CMakeLists.txt   (register)
   │   build → ctest -R p_ambi → ctest (full) → commit "F2 Ambisonics encoder stub"
   ▼
F1 ─ VBAP 3D triplet selection
   │   - core/src/render/AlgorithmAnalyticReference.{h,cpp}  (refactor)
   │   - core/tests/core_unit/test_p_vbap3d.cpp  (remove [KNOWN-2D] branch)
   │   build → ctest -R p_vbap3d → ctest -R p3_vbap (regression) → ctest (full) → commit "F1 VBAP 3D triplet"
   ▼
F3 ─ ElevationView (UI-side, parallel-safe with F4)
   │   - ui/spatial_engine_ui/views/topdown.py  (extend both branches)
   │   - ui/tests/test_elevation_view.py        (new, 4 tests)
   │   pytest ui/tests/test_elevation_view.py → pytest ui/tests → commit "F3 ElevationView"
   ▼
F4 ─ MIDI port auto-discovery + start()
   │   - ui/spatial_engine_ui/midi/midi_bridge.py  (implement start/stop, threading)
   │   - ui/tests/test_midi_bridge.py              (append loopback test)
   │   pytest ui/tests/test_midi_bridge.py → pytest ui/tests → commit "F4 MIDI auto-discovery + start"
   ▼
[v1: 31/31 ctest, ≥76 pytest passed, 0 failures]
```

---

## Build & Verification Procedure (per feature)

For each feature in order F2 → F1 → F3 → F4:

1. **Implement code changes**.
2. **Build (C++ features F1, F2)**:
   ```
   cd /home/seung/mmhoa/spatial_engine/core/build && \
     /home/seung/miniforge3/bin/cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON && \
     make -j$(nproc)
   ```
   - Acceptance: build exits 0, no warnings beyond the v0 baseline.
3. **Targeted test**:
   - F2: `/home/seung/miniforge3/bin/ctest -R p_ambi --output-on-failure` → 1/1 PASS.
   - F1: `/home/seung/miniforge3/bin/ctest -R "p_vbap3d|p3_vbap" --output-on-failure` → 2/2 PASS.
   - F3: `python3 -m pytest ui/tests/test_elevation_view.py -v` → 4 passed.
   - F4: `python3 -m pytest ui/tests/test_midi_bridge.py -v` → 7 passed + 2 skipped (no-mido or no-rtmidi env), OR 9 passed (full mido + rtmidi on Linux/macOS).
4. **Full regression**:
   - C++ side (after F1/F2): `/home/seung/miniforge3/bin/ctest --output-on-failure` → 30+/30+ PASS (31/31 after both C++ features).
   - Python side (after F3/F4): `python3 -m pytest` → ≥76 passed; skipped count varies by env (1 baseline + 0–2 MIDI loopback). Zero failed is the hard requirement.
5. **Commit** with the feature label, e.g. `F2 AmbisonicEncoder + test_p_ambi: 1st-order ACN/SN3D encoder, 5-case numerical test`.

---

## Risk Register & Mitigations

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| R1 | F1 3D triplet refactor breaks `test_p3_vbap` (existing 4ch horizontal test) | MEDIUM | HIGH (v0 regression) | Dimensionality probe at function entry: `max(\|speaker.y\|) < 1e-3` → fall through to 2D path unchanged. Verified by running `ctest -R p3_vbap` after F1 lands. |
| R2 | F1 triplet brute-force `C(N,3)` triplet enumeration too slow for `processBlock` call rate | LOW | MEDIUM (latency budget impact) | **RT-cost caveat**: `vbap_gain` IS called in `VBAPRenderer::processBlock` (RT path). For N=8: C(8,3)=56 triplets; at 48 kHz / 512 = 93.75 calls/sec → acceptable. For N=64: C(64,3)=41 664 → cache or pre-compute required. **v1 scope: N≤8 in 3D layout tests; caveat documented**. Defer pre-computed convex-hull optimization to v2. If profiling at v1 close-out shows budget overrun, revisit. |
| R3 | F2 ACN/SN3D coefficient ordering mismatch with downstream HOA decoder (future) | LOW | LOW | F2 is encoder stub only, no decoder coupling yet. Document ACN/SN3D in header comment. Future decoder is a v2 problem. |
| R4 | F3 PySide6 `paintEvent` divergence between display environments | LOW | LOW | v1 `ElevationView` body draws via `QPainter` only (no OpenGL); test uses headless stub branch only — visual fidelity is not asserted, only the coordinate transform `to_screen_coords`. |
| R5 | F4 mido virtual loopback not supported on the test host (mac CI, sandboxed CI, Windows, missing rtmidi backend) | MEDIUM | LOW | Test has three skipif decorators: `not MidiBridge.is_available()`, `not _RTMIDI_AVAILABLE`, and `sys.platform == 'win32'`. Additionally, the entire test body is wrapped in `try: ... except (IOError, OSError, RuntimeError) as e: pytest.skip(f"MIDI loopback unavailable: {e}")` to defend against runtime failures the static skipif cannot foresee. The test code spec in F4 above includes this wrapping verbatim. |
| R6 | F4 worker thread leaks on `bridge.stop()` failure (e.g. mido `recv` blocking) | LOW | MEDIUM (test flake) | Use `daemon=True` thread + 1 s join timeout; even if join fails, daemon thread dies with the process. mido `iter_pending()` is non-blocking; prefer that over blocking `receive()`. |
| R7 | Per-feature commit boundary violation (e.g. F1 changes accidentally touch F2 files) | LOW | LOW | Each feature's file list is enumerated above; reviewer checks `git diff --stat` against the spec. |
| R8 | F1's `[KNOWN-2D]` removal makes the test brittle if future layouts genuinely produce no diff | LOW | LOW | The `make_8ch_3d` layout has +45° upper ring; el=0 vs el=30 must produce a different active triplet by construction. The `> 1e-4` tolerance is two orders looser than the floating-point noise floor of normalized gains. |

---

## Acceptance Criteria Mapping

| Feature | Spec criterion | Test path | Pass condition |
|---------|----------------|-----------|----------------|
| F1 | `vbap_gain` honours `el_rad` on 3D layout | `core/tests/core_unit/test_p_vbap3d.cpp:179-184` | `any_diff == true` between el=0 and el=30° on `make_8ch_3d`; `CHECK(any_diff)` passes (no `[KNOWN-2D]` printout) |
| F1 | Dimensionality probe boundary (5e-4 → 2D, 2e-3 → 3D) | `core/tests/core_unit/test_p_vbap3d.cpp::test4` | `layout_a` (max\|y\|=5e-4) takes 2D pair path (≤2 non-zero gains); `layout_b` (max\|y\|=2e-3) takes 3D triplet path (≤3 non-zero gains); both sum-of-squares == 1 |
| F1 | Fallback gain pattern (extreme el) | `core/tests/core_unit/test_p_vbap3d.cpp::test2` | finite + non-negative + non-zero gain count ≤ 3 (nearest-3-speakers fallback proven) |
| F1 | 2D regression preserved | `core/tests/core_unit/test_p3_vbap.cpp` | unchanged: PASS |
| F2 | `AmbisonicEncoder::encode_1st_order` matches closed-form | `core/tests/core_unit/test_p_ambi.cpp` (5 cases) | each `\|gain[i] - expected[i]\| < 1e-6`; W=1.0 verified |
| F2 | Invalid input (NaN) behavior | `core/tests/core_unit/test_p_ambi.cpp` (NaN case) | `encode(NaN, 0)` propagates NaN to X/Y/Z; `std::isnan(out[1])` is true; W stays 1.0 |
| F2 | NO_JUCE build still green | `core/build/` after `cmake -DSPATIAL_ENGINE_NO_JUCE=ON` | all 31 ctest PASS |
| F3 | `ElevationView` importable headless | `ui/tests/test_elevation_view.py` (4 tests) | all 4 PASS |
| F3 | `ElevationControl` + slider regression preserved | `ui/tests/test_elevation_ui.py` (6 tests) | unchanged: PASS |
| F4 | `MidiBridge.start()` opens port + forwards PC | `ui/tests/test_midi_bridge.py::test_start_loopback` | with mido: `client.calls == [('/scene/load', 'scene_7')]`; without mido: skipped |
| F4 | Existing PC handler regression preserved | `ui/tests/test_midi_bridge.py` (6 tests) | unchanged: PASS |
| ALL | Zero v0 regression | `ctest && pytest` from repo root | 31/31 ctest + ≥75 passed/1 skipped pytest |

---

## ADR (per-feature mini-records)

### ADR F1 — VBAP 3D triplet selection algorithm
- **Decision**: brute-force C(N,3) triplet enumeration with non-negative-gain inside-triangle filter, picking the accepted triplet via a **minimum-spread heuristic (Pulkki-inspired, not Pulkki 1997's volume criterion)**. 2D fallback when layout has no upper-ring speakers.
- **Coordinate frame** (explicit): engine az convention is `atan2(x, z)` (az=0 → +z front, az=π/2 → +x right). Source unit vector: `s = (cos(el)·sin(az), sin(el), cos(el)·cos(az))`. Speaker unit vectors normalized from stored `(x, y, z)`.
- **Fallback tie-break**: among equidistant speakers (angular diff < 1e-6 rad) when the source falls outside the convex hull, prefer ascending channel index for deterministic gain assignment.
- **Drivers**: D1 (falsifiable: `[KNOWN-2D]` removed; `any_diff` predicate hard-checked), D2 (synthetic 8ch layout, no hardware), D3 (no convex-hull pre-comp; minimum API surface).
- **Alternatives considered**:
  - (a) Convex-hull pre-computation in `prepareToPlay` — rejected for v1: premature optimization; v1 scope is N≤8 (56 triplets) so brute force fits the RT budget at 48 kHz/512.
  - (b) Pulkki/Lokki 2000 virtual imaginary speaker for under-floor sources — rejected for v1: out of scope; nearest-3 fallback covers the "el extreme" test.
  - (c) Pulkki 1997 volume criterion (max `|det(L)|`) for triplet selection — rejected: minimum-spread is simpler to compute, easier to justify on the synthetic 8ch+upper-ring layout, and produces identical results when triplets do not overlap.
- **Why chosen**: minimum diff against the v0 analytic reference; preserves 2D path bit-for-bit; passes the falsifying `any_diff` predicate; bounded RT cost at v1 scope.
- **Consequences**: `vbap_gain` el_rad parameter is now load-bearing. Any future test that constructs a non-horizontal layout will see different gains than v0 produced. RT cost grows as C(N,3); v2 must add caching for N>8.
- **Follow-ups**: convex-hull pre-comp in v2 (mandatory for N>8). Pulkki/Lokki extension when mixed-platform / under-floor speakers enter the layout catalog. Re-evaluate spread heuristic vs volume criterion if a layout produces ambiguous triplet selection.

### ADR F2 — ACN ordering, W=1.0 (SN3D-consistent default; decoder convention deferred to v2)
- **Decision**: ACN component ordering for 1st order with `W = 1.0`, `X = cos(el)·cos(az)`, `Y = cos(el)·sin(az)`, `Z = sin(el)`. **W=1.0 (not 1/√2)** is chosen as the SN3D-consistent default for 1st-order encoder output; the per-channel gain that downstream decoders may apply (e.g., 1/√2 W weighting in some max-rE decoders) is treated as a decoder-side concern, not an encoder-side normalization. Final decoder convention is deferred to v2 when the decoder lands.
- **Drivers**: D1 (5 closed-form test cases plus NaN propagation — falsifiable), D3 (1st order only, encoder only).
- **Alternatives**: (a) FuMa ordering (W,X,Y,Z) with W=1/√2 — same component ordering in 1st order but different sign and W-scaling conventions; rejected as ACN is the IETF / IEM / pyambisonic ecosystem default. (b) N3D normalization — rejected: SN3D matches the field convention used by Reaper / IEM Plug-in Suite. (c) W=1/√2 baked into encoder (original draft) — rejected: forces a specific decoder weighting prematurely; v2 decoder may want full SN3D where W=1.0 is canonical, and applying decoder-side W gain is cleaner than reversing it.
- **Why chosen**: ACN ordering matches the dominant open-source HOA toolchain; W=1.0 keeps the encoder convention-neutral so the v2 decoder can pick any 1st-order weighting (SN3D, FuMa, max-rE) without per-channel post-multiplication on the encoder output.
- **Consequences**: future F2-decoder must apply the conventional 1/√2 W weighting (or equivalent) at decode time, not assume it from the encoder. Documented in header. NaN inputs propagate through `cos/sin` to X/Y/Z; W stays 1.0 (caller responsible for input validation).
- **Follow-ups**: HOA-N≥2, decoder convention finalization, rotation in v2.

### ADR F3 — ElevationView is read-only side-panel
- **Decision**: `ElevationView` is a sibling widget to `TopDownView`; both consume the same `object_model`. v1 is read-only (no drag).
- **Drivers**: D2 (headless stub parity), D3 (minimum API surface; no drag).
- **Alternatives**: (a) extend `TopDownView` itself with an "elevation overlay" mode — rejected: violates single-responsibility. (b) 3D `view3d.py` extension — rejected: `view3d.py` is a different OpenGL-backed surface; F3 is an explicit 2D side projection.
- **Why chosen**: aligns with `ElevationControl` (slider) — same separation of concerns; same headless stub pattern.
- **Consequences**: integration into `app.py` deferred to follow-up.
- **Follow-ups**: drag-to-edit, hover info, integration into main window.

### ADR F4 — MIDI port selection priority
- **Decision**: explicit `--midi-port-name` > "loopback" substring match > first available port > skip silently.
- **Drivers**: D1 (loopback test gates), D2 (mido virtual port; skipif when mido absent), D3 (PC only; no MIDI Learn).
- **Alternatives**: (a) error if no port — rejected: bridge is optional; should not crash UI launch. (b) require user CLI flag — rejected: lab-bench friction; "loopback" auto-match is the v0 use case.
- **Why chosen**: matches the v0 spec preamble ("loopback" or "first port" auto-select).
- **Consequences**: silent-skip on no-port may surprise users; mitigated by stderr log line `"[midi_bridge] no MIDI input ports available"`.
- **Follow-ups**: MIDI Learn, multi-port aggregation, reconnect.

---

## Open Questions

(Persisted to `.omc/plans/open-questions.md` per planner protocol.)

1. F1: should the `1e-3` y-axis dimensionality threshold be exposed as a `LayoutCompatibilityChecker` constant for consistency with v0 ADR M4? (Defer; v1 keeps it as a private constant in `AlgorithmAnalyticReference.cpp`.)
2. F2: should we reserve `core/src/ambi/` namespace for the future decoder, or co-locate decoder under `core/src/render/`? (Defer to v2 feature spec.)
3. F3: should `ElevationView` be wired into `app.py` main window in this milestone or as a follow-up? (Decision: follow-up, to keep F3 scope minimal.)
4. F4: on hosts without `mido`, should `MidiBridge.start()` log a warning or stay silent? (Decision: log to stderr; matches v0 backend-availability log style.)

---

## Success Criteria (overall)

- 31/31 ctest PASS (was 30/30; +1 = `p_ambi`).
- pytest pass-count thresholds (was 71 passed + 1 skipped baseline; +4 from `test_elevation_view.py`; +1 from `test_stop_without_start` always; +0 or +2 from `test_start_loopback` + `test_double_start_idempotent` depending on mido/rtmidi availability):
  - **≥76 passed** in no-mido env (3 skipped: 1 v0 juce-host + 2 MIDI loopback).
  - **≥78 passed** in full mido + rtmidi env (1 skipped: v0 juce-host only).
  - Hard requirement: zero failures.
- Each of F1/F2/F3/F4 lands as an independent commit; commit messages reference the feature ID (`F1`, `F2`, `F3`, `F4`).
- No new compiler warnings beyond the v0 baseline.
- `[KNOWN-2D]` tag removed from `test_p_vbap3d.cpp`.
- `core/src/ambi/AmbisonicEncoder.h` exists and is included by `test_p_ambi.cpp`.
- `ui/spatial_engine_ui/views/topdown.py` exports both `TopDownView` and `ElevationView`, both with PySide6 + headless variants.
- `ui/spatial_engine_ui/midi/midi_bridge.py::MidiBridge.start()` opens a real input port and spawns a worker thread when `mido` is available.

---

## Status: REVISED-AFTER-CONSENSUS (rev 1)

Architect + Critic REVISE feedback (2026-05-01) has been applied per the Revision log in the Header. The plan now reflects:
- F2 ACN ordering with W=1.0 (decoder weighting deferred to v2)
- F1 explicit coordinate frame, minimum-spread heuristic clarification, fallback tie-break, dimensionality-probe boundary test, and fallback gain pattern test
- F4 race-free stop() ordering, rtmidi backend probe + skipif, Windows skipif, try/except runtime defense, stop-without-start safety, double-start idempotency
- F2 NaN propagation test
- R2 RT-cost caveat with explicit N=8 vs N=64 cost analysis

Next steps:
1. Architect re-review — confirm critical/major items resolved.
2. Critic re-review — confirm test falsifiability is preserved across the new boundary/fallback/NaN cases.
3. On consensus, hand off to `/oh-my-claudecode:autopilot` with this plan as the active driver.
