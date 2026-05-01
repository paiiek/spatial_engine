"""MIDI Program Change → /scene/load OSC bridge.

Requires 'mido' package (pip install mido). If unavailable, MidiBridge is a no-op stub.
Usage: MidiBridge(osc_client, midi_port_name=None).start()
"""

try:
    import mido
    _MIDO_AVAILABLE = True
except ImportError:
    _MIDO_AVAILABLE = False

SCENE_NAME_TEMPLATE = "scene_{pc}"  # PC 0 → "scene_0", PC 5 → "scene_5"
OSC_SCENE_LOAD = "/scene/load"

class MidiBridge:
    """Routes MIDI PC events to /scene/load OSC messages."""

    def __init__(self, osc_client=None, midi_port_name: str | None = None):
        self._client = osc_client
        self._port_name = midi_port_name

    @staticmethod
    def is_available() -> bool:
        return _MIDO_AVAILABLE

    @staticmethod
    def pc_to_scene_name(pc: int) -> str:
        """MIDI PC number (0-127) → scene name string."""
        return SCENE_NAME_TEMPLATE.format(pc=pc)

    def handle_message(self, msg) -> str | None:
        """Process a MIDI message. Sends /scene/load OSC and returns scene name if PC event."""
        if hasattr(msg, 'type') and msg.type == 'program_change':
            name = self.pc_to_scene_name(msg.program)
            if self._client is not None:
                self._client.send_message(OSC_SCENE_LOAD, name)
            return name
        return None

    def start(self) -> None:
        """Start listening. No-op if mido not available or no port configured.

        Real port-open + PC forwarding deferred to v1+ lab deployment.
        """
        if not _MIDO_AVAILABLE or not self._port_name:
            return

    def stop(self) -> None:
        """No-op stop (paired with deferred start)."""
        return
