"""WGUI-S2 — Scene Save/Load/List TX wire-bytes E2E (SHA256 verification).

End-to-end check that ``/scene/save``, ``/scene/load`` and ``/scene/list``
travel from WebSocket -> server._dispatch_to_osc -> osc_bridge.send_osc ->
UDP 9100 with **bit-identical** wire bytes to the ADM-OSC standard
``pythonosc.osc_message_builder.OscMessageBuilder`` output.

Reply / RX path is out of scope (ADR-2, deferred).

Why raw UDP capture (no pythonosc parsing)?
  pythonosc decoding could mask single-byte regressions in the wire format
  (typetag, padding, NUL terminator).  Comparing the SHA256 of the captured
  datagram against the SHA256 of the verbatim ADM-OSC reference build catches
  any drift the moment it occurs.

Port strategy:
  osc_bridge.send_osc uses ``_osc_client = SimpleUDPClient(host, 9100)``.  To
  avoid colliding with a running spatial_engine on :9100 (and to make the
  test pytest-xdist safe in principle), we monkeypatch
  ``osc_bridge._osc_client`` to point at an ephemeral port that our raw
  capture socket bound first.  ``SimpleUDPClient`` is send-only and never
  bind()s the local port, so swapping the instance is sufficient.
"""
from __future__ import annotations

import hashlib
import json
import os
import socket
import sys
import threading
import time

import pytest
from fastapi.testclient import TestClient
from pythonosc.osc_message_builder import OscMessageBuilder
from pythonosc.udp_client import SimpleUDPClient

# Make repo root importable so `ui.webgui...` resolves when running pytest
# from any cwd (mirrors the pattern used by test_dispatch.py / test_server.py).
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from ui.webgui import osc_bridge  # noqa: E402
from ui.webgui import server as webgui_server  # noqa: E402
from ui.webgui.server import app  # noqa: E402


# ---------------------------------------------------------------------------
# Raw UDP capture — record wire bytes verbatim, never decode
# ---------------------------------------------------------------------------

