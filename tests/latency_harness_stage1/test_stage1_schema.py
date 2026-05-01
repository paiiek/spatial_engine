"""
pytest — CI-runnable tests for the stage-1 latency harness.

Tests:
  1. --dry-run produces a valid stage1_report.json with expected keys/types.
  2. Composition math: stage1_p99 + stages2_6_p99 = composed_p99.
  3. Graceful degradation when xdotool is absent.
"""

import json
import math
import sys
from pathlib import Path

import pytest

# Add the harness directory to path so we can import run_stage1 directly.
sys.path.insert(0, str(Path(__file__).parent))
import run_stage1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

REQUIRED_KEYS = {
    "schema_version",
    "stage",
    "timestamp_utc",
    "trials_requested",
    "trials_measured",
    "method",
    "stage1_p50_us",
    "stage1_p95_us",
    "stage1_p99_us",
    "stage1_mean_us",
    "stage1_std_us",
    "composed_p99_us",
}


# ---------------------------------------------------------------------------
# Test 1: dry-run produces valid report
# ---------------------------------------------------------------------------

def test_dry_run_produces_valid_report(tmp_path):
    output = tmp_path / "stage1_report.json"
    report = run_stage1.run(trials=50, dry_run=True, output_path=output)

    # File must exist and be valid JSON
    assert output.exists(), "stage1_report.json not written"
    loaded = json.loads(output.read_text())

    # Must contain all required keys
    missing = REQUIRED_KEYS - loaded.keys()
    assert not missing, f"Missing keys in report: {missing}"

    # Type checks
    assert loaded["schema_version"] == 1
    assert loaded["stage"] == 1
    assert loaded["trials_requested"] == 50
    assert loaded["trials_measured"] == 50
    assert loaded["method"] == "synthetic_dry_run"

    # Latency values must be positive finite floats
    for key in ("stage1_p50_us", "stage1_p95_us", "stage1_p99_us", "stage1_mean_us", "stage1_std_us"):
        val = loaded[key]
        assert isinstance(val, (int, float)), f"{key} is not numeric: {val!r}"
        assert math.isfinite(val), f"{key} is not finite: {val}"
        assert val > 0, f"{key} must be positive: {val}"

    # p50 <= p95 <= p99
    assert loaded["stage1_p50_us"] <= loaded["stage1_p95_us"], "p50 > p95"
    assert loaded["stage1_p95_us"] <= loaded["stage1_p99_us"], "p95 > p99"


# ---------------------------------------------------------------------------
# Test 2: composition math
# ---------------------------------------------------------------------------

def test_composition_math(tmp_path):
    # Create a fake baseline.json for stages 2-6
    baseline_dir = tmp_path / "latency_harness"
    baseline_dir.mkdir()
    stages2_6_p99 = 3500.0   # 3.5 ms in us
    (baseline_dir / "baseline.json").write_text(
        json.dumps({"p99_us": stages2_6_p99, "description": "test baseline"})
    )

    output = tmp_path / "stage1_report.json"

    # Patch the baseline path resolution inside run_stage1 by running the function
    # with a monkeypatched __file__ — instead, we call _run_with_baseline_dir directly.
    # Since run_stage1.run resolves baseline relative to __file__, we use a workaround:
    # write the baseline to the expected relative path from the harness directory.
    harness_dir = Path(run_stage1.__file__).parent
    real_baseline = harness_dir.parent / "latency_harness" / "baseline.json"
    real_baseline.parent.mkdir(parents=True, exist_ok=True)

    # Preserve existing baseline if present
    existing_content = real_baseline.read_text() if real_baseline.exists() else None
    try:
        real_baseline.write_text(json.dumps({"p99_us": stages2_6_p99}))
        report = run_stage1.run(trials=20, dry_run=True, output_path=output)
    finally:
        if existing_content is not None:
            real_baseline.write_text(existing_content)
        elif real_baseline.exists():
            real_baseline.unlink()

    loaded = json.loads(output.read_text())
    assert "stages2_6_p99_us" in loaded, "stages2_6_p99_us missing from report"
    assert loaded["stages2_6_p99_us"] == stages2_6_p99

    expected_composed = loaded["stage1_p99_us"] + stages2_6_p99
    assert abs(loaded["composed_p99_us"] - expected_composed) < 0.01, (
        f"Composition math wrong: {loaded['stage1_p99_us']} + {stages2_6_p99} "
        f"!= {loaded['composed_p99_us']}"
    )


# ---------------------------------------------------------------------------
# Test 3: graceful xdotool-missing path
# ---------------------------------------------------------------------------

def test_graceful_xdotool_missing(tmp_path, monkeypatch):
    """When xdotool is not available and dry_run=False, harness uses synthetic fallback."""
    import shutil as _shutil

    # Make xdotool appear absent
    original_which = _shutil.which

    def fake_which(name, *args, **kwargs):
        if name == "xdotool":
            return None
        return original_which(name, *args, **kwargs)

    monkeypatch.setattr(_shutil, "which", fake_which)
    # Also patch in run_stage1's imported shutil
    import shutil as shutil_mod
    monkeypatch.setattr(shutil_mod, "which", fake_which)

    output = tmp_path / "stage1_report.json"
    report = run_stage1.run(trials=30, dry_run=False, output_path=output)

    loaded = json.loads(output.read_text())

    assert loaded["method"] == "synthetic_fallback", (
        f"Expected synthetic_fallback, got {loaded['method']!r}"
    )
    assert "method_note" in loaded
    assert "xdotool" in loaded["method_note"].lower() or "graceful" in loaded["method_note"].lower()

    # Still produces valid latency numbers
    assert loaded["trials_measured"] == 30
    assert loaded["stage1_p99_us"] > 0
