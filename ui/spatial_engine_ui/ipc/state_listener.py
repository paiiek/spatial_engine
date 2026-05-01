"""OSC state listener: core → UI state port (threaded server)."""
from __future__ import annotations

import threading
from collections.abc import Callable
from typing import Any

from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import ThreadingOSCUDPServer

from .protocol import (
    ADDR_HEARTBEAT_MISS,
    ADDR_MATRIX,
    ADDR_PROTOCOL_VERSION,
    ADDR_STATE,
    ADDR_WARNING,
)


class StateListener:
    """Listens on the state port and dispatches callbacks."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9101) -> None:
        self.host = host
        self.port = port
        self._dispatcher = Dispatcher()
        self._server: ThreadingOSCUDPServer | None = None
        self._thread: threading.Thread | None = None

        # Public callback hooks — set by owner before start()
        self.on_state: Callable[[Any], None] | None = None
        self.on_protocol_version: Callable[[int], None] | None = None
        self.on_warning: Callable[[int, str, str], None] | None = None
        self.on_matrix: Callable[[bytes], None] | None = None
        self.on_heartbeat_miss: Callable[[], None] | None = None

        self._dispatcher.map(ADDR_STATE, self._handle_state)
        self._dispatcher.map(ADDR_PROTOCOL_VERSION, self._handle_protocol_version)
        self._dispatcher.map(ADDR_WARNING, self._handle_warning)
        self._dispatcher.map(ADDR_MATRIX, self._handle_matrix)
        self._dispatcher.map(ADDR_HEARTBEAT_MISS, self._handle_heartbeat_miss)

    # --- internal handlers ---

    def _handle_state(self, address: str, *args: Any) -> None:
        if self.on_state:
            self.on_state(args)

    def _handle_protocol_version(self, address: str, *args: Any) -> None:
        version = args[0] if args else 0
        if self.on_protocol_version:
            self.on_protocol_version(int(version))

    def _handle_warning(self, address: str, *args: Any) -> None:
        schema_ver = args[0] if len(args) > 0 else 0
        warn_type = args[1] if len(args) > 1 else ""
        detail = args[2] if len(args) > 2 else ""
        if self.on_warning:
            self.on_warning(int(schema_ver), str(warn_type), str(detail))

    def _handle_matrix(self, address: str, *args: Any) -> None:
        payload = args[0] if args else b""
        if self.on_matrix:
            self.on_matrix(payload)

    def _handle_heartbeat_miss(self, address: str, *args: Any) -> None:
        if self.on_heartbeat_miss:
            self.on_heartbeat_miss()

    # --- lifecycle ---

    def start(self) -> None:
        self._server = ThreadingOSCUDPServer((self.host, self.port), self._dispatcher)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        if self._server:
            self._server.shutdown()
            self._server = None
        if self._thread:
            self._thread.join(timeout=2)
            self._thread = None
