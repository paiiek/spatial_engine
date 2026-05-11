"""N4 multi-WS race — 2 concurrent clients, broadcast drop < 1 %.

Per Critic N4 and pre-mortem D (R=35), the FastAPI ``ConnectionManager``
must broadcast engine state to every connected client without dropping
under modest concurrency. This spec stresses the broadcast path with two
browser contexts simultaneously hammering an endpoint that is known to
fan out a message to every connected WS client.

We use ``POST /api/mode`` as the broadcast trigger:

* It invokes ``manager.broadcast({"type":"mode_change",...})`` exactly
  once per request — a deterministic 1:N fan-out.
* It does not depend on the OSC bridge being live (the bridge may or may
  not have bound 9101 in CI; ``mode_change`` is pure WS).
* Each request alternates between ``ai`` and ``low_latency`` so we
  generate ~2 × clients × rate broadcasts/s of measurable traffic.

Mechanism
---------

* Spawn two ``browser.new_context()`` instances → two independent WS
  clients.
* Each page installs ``__wsRecv`` capture via ``WsClient.onMessage``.
* Two background asyncio tasks POST ``/api/mode`` in a tight loop for
  ``DURATION_S`` seconds, alternating modes. Total POSTs = ``trigger_count``;
  total expected per-client broadcasts = ``trigger_count`` (because every
  POST → exactly one broadcast → every live client receives it).
* After the spam ends + 1 s drain, read ``len(__wsRecv)`` on each page.
* Drop per client = ``1 - recv / trigger_count``.

Acceptance
----------

For each client: ``drop < 1 %``. Also asserts ``trigger_count >= 50`` so
we don't trivially pass with too few broadcasts to be a real stress test.
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

from ._helpers import install_ws_capture  # noqa: E402

pytestmark = [pytest.mark.playwright, pytest.mark.asyncio]


DURATION_S = float(os.environ.get("S4_MULTI_CLIENT_DURATION_S", "30"))
DROP_THRESHOLD = float(os.environ.get("S4_MULTI_CLIENT_DROP_MAX", "0.01"))
MIN_BROADCAST_FLOOR = int(os.environ.get("S4_MULTI_CLIENT_MIN_BROADCASTS", "50"))
# Trigger period — server hard caps at one POST per ~few ms. 50 ms gives
# ~600 broadcasts per spammer in 30 s × 2 spammers = ~1200 total triggers.
TRIGGER_PERIOD_S = float(os.environ.get("S4_MULTI_CLIENT_TRIGGER_PERIOD_S", "0.05"))


async def _prepare(page, base_url: str) -> None:
    """Load the page and install the per-client recv capture."""
    await page.goto(base_url, wait_until="load")
    await page.wait_for_selector("#topdown-canvas", state="attached")
    await asyncio.sleep(0.5)
    cap = await install_ws_capture(page)
    assert cap.get("ok"), f"ws capture install failed: {cap!r}"
    # The inbound hook depends on WsClient being reachable from
    # page.evaluate — if it isn't, recv will be 0 and we'd fail with a
    # confusing "100% drop" message. Fail fast with the real reason.
    assert cap.get("hookInstalled"), (
        f"WsClient.onMessage hook NOT installed — broadcast recv will be 0. "
        f"capture result: {cap!r}"
    )
    # Reset the recv counter to ignore any messages that arrived during
    # page load (mode_change initial state, etc.).
    await page.evaluate("() => { window.__wsRecv = []; }")


async def _recv_mode_change_count(page) -> int:
    """Count only ``mode_change`` broadcasts on this client."""
    return await page.evaluate(
        """() => {
          const recv = window.__wsRecv || [];
          let n = 0;
          for (const r of recv) {
            try {
              const d = typeof r.data === 'string' ? JSON.parse(r.data) : r.data;
              if (d && d.type === 'mode_change') n++;
            } catch (e) {}
          }
          return n;
        }"""
    )


async def _wait_for_ws_open(page) -> None:
    """Sleep long enough for ws_client.js's auto-connect handshake to
    complete. WsClient closes over its socket reference so readyState is
    not directly probable from page.evaluate; locally the WS handshake
    completes in well under 100 ms — we wait 500 ms for safety margin.
    """
    await asyncio.sleep(0.5)


async def _broadcast_spam(base_url: str, duration_s: float, period_s: float,
                          tag: str) -> int:
    """POST /api/mode in a tight loop. Returns total successful POST count."""
    import aiohttp  # type: ignore[import-not-found]

    count = 0
    end = asyncio.get_event_loop().time() + duration_s
    modes = ("ai", "low_latency")
    timeout = aiohttp.ClientTimeout(total=5.0)
    async with aiohttp.ClientSession(timeout=timeout) as session:
        i = 0
        while asyncio.get_event_loop().time() < end:
            mode = modes[i & 1]
            i += 1
            try:
                async with session.post(
                    f"{base_url}/api/mode", params={"mode": mode}
                ) as resp:
                    if resp.status == 200:
                        count += 1
            except Exception:
                # Transient — keep going; we'll surface via assertion on
                # MIN_BROADCAST_FLOOR if things truly broke.
                pass
            await asyncio.sleep(period_s)
    return count


@pytest.mark.asyncio
async def test_two_concurrent_clients_broadcast_drop_under_1pct(
    uvicorn_server: str,
) -> None:
    """2 contexts + 2 spammers × DURATION_S s → drop rate < 1 % per client."""
    try:
        import aiohttp  # noqa: F401
    except ImportError:
        pytest.skip("aiohttp not installed — needed for /api/mode spam")

    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.launch(headless=True)
        except PWError as exc:
            pytest.skip(
                "chromium binary not installed for playwright. "
                f"Underlying error: {exc}"
            )

        ctx_a = await browser.new_context(viewport={"width": 1280, "height": 800})
        ctx_b = await browser.new_context(viewport={"width": 1280, "height": 800})
        page_a = await ctx_a.new_page()
        page_b = await ctx_b.new_page()

        try:
            await asyncio.gather(
                _prepare(page_a, uvicorn_server),
                _prepare(page_b, uvicorn_server),
            )
            # Give both WS handshakes time to complete BEFORE spam starts;
            # otherwise the first few broadcasts may land on a still-
            # connecting socket and be lost-by-design (not a drop).
            await _wait_for_ws_open(page_a)
            await _wait_for_ws_open(page_b)

            # Two concurrent spammers — different periods to interleave.
            spam_a = asyncio.create_task(
                _broadcast_spam(uvicorn_server, DURATION_S, TRIGGER_PERIOD_S, "A")
            )
            spam_b = asyncio.create_task(
                _broadcast_spam(uvicorn_server, DURATION_S, TRIGGER_PERIOD_S, "B")
            )

            # Mid-run sample for sanity.
            await asyncio.sleep(DURATION_S / 2)
            mid_recv_a = await _recv_mode_change_count(page_a)
            mid_recv_b = await _recv_mode_change_count(page_b)
            print(f"[N4] mid-run mode_change recv: A={mid_recv_a} B={mid_recv_b}")

            trigger_a, trigger_b = await asyncio.gather(spam_a, spam_b)
            triggers = trigger_a + trigger_b

            # Drain — let the server flush any in-flight broadcasts.
            await asyncio.sleep(1.0)

            recv_a = await _recv_mode_change_count(page_a)
            recv_b = await _recv_mode_change_count(page_b)

            # Each trigger is exactly one server broadcast. Each connected
            # client should receive every broadcast → expected = triggers.
            expected = triggers
            drop_a = 1.0 - (recv_a / expected) if expected > 0 else 1.0
            drop_b = 1.0 - (recv_b / expected) if expected > 0 else 1.0
            max_drop = max(drop_a, drop_b)

            print()
            print(f"[N4] duration={DURATION_S}s "
                  f"period={TRIGGER_PERIOD_S}s "
                  f"threshold={DROP_THRESHOLD*100:.2f}%")
            print(f"[N4] triggers: A={trigger_a} B={trigger_b} total={triggers}")
            print(f"[N4] client A recv={recv_a} drop={drop_a*100:.3f}%")
            print(f"[N4] client B recv={recv_b} drop={drop_b*100:.3f}%")
            print(f"[N4] max_drop = {max_drop*100:.3f}%")

            assert triggers >= MIN_BROADCAST_FLOOR, (
                f"N4 FAIL: too few broadcast triggers "
                f"(triggers={triggers} < {MIN_BROADCAST_FLOOR}). "
                f"Either /api/mode is unreachable or the spammer crashed. "
                f"trigger_a={trigger_a} trigger_b={trigger_b}"
            )
            assert max_drop < DROP_THRESHOLD, (
                f"N4 FAIL: broadcast drop = {max_drop*100:.3f}% "
                f">= {DROP_THRESHOLD*100:.2f}%. "
                f"A(recv={recv_a}/{expected}, drop={drop_a*100:.3f}%) "
                f"B(recv={recv_b}/{expected}, drop={drop_b*100:.3f}%)"
            )
        finally:
            await ctx_a.close()
            await ctx_b.close()
            await browser.close()
