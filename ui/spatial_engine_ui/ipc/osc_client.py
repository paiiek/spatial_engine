"""OSC client: UI → core command port."""
from __future__ import annotations

from pythonosc.udp_client import SimpleUDPClient


class OscClient:
    """Thin wrapper around python-osc UDP client."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9100) -> None:
        self._client = SimpleUDPClient(host, port)
        self.host = host
        self.port = port

    def send(self, address: str, *args: object) -> None:
        self._client.send_message(address, list(args))

    def send_object_pos(self, obj_id: int, x: float, z: float, seq: int) -> None:
        from .protocol import object_pos_address
        self._client.send_message(object_pos_address(obj_id), [obj_id, x, z, seq])

    def send_noise_type(self, channel: int, noise_type: str) -> None:
        from .protocol import noise_type_address
        self._client.send_message(noise_type_address(channel), [noise_type])

    def send_noise_gain(self, channel: int, gain_db: float) -> None:
        from .protocol import noise_gain_address
        self._client.send_message(noise_gain_address(channel), [gain_db])

    def send_object_algo(self, obj_id: int, algo: int) -> None:
        """Set per-object rendering algorithm (0=VBAP, 1=WFS, 2=DBAP)."""
        from .protocol import ADDR_OBJ_ALGO
        self._client.send_message(ADDR_OBJ_ALGO, [int(obj_id), int(algo)])

    def send_object_dsp(self, obj_id: int, param: int, value: float) -> None:
        """Set per-object DSP parameter (EQ/Delay/HF/ReverbSend)."""
        from .protocol import ADDR_OBJ_DSP
        self._client.send_message(ADDR_OBJ_DSP, [int(obj_id), int(param), float(value)])

    def send_transport_play(self) -> None:
        from .protocol import ADDR_TRANSPORT_PLAY
        self._client.send_message(ADDR_TRANSPORT_PLAY, [])

    def send_transport_stop(self) -> None:
        from .protocol import ADDR_TRANSPORT_STOP
        self._client.send_message(ADDR_TRANSPORT_STOP, [])
