"""G2 desktop sentinel — min-of-5-windows-p10 >= 60 fps.

Methodology (plan §2 G2, R2):

* 64 objects activated via dev-only ``page.evaluate`` injection (no
  production code changes — Option B in the plan).
* Concurrent drag synth on canvas (~50 Hz mouse moves) for the duration
  of the measurement to load the main thread.
* 5 windows × 5 s each, sampling ``window.__fps`` every 250 ms → 20
  samples per window, 100 samples total.
* Per-window p10 (10th-percentile sample) computed; gate = ``min`` of
  the five p10 values.

The harness skips cleanly if playwright or chromium is missing so the
rest of the pytest suite is unaffected.
"""
from __future__ import annotations

import asyncio
import math
import os
import time

import pytest

# --------------------------------------------------------------------------
# Optional-import guard — skip the whole module gracefully when playwright
# is unavailable (CI without dev deps, etc.).
# --------------------------------------------------------------------------

playwright_async = pytest.importorskip(
    "playwright.async_api",
    reason="playwright not installed (pip install -r requirements-dev.txt)",
)
from playwright.async_api import async_playwright, Error as PWError  # noqa: E402

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


# --------------------------------------------------------------------------
# 64-obj seed — injected via page.evaluate.
# Mirrors `canvas.js` object-state shape (azim/elev/dist/active).
# Deterministic so failures reproduce.
# --------------------------------------------------------------------------

SEED_64_OBJ_JS = """
() => {
  if (typeof objects === 'undefined') {
    // canvas.js declares `const objects = ...` at module scope.
    // It is not attached to window, but is reachable from the module's
    // closure via global eval (function-body lookup). Use a fallback that
    // dispatches through the same /adm/obj wire the runtime uses, by
    // simulating a WsClient.onMessage call.
    return { ok: false, reason: 'objects symbol unreachable' };
  }
  for (let i = 1; i <= 64; i++) {
    objects[i].active = true;
    objects[i].azim = ((i - 1) * 5.625) % 360;       // 0..360, deterministic
    if (objects[i].azim > 180) objects[i].azim -= 360; // normalise to -180..180
    objects[i].elev = ((i % 9) - 4) * 5;             // -20..+20
    objects[i].dist = 0.3 + ((i % 8) * 0.08);        // 0.3..0.86
  }
  return { ok: true, n: 64 };
}
"""

# Fallback seed via the public WsClient.onMessage path. canvas.js binds
# the handler at module load, so dispatching synthetic /adm/obj/{n}/aed
# packets activates each object exactly like a real wire message.
SEED_64_OBJ_VIA_WIRE_JS = """
() => {
  if (typeof WsClient === 'undefined' || !WsClient.onMessage) {
    return { ok: false, reason: 'WsClient missing' };
  }
  // The runtime registers exactly one onMessage handler in canvas.js;
  // we cannot enumerate it from here, so we synthesise a custom event
  // that mimics the WS frame shape and rely on the handler being live.
  // Easier path: directly call the registered listener if it was kept on
  // window for tests, otherwise return false and let the caller fall
  // back to a no-seed run (Option B failure case).
  if (window.__forceSeedObjects) {
    window.__forceSeedObjects(64);
    return { ok: true, n: 64, via: 'forceSeedObjects' };
  }
  return { ok: false, reason: 'no seed hook' };
}
"""


async def _seed_64(page) -> dict:
    """Activate 64 objects without touching production code.

    Try the closure-direct path first (works because ``canvas.js`` is a
    classic script — its module-level ``const objects`` is reachable from
    a Function evaluated in the same realm via global scope walking on
    most engines). If unreachable, fall through to the wire-simulation
    path.
    """
    # Path A — direct closure access (best effort; modern V8 lets globals
    # see module-level const declared by classic <script>).
    try:
        result = await page.evaluate(SEED_64_OBJ_JS)
        if result and result.get("ok"):
            return result
    except PWError:
        pass

    # Path B — wire simulation via /adm/obj/N/aed handler. canvas.js
    # registers its handler synchronously at load, and `WsClient.onMessage`
    # accumulates handlers into an array; we invoke them directly.
    inject_helper = """
    () => {
      if (window.__forceSeedObjects) return { ok: true };
      // WsClient is defined in ws_client.js; its onMessage stores handlers
      // on an internal array we cannot reach. Instead, dispatch synthetic
      // messages via the same code path the real socket would: directly
      // mutate `objects` if accessible, else give up.
      try {
        // eslint-disable-next-line no-eval
        const objs = (0, eval)('typeof objects !== "undefined" ? objects : null');
        if (!objs) return { ok: false, reason: 'objects unreachable in eval' };
        window.__forceSeedObjects = (n) => {
          for (let i = 1; i <= n; i++) {
            objs[i].active = true;
            objs[i].azim = ((i - 1) * 5.625) % 360;
            if (objs[i].azim > 180) objs[i].azim -= 360;
            objs[i].elev = ((i % 9) - 4) * 5;
            objs[i].dist = 0.3 + ((i % 8) * 0.08);
          }
        };
        window.__forceSeedObjects(64);
        return { ok: true, via: 'eval-installed' };
      } catch (e) {
        return { ok: false, reason: String(e) };
      }
    }
    """
    return await page.evaluate(inject_helper)


# --------------------------------------------------------------------------
# Drag synth — circle around canvas centre at ~50 Hz.
# --------------------------------------------------------------------------

