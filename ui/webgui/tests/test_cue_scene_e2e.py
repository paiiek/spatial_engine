"""WGUI-E4 — E-M4 scene extended ops + cue transport TX wire-bytes (SHA256).

End-to-end check that the new E-M4 message types travel from
WebSocket -> server._dispatch_to_osc -> osc_bridge.send_osc -> UDP 9100
with **bit-identical** wire bytes to OscMessageBuilder.

Message types covered:
  scene_rename     → /scene/rename    ,ss  from, to
  scene_duplicate  → /scene/duplicate ,ss  from, to
  scene_delete     → /scene/delete    ,s   name
  scene_meta       → /scene/meta      ,ss  name, meta_json
  cue_go           → /cue/go          ,i   index
  cue_next         → /cue/next        (no args)
  cue_prev         → /cue/prev        (no args)
  cue_stop         → /cue/stop        (no args)

Mirrors test_scene_e2e.py exactly (same fixture, same SHA256 pattern).
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

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from ui.webgui import osc_bridge  # noqa: E402
from ui.webgui import server as webgui_server  # noqa: E402
from ui.webgui.server import app  # noqa: E402


# ---------------------------------------------------------------------------
# Raw UDP capture (copied verbatim from test_scene_e2e.py)
# ---------------------------------------------------------------------------

class RawOscCapture:
    def __init__(self, port: int, host: str = "127.0.0.1") -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.settimeout(0.25)
        self.received: list[bytes] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._loop, daemon=True, name="raw-osc-capture-e4"
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
                break

    def wait_for(self, n: int, timeout: float = 2.0) -> bool:
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


# Use a different capture port than test_scene_e2e.py (19100) to avoid
# conflicts when running the full test suite.
CAPTURE_PORT = 19101


@pytest.fixture
def capture_and_client(monkeypatch):
    capture = RawOscCapture(CAPTURE_PORT)
    try:
        with TestClient(app) as client:
            monkeypatch.setattr(
                osc_bridge,
                "_osc_client",
                SimpleUDPClient("127.0.0.1", CAPTURE_PORT),
            )
            assert webgui_server.osc_send_fn is osc_bridge.send_osc, (
                "lifespan did not wire osc_send_fn → osc_bridge.send_osc"
            )
            yield capture, client
    finally:
        capture.close()


# ---------------------------------------------------------------------------
# Expected wire bytes helper
# ---------------------------------------------------------------------------

def _expected_bytes(address: str, *args) -> bytes:
    b = OscMessageBuilder(address=address)
    for a in args:
        b.add_arg(a)
    return b.build().dgram


def _sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def _assert_tx(capture, client, ws_msg: dict, osc_addr: str, *osc_args):
    """Drive one WS message and assert the captured UDP datagram matches."""
    expected = _expected_bytes(osc_addr, *osc_args)
    expected_hash = _sha256(expected)
    with client.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps(ws_msg))
        assert capture.wait_for(1, timeout=2.0), (
            f"timed out waiting for {osc_addr} datagram on :{CAPTURE_PORT}"
        )
    assert len(capture.received) == 1, (
        f"expected exactly 1 datagram, got {len(capture.received)}"
    )
    got = capture.received[0]
    got_hash = _sha256(got)
    assert got_hash == expected_hash, (
        f"{osc_addr} wire bytes mismatch\n"
        f"  expected sha256: {expected_hash}\n"
        f"  got      sha256: {got_hash}\n"
        f"  expected bytes : {expected!r}\n"
        f"  got      bytes : {got!r}"
    )
    print(f"\n[{osc_addr}] sha256={got_hash}")


# ---------------------------------------------------------------------------
# Scene extended ops
# ---------------------------------------------------------------------------

def test_scene_rename_tx_hash(capture_and_client):
    """`/scene/rename "old" "new"` wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "scene_rename", "from": "old", "to": "new"},
               "/scene/rename", "old", "new")


def test_scene_duplicate_tx_hash(capture_and_client):
    """`/scene/duplicate "src" "dst"` wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "scene_duplicate", "from": "src", "to": "dst"},
               "/scene/duplicate", "src", "dst")


def test_scene_delete_tx_hash(capture_and_client):
    """`/scene/delete "demo"` wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "scene_delete", "name": "demo"},
               "/scene/delete", "demo")


def test_scene_meta_tx_hash(capture_and_client):
    """`/scene/meta "demo" "{}"` wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "scene_meta", "name": "demo", "meta_json": '{"label":"test"}'},
               "/scene/meta", "demo", '{"label":"test"}')


# ---------------------------------------------------------------------------
# Cue transport
# ---------------------------------------------------------------------------

def test_cue_go_tx_hash(capture_and_client):
    """`/cue/go ,i 3` wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "cue_go", "index": 3},
               "/cue/go", 3)


def test_cue_next_tx_hash(capture_and_client):
    """`/cue/next` (no args) wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "cue_next"},
               "/cue/next")


def test_cue_prev_tx_hash(capture_and_client):
    """`/cue/prev` (no args) wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "cue_prev"},
               "/cue/prev")


def test_cue_stop_tx_hash(capture_and_client):
    """`/cue/stop` (no args) wire bytes match OscMessageBuilder SHA256."""
    capture, client = capture_and_client
    _assert_tx(capture, client,
               {"type": "cue_stop"},
               "/cue/stop")
