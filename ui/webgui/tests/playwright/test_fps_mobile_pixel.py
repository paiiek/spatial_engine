"""G2 mobile sentinel — Pixel 5 emulation, min-of-5-windows-p10 >= 50 fps.

Mirrors ``test_fps_mobile_iphone.py``; the only delta is the device profile
(``pw.devices["Pixel 5"]``). See that file's docstring for methodology and
the Q-3 decision rationale for the 50 fps gate.
"""
from __future__ import annotations

import asyncio
import os

import pytest

playwright_async = pytest.importorskip(
    "playwright.async_api",
    reason="playwright not installed (pip install -r requirements-dev.txt)",
)
from playwright.async_api import async_playwright, Error as PWError  # noqa: E402

from ._helpers import (  # noqa: E402
    apply_mobile_layout_fix,
    drag_synth_circular,
    install_test_fps_counter,
    measure_fps_min_of_windows,
    seed_64_objs,
)

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


MOBILE_THRESHOLD_FPS = 50.0
N_WINDOWS = 5
WINDOW_SECONDS = 5.0


@pytest.mark.asyncio
async def test_fps_mobile_pixel5_min_of_5_windows_p10_ge_50(uvicorn_server: str) -> None:
    """G2 mobile (Pixel 5) — min-of-5-windows-p10 >= 50 fps."""
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                "chromium binary not installed for playwright. "
                f"Underlying error: {exc}"
            )

        device = pw.devices["Pixel 5"]
        context = await browser.new_context(
            viewport=device["viewport"],
            user_agent=device["user_agent"],
            device_scale_factor=device["device_scale_factor"],
            is_mobile=device["is_mobile"],
            has_touch=device["has_touch"],
        )
        page = await context.new_page()

        try:
            await page.goto(uvicorn_server, wait_until="load")
            await page.wait_for_selector("#topdown-canvas", state="attached")
            await asyncio.sleep(0.5)

            # Same mobile-viewport layout workaround as the iPhone spec —
            # canvas.width collapses to 0 → frame() throws → __fps frozen.
            # Force 320×320 + install test-side rAF fps counter.
            fix = await apply_mobile_layout_fix(page)
            assert fix.get("ok"), f"layout fix failed: {fix!r}"
            tfps = await install_test_fps_counter(page)
            assert tfps.get("ok"), f"test fps install failed: {tfps!r}"

            seed = await seed_64_objs(page)
            assert seed and seed.get("ok"), f"failed to seed 64 objs: {seed!r}"

            drag_started = asyncio.Event()
            drag_duration = 2.5 + N_WINDOWS * WINDOW_SECONDS + 1.0
            drag_task = asyncio.create_task(
                drag_synth_circular(page, drag_duration, drag_started)
            )
            await drag_started.wait()
            await asyncio.sleep(2.5)

            raw, p10s, min_p10 = await measure_fps_min_of_windows(
                page, n_windows=N_WINDOWS, window_s=WINDOW_SECONDS
            )
            await drag_task

            print()
            print("[G2 Pixel 5] windows p10:", [f"{v:.1f}" for v in p10s])
            print(f"[G2 Pixel 5] min-of-5-windows-p10 = {min_p10:.2f} fps "
                  f"(threshold = {MOBILE_THRESHOLD_FPS} fps)")
            print("[G2 Pixel 5] raw fps per window:")
            for i, w in enumerate(raw):
                print(f"  W{i}: {[f'{v:.0f}' for v in w]}")

            threshold = float(os.environ.get(
                "G2_MOBILE_THRESHOLD_FPS", MOBILE_THRESHOLD_FPS
            ))
            assert min_p10 >= threshold, (
                f"G2 Pixel 5 FAIL: min-of-5-windows-p10 = {min_p10:.2f} fps "
                f"< {threshold} fps. Per-window p10 = {p10s!r}. "
                f"Raw samples = {raw!r}"
            )
        finally:
            await context.close()
            await browser.close()