async def _drag_synth(page, duration_s: float, started_evt: asyncio.Event) -> None:
    """Hold left mouse + circle around centre of #topdown-canvas."""
    canvas = page.locator("#topdown-canvas")
    box = await canvas.bounding_box()
    if box is None:
        # Canvas not laid out yet — skip drag, still report fps.
        started_evt.set()
        await asyncio.sleep(duration_s)
        return
    cx = box["x"] + box["width"] / 2
    cy = box["y"] + box["height"] / 2
    radius = min(box["width"], box["height"]) * 0.35

    await page.mouse.move(cx + radius, cy)
    await page.mouse.down()
    try:
        started_evt.set()
        end = time.monotonic() + duration_s
        angle = 0.0
        while time.monotonic() < end:
            angle += 0.10
            x = cx + radius * math.cos(angle)
            y = cy + radius * math.sin(angle)
            await page.mouse.move(x, y)
            await asyncio.sleep(0.020)  # ~50 Hz
    finally:
        await page.mouse.up()


# --------------------------------------------------------------------------
# FPS sampler — 5 windows × 5 s × 250 ms.
# --------------------------------------------------------------------------

N_WINDOWS = 5
WINDOW_SECONDS = 5.0
SAMPLE_INTERVAL = 0.250
SAMPLES_PER_WINDOW = int(WINDOW_SECONDS / SAMPLE_INTERVAL)  # 20

# G2 desktop threshold — plan §2.
DESKTOP_THRESHOLD_FPS = 60.0


def _p10(values: list[float]) -> float:
    """10th-percentile (lower-bound style) of a non-empty list."""
    if not values:
        return 0.0
    s = sorted(values)
    # idx ≈ 0.1*N, clamped to [0, len-1]. With N=20 → idx = max(0, 1) = 1
    # (i.e. 2nd smallest sample = p10 lower bound). Matches plan §2.
    idx = max(0, int(len(s) * 0.10) - 1)
    return float(s[idx])


async def _measure_fps_windows(page) -> tuple[list[list[float]], list[float]]:
    """Return (raw_samples_per_window, p10_per_window)."""
    raw: list[list[float]] = []
    p10s: list[float] = []
    for _ in range(N_WINDOWS):
        window_samples: list[float] = []
        for _ in range(SAMPLES_PER_WINDOW):
            fps = await page.evaluate("() => window.__fps || 0")
            window_samples.append(float(fps))
            await asyncio.sleep(SAMPLE_INTERVAL)
        raw.append(window_samples)
        p10s.append(_p10(window_samples))
    return raw, p10s


# --------------------------------------------------------------------------
# The test.
# --------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_fps_desktop_min_of_5_windows_p10_ge_60(uvicorn_server: str) -> None:
    """G2 desktop — min-of-5-windows-p10 >= 60 fps under 64-obj + drag synth."""
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                "chromium binary not installed for playwright. "
                "Run: python -m playwright install chromium. "
                f"Underlying error: {exc}"
            )

        # Desktop viewport — matches plan G2 (60fps target on desktop).
        context = await browser.new_context(viewport={"width": 1280, "height": 800})
        page = await context.new_page()

        try:
            await page.goto(uvicorn_server, wait_until="load")

            # Let canvas.js bind and start its rAF loop.
            await page.wait_for_selector("#topdown-canvas", state="attached")
            await asyncio.sleep(0.5)

            # Seed 64 objects (Option B — test-side inject, prod untouched).
            seed = await _seed_64(page)
            assert seed and seed.get("ok"), (
                f"failed to seed 64 objects (path A and B both failed): {seed!r}"
            )

            # Warmup — let rAF spin up and __fps stabilise.
            # canvas.js updates window.__fps every ~1 s of accumulated
            # frame time, so we need at least one full update cycle
            # *after* the 64-obj seed + drag both start before sampling.
            # Empirically 2.5 s eliminates the first-sample drop seen with
            # a 1.0 s warmup. Drag is started BEFORE warmup so the rAF
            # loop measures the loaded steady-state, not the bare paint.
            drag_started = asyncio.Event()
            drag_duration = (
                2.5  # warmup
                + N_WINDOWS * WINDOW_SECONDS
                + 1.0  # tail slack
            )
            drag_task = asyncio.create_task(_drag_synth(page, drag_duration, drag_started))
            await drag_started.wait()
            await asyncio.sleep(2.5)

            async def _measure_now():
                return await _measure_fps_windows(page)

            measure_task = asyncio.create_task(_measure_now())

            raw_windows, p10_windows = await measure_task
            await drag_task  # drain

            min_p10 = min(p10_windows)
            print()  # newline before our report
            print("[G2 desktop] windows p10:", [f"{v:.1f}" for v in p10_windows])
            print(f"[G2 desktop] min-of-5-windows-p10 = {min_p10:.2f} fps "
                  f"(threshold = {DESKTOP_THRESHOLD_FPS} fps)")
            print(f"[G2 desktop] raw samples per window (fps):")
            for i, w in enumerate(raw_windows):
                print(f"  W{i}: {[f'{v:.0f}' for v in w]}")

            # Headless chromium in CPU-constrained CI sometimes paints
            # below the wall-clock target even with zero overdraw. Allow
            # an env override for diagnostic runs, but default is strict.
            threshold = float(os.environ.get("G2_DESKTOP_THRESHOLD_FPS", DESKTOP_THRESHOLD_FPS))
            assert min_p10 >= threshold, (
                f"G2 desktop FAIL: min-of-5-windows-p10 = {min_p10:.2f} fps "
                f"< {threshold} fps. Per-window p10 = {p10_windows!r}. "
                f"Raw samples = {raw_windows!r}"
            )
        finally:
            await context.close()
            await browser.close()
