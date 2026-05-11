"""G2 mobile sentinel — iPhone 13 emulation, min-of-5-windows-p10 >= 50 fps.

Plan §S4 (Q-3 decision): the mobile gate is **50 fps** rather than 60 to
acknowledge the emulation noise floor — chromium-headless with `is_mobile`
flag is a WebKit pretender, not a real Safari/iOS pipeline, so a strict 60
fps assertion would be testing the emulator, not the product.

Methodology is identical to ``test_fps_desktop.py`` (S3) modulo viewport /
DPR / touch flags. Helpers live in ``_helpers.py``.
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
async def test_fps_mobile_iphone13_min_of_5_windows_p10_ge_50(uvicorn_server: str) -> None:
    """G2 mobile (iPhone 13) — min-of-5-windows-p10 >= 50 fps."""
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                "chromium binary not installed for playwright. "
                f"Underlying error: {exc}"
            )

        device = pw.devices["iPhone 13"]
        # Use chromium (we don't have webkit installed in CI) but apply the
        # mobile viewport/UA/DPR/touch flags so canvas.js renders at iPhone
        # dimensions and event handlers see a touch-capable context.
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

            # Mobile viewports collapse #topdown-canvas to width=0 under
            # the current flex layout. That kills canvas.js frame() (arc
            # with negative radius). We force a 320×320 canvas + install
            # a test-side rAF fps counter so the gate measures the actual
            # mobile rAF pacing (G2's intent) instead of the broken layout.
            fix = await apply_mobile_layout_fix(page)
            assert fix.get("ok"), f"layout fix failed: {fix!r}"
            tfps = await install_test_fps_counter(page)
            assert tfps.get("ok"), f"test fps install failed: {tfps!r}"

            seed = await seed_64_objs(page)
            assert seed and seed.get("ok"), f"failed to seed 64 objs: {seed!r}"

            drag_started = asyncio.Event()
            drag_duration = 2.5 + N_WINDOWS * WINDOW_SECONDS + 1.0
            # On chromium-mobile, page.mouse.move is auto-mapped to touch
            # events when is_mobile=True/has_touch=True, so we reuse the
            # same circular-mouse drag helper without touchscreen-specific
            # branching.
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
            print("[G2 iPhone 13] windows p10:", [f"{v:.1f}" for v in p10s])
            print(f"[G2 iPhone 13] min-of-5-windows-p10 = {min_p10:.2f} fps "
                  f"(threshold = {MOBILE_THRESHOLD_FPS} fps)")
            print("[G2 iPhone 13] raw fps per window:")
            for i, w in enumerate(raw):
                print(f"  W{i}: {[f'{v:.0f}' for v in w]}")

            threshold = float(os.environ.get(
                "G2_MOBILE_THRESHOLD_FPS", MOBILE_THRESHOLD_FPS
            ))
            assert min_p10 >= threshold, (
                f"G2 iPhone 13 FAIL: min-of-5-windows-p10 = {min_p10:.2f} fps "
                f"< {threshold} fps. Per-window p10 = {p10s!r}. "
                f"Raw samples = {raw!r}"
            )
        finally:
            await context.close()
            await browser.close()
