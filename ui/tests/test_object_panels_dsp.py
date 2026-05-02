# ui/tests/test_object_panels_dsp.py
# Headless tests for ObjectInspector DSP routing — no Qt required.

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "spatial_engine_ui"))

from views.object_panels import ObjectInspector


class _RecordingClient:
    """Stub OSC client recording per-object DSP / algorithm sends."""

    def __init__(self):
        self.dsp_calls = []
        self.algo_calls = []

    def send_object_dsp(self, obj_id, param, value):
        self.dsp_calls.append((int(obj_id), int(param), float(value)))

    def send_object_algo(self, obj_id, algo):
        self.algo_calls.append((int(obj_id), int(algo)))


class _Obj:
    def __init__(self, obj_id, x=0.0, z=0.0):
        self.obj_id = obj_id
        self.x = x
        self.z = z


def test_no_routing_before_object_selected():
    client = _RecordingClient()
    panel = ObjectInspector(osc_client=client)
    panel._send_dsp(0, -3.0)  # not selected → must NOT send
    panel._on_algo(1)
    assert client.dsp_calls == []
    assert client.algo_calls == []


def test_dsp_route_after_show_object():
    client = _RecordingClient()
    panel = ObjectInspector(osc_client=client)
    panel.show_object(_Obj(7, x=1.0, z=2.0))
    # All 7 DSP params (0..6)
    for param in range(7):
        panel._send_dsp(param, float(param) * 0.5)
    assert client.dsp_calls == [(7, p, p * 0.5) for p in range(7)]


def test_algo_route_after_show_object():
    client = _RecordingClient()
    panel = ObjectInspector(osc_client=client)
    panel.show_object(_Obj(3))
    panel._on_algo(0)  # VBAP
    panel._on_algo(1)  # WFS
    panel._on_algo(2)  # DBAP
    assert client.algo_calls == [(3, 0), (3, 1), (3, 2)]


def test_show_object_none_disables_routing():
    client = _RecordingClient()
    panel = ObjectInspector(osc_client=client)
    panel.show_object(_Obj(5))
    panel.show_object(None)
    panel._send_dsp(4, 50.0)
    panel._on_algo(2)
    # First show_object set obj_id=5; show_object(None) reset to -1.
    assert client.dsp_calls == []
    assert client.algo_calls == []
