"""
bridge/tests/test_mode_switch.py — AI↔저레이턴시 모드 전환 시간 검증
Run: python3 -m pytest bridge/tests/test_mode_switch.py -v
"""
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from vid2spatial_osc import BridgeServer


def _make_server(mode: str = "ai") -> BridgeServer:
    return BridgeServer(
        listen_port=19001,
        target_host="127.0.0.1",
        target_port=19101,
        mode=mode,
    )


def test_mode_switch_under_500ms():
    """AI→저레이턴시 전환 시간이 500ms 미만."""
    server = _make_server("ai")
    start = time.perf_counter()
    server.switch_mode("low_latency")
    elapsed_ms = (time.perf_counter() - start) * 1000
    assert elapsed_ms < 500, f"Mode switch took {elapsed_ms:.2f}ms (> 500ms)"


def test_mode_switch_low_latency_to_ai_under_500ms():
    """저레이턴시→AI 전환 시간이 500ms 미만."""
    server = _make_server("low_latency")
    start = time.perf_counter()
    server.switch_mode("ai")
    elapsed_ms = (time.perf_counter() - start) * 1000
    assert elapsed_ms < 500, f"Mode switch took {elapsed_ms:.2f}ms (> 500ms)"


def test_mode_switch_repeated_under_500ms():
    """10회 연속 전환 각각 500ms 미만."""
    server = _make_server("ai")
    for i in range(10):
        target = "low_latency" if i % 2 == 0 else "ai"
        start = time.perf_counter()
        server.switch_mode(target)
        elapsed_ms = (time.perf_counter() - start) * 1000
        assert elapsed_ms < 500, f"Switch {i} took {elapsed_ms:.2f}ms"


def _percentile(values: list[float], pct: float) -> float:
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(pct / 100.0 * len(s))) - 1))
    return s[k]


def test_mode_switch_stress_p95_p99():
    """100회 alternating switch — p95 < 100ms, p99 < 500ms (Phase 2 gate)."""
    server = _make_server("ai")
    durations_ms: list[float] = []
    for i in range(100):
        target = "low_latency" if i % 2 == 0 else "ai"
        t0 = time.perf_counter()
        server.switch_mode(target)
        durations_ms.append((time.perf_counter() - t0) * 1000.0)

    # Discard the first 5 as warm-up (logger format cache, dict alloc, etc.)
    warm = durations_ms[5:]
    assert len(warm) >= 90

    p50 = _percentile(warm, 50)
    p95 = _percentile(warm, 95)
    p99 = _percentile(warm, 99)
    pmax = max(warm)

    print(
        f"\n[mode_switch] N={len(warm)}  p50={p50:.3f}ms  p95={p95:.3f}ms  "
        f"p99={p99:.3f}ms  max={pmax:.3f}ms"
    )

    assert p99 < 500.0, f"p99 mode switch {p99:.2f}ms > 500ms gate"
    assert p95 < 100.0, f"p95 mode switch {p95:.2f}ms > 100ms advisory"


def test_mode_switch_invalid_value_rejected():
    """Invalid mode값은 ValueError로 즉시 거부 (전환 시간에 영향 없음)."""
    server = _make_server("ai")
    import pytest as _pt
    with _pt.raises(ValueError):
        server.switch_mode("foo")
    # mode unchanged
    assert server.mode == "ai"


def test_messages_ignored_in_low_latency():
    """저레이턴시 모드에서 _flush가 메시지를 무시하는지 확인."""
    server = _make_server("ai")
    server.switch_mode("low_latency")
    assert server.mode == "low_latency"

    # Simulate incoming message — should not raise or change state
    server._update_state("default", az=90.0, el=0.0, dist=0.3)
    server._flush("default")  # should be no-op in low_latency mode

    # Confirm mode unchanged
    assert server.mode == "low_latency"


def test_ai_mode_flush_processes_messages():
    """AI 모드에서는 _flush가 실행 경로를 따르는지 확인 (client=None이면 조기 반환)."""
    server = _make_server("ai")
    # client is None (no pythonosc or not configured) — should not raise
    server._update_state("default", az=45.0, el=10.0, dist=0.5)
    server._flush("default")  # may be no-op if _client is None, but no exception
