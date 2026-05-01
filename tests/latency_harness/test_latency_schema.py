"""
CI tests for latency harness — no hardware required.

Tests validate:
1. --dry-run produces valid baseline.json with all required fields
2. percentile math is correct
3. baseline.json schema (all keys present, correct types)
4. threshold gate logic (pass/fail)
"""

import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

HARNESS = Path(__file__).parent / "run_latency.py"

REQUIRED_KEYS = {
    "kernel": str,
    "governor": str,
    "jack_period_frames": int,
    "sample_rate_hz": int,
    "dante_driver_version": str,
    "audio_interface": str,
    "machine_model": str,
    "preempt_rt": bool,
    "num_objects": int,
    "n_samples": int,
    "p50_us": float,
    "p95_us": float,
    "p99_us": float,
    "pass": bool,
    "threshold_us": int,
    "timestamp_utc": str,
    "stages": str,
}


# ---------------------------------------------------------------------------
# Test 1: dry-run produces a valid baseline.json
# ---------------------------------------------------------------------------

def test_dry_run_produces_output():
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
        out = f.name

    result = subprocess.run(
        [sys.executable, str(HARNESS), "--dry-run", "--duration", "5", "--output", out],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, f"harness exited non-zero:\n{result.stderr}"

    data = json.loads(Path(out).read_text())
    assert data["n_samples"] > 0, "dry-run must produce at least one sample"
    assert data["stages"] == "2-6 (T0=OSC_recv, T1=Dante_PCIe_write)"
    assert data["num_objects"] == 8  # default


# ---------------------------------------------------------------------------
# Test 2: percentile math correctness
# ---------------------------------------------------------------------------

def test_percentile_math():
    # Import the module directly
    sys.path.insert(0, str(HARNESS.parent))
    from run_latency import percentile, generate_synthetic_samples

    # Known dataset
    data = sorted([100.0, 200.0, 300.0, 400.0, 500.0])
    assert percentile(data, 50) == 300.0
    assert percentile(data, 100) == 500.0
    assert percentile(data, 0) == 100.0  # nearest-rank: ceil(0) - 1 = -1 → clamp 0

    # Lognormal: p99 should be substantially above mean
    import random
    random.seed(42)
    samples = sorted(generate_synthetic_samples(10000, mean_us=2000.0))
    p99 = percentile(samples, 99)
    p50 = percentile(samples, 50)
    assert p50 > 0, "p50 must be positive"
    assert p99 > p50, "p99 must exceed p50"
    assert p99 < 20000, "p99 should be reasonable (<20ms for 2ms-mean lognormal)"


# ---------------------------------------------------------------------------
# Test 3: schema validation — all keys present and correct types
# ---------------------------------------------------------------------------

def test_schema_all_keys_present_and_typed():
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
        out = f.name

    subprocess.run(
        [sys.executable, str(HARNESS), "--dry-run", "--duration", "5", "--output", out],
        capture_output=True, text=True, timeout=30, check=True,
    )
    data = json.loads(Path(out).read_text())

    for key, expected_type in REQUIRED_KEYS.items():
        assert key in data, f"Missing key: {key}"
        # JSON numbers: int fields may be parsed as float if .0 — be lenient for numeric
        val = data[key]
        if expected_type in (int, float):
            assert isinstance(val, (int, float)), f"{key}: expected numeric, got {type(val)}"
        elif expected_type is bool:
            assert isinstance(val, bool), f"{key}: expected bool, got {type(val)}"
        else:
            assert isinstance(val, expected_type), f"{key}: expected {expected_type}, got {type(val)}"


# ---------------------------------------------------------------------------
# Test 4: threshold gate logic
# ---------------------------------------------------------------------------

def test_threshold_gate_pass():
    """p99 < 5000 us → pass: true"""
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
        out = f.name

    # Default dry-run ~2ms mean → p99 well under 5ms
    subprocess.run(
        [sys.executable, str(HARNESS), "--dry-run", "--duration", "200",
         "--threshold-us", "5000", "--output", out],
        capture_output=True, text=True, timeout=30, check=True,
    )
    data = json.loads(Path(out).read_text())
    assert data["p99_us"] < 5000, f"Expected p99 < 5000us, got {data['p99_us']}"
    assert data["pass"] is True


def test_threshold_gate_fail():
    """p99 >= threshold → pass: false (use very low threshold)"""
    sys.path.insert(0, str(HARNESS.parent))
    from run_latency import build_result, generate_synthetic_samples, gather_system_info
    import argparse, random

    random.seed(0)
    samples = generate_synthetic_samples(1000, mean_us=2000.0)

    args = argparse.Namespace(
        objects=8,
        threshold_us=100,  # impossibly tight → must fail
    )
    sysinfo = gather_system_info()
    result = build_result(samples, args, sysinfo)
    assert result["pass"] is False, "p99 should exceed 100us threshold"
    assert result["threshold_us"] == 100
