"""WGUI-S6 / G3b — RTT live UDP echo (looser gate than G3a, p99 < 20 ms).

Same wire path as ``test_rtt_mock.py`` but with the production
acceptance threshold (Phase-1 contract: WebGUI ↔ OSC ↔ WebGUI p99 < 20 ms).

Why a separate test from G3a?
  G3a (5 ms) is a regression alarm: if the mock-echo RTT drifts past
  5 ms in CI, something has slowed down even though the contract is
  still met.  G3b (20 ms) is the *falsifiable shipping gate* — it is
  what users actually depend on.  Keeping both expresses intent
  clearly in test names and lets each fail with a meaningful message.

Pipeline:
    WS send  →  server._dispatch_to_osc  →  osc_bridge.send_osc
             →  UDP :19100 (echo server bind)
             →  UDP :19101 (osc_bridge state listener)
             →  asyncio.run_coroutine_threadsafe(broadcast_state)
             →  WS recv
"""
from __future__ import annotations

import json
import os
import socket
import sys
import threading
import time

import pytest
from fastapi.testclient import TestClient

# Make repo root importable so `ui.webgui...` resolves.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from ui.webgui import osc_bridge  # noqa: E402
from ui.webgui import server as webgui_server  # noqa: E402
from ui.webgui.server import app  # noqa: E402


# ---------------------------------------------------------------------------
# Mock OSC echo server  :19100 → echo back to :19101
# ---------------------------------------------------------------------------

ECHO_LISTEN_PORT = 19100
ECHO_TARGET_PORT = 19101


class EchoServer:
    """Bind :19100, re-send every received datagram to :19101 verbatim."""

    def __init__(self, listen_port: int = ECHO_LISTEN_PORT,
                 target_port: int = ECHO_TARGET_PORT,
                 host: str = "127.0.0.1") -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, listen_port))
        self.sock.settimeout(0.1)
        self.target = (host, target_port)
        self.received_bytes: list[bytes] = []
        self.recv_count = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._loop, daemon=True, name="mock-osc-echo"
        )
        self._thread.start()

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                data, _addr = self.sock.recvfrom(8192)
            except socket.timeout:
                continue
            except OSError:
                break
            self.received_bytes.append(data)
            self.recv_count += 1
            try:
                self.sock.sendto(data, self.target)
            except OSError:
                pass

    def close(self) -> None:
        self._stop.set()
        try:
            self.sock.close()
        except OSError:
            pass
        self._thread.join(timeout=1.0)


# ---------------------------------------------------------------------------
# Fixture — same pattern as test_rtt_mock.py
# ---------------------------------------------------------------------------

@pytest.fixture
def rtt_client(monkeypatch):
    monkeypatch.setattr(osc_bridge, "OSC_CMD_PORT", ECHO_LISTEN_PORT)
    monkeypatch.setattr(osc_bridge, "OSC_STATE_PORT", ECHO_TARGET_PORT)
    monkeypatch.setattr(webgui_server, "_bridge_mode", "low_latency")

    echo = EchoServer()
    try:
        with TestClient(app) as client:
            assert webgui_server.osc_send_fn is osc_bridge.send_osc
            yield client, echo
    finally:
        echo.close()


def _percentile(values: list[float], pct: float) -> float:
    if not values:
        return float("nan")
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(pct / 100.0 * len(s))) - 1))
    return s[k]


# ---------------------------------------------------------------------------
# G3b — RTT live UDP, p99 < 20 ms over 200 samples
# ---------------------------------------------------------------------------

SAMPLES = 200
WARMUP = 5
P99_GATE_MS = 20.0


def test_rtt_udp_live_p99_gate(rtt_client):
    """G3b: WS → live UDP echo → WS round-trip p99 < 20 ms (N=200, warm=5)."""
    client, echo = rtt_client

    rtt_ms_samples: list[float] = []
    with client.websocket_connect("/ws") as ws:
        for i in range(SAMPLES):
            azim = float(i % 360)
            payload = {
                "type": "obj_pos",
                "n": 1,
                "azim": azim,
                "elev": 0.0,
                "dist": 0.5,
            }
            t0 = time.perf_counter()
            ws.send_text(json.dumps(payload))
            reply = ws.receive_text()
            t1 = time.perf_counter()
            _ = json.loads(reply)
            rtt_ms_samples.append((t1 - t0) * 1000.0)

    warm = rtt_ms_samples[WARMUP:]
    assert len(warm) >= 100, f"too few warm samples: {len(warm)}"

    p50 = _percentile(warm, 50)
    p95 = _percentile(warm, 95)
    p99 = _percentile(warm, 99)
    pmax = max(warm)

    print(
        f"\n[rtt_udp_live G3b] N={len(warm)}  p50={p50:.2f}ms  p95={p95:.2f}ms  "
        f"p99={p99:.2f}ms  max={pmax:.2f}ms  echo_recv={echo.recv_count}"
    )

    assert echo.recv_count >= SAMPLES - 1, (
        f"echo server only saw {echo.recv_count}/{SAMPLES} datagrams"
    )

    assert p99 < P99_GATE_MS, (
        f"G3b p99 RTT {p99:.2f} ms exceeds gate {P99_GATE_MS} ms "
        f"(p50={p50:.2f}, p95={p95:.2f}, max={pmax:.2f})"
    )
