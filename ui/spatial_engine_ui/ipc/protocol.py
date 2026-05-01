"""IPC protocol constants and message helpers for spatial_engine_ui."""
from __future__ import annotations

SCHEMA_VERSION: int = 1

# OSC addresses — core → UI
ADDR_STATE = "/sys/state"
ADDR_PROTOCOL_VERSION = "/sys/protocol_version"
ADDR_WARNING = "/sys/warning"
ADDR_MATRIX = "/sys/matrix"
ADDR_HEARTBEAT_MISS = "/sys/heartbeat_miss"

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


def noise_type_address(channel: int) -> str:
    return f"/noise/{channel}/type"


def noise_gain_address(channel: int) -> str:
    return f"/noise/{channel}/gain"
