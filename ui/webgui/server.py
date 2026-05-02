"""FastAPI + WebSocket hub for spatial_engine WebGUI MVP."""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Set

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles

logger = logging.getLogger(__name__)

app = FastAPI(title="spatial_engine WebGUI", version="0.1.0")

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
# Routes
# ---------------------------------------------------------------------------

@app.get("/health")
async def health() -> dict:
    return {"status": "ok"}


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
        azim = float(msg["azim"])
        elev = float(msg.get("elev", 0.0))
        dist = float(msg.get("dist", 1.0))
        if osc_send_fn:
            osc_send_fn(f"/adm/obj/{n}/aed", azim, elev, dist)
    elif mtype == "obj_gain":
        n = int(msg["n"])
        gain = float(msg["gain"])
        if osc_send_fn:
            osc_send_fn(f"/adm/obj/{n}/gain", gain)
    else:
        logger.debug("Unknown message type: %s", mtype)


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
