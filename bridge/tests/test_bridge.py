"""
Unit tests for bridge/vid2spatial_osc.py — production bridge.
Run: python3 -m pytest bridge/tests/test_bridge.py -v
"""
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from vid2spatial_osc import (
    OscTranslator,
    IIRSmoother,
    RateLimiter,
    ObjectMapper,
    BridgeServer,
)


# ---------------------------------------------------------------------------
# OscTranslator
# ---------------------------------------------------------------------------

class TestOscTranslator:
    def test_az_right_becomes_negative(self):
        assert OscTranslator.az_pipeline_to_adm(90.0) == -90.0

    def test_az_left_becomes_positive(self):
        assert OscTranslator.az_pipeline_to_adm(-45.0) == 45.0

    def test_az_zero_stays_zero(self):
        assert OscTranslator.az_pipeline_to_adm(0.0) == 0.0

    def test_az_180_boundary(self):
        assert OscTranslator.az_pipeline_to_adm(180.0) == -180.0

    def test_az_minus_180_boundary(self):
        assert OscTranslator.az_pipeline_to_adm(-180.0) == 180.0

    def test_dist_near_1_becomes_0(self):
        assert OscTranslator.dist_v2s_to_adm(1.0) == 0.0

    def test_dist_far_0_becomes_1(self):
        assert OscTranslator.dist_v2s_to_adm(0.0) == 1.0

    def test_dist_midpoint(self):
        assert abs(OscTranslator.dist_v2s_to_adm(0.5) - 0.5) < 1e-9

    def test_elev_identity(self):
        assert OscTranslator.elev_to_adm(30.0) == 30.0
        assert OscTranslator.elev_to_adm(-15.0) == -15.0


# ---------------------------------------------------------------------------
# IIRSmoother
# ---------------------------------------------------------------------------

class TestIIRSmoother:
    def test_first_call_returns_input(self):
        s = IIRSmoother(alpha=0.3)
        assert s.smooth("x", 10.0) == 10.0

    def test_converges_toward_target(self):
        s = IIRSmoother(alpha=0.3)
        s.smooth("x", 0.0)
        for _ in range(50):
            val = s.smooth("x", 100.0)
        assert val > 95.0

    def test_separate_keys_independent(self):
        s = IIRSmoother(alpha=0.3)
        s.smooth("a", 0.0)
        s.smooth("b", 100.0)
        va = s.smooth("a", 0.0)
        vb = s.smooth("b", 100.0)
        assert va < vb

    def test_alpha_1_passthrough(self):
        s = IIRSmoother(alpha=1.0)
        s.smooth("x", 0.0)
        assert s.smooth("x", 42.0) == 42.0

    def test_reset_clears_state(self):
        s = IIRSmoother(alpha=0.3)
        s.smooth("x", 0.0)
        s.smooth("x", 100.0)
        s.reset("x")
        # After reset, next call initialises fresh
        assert s.smooth("x", 50.0) == 50.0


# ---------------------------------------------------------------------------
# RateLimiter
# ---------------------------------------------------------------------------

class TestRateLimiter:
    def test_first_call_allowed(self):
        r = RateLimiter(hz=60.0)
        assert r.allow("obj_1") is True

    def test_immediate_second_call_blocked(self):
        r = RateLimiter(hz=60.0)
        r.allow("obj_1")
        assert r.allow("obj_1") is False

    def test_different_keys_independent(self):
        r = RateLimiter(hz=60.0)
        r.allow("obj_1")
        assert r.allow("obj_2") is True

    def test_passes_after_interval(self):
        r = RateLimiter(hz=200.0)  # 5 ms interval
        r.allow("x")
        time.sleep(0.006)
        assert r.allow("x") is True


# ---------------------------------------------------------------------------
# ObjectMapper
# ---------------------------------------------------------------------------

class TestObjectMapper:
    def test_auto_assign_starts_at_1(self):
        m = ObjectMapper()
        assert m.get("person_0") == 1

    def test_same_id_same_number(self):
        m = ObjectMapper()
        assert m.get("person_0") == m.get("person_0")

    def test_different_ids_different_numbers(self):
        m = ObjectMapper()
        assert m.get("person_0") != m.get("person_1")

    def test_static_map_respected(self):
        m = ObjectMapper(static_map={"cam_1": 5})
        assert m.get("cam_1") == 5

    def test_new_id_after_static_map(self):
        m = ObjectMapper(static_map={"cam_1": 5})
        assert m.get("cam_new") == 6

    def test_max_objects_limit(self):
        m = ObjectMapper(max_objects=2)
        m.get("a")
        m.get("b")
        result = m.get("c")  # exceeds max
        assert result is None


# ---------------------------------------------------------------------------
# BridgeServer mode switch
# ---------------------------------------------------------------------------

class TestBridgeServerMode:
    def _make_server(self, mode="ai"):
        return BridgeServer(
            listen_port=19000,
            target_host="127.0.0.1",
            target_port=19100,
            mode=mode,
        )

    def test_default_mode_ai(self):
        s = self._make_server("ai")
        assert s.mode == "ai"

    def test_switch_to_low_latency(self):
        s = self._make_server("ai")
        s.switch_mode("low_latency")
        assert s.mode == "low_latency"

    def test_switch_back_to_ai(self):
        s = self._make_server("low_latency")
        s.switch_mode("ai")
        assert s.mode == "ai"

    def test_invalid_mode_raises(self):
        s = self._make_server()
        try:
            s.switch_mode("invalid")
            assert False, "Expected ValueError"
        except ValueError:
            pass

    def test_low_latency_ignores_flush(self):
        """In low_latency mode, _flush must not send (no client set anyway, just check no error)."""
        s = self._make_server("low_latency")
        s._update_state("default", az=45.0, el=0.0, dist=0.5)
        # _flush returns early; no exception
        s._flush("default")

    def test_mode_switch_preserves_last_state(self):
        """After switching mode, accumulated state is preserved."""
        s = self._make_server("ai")
        s._update_state("default", az=30.0, el=5.0, dist=0.7)
        s.switch_mode("low_latency")
        with s._state_lock:
            state = s._state.get("default", {})
        assert state.get("az") == 30.0
