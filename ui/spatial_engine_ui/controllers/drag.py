"""Drag coalescer — 120 Hz rate-limit, last-write-wins per object, monotonic seq.

Design:
- Each object gets an independent u32 seq counter (monotonic, never resets).
- Pending dict maps obj_id -> (x, z, seq): newest position only (last-write-wins).
- A QTimer fires at 120 Hz and flushes all pending entries to the OSC client.
- If two updates arrive for the same object before the next tick, oldest is dropped.
"""
from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Dict


@dataclass
class _Pending:
    x: float
    z: float
    seq: int


class DragCoalescer:
    """Rate-limited, last-write-wins coalescer for drag OSC messages.

    Can be used without Qt (pure Python timer via callback) or with Qt
    (set use_qt=True and call start() after QApplication exists).
    """

    RATE_HZ: int = 120

    def __init__(
        self,
        send_fn: Callable[[int, float, float, int], None],
        *,
        use_qt: bool = False,
    ) -> None:
        """
        Args:
            send_fn: called as send_fn(obj_id, x, z, seq) on each flush tick.
            use_qt: if True, use QTimer for ticking; else manual tick() calls.
        """
        self._send = send_fn
        self._pending: Dict[int, _Pending] = {}
        self._seqs: Dict[int, int] = {}   # per-object monotonic counter
        self._use_qt = use_qt
        self._timer = None
        self._tick_interval_s: float = 1.0 / self.RATE_HZ

        # Stats
        self._ticks: int = 0
        self._start_time: float | None = None

    # --- public API ---

    def push(self, obj_id: int, x: float, z: float) -> int:
        """Record a new position for obj_id. Returns the assigned seq."""
        seq = self._next_seq(obj_id)
        self._pending[obj_id] = _Pending(x=x, z=z, seq=seq)
        return seq

    def tick(self) -> int:
        """Flush all pending entries. Returns number of messages sent."""
        sent = 0
        if not self._pending:
            self._ticks += 1
            return 0
        items = list(self._pending.items())
        self._pending.clear()
        for obj_id, p in items:
            self._send(obj_id, p.x, p.z, p.seq)
            sent += 1
        self._ticks += 1
        return sent

    def start_manual_loop(self, duration_s: float) -> None:
        """Blocking loop for test: tick at RATE_HZ for duration_s seconds."""
        import time as _time
        self._start_time = _time.monotonic()
        deadline = self._start_time + duration_s
        interval = self._tick_interval_s
        next_tick = self._start_time + interval
        while True:
            now = _time.monotonic()
            if now >= deadline:
                break
            sleep_for = next_tick - now
            if sleep_for > 0:
                _time.sleep(sleep_for)
            self.tick()
            next_tick += interval

    def start_qt(self) -> None:
        """Start a QTimer-driven flush loop (call after QApplication exists)."""
        from PySide6.QtCore import QTimer
        self._timer = QTimer()
        self._timer.setInterval(int(1000 / self.RATE_HZ))
        self._timer.timeout.connect(self.tick)
        self._timer.start()

    def stop_qt(self) -> None:
        if self._timer is not None:
            self._timer.stop()
            self._timer = None

    def current_seq(self, obj_id: int) -> int:
        """Return current seq for obj_id (0 if never pushed)."""
        return self._seqs.get(obj_id, 0)

    def pending_count(self) -> int:
        return len(self._pending)

    # --- private ---

    def _next_seq(self, obj_id: int) -> int:
        seq = self._seqs.get(obj_id, 0) + 1
        self._seqs[obj_id] = seq & 0xFFFFFFFF  # keep u32
        return self._seqs[obj_id]
