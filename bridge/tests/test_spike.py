"""
Unit tests for bridge/spike_vid2spatial_osc.py coordinate transforms and helpers.
Run: python3 -m pytest bridge/tests/test_spike.py -v
"""
import sys
import os

# Allow import without package install
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from spike_vid2spatial_osc import (
    az_pipeline_to_adm,
    dist_v2s_to_adm,
    IIRSmoother,
    RateLimiter,
    ObjectMapper,
)


class TestAzimuthConversion:
    def test_right_becomes_negative(self):
        """Pipeline RIGHT=+az -> ADM LEFT=+az means sign flip."""
        assert az_pipeline_to_adm(90.0) == -90.0

    def test_left_becomes_positive(self):
        assert az_pipeline_to_adm(-45.0) == 45.0

    def test_zero_stays_zero(self):
        assert az_pipeline_to_adm(0.0) == 0.0

    def test_180_boundary(self):
        assert az_pipeline_to_adm(180.0) == -180.0

    def test_minus_180_boundary(self):
        assert az_pipeline_to_adm(-180.0) == 180.0


class TestDistanceConversion:
    def test_near_1_becomes_far_0(self):
        """vid2spatial near=1.0 -> ADM dist=0.0 (close)."""
        assert dist_v2s_to_adm(1.0) == 0.0

    def test_far_0_becomes_near_1(self):
        """vid2spatial far=0.0 -> ADM dist=1.0 (far)."""
        assert dist_v2s_to_adm(0.0) == 1.0

    def test_midpoint(self):
        assert abs(dist_v2s_to_adm(0.5) - 0.5) < 1e-9

    def test_output_range_low(self):
        assert 0.0 <= dist_v2s_to_adm(0.9) <= 1.0

    def test_output_range_high(self):
        assert 0.0 <= dist_v2s_to_adm(0.1) <= 1.0


class TestIIRSmoother:
    def test_first_call_returns_input(self):
        s = IIRSmoother(alpha=0.3)
        assert s.smooth("x", 10.0) == 10.0

    def test_converges_toward_target(self):
        s = IIRSmoother(alpha=0.3)
        s.smooth("x", 0.0)  # init at 0
        for _ in range(50):
            val = s.smooth("x", 100.0)
        assert val > 95.0, f"Expected convergence, got {val}"

    def test_separate_keys_independent(self):
        s = IIRSmoother(alpha=0.3)
        s.smooth("a", 0.0)
        s.smooth("b", 100.0)
        va = s.smooth("a", 0.0)
        vb = s.smooth("b", 100.0)
        assert va < vb


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


class TestObjectMapper:
    def test_auto_assign_starts_at_1(self):
        m = ObjectMapper()
        assert m.get("person_0") == 1

    def test_same_id_same_number(self):
        m = ObjectMapper()
        n1 = m.get("person_0")
        n2 = m.get("person_0")
        assert n1 == n2

    def test_different_ids_different_numbers(self):
        m = ObjectMapper()
        n1 = m.get("person_0")
        n2 = m.get("person_1")
        assert n1 != n2

    def test_static_map_respected(self):
        m = ObjectMapper(static_map={"cam_1": 5})
        assert m.get("cam_1") == 5

    def test_new_id_after_static_map(self):
        m = ObjectMapper(static_map={"cam_1": 5})
        n = m.get("cam_new")
        assert n == 6
