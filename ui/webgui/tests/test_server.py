"""pytest: FastAPI route tests for spatial_engine WebGUI server."""
from __future__ import annotations

import sys
import os

# Ensure ui/webgui is importable from the repo root
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest
from fastapi.testclient import TestClient

from ui.webgui.server import app


@pytest.fixture
def client():
    return TestClient(app)


# ---------------------------------------------------------------------------
# GET /health
# ---------------------------------------------------------------------------

def test_health_returns_200(client):
    resp = client.get("/health")
    assert resp.status_code == 200


def test_health_body(client):
    resp = client.get("/health")
    body = resp.json()
    assert body["status"] == "ok"
    # /health also reports OSC-bridge readiness + active bridge mode so
    # operators can diagnose "the sound doesn't move" without server logs.
    assert "osc_ready" in body
    assert body["bridge_mode"] in ("ai", "low_latency")


# ---------------------------------------------------------------------------
# WS /ws
# ---------------------------------------------------------------------------

def test_ws_connect(client):
    with client.websocket_connect("/ws") as ws:
        # Connection accepted — no exception means success
        pass


def test_ws_send_obj_pos(client):
    """Client can send obj_pos without server error."""
    import json
    with client.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({
            "type": "obj_pos",
            "n": 1,
            "azim": 45.0,
            "elev": 10.0,
            "dist": 0.5,
        }))
        # No response expected (osc_send_fn is None in test env), just no crash


def test_ws_send_invalid_json(client):
    """Server handles malformed JSON gracefully."""
    with client.websocket_connect("/ws") as ws:
        ws.send_text("not-json{{{")
        # Server logs warning but keeps connection open — no exception here
