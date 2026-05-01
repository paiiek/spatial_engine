# test_adm_osc_protocol.py
# pytest tests for ADM-OSC address helpers in protocol.py

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "spatial_engine_ui"))

from ipc.protocol import (
    adm_obj_azim, adm_obj_elev, adm_obj_aed, adm_obj_gain, adm_obj_mute,
    ADM_OBJ_AZIM, ADM_OBJ_ELEV, ADM_OBJ_DIST, ADM_OBJ_GAIN, ADM_OBJ_MUTE, ADM_OBJ_AED,
    ADDR_OBJECT_POS, ADDR_STATE,
)


def test_adm_obj_azim():
    assert adm_obj_azim(3) == "/adm/obj/3/azim"
    assert adm_obj_azim(0) == "/adm/obj/0/azim"


def test_adm_obj_elev():
    assert adm_obj_elev(1) == "/adm/obj/1/elev"


def test_adm_obj_aed():
    assert adm_obj_aed(5) == "/adm/obj/5/aed"


def test_adm_obj_gain():
    assert adm_obj_gain(2) == "/adm/obj/2/gain"


def test_adm_obj_mute():
    assert adm_obj_mute(7) == "/adm/obj/7/mute"


def test_adm_template_constants():
    assert "{n}" in ADM_OBJ_AZIM
    assert "{n}" in ADM_OBJ_ELEV
    assert "{n}" in ADM_OBJ_DIST
    assert "{n}" in ADM_OBJ_GAIN
    assert "{n}" in ADM_OBJ_MUTE
    assert "{n}" in ADM_OBJ_AED


def test_no_collision_with_spe_addresses():
    """ADM-OSC addresses must not collide with existing SPE address namespace."""
    spe_prefixes = ["/obj/", "/sys/", "/hb/", "/object/", "/noise/"]
    adm_addresses = [
        adm_obj_azim(0), adm_obj_elev(0), adm_obj_aed(0),
        adm_obj_gain(0), adm_obj_mute(0),
    ]
    for adm_addr in adm_addresses:
        for prefix in spe_prefixes:
            assert not adm_addr.startswith(prefix), \
                f"ADM-OSC address {adm_addr!r} collides with SPE prefix {prefix!r}"
