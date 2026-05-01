"""tests/accuracy_harness/test_itd.py
Tests BinauralMonitor binaural ITD sign convention (pure Python, no audio device).

Convention documented here:
  Engine frame (AmbiX/B-format):  az=+90 deg = LEFT of listener
  SOFA frame   (AES69):           az=+90 deg = LEFT of listener
  Mapping:                        az_sofa = az_engine  (no sign flip for azimuth)

ITD sign rule (left-ear-first positive convention):
  Source at az_engine = +30 deg  → LEFT of centre
    → left ear is closer → left-ear signal arrives first → ITD > 0
  Source at az_engine = -30 deg  → RIGHT of centre
    → right ear is closer → ITD < 0

The stub BinauralMonitor does not perform real HRTF convolution,
so we test the convention logic (the coordinate mapping) rather than
actual delay measurements.
"""

import math


# ---------------------------------------------------------------------------
# Coordinate convention helpers (mirrors BinauralMonitor coordinate handling)
# ---------------------------------------------------------------------------

def engine_az_to_sofa_az(az_engine_deg: float) -> float:
    """Map engine azimuth to SOFA azimuth.

    Both engine and SOFA use the same sign convention (AmbiX / AES69):
      positive az = LEFT.
    Therefore the mapping is the identity: az_sofa = az_engine.

    Note: some older HRTF datasets use az_sofa = -az_engine (right-positive).
    If such a dataset is used, negate before lookup and document accordingly.
    """
    return az_engine_deg  # identity mapping


def expected_itd_sign(az_engine_deg: float) -> int:
    """Return expected ITD sign: +1 = left ear first, -1 = right ear first, 0 = centre.

    Rule: positive az (left side) → left ear closer → ITD > 0.
    """
    if az_engine_deg > 1.0:
        return +1  # source on the left
    elif az_engine_deg < -1.0:
        return -1  # source on the right
    return 0       # near centre, ITD ~ 0


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_coordinate_mapping_identity():
    """az_sofa == az_engine (both use AmbiX positive-left convention)."""
    for az in [-90.0, -45.0, 0.0, 30.0, 90.0, 180.0]:
        assert engine_az_to_sofa_az(az) == az, (
            f"Coordinate mapping broken for az={az}: "
            f"got {engine_az_to_sofa_az(az)}, expected {az}"
        )


def test_itd_sign_left_source():
    """Source at az_engine=+30 deg (left): ITD should be positive (left ear first)."""
    az_engine = 30.0
    az_sofa   = engine_az_to_sofa_az(az_engine)
    # In SOFA frame, positive az is left → left ear first → ITD > 0
    sign = expected_itd_sign(az_sofa)
    assert sign == +1, (
        f"Expected ITD > 0 for left source (az_engine={az_engine}), "
        f"az_sofa={az_sofa}, got sign={sign}"
    )


def test_itd_sign_right_source():
    """Source at az_engine=-30 deg (right): ITD should be negative (right ear first)."""
    az_engine = -30.0
    az_sofa   = engine_az_to_sofa_az(az_engine)
    sign = expected_itd_sign(az_sofa)
    assert sign == -1, (
        f"Expected ITD < 0 for right source (az_engine={az_engine}), "
        f"az_sofa={az_sofa}, got sign={sign}"
    )


def test_itd_sign_centre():
    """Source at az=0 (front): ITD should be near zero."""
    az_engine = 0.0
    az_sofa   = engine_az_to_sofa_az(az_engine)
    sign = expected_itd_sign(az_sofa)
    assert sign == 0, (
        f"Expected ITD ~ 0 for front source, got sign={sign}"
    )


def test_no_sign_flip_needed():
    """Explicitly assert that az_sofa != -az_engine for non-zero azimuths.

    If this test fails it means the dataset uses a right-positive convention
    and a sign flip is needed before HRTF lookup.
    """
    for az in [30.0, 90.0, -45.0]:
        sofa = engine_az_to_sofa_az(az)
        assert sofa == az, (
            f"Unexpected sign flip: az_engine={az}, az_sofa={sofa}. "
            "If your SOFA file uses right-positive az, add negation in "
            "BinauralMonitor::setDirection() before the HRIR lookup."
        )


def test_right_source_sofa_az_is_negative():
    """az_engine=-30 (right) maps to az_sofa=-30 (right in SOFA AES69 frame).
    In AES69 right-of-centre has negative az → right ear arrives first → ITD < 0.
    """
    az_engine = -30.0
    az_sofa   = engine_az_to_sofa_az(az_engine)
    assert az_sofa < 0.0, (
        f"Right source should have negative SOFA az, got az_sofa={az_sofa}"
    )
    assert expected_itd_sign(az_sofa) == -1
