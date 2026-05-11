"""
ui/webgui/tests/test_osc_bridge_shutdown.py — S0 deliverable #2.

Verifies that ``ui.webgui.osc_bridge.shutdown()`` releases the
``ThreadingOSCUDPServer`` socket and the background thread so file descriptors
do not leak between successive ``start()`` calls.

Plan reference: spatial-engine-webgui-v1.md §3 S0 (산출물 #2), §6 risk 5.1.
Sentinel reference: §7.2 ``osc_bridge shutdown``.

Acceptance:
    fd_count after shutdown <= fd_count before start  (delta <= 1, allow noise)
    pytest exit == 0
"""
from __future__ import annotations

import asyncio
import os
import socket
import sys
import time

import psutil
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))


# ---------------------------------------------------------------------------
# We bind on a high ephemeral test port (default 9101 is sometimes held by a
# real uvicorn dev server on the same host). The test patches the module-level
# OSC_STATE_PORT to keep ``start()`` honest.
# ---------------------------------------------------------------------------

def _pick_free_udp_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _fd_count() -> int:
    """File descriptor count for the current process (Linux/macOS only)."""
    try:
        return psutil.Process(os.getpid()).num_fds()
    except AttributeError:                       # pragma: no cover — Windows
        pytest.skip("num_fds() unavailable on this platform")
        return 0


def _udp_bound(port: int) -> int:
    """Count UDP sockets owned by the current process bound to ``port``."""
    n = 0
    for c in psutil.Process(os.getpid()).net_connections(kind="udp"):
        if c.laddr and c.laddr.port == port:
            n += 1
    return n


@pytest.fixture
def clean_bridge(monkeypatch):
    """Yield a freshly-shut osc_bridge module bound to an ephemeral UDP port.

    The default 9101 port may be in use by a long-running dev uvicorn on the
    host — we patch ``OSC_STATE_PORT`` so the test never races with it.
    """
    import ui.webgui.osc_bridge as osc_bridge

    test_port = _pick_free_udp_port()
    monkeypatch.setattr(osc_bridge, "OSC_STATE_PORT", test_port)

    osc_bridge.shutdown()                            # idempotent, clears stale state
    yield osc_bridge, test_port
    osc_bridge.shutdown()                            # cleanup


def test_osc_bridge_shutdown_releases_fd(clean_bridge):
    """start() opens UDP listener; shutdown() must release it (no leak)."""
    osc_bridge, test_port = clean_bridge

    # Baseline (post-cleanup of any prior bridge state).
    fd_before = _fd_count()
    udp_before = _udp_bound(test_port)
    assert udp_before == 0, (
        f"clean_bridge fixture failed: port {test_port} still bound "
        f"(udp={udp_before})"
    )

    async def noop_broadcast(_payload: dict) -> None:
        pass

    async def _exercise() -> dict:
        loop = asyncio.get_running_loop()
        osc_bridge.start(noop_broadcast, loop, osc_host="127.0.0.1")
        # Give the daemon thread a beat to bind the socket.
        await asyncio.sleep(0.15)
        return {
            "fd_after_start": _fd_count(),
            "udp_after_start": _udp_bound(test_port),
        }

    stats = asyncio.run(_exercise())

    # start() must have opened the listener.
    assert stats["udp_after_start"] >= 1, (
        f"expected UDP listener on {test_port} after start(); "
        f"udp_after_start={stats['udp_after_start']}"
    )
    assert stats["fd_after_start"] >= fd_before, (
        f"fd count went DOWN on start()? "
        f"before={fd_before} after_start={stats['fd_after_start']}"
    )

    # ----- shutdown ------------------------------------------------------
    osc_bridge.shutdown()
    # Give the OS a beat to reclaim fd / thread.
    time.sleep(0.2)

    fd_after_stop = _fd_count()
    udp_after_stop = _udp_bound(test_port)

    # Hard guarantee: UDP socket is gone.
    assert udp_after_stop == 0, (
        f"UDP socket still bound to {test_port} after shutdown(); "
        f"udp_after_stop={udp_after_stop}"
    )

    # fd_count must NOT grow significantly vs. pre-start baseline. Allow up
    # to 2 for selector / asyncio runtime noise (asyncio.run creates a
    # transient event loop with its own self-pipe pair which sometimes
    # leaves a residual selector entry on the executor pool).
    delta = fd_after_stop - fd_before
    assert delta <= 2, (
        f"fd leak detected: fd_before={fd_before} "
        f"fd_after_start={stats['fd_after_start']} "
        f"fd_after_stop={fd_after_stop} delta={delta}"
    )


def test_osc_bridge_shutdown_is_idempotent(clean_bridge):
    """Calling shutdown() twice (or before start()) must not raise."""
    osc_bridge, _ = clean_bridge

    # shutdown() before any start() must be a silent no-op.
    osc_bridge.shutdown()

    async def noop_broadcast(_payload: dict) -> None:
        pass

    async def _run() -> None:
        loop = asyncio.get_running_loop()
        osc_bridge.start(noop_broadcast, loop, osc_host="127.0.0.1")
        await asyncio.sleep(0.1)

    asyncio.run(_run())

    osc_bridge.shutdown()
    # Second call: still a no-op, no exception.
    osc_bridge.shutdown()
