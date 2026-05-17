"""CI-runnable schema + sentinel tests for ``run_soak_webgui.py``.

Runs a short (~8 s) in-process soak to populate a real JSON report, then
validates:

  * top-level schema (required fields + types)
  * presence of non-empty rss_series / fd_series / asyncio_series
  * each sentinel CLI (``extract_asyncio_slope.py``, ``rss_slope.py``,
    ``check_fd_leak.py``) exits 0 on the recorded data

Does NOT run a 48 h soak. The full 48 h invocation lives in
``SOAK_WEBGUI_README.md`` (user queue).
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest


HARNESS_DIR = Path(__file__).resolve().parent
REPO_ROOT = HARNESS_DIR.parent.parent  # tests/soak_harness/ → repo

RUN_SOAK_WEBGUI = HARNESS_DIR / "run_soak_webgui.py"
EXTRACT_ASYNCIO = HARNESS_DIR / "extract_asyncio_slope.py"
RSS_SLOPE = HARNESS_DIR / "rss_slope.py"
CHECK_FD_LEAK = HARNESS_DIR / "check_fd_leak.py"


REQUIRED_TOP_FIELDS = {
    "schema_version",
    "domain",
    "duration_s",
    "reconnect_interval_s",
    "sample_interval_hz",
    "timestamp_utc_start",
    "timestamp_utc_end",
    "duration_actual_s",
    "rss_series",
    "fd_series",
    "asyncio_series",
    "fault_injections",
    "ws_counters",
    "osc_sink_recv_count",
    "osc_sink_port_actual",
    "dry_run",
}

REQUIRED_WS_COUNTERS = {"sent", "received", "reconnects"}


# ---------------------------------------------------------------------------
# Fixture: a short in-process soak (no fault injection)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def short_soak_report(tmp_path_factory) -> Path:
    """Run an ~8 s soak harness invocation; cache the JSON path."""
    report = tmp_path_factory.mktemp("soak_webgui") / "report.json"
    # Skip if uvicorn or psutil are unavailable.
    try:
        import psutil  # noqa: F401
        import uvicorn  # noqa: F401
        import requests  # noqa: F401
        import websockets  # noqa: F401
    except ImportError as exc:
        pytest.skip(f"required dependency missing: {exc}")

    cmd = [
        sys.executable, str(RUN_SOAK_WEBGUI),
        "--duration", "8",
        "--reconnect-interval", "0",   # no fault injection in CI
        "--sample-interval-hz", "2",
        "--report-path", str(report),
        "--osc-sink-port", "0",        # ephemeral port — avoids collision with default 9100
        "--dry-run",
    ]
    env = os.environ.copy()
    env["SOAK_LOG_LEVEL"] = "WARNING"
    result = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        capture_output=True, text=True, timeout=90,
        env=env,
    )
    if result.returncode != 0:
        pytest.fail(
            f"run_soak_webgui.py exited rc={result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not report.exists():
        pytest.fail("report file not produced")
    return report


# ---------------------------------------------------------------------------
# Schema
# ---------------------------------------------------------------------------

class TestSchema:
    def test_required_top_level_fields(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        missing = REQUIRED_TOP_FIELDS - set(data.keys())
        assert not missing, f"missing top-level fields: {missing}"

    def test_ws_counters_shape(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        assert isinstance(data["ws_counters"], dict)
        missing = REQUIRED_WS_COUNTERS - set(data["ws_counters"].keys())
        assert not missing, f"missing ws_counters: {missing}"

    def test_series_nonempty(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        assert len(data["rss_series"]) > 0, "rss_series empty"
        assert len(data["fd_series"]) > 0, "fd_series empty"
        assert len(data["asyncio_series"]) > 0, "asyncio_series empty"

    def test_dry_run_tag(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        assert data["dry_run"] is True

    def test_duration_recorded(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        assert data["duration_s"] == 8
        assert data["duration_actual_s"] >= 7.0

    def test_series_sample_types(self, short_soak_report: Path):
        data = json.loads(short_soak_report.read_text())
        for s in data["rss_series"]:
            assert isinstance(s["t"], (int, float))
            assert isinstance(s["rss_mb"], (int, float))
        for s in data["fd_series"]:
            assert isinstance(s["t"], (int, float))
            assert isinstance(s["fd"], int)
        for s in data["asyncio_series"]:
            assert isinstance(s["t"], (int, float))
            assert isinstance(s["n_tasks"], int)


# ---------------------------------------------------------------------------
# Sentinel CLIs
# ---------------------------------------------------------------------------

def _run_sentinel(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, *args],
        capture_output=True, text=True, timeout=30,
    )


class TestSentinels:
    def test_extract_asyncio_slope_pass(self, short_soak_report: Path):
        # In a short, low-traffic soak the asyncio count should be flat.
        r = _run_sentinel([
            str(EXTRACT_ASYNCIO),
            "--report", str(short_soak_report),
            "--threshold-tasks-h", "1000",  # very lax for 8s window
        ])
        assert r.returncode == 0, (
            f"extract_asyncio_slope failed: {r.stdout}\n{r.stderr}"
        )
        assert "asyncio_slope_tasks_per_h=" in r.stdout

    def test_extract_asyncio_slope_print_median(self, short_soak_report: Path):
        r = _run_sentinel([
            str(EXTRACT_ASYNCIO),
            "--report", str(short_soak_report),
            "--print-median",
        ])
        assert r.returncode == 0
        assert "median_n_tasks=" in r.stdout

    def test_rss_slope_threshold_pass(self, short_soak_report: Path):
        # Generous threshold; 8s won't show real growth.
        r = _run_sentinel([
            str(RSS_SLOPE),
            "--report", str(short_soak_report),
            "--threshold-mb-h", "10000",
        ])
        assert r.returncode == 0, (
            f"rss_slope failed: {r.stdout}\n{r.stderr}"
        )
        assert "rss_slope_mb_per_h=" in r.stdout

    def test_rss_slope_print_median(self, short_soak_report: Path):
        r = _run_sentinel([
            str(RSS_SLOPE),
            "--report", str(short_soak_report),
            "--print-median",
        ])
        assert r.returncode == 0
        assert "median_rss_mb=" in r.stdout

    def test_rss_slope_derive_day2_threshold(self, short_soak_report: Path):
        r = _run_sentinel([
            str(RSS_SLOPE),
            "--report", str(short_soak_report),
            "--derive-day2-threshold",
        ])
        assert r.returncode == 0
        assert "day2_threshold_mb_per_h=" in r.stdout
        # Hard cap should always be <= 50.
        for tok in r.stdout.split():
            if tok.startswith("day2_threshold_mb_per_h="):
                val = float(tok.split("=", 1)[1])
                assert val <= 50.0, f"day-2 threshold violates hard cap: {val}"
                break

    def test_check_fd_leak_pass(self, short_soak_report: Path):
        r = _run_sentinel([
            str(CHECK_FD_LEAK),
            "--report", str(short_soak_report),
            "--threshold-fd", "1000",  # lax for short soak
        ])
        assert r.returncode == 0, (
            f"check_fd_leak failed: {r.stdout}\n{r.stderr}"
        )
        assert "fd_delta=" in r.stdout

    def test_check_fd_leak_explicit_threshold_fails_when_exceeded(
        self, short_soak_report: Path
    ):
        # Force fail with -1 threshold so we exercise the fail path.
        r = _run_sentinel([
            str(CHECK_FD_LEAK),
            "--report", str(short_soak_report),
            "--threshold-fd", "-1",
        ])
        assert r.returncode == 1, (
            "expected fd_delta > -1 to fail, but sentinel returned "
            f"rc={r.returncode}"
        )


# ---------------------------------------------------------------------------
# rss_slope.derive_day2_threshold formula
# ---------------------------------------------------------------------------

def test_derive_day2_threshold_formula():
    sys.path.insert(0, str(HARNESS_DIR))
    import rss_slope  # noqa: E402

    # Flat day-1 → floor (1 MB/h), not zero.
    assert rss_slope.derive_day2_threshold(0.0) == pytest.approx(1.0)
    # Negative day-1 → floor.
    assert rss_slope.derive_day2_threshold(-5.0) == pytest.approx(1.0)
    # Normal day-1 5 MB/h → 15 MB/h.
    assert rss_slope.derive_day2_threshold(5.0) == pytest.approx(15.0)
    # Day-1 20 MB/h → 60 MB/h → capped at 50.
    assert rss_slope.derive_day2_threshold(20.0) == pytest.approx(50.0)
    # Day-1 50 MB/h → 150 MB/h → capped at 50.
    assert rss_slope.derive_day2_threshold(50.0) == pytest.approx(50.0)
