"""
CI-runnable pytest suite for soak harness schema and gate logic.
Does NOT run a real soak — validates structure and gate math only.
"""

from __future__ import annotations

import json
import subprocess
import sys
import os
import tempfile
import types

import pytest

# ---------------------------------------------------------------------------
# Import run_soak module directly for unit tests
# ---------------------------------------------------------------------------

HARNESS_DIR = os.path.dirname(__file__)
sys.path.insert(0, HARNESS_DIR)

import run_soak  # noqa: E402


# ---------------------------------------------------------------------------
# Required top-level fields in every soak report
# ---------------------------------------------------------------------------

REQUIRED_FIELDS = {
    "duration_s", "n_objects", "n_samples",
    "xrun_count", "heartbeat_miss_count",
    "per_block_p99_us_max", "per_block_p99_us_threshold",
    "core_rss_slope_mb_per_h", "ui_rss_slope_mb_per_h",
    "osc_reorder_burst_count_total",
    "pass", "failures", "timestamp_utc", "gates",
}

REQUIRED_GATES = {
    "xrun_zero", "heartbeat_miss_zero", "per_block_p99_ok", "core_rss_slope_ok",
}


# ---------------------------------------------------------------------------
# Test 1: --dry-run produces valid soak_report.json with all required fields
# ---------------------------------------------------------------------------

class Test1DryRunSchema:
    def test_dry_run_produces_report(self, tmp_path):
        output = tmp_path / "soak_report.json"
        result = subprocess.run(
            [
                sys.executable, os.path.join(HARNESS_DIR, "run_soak.py"),
                "--dry-run", "--duration", "60", "--output", str(output),
            ],
            capture_output=True, text=True, timeout=30,
        )
        assert result.returncode == 0, f"run_soak.py exited non-zero:\n{result.stderr}"
        assert output.exists(), "soak_report.json not created"

        with open(output) as f:
            report = json.load(f)

        missing = REQUIRED_FIELDS - set(report.keys())
        assert not missing, f"Missing fields in report: {missing}"

        missing_gates = REQUIRED_GATES - set(report["gates"].keys())
        assert not missing_gates, f"Missing gate keys: {missing_gates}"

        assert isinstance(report["pass"], bool)
        assert isinstance(report["failures"], list)
        assert isinstance(report["n_samples"], int)
        assert report["n_samples"] > 0, "Expected at least 1 sample in dry-run"
        assert report["dry_run"] is True
        assert report["per_block_p99_us_threshold"] == 933


# ---------------------------------------------------------------------------
# Test 2: xrun_count > 0  →  pass: false, failures contains "xrun"
# ---------------------------------------------------------------------------

class Test2XrunGate:
    def test_xrun_triggers_fail(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=1,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=0.0,
        )
        assert gates["xrun_zero"] is False
        assert any("xrun" in f for f in failures), f"Expected 'xrun' in failures: {failures}"

    def test_zero_xrun_passes(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=0.0,
        )
        assert gates["xrun_zero"] is True
        assert not any("xrun" in f for f in failures)


# ---------------------------------------------------------------------------
# Test 3: per_block_p99 threshold = 933 µs exactly
# ---------------------------------------------------------------------------

class Test3P99Threshold:
    def test_threshold_value(self):
        """floor(64/48000 * 0.7 * 1e6) = floor(933.333...) = 933"""
        import math
        expected = math.floor(64 / 48000 * 0.7 * 1e6)
        assert expected == 933
        assert run_soak.PER_BLOCK_P99_THRESHOLD_US == 933

    def test_exactly_at_threshold_passes(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=933,
            core_rss_slope=0.0,
        )
        assert gates["per_block_p99_ok"] is True

    def test_one_over_threshold_fails(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=934,
            core_rss_slope=0.0,
        )
        assert gates["per_block_p99_ok"] is False
        assert any("per_block_p99" in f for f in failures)


# ---------------------------------------------------------------------------
# Test 4: heartbeat_miss gate — miss_count > 0  →  pass: false
# ---------------------------------------------------------------------------

class Test4HeartbeatMissGate:
    def test_miss_triggers_fail(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=1,
            per_block_p99_us_max=500,
            core_rss_slope=0.0,
        )
        assert gates["heartbeat_miss_zero"] is False
        assert any("heartbeat_miss" in f for f in failures)

    def test_zero_miss_passes(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=0.0,
        )
        assert gates["heartbeat_miss_zero"] is True


# ---------------------------------------------------------------------------
# Test 5: RSS slope gate — slope > 1.0 MB/h  →  core RSS fail
# ---------------------------------------------------------------------------

class Test5RssSlopeGate:
    def test_slope_over_limit_fails(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=1.01,
        )
        assert gates["core_rss_slope_ok"] is False
        assert any("core_rss_slope" in f for f in failures)

    def test_slope_at_limit_passes(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=1.0,
        )
        assert gates["core_rss_slope_ok"] is True

    def test_slope_under_limit_passes(self):
        gates, failures = run_soak.evaluate_gates(
            xrun_count=0,
            heartbeat_miss_count=0,
            per_block_p99_us_max=500,
            core_rss_slope=0.5,
        )
        assert gates["core_rss_slope_ok"] is True
