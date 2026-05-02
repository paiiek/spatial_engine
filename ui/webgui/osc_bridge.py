"""OSC bridge: WebSocket ↔ OSC 9100/9101 for spatial_engine WebGUI."""
from __future__ import annotations

import asyncio
import json
import logging
import threading
from typing import TYPE_CHECKING

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
    global _osc_client, _loop, _broadcast_fn

    _loop = loop
    _broadcast_fn = broadcast_async_fn
    _osc_client = SimpleUDPClient(osc_host, OSC_CMD_PORT)

    # Wire server.py to use our send_osc
    import ui.webgui.server as _server  # noqa: PLC0415
    _server.osc_send_fn = send_osc

    # Listener on 9101: engine state → WebSocket broadcast
    d_state = osc_dispatcher.Dispatcher()
    d_state.set_default_handler(_handle_state_message)
    srv_state = osc_server.ThreadingOSCUDPServer(
        (osc_host, OSC_STATE_PORT), d_state
    )
    t_state = threading.Thread(target=srv_state.serve_forever, daemon=True)
    t_state.start()

    logger.info(
        "OSC bridge started: listen=9101 send=9100 host=%s", osc_host
    )


# ---------------------------------------------------------------------------
# Internal OSC handler
# ---------------------------------------------------------------------------

def _handle_state_message(address: str, *args) -> None:
    """Receive OSC state from engine (port 9101) → broadcast to WS clients."""
    payload = {"osc_address": address, "args": list(args)}
    logger.debug("OSC ← 9101  %s %s", address, args)
    if _loop is not None and _broadcast_fn is not None:
        asyncio.run_coroutine_threadsafe(
            _broadcast_fn(payload), _loop
        )

