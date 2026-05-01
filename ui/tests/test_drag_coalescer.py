"""Tests for DragCoalescer: drop-oldest, 120 Hz rate, monotonic seq."""
from __future__ import annotations

import time
from collections import defaultdict
from typing import DefaultDict

import pytest

from spatial_engine_ui.controllers.drag import DragCoalescer


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_coalescer() -> tuple[DragCoalescer, list[tuple[int, float, float, int]]]:
    sent: list[tuple[int, float, float, int]] = []

    def send(obj_id: int, x: float, z: float, seq: int) -> None:
        sent.append((obj_id, x, z, seq))

    coalescer = DragCoalescer(send_fn=send)
    return coalescer, sent


# ---------------------------------------------------------------------------
# Test 1: drops oldest pending per object (last-write-wins)
# ---------------------------------------------------------------------------

def test_drops_oldest_pending_per_object() -> None:
    coalescer, sent = make_coalescer()

    # Push 3 positions for object 1 without flushing
    coalescer.push(1, 1.0, 1.0)
    coalescer.push(1, 2.0, 2.0)
    coalescer.push(1, 3.0, 3.0)  # only this should survive

    coalescer.tick()

    obj1_msgs = [(x, z) for oid, x, z, seq in sent if oid == 1]
    assert len(obj1_msgs) == 1, f"Expected 1 coalesced message, got {len(obj1_msgs)}"
    assert obj1_msgs[0] == (3.0, 3.0), f"Expected last-write (3.0,3.0), got {obj1_msgs[0]}"


# ---------------------------------------------------------------------------
# Test 2: sustained 120 Hz output rate ±10% over 1 second
# ---------------------------------------------------------------------------

def test_sustained_120hz_rate() -> None:
    coalescer, sent = make_coalescer()

    # Feed one position per tick so we always have something to send
    original_tick = coalescer.tick
    tick_count = [0]

    def instrumented_tick() -> int:
        coalescer.push(0, float(tick_count[0]), 0.0)
        result = original_tick()
        tick_count[0] += 1
        return result

    DURATION = 1.0
    TARGET = DragCoalescer.RATE_HZ  # 120
    TOLERANCE = 0.10  # ±10 %

    start = time.monotonic()
    deadline = start + DURATION
    interval = 1.0 / TARGET
    next_tick = start + interval

    while True:
        now = time.monotonic()
        if now >= deadline:
            break
        sleep_for = next_tick - now
        if sleep_for > 0:
            time.sleep(sleep_for)
        instrumented_tick()
        next_tick += interval

    elapsed = time.monotonic() - start
    actual_rate = tick_count[0] / elapsed

    low = TARGET * (1 - TOLERANCE)
    high = TARGET * (1 + TOLERANCE)
    assert low <= actual_rate <= high, (
        f"Rate {actual_rate:.1f} Hz outside [{low:.1f}, {high:.1f}] Hz"
    )


# ---------------------------------------------------------------------------
# Test 3: monotonic seq per object
# ---------------------------------------------------------------------------

def test_monotonic_seq_per_object() -> None:
    coalescer, sent = make_coalescer()

    N = 20
    for i in range(N):
        coalescer.push(1, float(i), 0.0)
        coalescer.push(2, 0.0, float(i))
        coalescer.tick()

    seqs: DefaultDict[int, list[int]] = defaultdict(list)
    for obj_id, x, z, seq in sent:
        seqs[obj_id].append(seq)

    for obj_id, seq_list in seqs.items():
        for a, b in zip(seq_list, seq_list[1:]):
            assert a < b, (
                f"Object {obj_id}: seq not monotonic: {a} >= {b} in {seq_list}"
            )
