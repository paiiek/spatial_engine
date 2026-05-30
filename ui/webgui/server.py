"""FastAPI + WebSocket hub for spatial_engine WebGUI MVP."""
from __future__ import annotations

import asyncio
import json
import logging
import math
import threading
from contextlib import asynccontextmanager
from dataclasses import asdict
from typing import Set

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from spatial_engine_ui.state.trajectory import TrajectoryConfig, TrajectoryShape
from ui.webgui.trajectory import WebTrajectoryRunner

logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    _osc_bridge_mod = None
    try:
        import ui.webgui.osc_bridge as osc_bridge  # noqa: PLC0415
        _osc_bridge_mod = osc_bridge
        loop = asyncio.get_running_loop()
        osc_bridge.start(broadcast_state, loop)
        logger.info("OSC bridge started via lifespan")
    except Exception as exc:  # pragma: no cover
        logger.warning("OSC bridge startup failed: %s", exc)
    _app.state.trajectory = WebTrajectoryRunner(osc_send_fn)
    await _app.state.trajectory.start()
    try:
        yield
    finally:
        await _app.state.trajectory.stop()
        # S0 #3 — release 9101 UDP socket / thread (risk 5.1, sentinel G7).
        if _osc_bridge_mod is not None:
            try:
                _osc_bridge_mod.shutdown()
            except Exception as exc:  # pragma: no cover — defensive
                logger.warning("osc_bridge shutdown failed: %s", exc)


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


