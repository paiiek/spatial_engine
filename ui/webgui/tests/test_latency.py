"""WebSocket round-trip latency measurement (informational, not a pass/fail gate)."""
from __future__ import annotations

import json
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest
from fastapi.testclient import TestClient

from ui.webgui.server import app, manager


LATENCY_BUDGET_MS = 50  # acceptable round-trip in test environment


@pytest.fixture
def client():
    return TestClient(app)


def test_broadcast_latency(client):
    """
    Measure time from broadcast_state() call to WS client receive.
    Uses TestClient synchronous WebSocket — measures serialisation + delivery.
    """
    import asyncio

    received: list[float] = []

    with client.websocket_connect("/ws") as ws:
        payload = {"osc_address": "/adm/obj/1/azim", "args": [30.0]}

        t0 = time.perf_counter()
        # Trigger broadcast synchronously via the async helper
        asyncio.get_event_loop().run_until_complete(
            manager.broadcast(json.dumps(payload))
        )
        raw = ws.receive_text()
        t1 = time.perf_counter()

        rtt_ms = (t1 - t0) * 1000
        received.append(rtt_ms)

    assert received, "No message received"
    assert received[0] < LATENCY_BUDGET_MS, (
        f"Round-trip {received[0]:.1f} ms exceeds budget {LATENCY_BUDGET_MS} ms"
    )
