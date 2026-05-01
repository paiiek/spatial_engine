"""IPC protocol constants and message helpers for spatial_engine_ui."""
from __future__ import annotations

SCHEMA_VERSION: int = 1

# OSC addresses — core → UI
ADDR_STATE = "/sys/state"
ADDR_PROTOCOL_VERSION = "/sys/protocol_version"
ADDR_WARNING = "/sys/warning"
ADDR_MATRIX = "/sys/matrix"
ADDR_HEARTBEAT_MISS = "/sys/heartbeat_miss"
ADDR_METRICS = "/sys/metrics"

# OSC addresses — UI → core
ADDR_OBJECT_POS = "/object/{id}/pos"          # ,iff  id x z
ADDR_OBJECT_SEQ = "/object/{id}/seq"          # appended to pos bundle
ADDR_NOISE_TYPE = "/noise/{ch}/type"          # ,s  white|pink
ADDR_NOISE_GAIN = "/noise/{ch}/gain"          # ,f  dB

HEARTBEAT_RATE_HZ = 10
HEARTBEAT_MISS_THRESHOLD = 3
DRAG_RATE_HZ = 120


def object_pos_address(obj_id: int) -> str:
    return f"/object/{obj_id}/pos"


# ADM-OSC addresses (receive only — sent by external controllers)
ADM_OBJ_AZIM = "/adm/obj/{n}/azim"
ADM_OBJ_ELEV = "/adm/obj/{n}/elev"
ADM_OBJ_DIST = "/adm/obj/{n}/dist"
ADM_OBJ_GAIN = "/adm/obj/{n}/gain"
ADM_OBJ_MUTE = "/adm/obj/{n}/mute"
ADM_OBJ_AED  = "/adm/obj/{n}/aed"


def adm_obj_azim(n: int) -> str: return f"/adm/obj/{n}/azim"
def adm_obj_elev(n: int) -> str: return f"/adm/obj/{n}/elev"
def adm_obj_aed(n: int)  -> str: return f"/adm/obj/{n}/aed"
def adm_obj_gain(n: int) -> str: return f"/adm/obj/{n}/gain"
def adm_obj_mute(n: int) -> str: return f"/adm/obj/{n}/mute"


def noise_type_address(channel: int) -> str:
    return f"/noise/{channel}/type"


def noise_gain_address(channel: int) -> str:
    return f"/noise/{channel}/gain"
