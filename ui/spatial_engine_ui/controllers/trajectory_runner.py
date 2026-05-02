"""Trajectory runner: calls send_fn at each tick for enabled configs."""
from __future__ import annotations

import time
from typing import Callable, Dict

from spatial_engine_ui.state.trajectory import TrajectoryConfig, sample


class TrajectoryRunner:
    def __init__(self, send_fn: Callable[[int, float, float, float], None]):
        self._send = send_fn
        self._configs: Dict[int, TrajectoryConfig] = {}
        self._t0 = time.monotonic()

    def add(self, cfg: TrajectoryConfig):
        self._configs[cfg.obj_id] = cfg

    def remove(self, obj_id: int):
        self._configs.pop(obj_id, None)

    def get(self, obj_id: int):
        return self._configs.get(obj_id)

    def tick(self, t_seconds: float):
        for cfg in self._configs.values():
            if not cfg.enabled:
                continue
            az, el, dist = sample(cfg, t_seconds)
            self._send(cfg.obj_id, az, el, dist)
