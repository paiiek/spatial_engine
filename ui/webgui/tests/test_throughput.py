"""WebSocket sustained throughput → 60fps proxy gate.

Phase 1 acceptance:
  HTML5 Canvas — 64 objects drag, rendering ≥ 60fps (Chrome DevTools).

Headless rationale:
  Direct DevTools fps measurement requires a real browser. As a server-side
  proxy, we verify that the WebSocket broadcast pipeline can sustain ≥ 60
  state messages per second per client across 1.0 s. If the server can serve
  60+ msg/s without backpressure or loss, the canvas-side rAF loop has the
  raw input rate it needs to render at 60 fps; rendering itself is the
  browser's responsibility (manually verified per Phase 1 spec).

Two tests:
  - test_burst_throughput_60hz_min: 60 broadcasts inside 1.0 s arrive intact.
  - test_sustained_throughput_120hz_headroom: 120 broadcasts inside 1.0 s
    (target: 2× headroom over 60 fps to absorb GC, scheduling jitter).
"""
from __future__ import annotations

import asyncio
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest
from fastapi.testclient import TestClient

from ui.webgui.server import app, manager


@pytest.fixture
def client():
    return TestClient(app)


def _broadcast_burst(n: int) -> list[float]:
    """Issue n manager.broadcast calls back-to-back; returns per-message walltime ms."""
    deltas: list[float] = []
    for i in range(n):
        payload = {"osc_address": "/adm/obj/1/azim", "args": [float(i)]}
        t0 = time.perf_counter()
        asyncio.run(manager.broadcast(json.dumps(payload)))
        deltas.append((time.perf_counter() - t0) * 1000.0)
    return deltas


def test_burst_throughput_60hz_min(client):
    """60 broadcasts must be deliverable in under 1.0 s wall-clock (≥ 60 Hz)."""
    with client.websocket_connect("/ws") as ws:
        n = 60
        t0 = time.perf_counter()
        deltas = _broadcast_burst(n)
        for _ in range(n):
            ws.receive_text()
        elapsed_s = time.perf_counter() - t0

    rate_hz = n / elapsed_s
    print(
        f"\n[throughput-60] N={n} elapsed={elapsed_s * 1000:.1f}ms rate={rate_hz:.1f}Hz "
        f"per-msg-mean={sum(deltas) / len(deltas):.3f}ms"
    )
    assert elapsed_s < 1.0, f"60 broadcasts took {elapsed_s * 1000:.1f}ms (> 1000ms)"
    assert rate_hz >= 60.0


def test_sustained_throughput_120hz_headroom(client):
    """120 broadcasts inside 1.0 s — 2× headroom above the 60 fps Phase 1 gate."""
    with client.websocket_connect("/ws") as ws:
        n = 120
        t0 = time.perf_counter()
        deltas = _broadcast_burst(n)
        for _ in range(n):
            ws.receive_text()
        elapsed_s = time.perf_counter() - t0

    rate_hz = n / elapsed_s
    print(
        f"\n[throughput-120] N={n} elapsed={elapsed_s * 1000:.1f}ms rate={rate_hz:.1f}Hz "
        f"per-msg-max={max(deltas):.3f}ms"
    )
    assert rate_hz >= 120.0, (
        f"Sustained throughput {rate_hz:.1f}Hz below 2× headroom (120Hz)"
    )


def test_no_message_loss_under_burst(client):
    """All N messages arrive intact and in order under a 60-message burst."""
    n = 60
    received: list[int] = []
    with client.websocket_connect("/ws") as ws:
        for i in range(n):
            payload = {"osc_address": "/adm/obj/1/azim", "args": [float(i)]}
            asyncio.run(manager.broadcast(json.dumps(payload)))
        for _ in range(n):
            raw = ws.receive_text()
            msg = json.loads(raw)
            received.append(int(msg["args"][0]))

    assert received == list(range(n)), (
        f"Message loss/reorder under burst: got {received[:10]}..."
    )
