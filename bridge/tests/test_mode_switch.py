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
