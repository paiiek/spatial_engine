"""pytest: trajectory API endpoint tests."""
from __future__ import annotations

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest
from fastapi.testclient import TestClient

from ui.webgui.server import app


@pytest.fixture
def client():
    with TestClient(app) as c:
        yield c


def test_trajectory_start_circle(client):
    resp = client.post("/api/trajectory/start", json={
        "obj_id": 0, "shape": "circle", "speed_hz": 1.0, "radius": 1.0
    })
    assert resp.status_code == 200
    assert resp.json()["ok"] is True


def test_trajectory_list(client):
    client.post("/api/trajectory/start", json={"obj_id": 7, "shape": "circle", "speed_hz": 0.5})
    resp = client.get("/api/trajectory/list")
    assert resp.status_code == 200
    items = resp.json()
    ids = [i["obj_id"] for i in items]
    assert 7 in ids


def test_trajectory_stop(client):
    client.post("/api/trajectory/start", json={"obj_id": 9, "shape": "line", "speed_hz": 1.0})
    resp = client.post("/api/trajectory/stop", json={"obj_id": 9})
    assert resp.status_code == 200
    assert resp.json()["ok"] is True
    items = client.get("/api/trajectory/list").json()
    assert all(i["obj_id"] != 9 for i in items)


def test_trajectory_start_invalid_shape(client):
    resp = client.post("/api/trajectory/start", json={"obj_id": 0, "shape": "spiral"})
    assert resp.status_code == 400


def test_trajectory_start_lissajous(client):
    resp = client.post("/api/trajectory/start", json={
        "obj_id": 2, "shape": "lissajous", "speed_hz": 1.0, "lissajous_ratio": 3.0
    })
    assert resp.status_code == 200
