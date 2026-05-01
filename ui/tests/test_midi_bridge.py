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
