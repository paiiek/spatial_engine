"""WebGUI asyncio trajectory runner."""
from __future__ import annotations

import asyncio
import time
from dataclasses import asdict
from typing import Dict, Optional

from spatial_engine_ui.state.trajectory import TrajectoryConfig, sample


class WebTrajectoryRunner:
    def __init__(self, osc_send_fn):
        """osc_send_fn: callable(address, *args) — same as osc_bridge.send_osc."""
        self._send = osc_send_fn
        self._configs: Dict[int, TrajectoryConfig] = {}
        self._task: Optional[asyncio.Task] = None
        self._running = False
        self._t0 = time.monotonic()

    def upsert(self, cfg: TrajectoryConfig):
        self._configs[cfg.obj_id] = cfg

    def remove(self, obj_id: int):
        self._configs.pop(obj_id, None)

    def list_configs(self):
        return list(self._configs.values())

    def _send_aed(self, obj_id: int, az: float, el: float, dist: float):
        if self._send is not None:
            self._send(f"/adm/obj/{obj_id}/aed", az, el, dist)

    async def _loop(self):
        while self._running:
            t = time.monotonic() - self._t0
            for cfg in list(self._configs.values()):
                if cfg.enabled:
                    az, el, dist = sample(cfg, t)
                    self._send_aed(cfg.obj_id, az, el, dist)
            await asyncio.sleep(1.0 / 60.0)

    async def start(self):
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self._loop())

    async def stop(self):
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None
