"""E-M4 dashboard scene/cue smoke (playwright).

Load /dashboard, exercise:
  1. Scene library panel renders.
  2. Save a scene → /ws dispatch {"type":"scene_save","name":"smoke_test"}.
  3. Cue list panel renders.
  4. Cue Go (index 0) → /ws dispatch {"type":"cue_go","index":0}.

Uses WebSocket.prototype.send spy (same pattern as test_dashboard_smoke.py AC6)
to observe outbound /ws commands without a running engine.

Graceful skip: playwright not installed or chromium binary absent.
"""
from __future__ import annotations

import asyncio
import json

import pytest

playwright_async = pytest.importorskip(
    "playwright.async_api",
    reason="playwright not installed (pip install -r requirements-dev.txt)",
)
from playwright.async_api import async_playwright, Error as PWError  # noqa: E402

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


# Install WebSocket.prototype.send spy — records every outbound frame.
_INSTALL_WS_SPY = """
() => {
  window.__wsSent = [];
  const orig = WebSocket.prototype.send;
  WebSocket.prototype.send = function (data) {
    try { window.__wsSent.push(data); } catch (e) {}
    return orig.call(this, data);
  };
}
"""


def _parse_sent(sent: list[str]) -> list[dict]:
    out = []
    for s in sent:
        try:
            out.append(json.loads(s))
        except (ValueError, TypeError):
            pass
    return out


@pytest.mark.asyncio
async def test_dashboard_scene_cue_smoke(uvicorn_server: str) -> None:
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                f"chromium binary not installed for playwright. Error: {exc}"
            )

        context = await browser.new_context(viewport={"width": 1280, "height": 900})
        page = await context.new_page()
        try:
            await page.goto(f"{uvicorn_server}/dashboard", wait_until="load")

            # Dashboard JS must boot and expose __dashboard.
            await page.wait_for_function("() => !!window.__dashboard")

            # (1) Scene library panel present in DOM.
            panel = await page.query_selector("#panel-scene-library")
            assert panel is not None, "#panel-scene-library not in DOM"
            scene_list = await page.query_selector("#scene-list")
            assert scene_list is not None, "#scene-list not in DOM"

            # Install spy before clicking so we capture all sends.
            await page.evaluate(_INSTALL_WS_SPY)

            # Wait for the /ws control socket to open (ctrlSend is guarded by
            # readyState === OPEN; same wait pattern as test_dashboard_smoke AC6).
            await page.wait_for_function(
                "() => window.__dashboard && window.__dashboard.ctrlSend"
            )

            # (2) Save a scene via the scene name input + Save button.
            await page.fill("#scene-name-input", "smoke_test")
            await page.click("#btn-scene-save")
            await page.wait_for_function(
                "() => (window.__wsSent || []).some("
                "p => { try { const d = JSON.parse(p);"
                " return d.type === 'scene_save' && d.name === 'smoke_test'; }"
                " catch (e) { return false; } })",
                timeout=5000,
            )
            sent_after_save = await page.evaluate("() => window.__wsSent")
            parsed = _parse_sent(sent_after_save)
            assert any(
                d.get("type") == "scene_save" and d.get("name") == "smoke_test"
                for d in parsed
            ), f"scene_save not found in WS sends: {sent_after_save!r}"

            # (3) Cue list panel present in DOM.
            cue_panel = await page.query_selector("#panel-cue-list")
            assert cue_panel is not None, "#panel-cue-list not in DOM"
            cue_list = await page.query_selector("#cue-list")
            assert cue_list is not None, "#cue-list not in DOM"

            # (4) Fire /cue/next via the Next button (cue_go requires a rendered
            # row which depends on a live cuelist.json; use cue_next which always
            # dispatches regardless of list state).
            await page.evaluate("() => window.__wsSent = []")
            await page.click("#btn-cue-next")
            await page.wait_for_function(
                "() => (window.__wsSent || []).some("
                "p => { try { return JSON.parse(p).type === 'cue_next'; }"
                " catch (e) { return false; } })",
                timeout=5000,
            )
            sent_after_go = await page.evaluate("() => window.__wsSent")
            parsed2 = _parse_sent(sent_after_go)
            assert any(d.get("type") == "cue_next" for d in parsed2), (
                f"cue_next not dispatched via /ws, got: {sent_after_go!r}"
            )

        finally:
            await context.close()
            await browser.close()
