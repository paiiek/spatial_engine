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