class RawOscCapture:
    """Background thread that bind()s a UDP port and records raw datagrams.

    Used as a wire tap on the spatial_engine cmd port so we can SHA256-compare
    the bytes against the canonical ADM-OSC encoding.
    """

    def __init__(self, port: int, host: str = "127.0.0.1") -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # SO_REUSEADDR so back-to-back tests do not hit TIME_WAIT on the port.
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.settimeout(0.25)
        self.received: list[bytes] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._loop, daemon=True, name="raw-osc-capture"
        )
        self._thread.start()

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                data, _addr = self.sock.recvfrom(8192)
                self.received.append(data)
            except socket.timeout:
                continue
            except OSError:
                # Socket closed during teardown.
                break

    def wait_for(self, n: int, timeout: float = 2.0) -> bool:
        """Block until at least ``n`` datagrams captured (or timeout)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if len(self.received) >= n:
                return True
            time.sleep(0.01)
        return False

    def close(self) -> None:
        self._stop.set()
        try:
            self.sock.close()
        except OSError:
            pass
        self._thread.join(timeout=1.0)


# Use an ephemeral high port to avoid colliding with a real spatial_engine
# instance on :9100.  Capture binds; SimpleUDPClient does not bind, so port
# reuse is not an issue on the send side.
CAPTURE_PORT = 19100


@pytest.fixture
def capture_and_client(monkeypatch):
    """Stand up raw UDP capture on CAPTURE_PORT and a TestClient with lifespan.

    Order matters: bind() the capture socket BEFORE entering the TestClient
    context manager so that even if osc_bridge.start() fires immediately we
    do not race against it (capture is passive — it can be ready earlier
    without side effects).
    """
    capture = RawOscCapture(CAPTURE_PORT)
    try:
        # Enter lifespan → osc_bridge.start() builds a SimpleUDPClient on 9100.
        with TestClient(app) as client:
            # Redirect the client to our capture port. SimpleUDPClient is
            # send-only (no bind), so we can safely build a fresh instance
            # targeting CAPTURE_PORT and swap it in.
            monkeypatch.setattr(
                osc_bridge,
                "_osc_client",
                SimpleUDPClient("127.0.0.1", CAPTURE_PORT),
            )
            # Sanity: server.py's osc_send_fn must already be wired to
            # osc_bridge.send_osc by lifespan (otherwise the WS dispatch
            # path is a no-op and the test would silently pass with 0
            # captured datagrams — see assertions below).
            assert webgui_server.osc_send_fn is osc_bridge.send_osc, (
                "lifespan did not wire osc_send_fn → osc_bridge.send_osc"
            )
            yield capture, client
    finally:
        capture.close()


# ---------------------------------------------------------------------------
# Expected wire bytes — built from the canonical ADM-OSC encoder
# ---------------------------------------------------------------------------

def _expected_bytes(address: str, *args) -> bytes:
    """Canonical ADM-OSC datagram for ``address`` + ``args``.

    Uses pythonosc.OscMessageBuilder directly (the same encoder
    SimpleUDPClient.send_message uses internally), so a match here proves
    osc_bridge.send_osc forwards args unchanged and pythonosc has not
    silently changed its encoding between versions.
    """
    b = OscMessageBuilder(address=address)
    for a in args:
        b.add_arg(a)
    return b.build().dgram


def _sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_scene_save_tx_hash(capture_and_client):
    """`/scene/save "demo"` wire bytes match ADM-OSC standard SHA256."""
    capture, client = capture_and_client
    expected = _expected_bytes("/scene/save", "demo")
    expected_hash = _sha256(expected)

    with client.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"type": "scene_save", "name": "demo"}))
        assert capture.wait_for(1, timeout=2.0), (
            f"timed out waiting for /scene/save datagram on :{CAPTURE_PORT}"
        )

    assert len(capture.received) == 1, (
        f"expected exactly 1 datagram, got {len(capture.received)}"
    )
    got = capture.received[0]
    got_hash = _sha256(got)
    assert got_hash == expected_hash, (
        f"/scene/save wire bytes mismatch\n"
        f"  expected sha256: {expected_hash}\n"
        f"  got      sha256: {got_hash}\n"
        f"  expected bytes : {expected!r}\n"
        f"  got      bytes : {got!r}"
    )
    print(f"\n[scene_save] sha256={got_hash}")


def test_scene_load_tx_hash(capture_and_client):
    """`/scene/load "demo"` wire bytes match ADM-OSC standard SHA256."""
    capture, client = capture_and_client
    expected = _expected_bytes("/scene/load", "demo")
    expected_hash = _sha256(expected)

    with client.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"type": "scene_load", "name": "demo"}))
        assert capture.wait_for(1, timeout=2.0), (
            f"timed out waiting for /scene/load datagram on :{CAPTURE_PORT}"
        )

    assert len(capture.received) == 1, (
        f"expected exactly 1 datagram, got {len(capture.received)}"
    )
    got = capture.received[0]
    got_hash = _sha256(got)
    assert got_hash == expected_hash, (
        f"/scene/load wire bytes mismatch\n"
        f"  expected sha256: {expected_hash}\n"
        f"  got      sha256: {got_hash}\n"
        f"  expected bytes : {expected!r}\n"
        f"  got      bytes : {got!r}"
    )
    print(f"\n[scene_load] sha256={got_hash}")


def test_scene_list_tx_only(capture_and_client):
    """`/scene/list` (no args) wire bytes match ADM-OSC standard SHA256.

    Validates the empty-typetag path (",\\0\\0\\0" 4-byte aligned).  RX is
    out of scope (ADR-2), so we only assert the TX datagram is well-formed.
    """
    capture, client = capture_and_client
    expected = _expected_bytes("/scene/list")
    expected_hash = _sha256(expected)

    with client.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"type": "scene_list"}))
        assert capture.wait_for(1, timeout=2.0), (
            f"timed out waiting for /scene/list datagram on :{CAPTURE_PORT}"
        )

    assert len(capture.received) == 1, (
        f"expected exactly 1 datagram, got {len(capture.received)}"
    )
    got = capture.received[0]
    got_hash = _sha256(got)
    assert got_hash == expected_hash, (
        f"/scene/list wire bytes mismatch\n"
        f"  expected sha256: {expected_hash}\n"
        f"  got      sha256: {got_hash}\n"
        f"  expected bytes : {expected!r}\n"
        f"  got      bytes : {got!r}"
    )
    print(f"\n[scene_list] sha256={got_hash}")
