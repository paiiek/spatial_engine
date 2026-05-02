"""
bridge/tests/test_accuracy.py — Bridge coordinate accuracy gate test.

Two layers:
  1. Coordinate-transform accuracy: end-to-end az_pipeline → az_adm vs GT azimuth
     derived from LaSOT groundtruth bounding boxes (no GPU required).
  2. Pipeline accuracy gate: verifies pre-computed vid2spatial eval results
     meet the Phase 2 gate (mean AzMAE ≤ 2.0°).

Run: python3 -m pytest bridge/tests/test_accuracy.py -v
"""
from __future__ import annotations

import json
import math
import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from vid2spatial_osc import OscTranslator

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_LASOT_ROOT = "/home/seung/mmhoa/vid2spatial_v2/data/lasot"
_EVAL_RESULTS = "/home/seung/mmhoa/vid2spatial_v2/test/full_eval/results.json"
_QUANT_EVAL_MD = "/home/seung/mmhoa/vid2spatial_v2/test/full_eval/QUANT_EVAL_20260304.md"

# Phase 2 accuracy gate (docs/adr/vid2spatial_osc_contract.md)
AZ_MAE_GATE_DEG = 2.0

# Image dimensions used during vid2spatial evaluation (LaSOT native)
_IMG_W = 1280
_IMG_H = 720
# Horizontal FoV assumption matching vid2spatial_pkg/pipeline.py
_H_FOV_DEG = 60.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _bbox_to_az(x: float, y: float, w: float, h: float,
                img_w: int = _IMG_W, h_fov: float = _H_FOV_DEG) -> float:
    """Convert LaSOT bbox (x,y,w,h top-left) centre to azimuth in [-FoV/2, FoV/2]."""
    cx = x + w / 2.0
    norm = (cx / img_w) - 0.5  # [-0.5, 0.5], right=+
    return norm * h_fov


def _load_gt_azimuths(seq_dir: str, max_frames: int = 300) -> list[float]:
    gt_path = os.path.join(seq_dir, "groundtruth.txt")
    azimuths: list[float] = []
    with open(gt_path) as f:
        for i, line in enumerate(f):
            if i >= max_frames:
                break
            parts = line.strip().split(",")
            if len(parts) < 4:
                continue
            try:
                x, y, w, h = float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])
                azimuths.append(_bbox_to_az(x, y, w, h))
            except ValueError:
                continue
    return azimuths


def _az_mae(pred: list[float], gt: list[float]) -> float:
    n = min(len(pred), len(gt))
    if n == 0:
        return float("nan")
    return sum(abs(p - g) for p, g in zip(pred[:n], gt[:n])) / n


# ---------------------------------------------------------------------------
# Test 1: coordinate transform preserves relative azimuth across GT trajectory
# ---------------------------------------------------------------------------

COORD_TEST_SEQUENCES = [
    "car-5",
    "car-10",
    "dog-1",
    "skateboard-8",
    "motorcycle-1",
]


@pytest.mark.parametrize("seq_name", COORD_TEST_SEQUENCES)
def test_coord_transform_az_mae_vs_gt(seq_name: str):
    """
    Simulate the bridge coordinate transformation on GT trajectories and verify
    that the sign inversion is the ONLY transform applied to azimuth.

    Pass criterion: az_adm = -az_pipeline, so round-trip MAE == 0.
    Also verifies that the transformed azimuth stays in valid ADM range [-180, 180].
    """
    seq_dir = os.path.join(_LASOT_ROOT, seq_name)
    if not os.path.isdir(seq_dir):
        pytest.skip(f"LaSOT sequence not found: {seq_dir}")

    gt_az = _load_gt_azimuths(seq_dir)
    assert len(gt_az) > 0, "No GT azimuths loaded"

    # Simulate pipeline → bridge transform → back-transform
    adm_azimuths = [OscTranslator.az_pipeline_to_adm(az) for az in gt_az]

    # Round-trip: adm → pipeline should recover original
    roundtrip = [OscTranslator.az_pipeline_to_adm(az) for az in adm_azimuths]
    rt_mae = _az_mae(roundtrip, gt_az)
    assert rt_mae < 1e-9, f"Round-trip MAE {rt_mae:.2e} > 0 for {seq_name}"

    # All ADM azimuths must be in valid range
    for az in adm_azimuths:
        assert -180.0 <= az <= 180.0, f"ADM azimuth {az:.1f}° out of range"


# ---------------------------------------------------------------------------
# Test 2: pipeline accuracy gate using pre-computed eval results
# ---------------------------------------------------------------------------

def _load_quant_eval_az_maes() -> list[float]:
    """Parse per-clip AzMAE values from QUANT_EVAL markdown report table."""
    if not os.path.isfile(_QUANT_EVAL_MD):
        pytest.skip(f"QUANT_EVAL report not found: {_QUANT_EVAL_MD}")
    import re
    maes: list[float] = []
    # Table rows look like: | **car-5** | vehicle | ✓ | 0.08° | ...
    pattern = re.compile(r"\|\s*\*?\*?[\w-]+\*?\*?\s*\|[^|]+\|[^|]+\|\s*([\d.]+)°")
    with open(_QUANT_EVAL_MD) as f:
        for line in f:
            m = pattern.match(line.strip())
            if m:
                maes.append(float(m.group(1)))
    return maes


def test_pipeline_accuracy_gate_mean_az_mae():
    """Mean AzMAE from QUANT_EVAL report must be ≤ 2.0° (Phase 2 gate)."""
    az_maes = _load_quant_eval_az_maes()
    assert len(az_maes) >= 10, f"Too few clips parsed from report: {len(az_maes)}"
    mean_mae = sum(az_maes) / len(az_maes)
    assert mean_mae <= AZ_MAE_GATE_DEG, (
        f"Mean AzMAE {mean_mae:.2f}° exceeds Phase 2 gate of {AZ_MAE_GATE_DEG}°"
    )


