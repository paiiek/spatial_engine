"""WGUI-S6 / G3c — RTT e2e wire-bytes SHA256 + p99 (CRITICAL — Critic N6).

End-to-end RTT plus per-sample wire-bytes SHA256 verification:

    WS send  →  server._dispatch_to_osc  →  osc_bridge.send_osc
             →  UDP :19100 (echo records raw datagram BYTES)
             →  UDP :19101 (osc_bridge listener)
             →  WS recv

For every one of N=200 samples the test asserts:
  * SHA256(captured datagram) == SHA256(canonical ADM-OSC build)
    (i.e. ``/adm/obj/{n}/aed`` typetag ``,fff`` + 3 × float32 verbatim)
  * round-trip latency contributes to a p99 < 30 ms aggregate

A single hash mismatch fails the run.  This is the falsifiable "wire is
ADM-OSC" claim: the engine receives the *exact* bytes ``pythonosc``
would produce, byte-for-byte, across 200 randomised samples.

Acceptance gate (S6 G3c, CRITICAL):
  * N = 200 samples (5 warm-up dropped → 195 measured for p99)
  * 100 % SHA256 match across ALL 200 samples (warmup included — warmup
    only affects timing distribution, not wire correctness)
  * p99 RTT < 30 ms
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
from ui.webgui.tests.wire_hash_compare import (  # noqa: E402
    compare,
    expected_aed_bytes,
)


# ---------------------------------------------------------------------------
# Mock OSC echo server  :19100 → echo back to :19101
# ---------------------------------------------------------------------------

ECHO_LISTEN_PORT = 19100
ECHO_TARGET_PORT = 19101


class EchoServer:
    """Bind :19100, append received bytes to a list, echo to :19101.

    The per-sample bytes-list grows monotonically; ``test_rtt_e2e_wire`` reads
    it by index after each WS round-trip so the hash compare is naturally
    sequential (no per-sample race with the echo thread — the WS recv
    handshake already enforces happens-before for the matching datagram).
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
            target=self._loop, daemon=True, name="mock-osc-echo-wire"
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
# Fixture
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
# G3c — RTT + 100 % SHA256 wire-bytes match (CRITICAL)
# ---------------------------------------------------------------------------

SAMPLES = 200
WARMUP = 5
P99_GATE_MS = 30.0


def test_rtt_e2e_wire_sha256_and_p99(rtt_client):
    """G3c: per-sample wire-bytes SHA256 match (200/200) + p99 < 30 ms.

    Per-sample randomisation:
      ``azim`` cycles 0..359 deg, ``elev`` 0, ``dist`` 0.5 → 200 distinct
      datagrams. We hash each datagram against the canonical
      ``pythonosc.OscMessageBuilder`` output for those exact values.
    """
    client, echo = rtt_client

    rtt_ms_samples: list[float] = []
    hash_mismatches: list[tuple[int, str, str]] = []
    matched = 0

    with client.websocket_connect("/ws") as ws:
        for i in range(SAMPLES):
            azim = float(i % 360)
            elev = 0.0
            dist = 0.5
            n = 1

            expected = expected_aed_bytes(n=n, azim=azim, elev=elev, dist=dist)

            t0 = time.perf_counter()
            ws.send_text(json.dumps({
                "type": "obj_pos", "n": n,
                "azim": azim, "elev": elev, "dist": dist,
            }))
            reply = ws.receive_text()
            t1 = time.perf_counter()
            _ = json.loads(reply)
            rtt_ms_samples.append((t1 - t0) * 1000.0)

            # The echo thread appended the datagram before the WS reply
            # could be broadcast (broadcast is downstream of the echo →
            # listener → handler chain), so by the time ws.receive_text()
            # returns we are guaranteed echo.received_bytes[i] exists.
            assert len(echo.received_bytes) >= i + 1, (
                f"sample {i}: echo missing datagram "
                f"(have {len(echo.received_bytes)}, need ≥ {i + 1})"
            )
            actual = echo.received_bytes[i]
            ok, eh, ah = compare(expected, actual, debug=(len(hash_mismatches) == 0))
            if ok:
                matched += 1
            else:
                hash_mismatches.append((i, eh, ah))

    warm = rtt_ms_samples[WARMUP:]
    assert len(warm) >= 100, f"too few warm samples: {len(warm)}"

    p50 = _percentile(warm, 50)
    p95 = _percentile(warm, 95)
    p99 = _percentile(warm, 99)
    pmax = max(warm)

    print(
        f"\n[rtt_e2e_wire G3c] N={len(warm)}  p50={p50:.2f}ms  p95={p95:.2f}ms  "
        f"p99={p99:.2f}ms  max={pmax:.2f}ms  "
        f"sha256_match={matched}/{SAMPLES}  echo_recv={echo.recv_count}"
    )

    # 100 % wire-bytes match is the CRITICAL gate (Critic N6).
    assert not hash_mismatches, (
        f"G3c SHA256 mismatch on {len(hash_mismatches)}/{SAMPLES} samples; "
        f"first mismatch: idx={hash_mismatches[0][0]} "
        f"expected={hash_mismatches[0][1]} got={hash_mismatches[0][2]}"
    )
    assert matched == SAMPLES, (
        f"G3c expected {SAMPLES}/{SAMPLES} hash match, got {matched}"
    )

    assert p99 < P99_GATE_MS, (
        f"G3c p99 RTT {p99:.2f} ms exceeds gate {P99_GATE_MS} ms "
        f"(p50={p50:.2f}, p95={p95:.2f}, max={pmax:.2f})"
    )