class MetricsHub:
    """Dashboard-only fan-out for classified telemetry (A-M3).

    Holds the LATEST metrics/warning snapshot in a SINGLE slot (latest-wins —
    §3 DD-D: queue size 1, drop-older, no unbounded buffering) plus a set of
    ``/ws/metrics`` subscribers. On a fresh connect the cached snapshot is
    pushed immediately so a dashboard opened mid-stream is not blank.

    Reuses the ``ConnectionManager`` connect/disconnect/drop-on-fail pattern:
    a per-client send failure → immediate disconnect (mirrors ``broadcast``).
    """

    def __init__(self) -> None:
        self.active: Set[WebSocket] = set()
        self._last_snapshot: str | None = None

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self.active.add(ws)
        logger.info("metrics WS client connected, total=%d", len(self.active))
        # Push the last cached snapshot immediately so a fresh subscriber sees
        # current state without waiting for the next 1 Hz broadcast.
        if self._last_snapshot is not None:
            try:
                await ws.send_text(self._last_snapshot)
            except Exception:
                self.active.discard(ws)

    def disconnect(self, ws: WebSocket) -> None:
        self.active.discard(ws)
        logger.info("metrics WS client disconnected, total=%d", len(self.active))

    async def publish(self, payload: dict) -> None:
        """Cache the latest snapshot (single slot) and fan out to subscribers."""
        message = json.dumps(payload)
        self._last_snapshot = message  # latest-wins, no queue (DD-D)
        dead: list[WebSocket] = []
        for ws in list(self.active):
            try:
                await ws.send_text(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.active.discard(ws)


metrics_hub = MetricsHub()

# ---------------------------------------------------------------------------
# OSC bridge handle — injected by osc_bridge on startup
# ---------------------------------------------------------------------------

osc_send_fn = None  # callable(address: str, *args) — set by osc_bridge

# ---------------------------------------------------------------------------
# Bridge mode state — shared with bridge process via signal/file or in-process
# ---------------------------------------------------------------------------

# ADR 0015: default is "low_latency" so the WebGUI is the 9100 producer
# out-of-the-box and canvas drags / trajectories actually reach the engine.
# "ai" mode is an explicit opt-in for when vid2spatial owns the wire.
_bridge_mode: str = "low_latency"  # "ai" | "low_latency"
bridge_switch_fn = None   # callable(mode: str) — injected by bridge when co-located

_BRIDGE_MODE_FILE = "/tmp/.spe_bridge_mode"


def _write_bridge_mode_file(mode: str) -> None:
    """Write mode to shared file so a separate bridge process can pick it up."""
    try:
        import os
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
    # ``osc_ready`` surfaces whether the OSC bridge wired up at startup. If
    # it is False, every drag/trajectory send is silently dropped — this is
    # the first thing to check when "the sound doesn't move".
    return {
        "status": "ok",
        "osc_ready": osc_send_fn is not None,
        "bridge_mode": _bridge_mode,
    }


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
            except json.JSONDecodeError as exc:
                logger.warning("Invalid WS JSON: %s — %s", data, exc)
                continue
            if osc_send_fn is None:
                logger.debug("osc_send_fn not ready, dropping: %s", data)
                continue
            # _dispatch_to_osc must never crash the receive loop: a single
            # malformed field (bad int / missing key) would otherwise kill
            # the socket. Catch the value-shape errors it can raise.
            try:
                notice = _dispatch_to_osc(msg)
            except (KeyError, ValueError, TypeError) as exc:
                logger.warning("Malformed WS message: %s — %s", data, exc)
                continue
            # ADR 0013 / 0015: positional sends dropped in AI mode return a
            # notice so the browser can surface it instead of silently lying.
            if notice is not None:
                await manager.broadcast(json.dumps(notice))
    except WebSocketDisconnect:
        manager.disconnect(ws)


@app.websocket("/ws/metrics")
async def websocket_metrics(ws: WebSocket) -> None:
    """Dashboard-only telemetry channel (A-M3).

    On connect the last cached snapshot (if any) is pushed immediately by
    ``MetricsHub.connect``; subsequent classified metrics/warning broadcasts
    stream in. This is a read-only channel — inbound frames are drained and
    ignored so the receive loop stays alive without driving the control plane
    (positional control stays on ``/ws``).
    """
    await metrics_hub.connect(ws)
    try:
        while True:
            await ws.receive_text()  # drain; metrics channel is push-only
    except WebSocketDisconnect:
        metrics_hub.disconnect(ws)


#: Set of message types that synthesise positional data and therefore
#: contend with vid2spatial_osc for the 9100 wire. Per ADR 0013, these are
#: SUPPRESSED while ``_bridge_mode == 'ai'`` (vid2spatial is the producer).
_POSITION_MTYPES = frozenset({"obj_pos", "obj_gain"})


def _ai_mode_position_conflict(mtype: str) -> bool:
    """Return True if a positional OSC send should be suppressed under AI mode.

    ADR 0013 (Single Producer, Single Wire — plan §1.1 P3):
      * ``low_latency`` mode → WebGUI is the 9100 producer; vid2spatial is OFF.
      * ``ai``           mode → vid2spatial_osc owns 9100; WebGUI positional
        sends would interleave on the wire and corrupt the engine's
        per-object state.
    """
    return _bridge_mode == "ai" and mtype in _POSITION_MTYPES


def _dispatch_to_osc(msg: dict) -> dict | None:
    """Forward parsed WebSocket message to OSC 9100.

    Single-producer contract (ADR 0013):
      * Positional traffic (``obj_pos`` / ``obj_gain``) is suppressed when
        ``_bridge_mode == 'ai'`` — vid2spatial_osc is the active 9100
        producer and concurrent sends would race on the wire.
      * All other message types (scene/transport/algo/dsp/noise) are
        control-plane only, do not contend with vid2spatial, and pass
        through unconditionally.

    Returns an optional WS-notice dict that the caller should broadcast back
    to clients (currently only ``drag_suppressed`` for an AI-mode drop, so
    the browser can show feedback instead of silently moving the dot).
    """
    mtype = msg.get("type")

    # ADR 0013 — single-producer guard rail.
    if _ai_mode_position_conflict(mtype):
        logger.warning(
            "9100 wire contention: dropping %s (vid2spatial owns 9100 in "
            "AI mode; switch to low_latency to send positional from WebGUI)",
            mtype,
        )
        return {
            "type": "drag_suppressed",
            "mode": _bridge_mode,
            "mtype": mtype,
            "n": msg.get("n"),
        }

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
    elif mtype == "binaural_reset_demote":
        # Control-plane only (not in _POSITION_MTYPES) → passes through in any
        # bridge mode. Engine decodes /sys/binaural_reset_demote ,i 1 →
        # CommandTag::SysBinauralResetDemote (CommandDecoder.cpp:429).
        if osc_send_fn:
            osc_send_fn("/sys/binaural_reset_demote", 1)
    elif mtype == "binaural_sofa_select":
        # B-M5: HRTF dataset selector → /sys/binaural_sofa_select <name>.
        # Control-plane only; not in _POSITION_MTYPES → passes through in any
        # bridge mode (mirrors binaural_reset_demote above).
        name = str(msg.get("name", ""))
        if osc_send_fn:
            osc_send_fn("/sys/binaural_sofa_select", name)
    else:
        logger.debug("Unknown message type: %s", mtype)


# ---------------------------------------------------------------------------
# Trajectory API
# ---------------------------------------------------------------------------

class TrajectoryRequest(BaseModel):
    obj_id: int
    shape: str = "circle"
    speed_hz: float = 0.5
    radius: float = 1.0
    elevation_rad: float = 0.0
    az_start_rad: float = 0.0
    az_end_rad: float = math.pi
    lissajous_ratio: float = 2.0


class TrajectoryStopRequest(BaseModel):
    obj_id: int


@app.post("/api/trajectory/start")
async def trajectory_start(req: TrajectoryRequest) -> JSONResponse:
    # obj_id must be in the engine's [0, 64) range — the WS obj_pos path
    # already validates this; the HTTP trajectory path must match or it
    # emits /adm/obj/{bad}/aed that the engine silently drops.
    if not (0 <= req.obj_id < 64):
        raise HTTPException(
            status_code=400,
            detail=f"obj_id {req.obj_id} out of range [0, 64)",
        )
    try:
        shape = TrajectoryShape(req.shape)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid shape: {req.shape!r}")
    cfg = TrajectoryConfig(
        obj_id=req.obj_id,
        shape=shape,
        speed_hz=req.speed_hz,
        radius=req.radius,
        elevation_rad=req.elevation_rad,
        az_start_rad=req.az_start_rad,
        az_end_rad=req.az_end_rad,
        lissajous_ratio=req.lissajous_ratio,
        enabled=True,
    )
    app.state.trajectory.upsert(cfg)
    return JSONResponse({"ok": True})


@app.post("/api/trajectory/stop")
async def trajectory_stop(req: TrajectoryStopRequest) -> JSONResponse:
    app.state.trajectory.remove(req.obj_id)
    return JSONResponse({"ok": True})


@app.get("/api/trajectory/list")
async def trajectory_list() -> list:
    return [asdict(c) for c in app.state.trajectory.list_configs()]


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
# S5 / G7 — dev-only introspection endpoints (env-gated)
# ---------------------------------------------------------------------------
# Route is always registered, but request handling is gated at runtime on
# ``SPE_DEBUG_ENDPOINTS=1``. This avoids the import-time-only check pitfall
# where setting the env var post-import would have no effect (and conversely
# where a forgotten dev-time env var would freeze the route into production
# at import time). 404 is returned when the gate is off so the surface looks
# identical to a deployment that never compiled the route in.
# Used by the 48h soak harness (tests/soak_harness/run_soak_webgui.py) to
# track ``asyncio.all_tasks()`` slope (G7 sentinel ``extract_asyncio_slope.py``).

import os as _dbg_os  # noqa: E402


@app.get("/api/_debug/asyncio_tasks")
async def _debug_asyncio_tasks() -> JSONResponse:
    """Return current count of asyncio tasks alive in the uvicorn loop.

    Runtime-gated by env ``SPE_DEBUG_ENDPOINTS=1``. When the gate is off the
    handler responds with 404 so production probes cannot distinguish it from
    an unregistered route. Soak harness samples this at 1 Hz; linear
    regression on the returned counts feeds the
    ``asyncio_slope_tasks_per_h`` G7 sentinel (must be ≤ 1 task/h).
    """
    if _dbg_os.environ.get("SPE_DEBUG_ENDPOINTS") != "1":
        return JSONResponse({"detail": "Not Found"}, status_code=404)
    try:
        tasks = asyncio.all_tasks()
        return JSONResponse({
            "ok": True,
            "n_tasks": len(tasks),
            "ws_connections": len(manager.active),
        })
    except Exception as exc:  # pragma: no cover — defensive
        return JSONResponse({"ok": False, "error": str(exc)}, status_code=500)


# ---------------------------------------------------------------------------
# Broadcast helper (called by osc_bridge when OSC 9101 state arrives)
# ---------------------------------------------------------------------------

async def broadcast_state(payload: dict) -> None:
    """Called from osc_bridge to push engine state to WS clients.

    Routing (A-M3 + m1 decision):
      * type in {"metrics", "warning"} → MetricsHub (/ws/metrics dashboard).
      * everything else (incl. /sys/state shm raw {osc_address, args}) → the
        existing /ws control plane. /sys/state is NOT routed to MetricsHub
        because its latest-wins single slot would clobber distinct shm keys.
    """
    if payload.get("type") in ("metrics", "warning"):
        await metrics_hub.publish(payload)
    else:
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


# Dashboard route is wired now (A-M3); the real HTML/JS lands in A-M4. A
# placeholder file is shipped so the route serves 200 until then. If the file
# is missing for any reason, return 404 gracefully rather than raising.
@app.get("/api/hrtf/catalog")
async def get_hrtf_catalog() -> JSONResponse:
    """B-M5: Return the HRTF dataset catalog for the dashboard selector."""
    _catalog_path = _os.path.join(
        _os.path.dirname(__file__), "..", "..", "assets", "hrtf", "catalog.json"
    )
    _catalog_path = _os.path.normpath(_catalog_path)
    if not _os.path.isfile(_catalog_path):
        raise HTTPException(status_code=404, detail="catalog.json not found")
    with open(_catalog_path, encoding="utf-8") as _f:
        _data = json.load(_f)
    return JSONResponse(_data)


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard() -> FileResponse:
    _path = _os.path.join(_static_dir, "dashboard.html")
    if not _os.path.isfile(_path):
        raise HTTPException(status_code=404, detail="dashboard.html not found")
    return FileResponse(_path)
