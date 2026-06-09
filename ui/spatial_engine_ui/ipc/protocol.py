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
# C6 — full-state UDP-loss resync. The client sends ,i token; the engine
# re-emits every touched object on the /adm/obj/N/* echo addresses, then a
# single /sys/state ,i <count> completion sentinel (ADDR_STATE above).
ADDR_STATE_REQUEST = "/sys/state_request"     # ,i token
ADDR_OBJECT_POS = "/object/{id}/pos"          # ,iff  id x z
ADDR_OBJECT_SEQ = "/object/{id}/seq"          # appended to pos bundle
ADDR_NOISE_TYPE = "/noise/{ch}/type"          # ,s  white|pink
ADDR_NOISE_GAIN = "/noise/{ch}/gain"          # ,f  dB

# Per-object algorithm selector
ADDR_OBJ_ALGO   = "/obj/algo"                 # ,ii obj_id algo_int (0=VBAP 1=WFS 2=DBAP)
# Per-object DSP parameter (bundled): obj_id, param_id, value
#   param 0..3 = EQ band gain dB (low/lowmid/highmid/high)
#   param 4    = user delay ms
#   param 5    = distance HF rolloff k_hf (0..1)
#   param 6    = reverb send (0..1)
ADDR_OBJ_DSP    = "/obj/dsp"                  # ,iif obj_id param_id value

# Transport
ADDR_TRANSPORT_PLAY = "/transport/play"       # no args
ADDR_TRANSPORT_STOP = "/transport/stop"       # no args

# DSP param IDs (mirror PayloadObjDsp::Param in core/src/ipc/Command.h)
DSP_PARAM_EQ_LOW       = 0
DSP_PARAM_EQ_LOWMID    = 1
DSP_PARAM_EQ_HIGHMID   = 2
DSP_PARAM_EQ_HIGH      = 3
DSP_PARAM_DELAY_MS     = 4
DSP_PARAM_K_HF         = 5
DSP_PARAM_REVERB_SEND  = 6
DSP_PARAM_WIDTH        = 7  # source spread in radians (0..π)

# Algorithm enum (mirror ipc::Algorithm in core/src/ipc/Command.h)
ALGO_VBAP = 0
ALGO_WFS  = 1
ALGO_DBAP = 2

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
