"""MIDI Program Change → /scene/load OSC bridge."""

try:
    import mido
    _MIDO_AVAILABLE = True
except ImportError:
    _MIDO_AVAILABLE = False

try:
    if _MIDO_AVAILABLE:
        mido.get_input_names()
        _RTMIDI_AVAILABLE = True
    else:
        _RTMIDI_AVAILABLE = False
except Exception:
    _RTMIDI_AVAILABLE = False

import threading
import time

SCENE_NAME_TEMPLATE = "scene_{pc}"
OSC_SCENE_LOAD = "/scene/load"


class MidiBridge:
    """Routes MIDI PC events to /scene/load OSC messages."""

    def __init__(self, osc_client=None, midi_port_name: str | None = None):
        self._client = osc_client
        self._port_name = midi_port_name
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._port = None

    @staticmethod
    def is_available() -> bool:
        return _MIDO_AVAILABLE and _RTMIDI_AVAILABLE

    @staticmethod
    def pc_to_scene_name(pc: int) -> str:
        return SCENE_NAME_TEMPLATE.format(pc=pc)

    def handle_message(self, msg) -> str | None:
        if hasattr(msg, 'type') and msg.type == 'program_change':
            name = self.pc_to_scene_name(msg.program)
            if self._client is not None:
                self._client.send_message(OSC_SCENE_LOAD, name)
            return name
        return None

    def _discover_port(self) -> str | None:
        if not _RTMIDI_AVAILABLE:
            return None
        if self._port_name:
            return self._port_name
        available = mido.get_input_names()
        if not available:
            import sys
            print("[midi_bridge] no MIDI input ports available", file=sys.stderr)
            return None
        # prefer port with "loopback" in name (case-insensitive), else first
        for name in available:
            if "loopback" in name.lower():
                return name
        return available[0]

    def _worker(self) -> None:
        while not self._stop_event.is_set():
            if self._port is None:
                break
            # REQUIRED: iter_pending() non-blocking poll; blocking receive() PROHIBITED
            for msg in self._port.iter_pending():
                self.handle_message(msg)
            time.sleep(0.001)

    def start(self) -> None:
        """Start listening. No-op if mido/rtmidi unavailable or already running."""
        if not _RTMIDI_AVAILABLE:
            return
        if self._thread is not None and self._thread.is_alive():
            return
        port_name = self._discover_port()
        if port_name is None:
            return
        try:
            self._port = mido.open_input(port_name)
        except (IOError, OSError, RuntimeError):
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._worker, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """Stop listening. No-op if not started."""
        if self._thread is None:
            return
        # Correct ordering: set event → join → close (prevents race on half-closed port)
        self._stop_event.set()
        self._thread.join(timeout=1.0)
        if self._port is not None:
            try:
                self._port.close()
            except Exception:
                pass
            self._port = None
        self._thread = None
