"""Trajectory data model and sampling functions."""
from __future__ import annotations

import math
from dataclasses import dataclass
from enum import Enum
from typing import Tuple


class TrajectoryShape(Enum):
    CIRCLE = "circle"
    LINE = "line"
    LISSAJOUS = "lissajous"


@dataclass
class TrajectoryConfig:
    obj_id: int = 0
    shape: TrajectoryShape = TrajectoryShape.CIRCLE
    speed_hz: float = 0.5    # cycles per second
    radius: float = 1.0
    elevation_rad: float = 0.0
    # Line trajectory: az_start_rad → az_end_rad
    az_start_rad: float = 0.0
    az_end_rad: float = math.pi
    # Lissajous: az ratio
    lissajous_ratio: float = 2.0
    enabled: bool = False


def sample(cfg: TrajectoryConfig, t_seconds: float) -> Tuple[float, float, float]:
    """Return (az_rad, el_rad, dist_m) at time t."""
    if cfg.shape == TrajectoryShape.CIRCLE:
        phase = 2.0 * math.pi * cfg.speed_hz * t_seconds
        az = phase % (2.0 * math.pi)
        if az > math.pi:
            az -= 2.0 * math.pi
        return (az, cfg.elevation_rad, cfg.radius)
    elif cfg.shape == TrajectoryShape.LINE:
        # Triangle wave [0, 1, 0] over period 1/speed_hz
        phase = (cfg.speed_hz * t_seconds) % 1.0
        u = 2.0 * phase if phase < 0.5 else 2.0 * (1.0 - phase)
        az = cfg.az_start_rad + (cfg.az_end_rad - cfg.az_start_rad) * u
        return (az, cfg.elevation_rad, cfg.radius)
    elif cfg.shape == TrajectoryShape.LISSAJOUS:
        phase = 2.0 * math.pi * cfg.speed_hz * t_seconds
        az = math.pi * math.sin(phase)
        el = (math.pi / 4.0) * math.sin(cfg.lissajous_ratio * phase)
        return (az, el, cfg.radius)
    return (0.0, 0.0, cfg.radius)
