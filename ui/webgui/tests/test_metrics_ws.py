"""ui/webgui/tests/test_metrics_ws.py — v0.9 A-M3 (AC4).

Verifies the dashboard telemetry channel wired into ``ui.webgui.server``:

  * MetricsHub + /ws/metrics: classified metrics/warning broadcasts reach a
    /ws/metrics subscriber.
  * latest-wins snapshot cache (DD-D, queue size 1): a client connecting AFTER
    a snapshot was cached receives the last snapshot immediately on connect.
  * /sys/state (shm) + raw addresses do NOT appear on /ws/metrics — they stay
    on the existing /ws control plane (m1 decision).

We drive the server's routing entry point ``broadcast_state`` (where the
osc_bridge ``_broadcast_fn`` lands) and observe what each WS channel receives
via FastAPI's ``TestClient``.

Plan reference: spatial-engine-v0.9-laneA-metrics-dashboard.md §A-M3 / AC4.
"""
from __future__ import annotations

import asyncio
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest
from fastapi.testclient import TestClient

import ui.webgui.server as server
from ui.webgui.server import app, broadcast_state, metrics_hub


@pytest.fixture
def client():
    return TestClient(app)


@pytest.fixture(autouse=True)
def _reset_hub():
    """Each test starts with a pristine MetricsHub (no leaked snapshot/subs)."""
    metrics_hub.active.clear()
    metrics_hub._last_snapshot = None
    yield
    metrics_hub.active.clear()
    metrics_hub._last_snapshot = None


def test_metrics_broadcast_received(client):
    """(1) A mock metrics broadcast is received by a /ws/metrics client."""
    with client.websocket_connect("/ws/metrics") as ws:
        asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 42}))
        msg = json.loads(ws.receive_text())
    assert msg == {"type": "metrics", "cpu_pct": 42}


def test_warning_routes_to_metrics(client):
    """(3) warning messages route to /ws/metrics too (A-M2: type 'warning')."""
    with client.websocket_connect("/ws/metrics") as ws:
        asyncio.run(broadcast_state(
            {"type": "warning", "ts": 1.0, "category": "rt_timing", "payload": 0.9}
        ))
        msg = json.loads(ws.receive_text())
    assert msg["type"] == "warning"
    assert msg["category"] == "rt_timing"
    assert msg["payload"] == 0.9


def test_fresh_connect_receives_cached_snapshot(client):
    """(2) A client connecting AFTER a snapshot was cached gets it immediately."""
    # Cache a snapshot with no subscribers attached.
    asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 71, "xrun_count": 4}))
    assert len(metrics_hub.active) == 0  # nobody was listening

    # A fresh connect must immediately receive the last cached snapshot.
    with client.websocket_connect("/ws/metrics") as ws:
        msg = json.loads(ws.receive_text())
    assert msg == {"type": "metrics", "cpu_pct": 71, "xrun_count": 4}


def test_latest_wins_single_slot(client):
    """DD-D: cache is latest-wins single slot — newest snapshot served on connect."""
    asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 10}))
    asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 20}))
    asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 30}))
    with client.websocket_connect("/ws/metrics") as ws:
        msg = json.loads(ws.receive_text())
    assert msg == {"type": "metrics", "cpu_pct": 30}  # only latest survives


def test_state_raw_does_not_reach_metrics(client):
    """(4) /sys/state shm raw payloads do NOT appear on /ws/metrics."""
    with client.websocket_connect("/ws/metrics") as ws_metrics, \
         client.websocket_connect("/ws") as ws_ctrl:
        # Raw shm-style payload (no 'type' key) → control plane only.
        raw = {"osc_address": "/sys/state", "args": ["shm_producer_alive=1"]}
        asyncio.run(broadcast_state(raw))
        # Control plane receives it.
        ctrl_msg = json.loads(ws_ctrl.receive_text())
        assert ctrl_msg == raw
        # Metrics channel must NOT have received it; a follow-up metrics
        # broadcast is what the metrics client actually sees next.
        asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 5}))
        metrics_msg = json.loads(ws_metrics.receive_text())
    assert metrics_msg == {"type": "metrics", "cpu_pct": 5}


def test_metrics_does_not_leak_to_control_plane(client):
    """Symmetry check: metrics broadcasts do NOT land on the /ws control plane."""
    with client.websocket_connect("/ws") as ws_ctrl, \
         client.websocket_connect("/ws/metrics") as ws_metrics:
        asyncio.run(broadcast_state({"type": "metrics", "cpu_pct": 99}))
        # Metrics client receives it.
        assert json.loads(ws_metrics.receive_text()) == {"type": "metrics", "cpu_pct": 99}
        # Control plane sees a raw state message but never the metrics one.
        raw = {"osc_address": "/sys/state", "args": ["shm_consumer_locked=1"]}
        asyncio.run(broadcast_state(raw))
        assert json.loads(ws_ctrl.receive_text()) == raw


def test_dashboard_route_registered():
    """/dashboard route is REGISTERED now (A-M3); real HTML lands in A-M4.

    Tolerant of A-M4 not being done: we assert the route exists in the app and
    that hitting it returns either 200 (placeholder/real file present) or a
    graceful 404 (file not yet shipped) — never a 500 / unregistered route.
    """
    paths = {route.path for route in app.routes}
    assert "/dashboard" in paths, "/dashboard route must be registered in A-M3"

    c = TestClient(app)
    resp = c.get("/dashboard")
    assert resp.status_code in (200, 404), (
        f"/dashboard returned {resp.status_code}; expected 200 (file present) "
        f"or 404 (file deferred to A-M4)"
    )
