# ui/tests/test_transport_panel.py
# Headless tests for transport_panel — no Qt required.

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "spatial_engine_ui"))

from views.transport_panel import (
    TransportPanel, OSC_TRANSPORT_PLAY, OSC_TRANSPORT_STOP,
)


class _RecordingClient:
    """Stub OSC client that records every send."""

    def __init__(self):
        self.calls = []

    def send_transport_play(self):
        self.calls.append(("send_transport_play",))

    def send_transport_stop(self):
        self.calls.append(("send_transport_stop",))


class _RecordingCallable:
    """Stub OSC callable: collects (addr, *args)."""

    def __init__(self):
        self.calls = []

    def __call__(self, addr, *args):
        self.calls.append((addr, args))


def test_addresses_constants():
    assert OSC_TRANSPORT_PLAY == "/transport/play"
    assert OSC_TRANSPORT_STOP == "/transport/stop"


def test_play_routes_via_client_method():
    client = _RecordingClient()
    panel = TransportPanel(osc_sender=client)
    panel._on_play()
    assert client.calls == [("send_transport_play",)]


def test_stop_routes_via_client_method():
    client = _RecordingClient()
    panel = TransportPanel(osc_sender=client)
    panel._on_stop()
    assert client.calls == [("send_transport_stop",)]


def test_play_routes_via_callable_fallback():
    sender = _RecordingCallable()
    panel = TransportPanel(osc_sender=sender)
    panel._on_play()
    assert sender.calls == [(OSC_TRANSPORT_PLAY, ())]


def test_stop_routes_via_callable_fallback():
    sender = _RecordingCallable()
    panel = TransportPanel(osc_sender=sender)
    panel._on_stop()
    assert sender.calls == [(OSC_TRANSPORT_STOP, ())]


def test_no_op_when_no_sender():
    panel = TransportPanel(osc_sender=None)
    panel._on_play()  # must not raise
    panel._on_stop()  # must not raise
