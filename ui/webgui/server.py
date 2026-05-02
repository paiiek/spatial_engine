"""FastAPI + WebSocket hub for spatial_engine WebGUI MVP."""
from __future__ import annotations

import asyncio
import json
import logging
import threading
from contextlib import asynccontextmanager
from typing import Set

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    try:
        import ui.webgui.osc_bridge as osc_bridge  # noqa: PLC0415
        loop = asyncio.get_running_loop()
        osc_bridge.start(broadcast_state, loop)
        logger.info("OSC bridge started via lifespan")
    except Exception as exc:  # pragma: no cover
        logger.warning("OSC bridge startup failed: %s", exc)
    yield


app = FastAPI(title="spatial_engine WebGUI", version="0.1.0", lifespan=lifespan)

# ---------------------------------------------------------------------------
# Connection manager
# ---------------------------------------------------------------------------

class ConnectionManager:
    def __init__(self) -> None:
        self.active: Set[WebSocket] = set()

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self.active.add(ws)
        logger.info("WS client connected, total=%d", len(self.active))

    def disconnect(self, ws: WebSocket) -> None:
        self.active.discard(ws)
        logger.info("WS client disconnected, total=%d", len(self.active))

    async def broadcast(self, message: str) -> None:
        dead: list[WebSocket] = []
        for ws in list(self.active):
            try:
                await ws.send_text(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.active.discard(ws)


manager = ConnectionManager()

# ---------------------------------------------------------------------------
# OSC bridge handle — injected by osc_bridge on startup
# ---------------------------------------------------------------------------

osc_send_fn = None  # callable(address: str, *args) — set by osc_bridge

# ---------------------------------------------------------------------------
# Bridge mode state — shared with bridge process via signal/file or in-process
# ---------------------------------------------------------------------------

_bridge_mode: str = "ai"  # "ai" | "low_latency"
bridge_switch_fn = None   # callable(mode: str) — injected by bridge when co-located

_BRIDGE_MODE_FILE = "/tmp/.spe_bridge_mode"


def _write_bridge_mode_file(mode: str) -> None:
    """Write mode to shared file so a separate bridge process can pick it up."""
    try:
        import tempfile, os
        tmp = _BRIDGE_MODE_FILE + ".tmp"
        with open(tmp, "w") as f:
            f.write(mode)
        os.replace(tmp, _BRIDGE_MODE_FILE)
    except OSError as exc:
        logger.warning("Could not write bridge mode file: %s", exc)


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.get("/health")
async def health() -> dict:
    return {"status": "ok"}


@app.post("/api/mode")
async def set_mode(mode: str = Query(..., description="ai | low_latency")) -> JSONResponse:
    """ai | low_latency 모드 전환. bridge process에 신호 전달."""
    global _bridge_mode
    if mode not in ("ai", "low_latency"):
        raise HTTPException(status_code=400, detail=f"Invalid mode: {mode!r}. Must be 'ai' or 'low_latency'.")
    _bridge_mode = mode
    _write_bridge_mode_file(mode)
    if bridge_switch_fn is not None:
        try:
            bridge_switch_fn(mode)
        except Exception as exc:
            logger.warning("bridge_switch_fn failed: %s", exc)
    await manager.broadcast(json.dumps({"type": "mode_change", "mode": mode}))
    logger.info("Bridge mode set to %s", mode)
    return JSONResponse({"status": "ok", "mode": mode})


@app.get("/api/mode")
async def get_mode() -> JSONResponse:
    """현재 브리지 모드 조회."""
    return JSONResponse({"mode": _bridge_mode})


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await manager.connect(ws)
    try:
        while True:
            data = await ws.receive_text()
            # Client → OSC 9100 (cmd): forward object position commands
            try:
                msg = json.loads(data)
                if osc_send_fn is not None:
                    _dispatch_to_osc(msg)
                else:
                    logger.debug("osc_send_fn not ready, dropping: %s", data)
            except (json.JSONDecodeError, KeyError) as exc:
                logger.warning("Invalid WS message: %s — %s", data, exc)
    except WebSocketDisconnect:
        manager.disconnect(ws)


def _dispatch_to_osc(msg: dict) -> None:
    """Forward parsed WebSocket message to OSC 9100."""
    mtype = msg.get("type")
    if mtype == "obj_pos":
        n = int(msg["n"])
        if not (0 <= n < 64):
            logger.warning("obj_pos n=%d out of range [0,63]", n)
            return
        azim = float(msg["azim"])
        elev = float(msg.get("elev", 0.0))
        dist = float(msg.get("dist", 1.0))
        if osc_send_fn:
            osc_send_fn(f"/adm/obj/{n}/aed", azim, elev, dist)
    elif mtype == "obj_gain":
        n = int(msg["n"])
        if not (0 <= n < 64):
            logger.warning("obj_gain n=%d out of range [0,63]", n)
            return
        gain = float(msg["gain"])
        if osc_send_fn:
            osc_send_fn(f"/adm/obj/{n}/gain", gain)
    elif mtype == "scene_save":
        name = str(msg.get("name", "scene"))[:64]
        if osc_send_fn:
            osc_send_fn("/scene/save", name)
    elif mtype == "scene_load":
        name = str(msg.get("name", "scene"))[:64]
        if osc_send_fn:
            osc_send_fn("/scene/load", name)
    elif mtype == "scene_list":
        if osc_send_fn:
            osc_send_fn("/scene/list")
    elif mtype == "transport":
        action = str(msg.get("action", "")).lower()
        if action == "play" and osc_send_fn:
            osc_send_fn("/transport/play")
        elif action == "stop" and osc_send_fn:
            osc_send_fn("/transport/stop")
        else:
            logger.warning("transport: unknown action %r", action)
    elif mtype == "obj_algo":
        n = int(msg["n"])
        if not (0 <= n < 64):
            logger.warning("obj_algo n=%d out of range [0,63]", n)
            return
        algo = int(msg.get("algo", 0))   # 0=VBAP 1=WFS 2=DBAP
        if not (0 <= algo <= 2):
            logger.warning("obj_algo algo=%d out of range [0,2]", algo)
            return
        if osc_send_fn:
            osc_send_fn("/obj/algo", n, algo)
    elif mtype == "obj_dsp":
        n = int(msg["n"])
        if not (0 <= n < 64):
            logger.warning("obj_dsp n=%d out of range [0,63]", n)
            return
        param = int(msg.get("param", 0))  # 0..6 (see protocol.py DSP_PARAM_*)
        if not (0 <= param <= 6):
            logger.warning("obj_dsp param=%d out of range [0,6]", param)
            return
        value = float(msg.get("value", 0.0))
        if osc_send_fn:
            osc_send_fn("/obj/dsp", n, param, value)
    elif mtype == "noise":
        ch = int(msg["ch"])
        if ch < 0 or ch >= 64:
            logger.warning("noise ch=%d out of range", ch)
            return
        ntype = msg.get("ntype")
        ngain = msg.get("gain_db")
        if osc_send_fn and ntype is not None:
            osc_send_fn(f"/noise/{ch}/type", str(ntype))
        if osc_send_fn and ngain is not None:
            osc_send_fn(f"/noise/{ch}/gain", float(ngain))
    else:
        logger.debug("Unknown message type: %s", mtype)


# ---------------------------------------------------------------------------
# vid2spatial bridge management
# ---------------------------------------------------------------------------

_v2s_bridge = None   # Vid2SpatialBridge instance
_v2s_thread = None   # background thread running bridge.start()


def _v2s_running() -> bool:
    return _v2s_bridge is not None and getattr(_v2s_bridge, "_running", False)


@app.post("/api/vid2spatial/start")
async def v2s_start() -> JSONResponse:
    """Start vid2spatial OSC bridge (port 9000 → 9100)."""
    global _v2s_bridge, _v2s_thread
    if _v2s_running():
        return JSONResponse({"status": "already_running", "running": True})
    try:
        import sys as _sys
        _sys.path.insert(0, "")
        from bridge.vid2spatial_osc import BridgeServer  # noqa: PLC0415
        _v2s_bridge = BridgeServer(
            listen_port=9000, target_port=9100, target_host="127.0.0.1", mode=_bridge_mode
        )
        _v2s_thread = threading.Thread(target=_v2s_bridge.start, daemon=True)
        _v2s_thread.start()
        await asyncio.sleep(0.2)  # allow server socket to bind
        await manager.broadcast(json.dumps({"type": "v2s_status", "running": True}))
        logger.info("vid2spatial bridge started on port 9000")
        return JSONResponse({"status": "started", "running": True})
    except Exception as exc:
        logger.error("vid2spatial bridge start failed: %s", exc)
        return JSONResponse({"status": "error", "error": str(exc), "running": False}, status_code=500)


@app.post("/api/vid2spatial/stop")
async def v2s_stop() -> JSONResponse:
    """Stop vid2spatial OSC bridge."""
    global _v2s_bridge, _v2s_thread
    if not _v2s_running():
        return JSONResponse({"status": "not_running", "running": False})
    try:
        _v2s_bridge.stop()
        _v2s_bridge = None
        _v2s_thread = None
        await manager.broadcast(json.dumps({"type": "v2s_status", "running": False}))
        logger.info("vid2spatial bridge stopped")
        return JSONResponse({"status": "stopped", "running": False})
    except Exception as exc:
        logger.error("vid2spatial bridge stop failed: %s", exc)
        return JSONResponse({"status": "error", "error": str(exc), "running": False}, status_code=500)


@app.get("/api/vid2spatial/status")
async def v2s_status() -> JSONResponse:
    """vid2spatial bridge 상태 조회."""
    return JSONResponse({"running": _v2s_running(), "port": 9000})


# ---------------------------------------------------------------------------
# Broadcast helper (called by osc_bridge when OSC 9101 state arrives)
# ---------------------------------------------------------------------------

async def broadcast_state(payload: dict) -> None:
    """Called from osc_bridge to push engine state to all WS clients."""
    await manager.broadcast(json.dumps(payload))


# ---------------------------------------------------------------------------
# Static files (served after API routes are registered)
# ---------------------------------------------------------------------------

import os as _os
_static_dir = _os.path.join(_os.path.dirname(__file__), "static")
if _os.path.isdir(_static_dir):
    app.mount("/static", StaticFiles(directory=_static_dir), name="static")

    @app.get("/", response_class=HTMLResponse)
    async def root() -> FileResponse:
        return FileResponse(_os.path.join(_static_dir, "index.html"))
