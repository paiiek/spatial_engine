"""WebGUI dispatcher unit tests (transport / obj_algo / obj_dsp / noise).

Calls _dispatch_to_osc directly with a recording osc_send_fn so we can verify
the OSC routing without standing up the full WS / FastAPI stack.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

import pytest

from ui.webgui import server as webgui_server


class _Recorder:
    def __init__(self):
        self.calls = []

    def __call__(self, addr, *args):
        self.calls.append((addr, args))


@pytest.fixture
def recorder(monkeypatch):
    rec = _Recorder()
    monkeypatch.setattr(webgui_server, "osc_send_fn", rec)
    return rec


def test_dispatch_obj_pos(recorder):
    webgui_server._dispatch_to_osc({
        "type": "obj_pos", "n": 5, "azim": 30.0, "elev": 10.0, "dist": 0.7,
    })
    assert recorder.calls == [("/adm/obj/5/aed", (30.0, 10.0, 0.7))]


def test_dispatch_transport_play(recorder):
    webgui_server._dispatch_to_osc({"type": "transport", "action": "play"})
    assert recorder.calls == [("/transport/play", ())]


def test_dispatch_transport_stop(recorder):
    webgui_server._dispatch_to_osc({"type": "transport", "action": "stop"})
    assert recorder.calls == [("/transport/stop", ())]


def test_dispatch_transport_unknown_action(recorder):
    webgui_server._dispatch_to_osc({"type": "transport", "action": "weird"})
    assert recorder.calls == []  # silently ignored (logged warning)


def test_dispatch_obj_algo(recorder):
    webgui_server._dispatch_to_osc({"type": "obj_algo", "n": 3, "algo": 1})  # WFS
    assert recorder.calls == [("/obj/algo", (3, 1))]


def test_dispatch_obj_algo_out_of_range(recorder):
    webgui_server._dispatch_to_osc({"type": "obj_algo", "n": 3, "algo": 99})
    assert recorder.calls == []


def test_dispatch_obj_dsp(recorder):
    # param=0 (eq_low), value=-3.5 dB
    webgui_server._dispatch_to_osc({"type": "obj_dsp", "n": 2, "param": 0, "value": -3.5})
    # param=4 (delay_ms), value=12.5 ms
    webgui_server._dispatch_to_osc({"type": "obj_dsp", "n": 2, "param": 4, "value": 12.5})
    # param=6 (reverb_send), value=0.4
    webgui_server._dispatch_to_osc({"type": "obj_dsp", "n": 2, "param": 6, "value": 0.4})
    assert recorder.calls == [
        ("/obj/dsp", (2, 0, -3.5)),
        ("/obj/dsp", (2, 4, 12.5)),
        ("/obj/dsp", (2, 6, 0.4)),
    ]


def test_dispatch_obj_dsp_out_of_range_param(recorder):
    webgui_server._dispatch_to_osc({"type": "obj_dsp", "n": 0, "param": 99, "value": 0.0})
    assert recorder.calls == []


def test_dispatch_noise_type_only(recorder):
    webgui_server._dispatch_to_osc({"type": "noise", "ch": 1, "ntype": "pink"})
    assert recorder.calls == [("/noise/1/type", ("pink",))]


def test_dispatch_noise_gain_only(recorder):
    webgui_server._dispatch_to_osc({"type": "noise", "ch": 2, "gain_db": -12.0})
    assert recorder.calls == [("/noise/2/gain", (-12.0,))]


def test_dispatch_noise_both(recorder):
    webgui_server._dispatch_to_osc({"type": "noise", "ch": 0, "ntype": "white", "gain_db": -6.0})
    assert recorder.calls == [
        ("/noise/0/type", ("white",)),
        ("/noise/0/gain", (-6.0,)),
    ]


def test_dispatch_obj_pos_out_of_range(recorder):
    webgui_server._dispatch_to_osc({"type": "obj_pos", "n": 999, "azim": 0.0, "elev": 0.0, "dist": 0.0})
    assert recorder.calls == []
