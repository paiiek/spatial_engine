"""A-M4 dashboard smoke (AC5) — canvas mount + draw-on-metrics + reset button.

Headless chromium, reusing the module-scoped ``uvicorn_server`` fixture from
``conftest.py``. Asserts:

  1. the ``<canvas>`` chart elements mount under ``/dashboard``;
  2. injecting a mock ``metrics`` message over the dashboard's message handler
     drives at least one canvas 2d draw call (spied on
     ``CanvasRenderingContext2D.prototype``);
  3. the reset-demote button exists in the DOM.

We inject via ``window.__dashboard.handleMessage`` rather than over a live
``/ws/metrics`` socket so the test is deterministic and needs no running
engine — the handler is the exact code path a real metrics frame takes.

DD-C is LOCKED (self-hosted canvas, zero external JS/CDN); this test verifies
the self-rendered minichart actually paints.

Graceful skip: ``pytest.importorskip`` covers a missing playwright package;
a missing chromium binary skips at launch (mirrors test_coord_precision.py).
"""
from __future__ import annotations

import asyncio

import pytest

playwright_async = pytest.importorskip(
    "playwright.async_api",
    reason="playwright not installed (pip install -r requirements-dev.txt)",
)
from playwright.async_api import async_playwright, Error as PWError  # noqa: E402

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


# Spy that wraps the canvas 2d drawing primitives and counts invocations.
# Installed BEFORE we inject a metrics frame so the count reflects only the
# draw triggered by that frame's render().
_INSTALL_DRAW_SPY_JS = """
() => {
  if (window.__drawSpyInstalled) {
    window.__drawCalls = 0;
    return { ok: true, already: true };
  }
  window.__drawCalls = 0;
  const proto = CanvasRenderingContext2D.prototype;
  const methods = ['stroke', 'fill', 'fillRect', 'lineTo', 'arc', 'fillText'];
  for (const m of methods) {
    const orig = proto[m];
    if (typeof orig !== 'function') continue;
    proto[m] = function (...args) {
      window.__drawCalls++;
      return orig.apply(this, args);
    };
  }
  window.__drawSpyInstalled = true;
  return { ok: true };
}
"""

_MOCK_METRICS = {
    "type": "metrics",
    "cpu_pct": 23,
    "cpu_peak_pct": 41,
    "p99_us": 850,
    "xrun_count": 0,
    "engine_overrun_count": 0,
    "binaural_demote_count": 0,
}


@pytest.mark.asyncio
async def test_dashboard_canvas_mounts_and_draws_on_metrics(uvicorn_server: str) -> None:
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                "chromium binary not installed for playwright. "
                f"Underlying error: {exc}"
            )

        context = await browser.new_context(viewport={"width": 1280, "height": 800})
        page = await context.new_page()
        try:
            await page.goto(f"{uvicorn_server}/dashboard", wait_until="load")

            # (1) canvas chart elements mount.
            await page.wait_for_selector("#chart-cpu", state="attached")
            n_canvas = await page.evaluate(
                "() => document.querySelectorAll('canvas.chart').length"
            )
            assert n_canvas >= 1, f"expected >=1 chart canvas, got {n_canvas}"

            # Dashboard JS must have booted and exposed its test surface.
            await page.wait_for_function("() => !!window.__dashboard")

            # (2) install the draw spy, then inject a mock metrics frame and
            # confirm the self-rendered minichart actually paints.
            spy = await page.evaluate(_INSTALL_DRAW_SPY_JS)
            assert spy.get("ok"), f"draw spy install failed: {spy!r}"

            before = await page.evaluate("() => window.__drawCalls")
            await page.evaluate(
                "(m) => window.__dashboard.handleMessage(m)", _MOCK_METRICS
            )
            await asyncio.sleep(0.1)
            after = await page.evaluate("() => window.__drawCalls")

            assert after > before, (
                "no canvas draw calls after injecting a metrics frame "
                f"(before={before}, after={after}) — minichart did not render"
            )

            # (3) reset-demote button exists in the DOM.
            btn = await page.query_selector("#btn-reset-demote")
            assert btn is not None, "#btn-reset-demote not found in dashboard DOM"
        finally:
            await context.close()
            await browser.close()
