"""WGUI-S6 / G3a — RTT mock echo (in-process WS ↔ mock OSC :19100 → :19101 ↔ WS).

End-to-end RTT measurement with the mock OSC echo server in the loop:

    WS send  →  server._dispatch_to_osc  →  osc_bridge.send_osc
             →  UDP :19100 (mock echo server)
             →  UDP :19101 (osc_bridge state listener)
             →  osc_bridge._handle_state_message
             →  asyncio.run_coroutine_threadsafe(broadcast_state)
             →  WS recv

Acceptance gate (S6 G3a):
  * N = 200 samples (5 warm-up dropped → 195 measured)
  * p99 RTT < 5 ms

This is the in-process variant: TestClient and the asyncio loop are in the
same Python process as osc_bridge, but the OSC traffic still rides real
UDP sockets (loopback) — that is what makes it a "mock echo" RTT rather
than the pure ``manager.broadcast`` micro-benchmark in ``test_latency.py``.
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
from pythonosc.udp_client import SimpleUDPClient

# Make repo root importable so `ui.webgui...` resolves.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from ui.webgui import osc_bridge  # noqa: E402
from ui.webgui import server as webgui_server  # noqa: E402
from ui.webgui.server import app  # noqa: E402


# ---------------------------------------------------------------------------
# Mock OSC echo server  :19100 → echo back to :19101
# ---------------------------------------------------------------------------

ECHO_LISTEN_PORT = 19100   # where osc_bridge sends (cmd-side mock)
ECHO_TARGET_PORT = 19101   # where osc_bridge listens (state-side mock)


class EchoServer:
    """Bind :19100, re-send every received datagram to :19101 verbatim.

    Verbatim re-send is what makes this an "echo": the bytes that arrive on
    the listen side are exactly the bytes the engine would otherwise have
    emitted, so the resulting RTT and (in G3c) wire-hash semantics are
    unchanged.
    """

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
# Fixture — patch OSC ports BEFORE lifespan, then stand up echo server
# ---------------------------------------------------------------------------

@pytest.fixture
def rtt_client(monkeypatch):
    """TestClient with osc_bridge bound to :19100/:19101 + echo server.

    Strategy (cleaner than mid-lifespan rebind):
      1. Monkeypatch ``osc_bridge.OSC_CMD_PORT`` / ``OSC_STATE_PORT`` BEFORE
         entering the TestClient context manager. Lifespan then naturally
         calls ``osc_bridge.start()`` against the test ports.
      2. ``TestClient(app)`` enters lifespan → SimpleUDPClient targets
         :19100, ThreadingOSCUDPServer binds :19101.
      3. Spin up the EchoServer on :19100 (echoes to :19101).
      4. Teardown: TestClient context exits → lifespan finally →
         osc_bridge.shutdown() releases :19101. We then close the echo.

    Why pre-lifespan monkeypatch instead of shutdown+restart:
      Calling ``shutdown()`` then ``start()`` from a pytest fixture forces
      us to recover the lifespan loop, which is non-trivial across
      starlette versions (portal API churn). Pre-patching the module
      constants is one-line and version-agnostic.
    """
    monkeypatch.setattr(osc_bridge, "OSC_CMD_PORT", ECHO_LISTEN_PORT)
    monkeypatch.setattr(osc_bridge, "OSC_STATE_PORT", ECHO_TARGET_PORT)
    # ADR 0013 — default mode is "ai" which suppresses obj_pos on :9100
    # (vid2spatial owns the wire). For RTT we are the sole producer, so
    # switch to low_latency before lifespan reads the flag.
    monkeypatch.setattr(webgui_server, "_bridge_mode", "low_latency")

    # Echo must be alive BEFORE lifespan binds :19101 only if we cared about
    # ordering, but since we bind :19100 here and osc_bridge binds :19101,
    # the two sockets are independent — order does not matter. We choose
    # echo-first so any startup retry on the listener side sees a target.
    echo = EchoServer()
    try:
        with TestClient(app) as client:
            # Sanity: lifespan must have rebuilt _osc_client against the
            # patched OSC_CMD_PORT (start() reads the module constant fresh).
            assert webgui_server.osc_send_fn is osc_bridge.send_osc, (
                "lifespan did not wire osc_send_fn → osc_bridge.send_osc"
            )
            yield client, echo
    finally:
        echo.close()


# ---------------------------------------------------------------------------
# Percentile helper (nearest-rank, no interpolation — matches test_latency.py)
# ---------------------------------------------------------------------------

def _percentile(values: list[float], pct: float) -> float:
    if not values:
        return float("nan")
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(pct / 100.0 * len(s))) - 1))
    return s[k]


# ---------------------------------------------------------------------------
# G3a — RTT mock echo, p99 < 5 ms over 200 samples
# ---------------------------------------------------------------------------

SAMPLES = 200
WARMUP = 5
P99_GATE_MS = 5.0


def test_rtt_mock_echo_p99_gate(rtt_client):
    """G3a: WS → mock OSC echo → WS round-trip p99 < 5 ms (N=200, warm=5)."""
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
            # Sanity: reply must be a valid JSON broadcast (osc_address+args).
            _ = json.loads(reply)
            rtt_ms_samples.append((t1 - t0) * 1000.0)

    warm = rtt_ms_samples[WARMUP:]
    assert len(warm) >= 100, f"too few warm samples: {len(warm)}"

    p50 = _percentile(warm, 50)
    p95 = _percentile(warm, 95)
    p99 = _percentile(warm, 99)
    pmax = max(warm)

    print(
        f"\n[rtt_mock G3a] N={len(warm)}  p50={p50:.2f}ms  p95={p95:.2f}ms  "
        f"p99={p99:.2f}ms  max={pmax:.2f}ms  echo_recv={echo.recv_count}"
    )

    # Echo must have seen all SAMPLES datagrams (loopback is lossless under
    # normal conditions). Allow a 1-sample slack for the rare scheduler
    # hiccup during teardown.
    assert echo.recv_count >= SAMPLES - 1, (
        f"echo server only saw {echo.recv_count}/{SAMPLES} datagrams"
    )

    assert p99 < P99_GATE_MS, (
        f"G3a p99 RTT {p99:.2f} ms exceeds gate {P99_GATE_MS} ms "
        f"(p50={p50:.2f}, p95={p95:.2f}, max={pmax:.2f})"
    )