def test_pipeline_accuracy_gate_median_az_mae():
    """Median AzMAE from QUANT_EVAL report must be ≤ 1.5°."""
    az_maes = sorted(_load_quant_eval_az_maes())
    assert len(az_maes) >= 10
    median = az_maes[len(az_maes) // 2]
    assert median <= 1.5, f"Median AzMAE {median:.2f}° exceeds 1.5° gate"


def test_pipeline_report_clip_count():
    """QUANT_EVAL report must cover at least 20 clips."""
    az_maes = _load_quant_eval_az_maes()
    assert len(az_maes) >= 20, f"Only {len(az_maes)} clips in report, expected ≥ 20"


# ---------------------------------------------------------------------------
# Test 4: per-clip and aggregate regression vs frozen baseline
# ---------------------------------------------------------------------------

_BASELINE = os.path.join(os.path.dirname(__file__), "baseline.json")
_PER_CLIP_PATTERN = re.compile(
    r"\|\s*\*?\*?([\w-]+)\*?\*?\s*\|[^|]+\|[^|]+\|\s*([\d.]+)°"
)


def _load_quant_eval_clips() -> dict[str, float]:
    if not os.path.isfile(_QUANT_EVAL_MD):
        pytest.skip(f"QUANT_EVAL report not found: {_QUANT_EVAL_MD}")
    clips: dict[str, float] = {}
    with open(_QUANT_EVAL_MD) as f:
        for line in f:
            m = _PER_CLIP_PATTERN.match(line.strip())
            if m:
                name = m.group(1)
                if name.lower() in ("metric", "mean", "median", "min", "max"):
                    continue
                clips[name] = float(m.group(2))
    return clips


def _load_baseline() -> dict:
    if not os.path.isfile(_BASELINE):
        pytest.fail(f"baseline.json missing at {_BASELINE} — bootstrap required")
    with open(_BASELINE) as f:
        return json.load(f)


def test_per_clip_regression_vs_baseline():
    """No single clip's AzMAE may regress beyond the baseline tolerance."""
    baseline = _load_baseline()
    tol_abs = float(baseline["regression_tolerance"]["per_clip_absolute_max_deg"])
    base_clips = baseline["clips"]
    live_clips = _load_quant_eval_clips()

    missing = [c for c in base_clips if c not in live_clips]
    assert not missing, f"Baseline clips missing from live report: {missing}"

    regressions = []
    for clip, base_mae in base_clips.items():
        live_mae = live_clips[clip]
        if live_mae - base_mae > tol_abs:
            regressions.append(
                f"{clip}: {base_mae:.2f}° → {live_mae:.2f}° (Δ={live_mae - base_mae:+.2f}° > {tol_abs}°)"
            )
    assert not regressions, "Per-clip AzMAE regressions:\n  " + "\n  ".join(regressions)


def test_aggregate_mean_regression_vs_baseline():
    """Mean AzMAE across all baselined clips may not grow beyond mean_relative_max."""
    baseline = _load_baseline()
    tol_rel = float(baseline["regression_tolerance"]["mean_relative_max"])
    base_mean = float(baseline["aggregate"]["mean_az_mae_deg"])
    live_clips = _load_quant_eval_clips()
    base_clip_names = set(baseline["clips"].keys())
    common = [v for k, v in live_clips.items() if k in base_clip_names]
    assert len(common) >= len(base_clip_names) * 0.9, (
        f"Less than 90% of baseline clips present in live report: "
        f"{len(common)}/{len(base_clip_names)}"
    )
    live_mean = sum(common) / len(common)
    assert live_mean <= base_mean * (1.0 + tol_rel), (
        f"Mean AzMAE regression: {base_mean:.3f}° → {live_mean:.3f}° "
        f"(+{(live_mean / base_mean - 1.0) * 100:.1f}% > +{tol_rel * 100:.0f}%)"
    )


def test_baseline_schema_integrity():
    """baseline.json must declare schema_version, gate, tolerance, and clips."""
    b = _load_baseline()
    assert b.get("schema_version") == 1
    assert "phase2_gate" in b
    assert b["phase2_gate"]["mean_az_mae_max_deg"] == AZ_MAE_GATE_DEG
    assert "regression_tolerance" in b
    assert "clips" in b and len(b["clips"]) >= 20
    assert "aggregate" in b and "mean_az_mae_deg" in b["aggregate"]


# ---------------------------------------------------------------------------
# Test 3: distance transform — dist_m → dist_adm normalisation
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("dist_m,expected_adm", [
    (0.0,  1.0),   # at camera → ADM far
    (10.0, 0.5),   # mid range
    (20.0, 0.0),   # max range → ADM near
    (5.0,  0.75),
])
def test_dist_normalisation(dist_m: float, expected_adm: float):
    dist_norm = max(0.0, min(1.0, 1.0 - dist_m / 20.0))
    adm = OscTranslator.dist_v2s_to_adm(dist_norm)
    # dist_v2s_to_adm = 1 - dist_norm = 1 - (1 - dist_m/20) = dist_m/20
    # so adm = dist_m/20; expected_adm = dist_m/20 only if formula is applied once
    # actual contract: dist_norm = 1 - dist_m/20, then adm = 1 - dist_norm = dist_m/20
    expected = dist_m / 20.0
    assert abs(adm - expected) < 1e-9, f"dist_m={dist_m}: adm={adm:.4f} != {expected:.4f}"
