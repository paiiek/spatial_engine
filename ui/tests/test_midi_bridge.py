import pytest

def test_pc_to_scene_name():
    from spatial_engine_ui.midi.midi_bridge import MidiBridge
    assert MidiBridge.pc_to_scene_name(0) == "scene_0"
    assert MidiBridge.pc_to_scene_name(5) == "scene_5"
    assert MidiBridge.pc_to_scene_name(127) == "scene_127"

def test_midi_bridge_import():
    from spatial_engine_ui.midi.midi_bridge import MidiBridge
    bridge = MidiBridge()
    assert bridge is not None

def test_handle_non_pc_message():
    from spatial_engine_ui.midi.midi_bridge import MidiBridge
    bridge = MidiBridge()

    class FakeMsg:
        type = "note_on"

    assert bridge.handle_message(FakeMsg()) is None

def test_scene_name_template():
    from spatial_engine_ui.midi.midi_bridge import SCENE_NAME_TEMPLATE
    assert "{pc}" in SCENE_NAME_TEMPLATE

def test_osc_address():
    from spatial_engine_ui.midi.midi_bridge import OSC_SCENE_LOAD
    assert OSC_SCENE_LOAD == "/scene/load"


def test_handle_pc_sends_osc():
    """PC=5 should dispatch /scene/load with 'scene_5' to osc_client."""
    from spatial_engine_ui.midi.midi_bridge import MidiBridge, OSC_SCENE_LOAD

    class MockClient:
        def __init__(self):
            self.calls = []
        def send_message(self, addr, arg):
            self.calls.append((addr, arg))

    class FakePC:
        type = "program_change"
        program = 5

    client = MockClient()
    bridge = MidiBridge(osc_client=client)
    result = bridge.handle_message(FakePC())

    assert result == "scene_5"
    assert len(client.calls) == 1
    assert client.calls[0] == (OSC_SCENE_LOAD, "scene_5")


def test_midi_cue_trigger():
    """E-M5: cue mode dispatches /cue/go with mapped index; scene mode unchanged."""
    from spatial_engine_ui.midi.midi_bridge import MidiBridge, OSC_CUE_GO, OSC_SCENE_LOAD

    class MockClient:
        def __init__(self):
            self.calls = []
        def send_message(self, addr, arg):
            self.calls.append((addr, arg))

    class FakePC:
        type = "program_change"
        def __init__(self, program):
            self.program = program

    # --- cue mode: identity fallback (no pc_to_cue table) ---
    client = MockClient()
    bridge = MidiBridge(osc_client=client, mode="cue")
    result = bridge.handle_message(FakePC(3))
    assert result == 3
    assert client.calls == [(OSC_CUE_GO, 3)]

    # --- cue mode: custom pc_to_cue table ---
    client2 = MockClient()
    bridge2 = MidiBridge(osc_client=client2, mode="cue", pc_to_cue={3: 10, 7: 42})
    assert bridge2.handle_message(FakePC(3)) == 10
    assert client2.calls[-1] == (OSC_CUE_GO, 10)
    assert bridge2.handle_message(FakePC(7)) == 42
    assert client2.calls[-1] == (OSC_CUE_GO, 42)
    # pc not in table → identity
    assert bridge2.handle_message(FakePC(5)) == 5
    assert client2.calls[-1] == (OSC_CUE_GO, 5)

    # --- scene mode (default): existing /scene/load path unchanged ---
    client3 = MockClient()
    bridge3 = MidiBridge(osc_client=client3)  # default mode="scene"
    result3 = bridge3.handle_message(FakePC(5))
    assert result3 == "scene_5"
    assert client3.calls == [(OSC_SCENE_LOAD, "scene_5")]

    # --- explicit mode="scene" ---
    client4 = MockClient()
    bridge4 = MidiBridge(osc_client=client4, mode="scene")
    assert bridge4.handle_message(FakePC(0)) == "scene_0"
    assert client4.calls == [(OSC_SCENE_LOAD, "scene_0")]


def test_stop_without_start():
    """stop() before start() must be a no-op (no exception)."""
    from spatial_engine_ui.midi.midi_bridge import MidiBridge
    bridge = MidiBridge()
    bridge.stop()  # must not raise


import sys

_RTMIDI_OK = False
try:
    import mido
    mido.get_input_names()
    _RTMIDI_OK = True
except Exception:
    pass


@pytest.mark.skipif(not _RTMIDI_OK, reason="rtmidi backend not available")
@pytest.mark.skipif(sys.platform == "win32", reason="virtual MIDI ports not supported on Windows")
def test_start_loopback():
    """PC=7 → /scene/load 'scene_7' via virtual MIDI loopback."""
    import mido
    import time
    from spatial_engine_ui.midi.midi_bridge import MidiBridge, OSC_SCENE_LOAD

    class MockClient:
        def __init__(self): self.calls = []
        def send_message(self, addr, arg): self.calls.append((addr, arg))

    try:
        out_port = mido.open_output("test_loopback", virtual=True)
    except (IOError, OSError, RuntimeError):
        pytest.skip("MIDI loopback unavailable")

    client = MockClient()
    bridge = MidiBridge(osc_client=client, midi_port_name="test_loopback")
    bridge.start()

    try:
        out_port.send(mido.Message("program_change", program=7))
        deadline = time.monotonic() + 0.5
        while time.monotonic() < deadline:
            if len([c for c in client.calls if c[0] == OSC_SCENE_LOAD]) >= 1:
                break
            time.sleep(0.01)
    finally:
        bridge.stop()
        out_port.close()

    assert len([c for c in client.calls if c[0] == OSC_SCENE_LOAD]) == 1
    assert client.calls[0] == (OSC_SCENE_LOAD, "scene_7")


@pytest.mark.skipif(not _RTMIDI_OK, reason="rtmidi backend not available")
@pytest.mark.skipif(sys.platform == "win32", reason="virtual MIDI ports not supported on Windows")
def test_double_start_idempotent():
    """start() called twice must not spawn a second thread."""
    import mido
    from spatial_engine_ui.midi.midi_bridge import MidiBridge

    try:
        port = mido.open_output("test_idempotent", virtual=True)
    except (IOError, OSError, RuntimeError):
        pytest.skip("MIDI loopback unavailable")

    bridge = MidiBridge(midi_port_name="test_idempotent")
    bridge.start()
    t1 = bridge._thread
    bridge.start()  # second call — must be no-op
    try:
        assert bridge._thread is t1
    finally:
        bridge.stop()
        port.close()
