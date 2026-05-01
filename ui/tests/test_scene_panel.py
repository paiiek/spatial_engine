# ui/tests/test_scene_panel.py
# Headless tests for scene_panel — no Qt required.

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "spatial_engine_ui"))

from views.scene_panel import OSC_SCENE_SAVE, OSC_SCENE_LOAD, OSC_SCENE_LIST


def test_osc_save_address():
    assert OSC_SCENE_SAVE == "/scene/save"


def test_osc_load_address():
    assert OSC_SCENE_LOAD == "/scene/load"


def test_osc_list_address():
    assert OSC_SCENE_LIST == "/scene/list"


def test_addresses_start_with_slash():
    for addr in (OSC_SCENE_SAVE, OSC_SCENE_LOAD, OSC_SCENE_LIST):
        assert addr.startswith("/"), f"Expected leading slash: {addr!r}"


def test_addresses_unique():
    addrs = [OSC_SCENE_SAVE, OSC_SCENE_LOAD, OSC_SCENE_LIST]
    assert len(set(addrs)) == len(addrs), "OSC addresses must be unique"
