"""OSC bridge: WebSocket ↔ OSC 9100/9101 for spatial_engine WebGUI."""
from __future__ import annotations

import asyncio
import json
import logging
import threading
import time
from typing import TYPE_CHECKING, Optional

from pythonosc import dispatcher as osc_dispatcher
from pythonosc import osc_server
from pythonosc.udp_client import SimpleUDPClient

if TYPE_CHECKING:
    pass

logger = logging.getLogger(__name__)

OSC_CMD_PORT = 9100   # spatial_engine receives commands
OSC_STATE_PORT = 9101  # spatial_engine sends state

_osc_client: SimpleUDPClient | None = None
_loop: asyncio.AbstractEventLoop | None = None
_broadcast_fn = None  # async callable(dict) — injected from server

# Handles for orderly shutdown (S0 deliverable #2 — risk 5.1).
_srv_state: Optional[osc_server.ThreadingOSCUDPServer] = None
_srv_thread: Optional[threading.Thread] = None


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def send_osc(address: str, *args) -> None:
    """Send an OSC message to spatial_engine cmd port (9100)."""
    if _osc_client is None:
        logger.warning("OSC client not initialised, dropping %s", address)
        return
    _osc_client.send_message(address, list(args))
    logger.debug("OSC → 9100  %s %s", address, args)


def start(
    broadcast_async_fn,
    loop: asyncio.AbstractEventLoop,
    osc_host: str = "127.0.0.1",
) -> None:
    """
    Start the OSC state listener on 9101 in a background thread.

    Parameters
    ----------
    broadcast_async_fn:
        Coroutine function ``async (payload: dict) -> None`` from server.py.
    loop:
        The running asyncio event loop (server's loop).
    osc_host:
        Bind host for OSC state listener.
    """
    global _osc_client, _loop, _broadcast_fn, _srv_state, _srv_thread

    # If a previous start() left a server running, tear it down first so we
    # never leak fd / threads across hot-reloads (e.g. uvicorn --reload, tests).
    if _srv_state is not None:
        shutdown()

    _loop = loop
    _broadcast_fn = broadcast_async_fn
    _osc_client = SimpleUDPClient(osc_host, OSC_CMD_PORT)

    # Wire server.py to use our send_osc
    import ui.webgui.server as _server  # noqa: PLC0415
    _server.osc_send_fn = send_osc

    # Listener on 9101: engine state → WebSocket broadcast
    d_state = osc_dispatcher.Dispatcher()
    d_state.set_default_handler(_handle_state_message)
    _srv_state = osc_server.ThreadingOSCUDPServer(
        (osc_host, OSC_STATE_PORT), d_state
    )
    _srv_thread = threading.Thread(
        target=_srv_state.serve_forever, daemon=True, name="osc-bridge-9101"
    )
    _srv_thread.start()

    logger.info(
        "OSC bridge started: listen=9101 send=9100 host=%s", osc_host
    )


def shutdown() -> None:
    """Tear down the 9101 ThreadingOSCUDPServer + thread + UDP client.

    Idempotent: calling without a prior ``start()`` (or twice) is a no-op.
    Used by:
      * ``ui.webgui.server`` lifespan ``finally`` block (S0 deliverable #3)
      * ``tests/test_osc_bridge_shutdown.py`` fd-leak sentinel (G7)
    """
    global _osc_client, _loop, _broadcast_fn, _srv_state, _srv_thread

    srv = _srv_state
    thr = _srv_thread
    _srv_state = None
    _srv_thread = None

    if srv is not None:
        try:
            srv.shutdown()       # stops serve_forever() loop
        except Exception as exc:  # pragma: no cover — defensive
            logger.warning("osc_bridge: srv.shutdown() raised: %s", exc)
        try:
            srv.server_close()   # closes UDP socket fd
        except Exception as exc:  # pragma: no cover — defensive
            logger.warning("osc_bridge: srv.server_close() raised: %s", exc)

    if thr is not None and thr.is_alive():
        thr.join(timeout=2.0)
        if thr.is_alive():
            logger.warning(
                "osc_bridge: state listener thread did not exit within 2s"
            )

    _osc_client = None
    _loop = None
    _broadcast_fn = None
    logger.info("OSC bridge shut down (fd released)")


# ---------------------------------------------------------------------------
# Address-based classification (v0.9 A-M2)
# ---------------------------------------------------------------------------

# /sys/metrics emits one ",s" key=value message per field (MetricsEmit.h).
# Map each contract key to the parser used to coerce the raw string value.
# Keys absent from this table are forwarded as-is under their raw key (robust
# to extra/unknown keys — never crash).
_METRICS_FIELD_PARSERS = {
    "cpu_pct": int,
    "cpu_peak_pct": int,
    "p99_us": int,
    "xrun_count": int,
    "engine_overrun_count": int,
    "binaural_demote_count": int,
}


def _classify_metrics(args) -> dict:
    """Parse a /sys/metrics ",s" key=value message into a typed metrics dict.

    The engine emits ONE key=value pair per OSC message (MetricsEmit.h), so
    ``args`` is normally a single ``"key=value"`` string. We tolerate multiple
    args defensively. Malformed/junk values and unknown keys never raise: an
    unparseable value falls back to the raw string, a malformed pair is skipped.
    """
    out: dict = {"type": "metrics"}
    for arg in args:
        if not isinstance(arg, str) or "=" not in arg:
            continue  # skip junk / non key=value tokens
        key, _, raw = arg.partition("=")
        key = key.strip()
        if not key:
            continue
        parser = _METRICS_FIELD_PARSERS.get(key)
        if parser is None:
            out[key] = raw  # unknown key: forward raw string
            continue
        try:
            out[key] = parser(raw)
        except (ValueError, TypeError):
            out[key] = raw  # junk value: forward raw, do not crash
    return out


def _classify_warning(address: str, args) -> dict:
    """Parse a /sys/{binaural_,}warning ",sf" message → warning dict.

    Wire shape (SpatialEngine.cpp): args = [category:str, payload:float].
    Robust to missing trailing args.
    """
    arglist = list(args)
    category = arglist[0] if len(arglist) >= 1 else None
    payload = arglist[1] if len(arglist) >= 2 else None
    return {
        "type": "warning",
        "ts": time.time(),
        "category": category,
        "payload": payload,
    }


# ---------------------------------------------------------------------------
# Internal OSC handler
# ---------------------------------------------------------------------------

def _handle_state_message(address: str, *args) -> None:
    """Receive OSC state from engine (port 9101) → broadcast to WS clients.

    Address-based classification (A-M2):
      * /sys/metrics            → typed metrics dict
      * /sys/warning,
        /sys/binaural_warning   → warning dict
      * /sys/state (shm)        → raw fallthrough (m1 decision: stays on /ws)
      * unknown addresses       → raw {osc_address, args} fallthrough
    """
    logger.debug("OSC ← 9101  %s %s", address, args)
    if address == "/sys/metrics":
        payload = _classify_metrics(args)
    elif address in ("/sys/warning", "/sys/binaural_warning"):
        payload = _classify_warning(address, args)
    else:
        # /sys/state (shm on-change key=value) + every other address keep the
        # existing raw shape for backward compat with current WS consumers.
        payload = {"osc_address": address, "args": list(args)}
    if _loop is not None and _broadcast_fn is not None:
        asyncio.run_coroutine_threadsafe(
            _broadcast_fn(payload), _loop
        )

