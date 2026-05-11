"""G4 coordinate precision — drag → WS ``obj_pos`` round-trip <= 2°.

Derivation of the 2° budget
---------------------------

The end-to-end pixel→ADM-OSC path picks up error in two places:

1. ADM-OSC ``azim`` is encoded as IEEE-754 float32 on the wire. Around
   45°, ulp(45.0_f32) ≈ 3e-6 → < 0.005° quantisation. Negligible.
2. Canvas hit-test discretises cursor position to pixels. At a 720-px-wide
   topdown canvas with the orbit radius ≈ 320 px, one pixel at 45°
   corresponds to ``atan(1/320) * 180/pi`` ≈ 0.18°. We tolerate a worst
   case of one pixel slop on each axis → ≈ 0.36°, conservatively rounded
   to ~0.5°.

Sum of upper bounds ≈ 0.55°. We add ~1.45° of margin for:
* Subpixel mouse-event mapping inside chromium-headless,
* Floating-point round-off in ``Math.atan2`` and our reverse calculation.

Final budget: **2°** absolute azimuth error. Spec asserts
``|measured_azim - expected_azim| < 2.0`` (degrees).

Methodology
-----------

* Seed a single object (#1) at known (azim=0, elev=0, dist=0.5).
* Install ``WebSocket.prototype.send`` capture so every ``obj_pos`` frame
  is recorded in-page.
* Drag from the current rendered position of #1 to a target offset of
  (+100, -100) pixels (canvas-local). Expected azimuth at that offset:
  ``atan2(100, 100) * 180/pi`` = **45.0°**.
* Read back the last captured ``obj_pos`` frame and compare its azim to
  45.0° with tolerance 2.0°.
"""
from __future__ import annotations

import asyncio
import math

import pytest

playwright_async = pytest.importorskip(
    "playwright.async_api",
    reason="playwright not installed (pip install -r requirements-dev.txt)",
)
from playwright.async_api import async_playwright, Error as PWError  # noqa: E402

from ._helpers import (  # noqa: E402
    install_ws_capture,
    seed_single_obj,
)

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


AZIM_TOL_DEG = 2.0


