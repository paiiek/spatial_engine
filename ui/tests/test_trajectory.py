"""Unit tests for trajectory model and runner."""
from __future__ import annotations

import math
from unittest.mock import MagicMock

import pytest

from spatial_engine_ui.state.trajectory import TrajectoryConfig, TrajectoryShape, sample
from spatial_engine_ui.controllers.trajectory_runner import TrajectoryRunner


def test_circle_period():
    """speed_hz=0.5, t=2.0 → one full cycle, az back near start (0)."""
    cfg = TrajectoryConfig(obj_id=0, shape=TrajectoryShape.CIRCLE, speed_hz=0.5, radius=1.0)
    az0, _, _ = sample(cfg, 0.0)
    az_end, _, _ = sample(cfg, 2.0)  # period = 1/0.5 = 2 s
    assert abs(az_end - az0) < 1e-9


def test_circle_at_quarter():
    """speed_hz=1.0, t=0.25 → az ≈ π/2."""
    cfg = TrajectoryConfig(obj_id=0, shape=TrajectoryShape.CIRCLE, speed_hz=1.0, radius=1.0)
    az, _, _ = sample(cfg, 0.25)
    assert abs(az - math.pi / 2.0) < 1e-9


def test_line_endpoints():
    """Line: t=0 → az_start; t=0.5/speed_hz → az_end."""
    cfg = TrajectoryConfig(
        obj_id=1,
        shape=TrajectoryShape.LINE,
        speed_hz=1.0,
        radius=1.0,
        az_start_rad=-math.pi / 2,
        az_end_rad=math.pi / 2,
    )
    az_start, _, _ = sample(cfg, 0.0)
    az_end, _, _ = sample(cfg, 0.5)  # half period → peak
    assert abs(az_start - (-math.pi / 2)) < 1e-9
    assert abs(az_end - (math.pi / 2)) < 1e-9


def test_runner_calls_send():
    """TrajectoryRunner.tick calls send_fn for enabled configs."""
    calls = []
    runner = TrajectoryRunner(send_fn=lambda obj_id, az, el, dist: calls.append((obj_id, az, el, dist)))
    cfg = TrajectoryConfig(obj_id=3, shape=TrajectoryShape.CIRCLE, speed_hz=1.0, enabled=True)
    runner.add(cfg)
    runner.tick(0.5)
    assert len(calls) == 1
    assert calls[0][0] == 3  # obj_id


def test_runner_disabled_not_called():
    """Disabled config must not trigger send_fn."""
    calls = []
    runner = TrajectoryRunner(send_fn=lambda *a: calls.append(a))
    cfg = TrajectoryConfig(obj_id=5, shape=TrajectoryShape.CIRCLE, speed_hz=1.0, enabled=False)
    runner.add(cfg)
    runner.tick(1.0)
    assert calls == []


def test_lissajous_sample():
    """Lissajous at t=0 → az=0, el=0."""
    cfg = TrajectoryConfig(obj_id=0, shape=TrajectoryShape.LISSAJOUS, speed_hz=1.0, radius=2.0)
    az, el, dist = sample(cfg, 0.0)
    assert abs(az) < 1e-9
    assert abs(el) < 1e-9
    assert dist == 2.0
