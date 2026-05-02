"""WebSocket round-trip latency measurements.

Phase 1 acceptance gate (.omc/plans/spatial-engine-commercialization-v1.md):
  WebGUI ↔ OSC 왕복 지연 p99 < 20ms.

Two tests:
  - test_broadcast_latency_singleshot: warm-up smoke (informational, < 50ms).
  - test_broadcast_latency_p99_gate: 200-sample distribution → p99 < 20ms.
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


SINGLE_SHOT_BUDGET_MS = 50.0   # smoke (cold path)
P99_GATE_MS = 20.0             # Phase 1 acceptance
P95_GATE_MS = 10.0             # informational: should be well below p99
SAMPLES = 200                  # statistically meaningful for p99


@pytest.fixture
def client():
    return TestClient(app)


def _percentile(values: list[float], pct: float) -> float:
    """Nearest-rank percentile (no interpolation) — robust for small N."""
    if not values:
        return float("nan")
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(pct / 100.0 * len(s))) - 1))
    return s[k]


def test_broadcast_latency_singleshot(client):
    """Smoke test: a single broadcast must complete within the loose 50 ms budget."""
    with client.websocket_connect("/ws") as ws:
        payload = {"osc_address": "/adm/obj/1/azim", "args": [30.0]}
        t0 = time.perf_counter()
        asyncio.run(manager.broadcast(json.dumps(payload)))
        ws.receive_text()
        t1 = time.perf_counter()
        rtt_ms = (t1 - t0) * 1000
    assert rtt_ms < SINGLE_SHOT_BUDGET_MS, (
        f"Single-shot RTT {rtt_ms:.2f} ms > {SINGLE_SHOT_BUDGET_MS} ms"
    )


def test_broadcast_latency_p99_gate(client):
    """
    Phase 1 gate: 200-sample WS broadcast RTT distribution → p99 < 20 ms.

    Excludes the first 5 samples as warm-up (TestClient + asyncio loop spin-up).
    """
    rtt_ms_samples: list[float] = []
    with client.websocket_connect("/ws") as ws:
        for i in range(SAMPLES):
            payload = {"osc_address": "/adm/obj/1/azim", "args": [float(i)]}
            t0 = time.perf_counter()
            asyncio.run(manager.broadcast(json.dumps(payload)))
            ws.receive_text()
            t1 = time.perf_counter()
            rtt_ms_samples.append((t1 - t0) * 1000.0)

    warm = rtt_ms_samples[5:]  # skip warm-up
    assert len(warm) >= 100, f"Too few warm samples: {len(warm)}"

    p50 = _percentile(warm, 50)
    p95 = _percentile(warm, 95)
    p99 = _percentile(warm, 99)
    pmax = max(warm)

    print(
        f"\n[latency] N={len(warm)}  p50={p50:.2f}ms  p95={p95:.2f}ms  "
        f"p99={p99:.2f}ms  max={pmax:.2f}ms"
    )

    assert p99 < P99_GATE_MS, (
        f"p99 RTT {p99:.2f} ms exceeds Phase 1 gate {P99_GATE_MS} ms "
        f"(p50={p50:.2f}, p95={p95:.2f}, max={pmax:.2f})"
    )
    assert p95 < P95_GATE_MS, (
        f"p95 RTT {p95:.2f} ms exceeds advisory threshold {P95_GATE_MS} ms"
    )
