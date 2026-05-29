"""ui/webgui/tests/test_osc_bridge_dashboard.py — v0.9 A-M2 (AC3).

Verifies the address-based classification added to
``ui.webgui.osc_bridge._handle_state_message``:

  * /sys/metrics            → typed metrics dict ({type:"metrics", ...})
  * /sys/warning,
    /sys/binaural_warning   → warning dict ({type:"warning", ts, category, payload})
  * /sys/state (shm)        → raw fallthrough (m1 decision — stays on /ws)
  * unknown addresses       → raw {osc_address, args} fallthrough

We drive the handler directly (as the existing tests do) and capture what the
injected ``_broadcast_fn`` is called with, by wiring a tiny event loop on a
background thread (the handler uses ``run_coroutine_threadsafe``).

Plan reference: spatial-engine-v0.9-laneA-metrics-dashboard.md §A-M2 / AC3.
"""
from __future__ import annotations

import asyncio
import os
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest

import ui.webgui.osc_bridge as osc_bridge


class _BroadcastRecorder:
    """Async-callable that records every payload it is broadcast."""

    def __init__(self):
        self.payloads = []

    async def __call__(self, payload: dict) -> None:
        self.payloads.append(payload)


@pytest.fixture
def wired_bridge(monkeypatch):
    """Wire osc_bridge with a real (background) event loop + recording broadcast.

    ``_handle_state_message`` hands the payload to the loop via
    ``run_coroutine_threadsafe`` — so we need a live loop running off-thread.
    """
    rec = _BroadcastRecorder()

    loop = asyncio.new_event_loop()
    ready = threading.Event()

    def _run_loop():
        asyncio.set_event_loop(loop)
        loop.call_soon(ready.set)
        loop.run_forever()

    t = threading.Thread(target=_run_loop, daemon=True, name="test-loop")
    t.start()
    ready.wait(timeout=2.0)

    monkeypatch.setattr(osc_bridge, "_loop", loop)
    monkeypatch.setattr(osc_bridge, "_broadcast_fn", rec)

    yield rec, loop

    loop.call_soon_threadsafe(loop.stop)
    t.join(timeout=2.0)
    loop.close()


def _emit(loop, address, *args):
    """Invoke the handler and wait for the queued coroutine to drain."""
    osc_bridge._handle_state_message(address, *args)
    # Bounce a no-op through the loop to ensure the scheduled broadcast ran.
    fut = asyncio.run_coroutine_threadsafe(asyncio.sleep(0), loop)
    fut.result(timeout=2.0)


# ---------------------------------------------------------------------------
# /sys/metrics → typed metrics dict
# ---------------------------------------------------------------------------

def test_metrics_classified(wired_bridge):
    rec, loop = wired_bridge
    # Engine emits ONE key=value per message (MetricsEmit.h).
    _emit(loop, "/sys/metrics", "cpu_pct=42")
    _emit(loop, "/sys/metrics", "cpu_peak_pct=87")
    _emit(loop, "/sys/metrics", "p99_us=1234")
    _emit(loop, "/sys/metrics", "xrun_count=3")
    _emit(loop, "/sys/metrics", "engine_overrun_count=5")
    _emit(loop, "/sys/metrics", "binaural_demote_count=1")

    assert rec.payloads == [
        {"type": "metrics", "cpu_pct": 42},
        {"type": "metrics", "cpu_peak_pct": 87},
        {"type": "metrics", "p99_us": 1234},
        {"type": "metrics", "xrun_count": 3},
        {"type": "metrics", "engine_overrun_count": 5},
        {"type": "metrics", "binaural_demote_count": 1},
    ]
    # Values are typed ints, not strings.
    assert all(isinstance(p[k], int)
               for p in rec.payloads for k in p if k != "type")


def test_metrics_multiple_pairs_in_one_message(wired_bridge):
    """Defensive: tolerate >1 key=value pair in a single message."""
    rec, loop = wired_bridge
    _emit(loop, "/sys/metrics", "cpu_pct=10", "xrun_count=2")
    assert rec.payloads == [{"type": "metrics", "cpu_pct": 10, "xrun_count": 2}]


def test_metrics_malformed_payload_no_crash(wired_bridge):
    rec, loop = wired_bridge
    # Missing keys (only one present), junk value, unknown key, bare token.
    _emit(loop, "/sys/metrics", "cpu_pct=not_a_number")
    _emit(loop, "/sys/metrics", "totally_unknown_key=99")
    _emit(loop, "/sys/metrics", "no_equals_sign")
    _emit(loop, "/sys/metrics", "p99_us=500")  # partial valid dict

    # No exception raised; partial dicts forwarded.
    assert rec.payloads[0] == {"type": "metrics", "cpu_pct": "not_a_number"}
    assert rec.payloads[1] == {"type": "metrics", "totally_unknown_key": "99"}
    assert rec.payloads[2] == {"type": "metrics"}  # bare token skipped
    assert rec.payloads[3] == {"type": "metrics", "p99_us": 500}


# ---------------------------------------------------------------------------
# /sys/warning + /sys/binaural_warning → warning dict
# ---------------------------------------------------------------------------

def test_binaural_warning_classified(wired_bridge):
    rec, loop = wired_bridge
    _emit(loop, "/sys/binaural_warning", "rt_timing_unavailable", 0.95)
    assert len(rec.payloads) == 1
    p = rec.payloads[0]
    assert p["type"] == "warning"
    assert p["category"] == "rt_timing_unavailable"
    assert p["payload"] == 0.95
    assert isinstance(p["ts"], float)


def test_warning_classified(wired_bridge):
    rec, loop = wired_bridge
    _emit(loop, "/sys/warning", "some_category", 1.0)
    p = rec.payloads[0]
    assert p["type"] == "warning"
    assert p["category"] == "some_category"
    assert p["payload"] == 1.0


def test_warning_missing_trailing_arg_no_crash(wired_bridge):
    rec, loop = wired_bridge
    _emit(loop, "/sys/binaural_warning", "lonely_category")
    p = rec.payloads[0]
    assert p["type"] == "warning"
    assert p["category"] == "lonely_category"
    assert p["payload"] is None


# ---------------------------------------------------------------------------
# /sys/state (shm) → raw fallthrough (m1 decision)
# ---------------------------------------------------------------------------

def test_state_raw_fallthrough(wired_bridge):
    rec, loop = wired_bridge
    _emit(loop, "/sys/state", "shm_producer_alive=1")
    assert rec.payloads == [
        {"osc_address": "/sys/state", "args": ["shm_producer_alive=1"]},
    ]


# ---------------------------------------------------------------------------
# unknown address → raw fallthrough (backward compat)
# ---------------------------------------------------------------------------

def test_unknown_address_raw_fallthrough(wired_bridge):
    rec, loop = wired_bridge
    _emit(loop, "/adm/obj/5/aed", 30.0, 10.0, 0.7)
    assert rec.payloads == [
        {"osc_address": "/adm/obj/5/aed", "args": [30.0, 10.0, 0.7]},
    ]