@pytest.mark.asyncio
async def test_coord_precision_drag_azim_within_2deg(uvicorn_server: str) -> None:
    """Drag obj #1 to (+100,-100) px → WS azim within 2° of 45.0°."""
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
            await page.goto(uvicorn_server, wait_until="load")
            await page.wait_for_selector("#topdown-canvas", state="attached")
            await asyncio.sleep(0.5)

            # Install WS capture BEFORE any further mutation so we record
            # every outgoing obj_pos frame, but AFTER the page has loaded
            # so ws_client.js has constructed its socket and registered its
            # onMessage callback (we hook via WsClient.onMessage, not by
            # replacing window.WebSocket which would reset the live socket).
            cap = await install_ws_capture(page)
            assert cap.get("ok"), f"ws capture install failed: {cap!r}"

            # Place obj #1 at azim=0, dist=0.5 — front centre, mid-radius.
            seed = await seed_single_obj(page, obj_id=1, azim_deg=0.0,
                                         elev_deg=0.0, dist=0.5)
            assert seed.get("ok"), f"seed_single_obj failed: {seed!r}"

            # Compute the on-screen pixel coordinates where obj #1 lives,
            # then derive (start_px, start_py) for the drag using the same
            # math as canvas.js admToTopDown.
            geom = await page.evaluate("""
                () => {
                  const c = document.getElementById('topdown-canvas');
                  const box = c.getBoundingClientRect();
                  const w = c.width, h = c.height;
                  const cx_canvas = w / 2, cy_canvas = h / 2;
                  const radius = Math.min(w, h) / 2 - 20;
                  // admToTopDown for azim=0, dist=0.5:
                  // rad=0 → x = cx, y = cy - 0.5*radius
                  const obj_x = cx_canvas;
                  const obj_y = cy_canvas - 0.5 * radius;
                  // Convert from canvas-internal coords back to client px
                  // (canvas may be CSS-scaled). canvas.js getCanvasPos
                  // does: canvas_px = (client_px - rect.left) * (w / rect.w)
                  // so inverse is: client_px = rect.left + canvas_px*(rect.w/w)
                  const sx = box.left + obj_x * (box.width / w);
                  const sy = box.top + obj_y * (box.height / h);
                  return {
                    box: { x: box.left, y: box.top, w: box.width, h: box.height },
                    canvas: { w, h, cx: cx_canvas, cy: cy_canvas, radius },
                    obj_canvas: { x: obj_x, y: obj_y },
                    start_client: { x: sx, y: sy },
                  };
                }
            """)
            print("[G4 coord] geometry:", geom)

            sx = geom["start_client"]["x"]
            sy = geom["start_client"]["y"]

            # Drag to a target whose CANVAS-internal offset from centre is
            # (+100, -100) px. In ADM-OSC space (cx-centred, y-flipped):
            #   dx = +100, dy = +100  →  azim = atan2(100, 100) = 45°
            #   dist = hypot(100,100)/radius
            # Convert canvas offset to client offset using the same scale.
            scale_x = geom["box"]["w"] / geom["canvas"]["w"]
            scale_y = geom["box"]["h"] / geom["canvas"]["h"]
            radius = geom["canvas"]["radius"]
            cx_canvas = geom["canvas"]["cx"]
            cy_canvas = geom["canvas"]["cy"]

            target_canvas_x = cx_canvas + 100
            target_canvas_y = cy_canvas - 100  # canvas y grows down; -100 = up
            tx = geom["box"]["x"] + target_canvas_x * scale_x
            ty = geom["box"]["y"] + target_canvas_y * scale_y

            expected_azim = (math.atan2(100, 100) * 180 / math.pi)  # 45.0
            expected_dist = min(math.hypot(100, 100) / radius, 1.0)

            print(f"[G4 coord] start=(client {sx:.1f},{sy:.1f}) "
                  f"target=(client {tx:.1f},{ty:.1f}) "
                  f"expected azim={expected_azim:.3f}° dist={expected_dist:.3f}")

            # Perform the drag: down on the object, several moves to the
            # target (steps=10 keeps it inside-canvas and triggers many
            # mousemove dispatches; canvas.js sends an obj_pos per move).
            await page.mouse.move(sx, sy)
            await page.mouse.down()
            steps = 10
            for i in range(1, steps + 1):
                fx = sx + (tx - sx) * i / steps
                fy = sy + (ty - sy) * i / steps
                await page.mouse.move(fx, fy)
                await asyncio.sleep(0.020)
            await page.mouse.up()
            # Let any pending WS sends flush.
            await asyncio.sleep(0.2)

            sent = await page.evaluate(
                "() => (window.__wsSent || []).filter(m => m && m.type === 'obj_pos')"
            )
            print(f"[G4 coord] captured {len(sent)} obj_pos frames")
            assert sent, (
                "No obj_pos WS messages captured. "
                "Did the drag synthesise mousedown on the object? "
                f"window.__wsSent (any type) length = "
                f"{await page.evaluate('() => (window.__wsSent || []).length')}"
            )

            last = sent[-1]
            measured_azim = float(last["azim"])
            measured_dist = float(last["dist"])
            azim_diff = abs(measured_azim - expected_azim)

            print(f"[G4 coord] measured azim={measured_azim:.4f}° "
                  f"dist={measured_dist:.4f} (last of {len(sent)} frames)")
            print(f"[G4 coord] |azim_diff| = {azim_diff:.4f}° "
                  f"(tolerance = {AZIM_TOL_DEG}°)")

            assert azim_diff < AZIM_TOL_DEG, (
                f"G4 coord FAIL: |measured - expected| = "
                f"{azim_diff:.4f}° >= {AZIM_TOL_DEG}°. "
                f"measured={measured_azim:.4f}, expected={expected_azim:.4f}. "
                f"last frame = {last!r}"
            )
        finally:
            await context.close()
            await browser.close()
