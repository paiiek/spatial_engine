"""B-M5 WebGUI SOFA selector smoke tests.

Two test layers:

1. **Dispatch unit tests** — call ``_dispatch_to_osc`` directly with a
   recording ``osc_send_fn`` (mirrors test_dispatch.py patterns) to verify
   ``binaural_sofa_select`` routes to ``/sys/binaural_sofa_select <name>``.

2. **Catalog API test** — hit the live ``GET /api/hrtf/catalog`` endpoint via
   ``httpx`` / ``requests`` against the ASGI app (no browser needed) and
   assert ≥4 catalog entries are returned.

No playwright dependency is introduced here.  The fuller DOM/WS integration
(selector renders ≥4 options in the browser, onchange fires ctrlSend) lives
in the playwright layer ``playwright/test_dashboard_smoke.py`` and is guarded
by the existing graceful-skip logic there.  This module provides CI-safe
coverage for the dispatch and catalog routes.
"""
from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest

from ui.webgui import server as webgui_server


# ---------------------------------------------------------------------------
# Shared recorder fixture (mirrors test_dispatch.py exactly)
# ---------------------------------------------------------------------------

class _Recorder:
    def __init__(self):
        self.calls = []

    def __call__(self, addr, *args):
        self.calls.append((addr, args))


@pytest.fixture
def recorder(monkeypatch):
    rec = _Recorder()
    monkeypatch.setattr(webgui_server, "osc_send_fn", rec)
    # Force low_latency so the ADR-0013 guard does not block control-plane.
    monkeypatch.setattr(webgui_server, "_bridge_mode", "low_latency")
    return rec


# ---------------------------------------------------------------------------
# 1. Dispatch unit tests
# ---------------------------------------------------------------------------

def test_dispatch_binaural_sofa_select(recorder):
    """B-M5: binaural_sofa_select → /sys/binaural_sofa_select <name>."""
    webgui_server._dispatch_to_osc({"type": "binaural_sofa_select", "name": "kemar"})
    assert recorder.calls == [("/sys/binaural_sofa_select", ("kemar",))]


def test_dispatch_binaural_sofa_select_sadie(recorder):
    """B-M5: another catalog entry round-trips correctly."""
    webgui_server._dispatch_to_osc({"type": "binaural_sofa_select", "name": "sadie2_H08"})
    assert recorder.calls == [("/sys/binaural_sofa_select", ("sadie2_H08",))]


def test_dispatch_binaural_sofa_select_passes_through_ai_mode(monkeypatch):
    """B-M5: binaural_sofa_select is control-plane; not gated by AI mode."""
    rec = _Recorder()
    monkeypatch.setattr(webgui_server, "osc_send_fn", rec)
    monkeypatch.setattr(webgui_server, "_bridge_mode", "ai")
    webgui_server._dispatch_to_osc({"type": "binaural_sofa_select", "name": "hutubs_pp1"})
    assert rec.calls == [("/sys/binaural_sofa_select", ("hutubs_pp1",))], (
        f"binaural_sofa_select is control-plane and must pass through AI mode, "
        f"got: {rec.calls}"
    )


def test_dispatch_binaural_sofa_select_empty_name(recorder):
    """B-M5: missing name key defaults to empty string (safe no-op on engine)."""
    webgui_server._dispatch_to_osc({"type": "binaural_sofa_select"})
    assert recorder.calls == [("/sys/binaural_sofa_select", ("",))]


# ---------------------------------------------------------------------------
# 2. Catalog API — ASGI-level (no browser required)
# ---------------------------------------------------------------------------

def _get_catalog_data():
    """Load catalog.json directly for assertions independent of the HTTP stack."""
    catalog_path = os.path.normpath(
        os.path.join(os.path.dirname(__file__), "..", "..", "..", "assets", "hrtf", "catalog.json")
    )
    with open(catalog_path, encoding="utf-8") as f:
        return json.load(f)


def test_catalog_json_has_at_least_4_entries():
    """B-M5: catalog.json must contain ≥4 HRTF datasets for the selector."""
    data = _get_catalog_data()
    entries = data.get("hrtf_catalog", [])
    assert len(entries) >= 4, (
        f"catalog.json must have ≥4 entries for the selector, got {len(entries)}: "
        f"{[e.get('name') for e in entries]}"
    )


def test_catalog_entries_have_required_fields():
    """B-M5: every catalog entry must have name and display_name."""
    data = _get_catalog_data()
    for entry in data.get("hrtf_catalog", []):
        assert "name" in entry, f"catalog entry missing 'name': {entry}"
        assert "display_name" in entry, f"catalog entry missing 'display_name': {entry}"


@pytest.mark.asyncio
async def test_api_hrtf_catalog_returns_entries():
    """B-M5: GET /api/hrtf/catalog returns ≥4 entries via ASGI (no browser)."""
    httpx = pytest.importorskip("httpx", reason="httpx not installed")
    from httpx import AsyncClient, ASGITransport

    transport = ASGITransport(app=webgui_server.app)
    async with AsyncClient(transport=transport, base_url="http://testserver") as client:
        r = await client.get("/api/hrtf/catalog")
    assert r.status_code == 200, f"expected 200, got {r.status_code}: {r.text}"
    data = r.json()
    entries = data.get("hrtf_catalog", [])
    assert len(entries) >= 4, (
        f"GET /api/hrtf/catalog must return ≥4 entries, got {len(entries)}"
    )
