"""Room geometry model (speaker positions, room bounds)."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Speaker:
    speaker_id: int
    x: float
    y: float
    z: float
    label: str = ""


@dataclass
class RoomGeometry:
    width: float = 10.0   # metres
    depth: float = 10.0
    height: float = 3.0
    speakers: list[Speaker] = field(default_factory=list)

    def add_speaker(self, speaker_id: int, x: float, y: float, z: float, label: str = "") -> Speaker:
        sp = Speaker(speaker_id=speaker_id, x=x, y=y, z=z, label=label)
        self.speakers.append(sp)
        return sp
