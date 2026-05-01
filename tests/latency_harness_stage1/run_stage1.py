#!/usr/bin/env python3
"""
Stage-1 latency harness — Qt event-loop tail.

Measures time from mouse event (synthesized via xdotool) to the
python-osc send_message() call site. This is the UI-side contribution
to the total end-to-end latency budget.

Usage:
    python run_stage1.py --trials 100 --output stage1_report.json
    python run_stage1.py --dry-run --output stage1_report.json

The --dry-run flag uses synthetic samples (mean ~0.5 ms) and is CI-safe
(no xdotool required, no live UI needed).

If ../latency_harness/baseline.json exists (stages 2-6 measurement),
the report also includes composed_p99_us = stage1_p99_us + stages2_6_p99_us.
"""

import argparse
import json
import math
import os
import random
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _percentile(data: list[float], p: float) -> float:
    """Compute the p-th percentile of a sorted list (linear interpolation)."""
    if not data:
        raise ValueError("empty data")
    sorted_data = sorted(data)
    n = len(sorted_data)
    idx = (p / 100.0) * (n - 1)
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    frac = idx - lo
    return sorted_data[lo] * (1.0 - frac) + sorted_data[hi] * frac


def _synthetic_samples(n: int, mean_ms: float = 0.5, sigma_ms: float = 0.15) -> list[float]:
    """Return n synthetic latency samples in microseconds."""
    rng = random.Random(42)
    samples_us = []
    for _ in range(n):
        s = rng.gauss(mean_ms * 1000.0, sigma_ms * 1000.0)
        samples_us.append(max(50.0, s))   # clamp to 50 us minimum
    return samples_us


# ---------------------------------------------------------------------------
# xdotool measurement
# ---------------------------------------------------------------------------

def _measure_with_xdotool(trials: int) -> tuple[list[float], str]:
    """
    Synthesize mouse-move events via xdotool and measure the time from event
    injection to the instrumented send_message() call site.

    Returns (samples_us, method_note).

    NOTE: This requires:
      - xdotool installed
      - A running spatial_engine_ui instance with stage-1 instrumentation enabled
        (set env SPATIAL_ENGINE_STAGE1_INSTRUMENT=1 before launching UI)
      - The UI writes a nanosecond timestamp to /tmp/spe_stage1_ts on each
        send_message() call.

    If any of these preconditions fail, falls back to synthetic samples.
    """
    if not shutil.which("xdotool"):
        return [], "xdotool_not_found"

    ts_file = Path("/tmp/spe_stage1_ts")
    # Check UI is running with instrumentation
    if not ts_file.exists():
        return [], "ui_not_instrumented"

    samples_us: list[float] = []
    for i in range(trials):
        # Inject a small mouse move at a stable screen position
        x = 400 + (i % 10)
        y = 300
        t_inject_ns = time.monotonic_ns()
        try:
            subprocess.run(
                ["xdotool", "mousemove", "--sync", str(x), str(y)],
                check=True, capture_output=True, timeout=1.0,
            )
        except subprocess.CalledProcessError:
            continue

        # Read the timestamp written by the instrumented UI
        deadline = time.monotonic() + 0.1   # 100 ms window
        t_send_ns: Optional[int] = None
        while time.monotonic() < deadline:
            try:
                raw = ts_file.read_text().strip()
                ts = int(raw)
                if ts > t_inject_ns:
                    t_send_ns = ts
                    break
            except (ValueError, OSError):
                pass
            time.sleep(0.001)

        if t_send_ns is not None:
            latency_us = (t_send_ns - t_inject_ns) / 1000.0
            samples_us.append(latency_us)

    if len(samples_us) < trials // 2:
        return [], "insufficient_responses"

    return samples_us, "xdotool"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(trials: int, dry_run: bool, output_path: Path) -> dict:
    method_note = ""

    if dry_run:
        samples_us = _synthetic_samples(trials)
        method = "synthetic_dry_run"
        method_note = "dry-run: synthetic samples, mean ~0.5 ms"
    else:
        samples_us, method = _measure_with_xdotool(trials)
        if not samples_us:
            # Graceful degradation
            samples_us = _synthetic_samples(trials)
            method_note = (
                f"graceful_degradation (reason: {method}): "
                "xdotool unavailable or UI not instrumented; synthetic samples used"
            )
            method = "synthetic_fallback"
        else:
            method_note = "xdotool mouse-event injection"

    p50 = _percentile(samples_us, 50)
    p95 = _percentile(samples_us, 95)
    p99 = _percentile(samples_us, 99)
    mean = sum(samples_us) / len(samples_us)
    std = math.sqrt(sum((x - mean) ** 2 for x in samples_us) / len(samples_us))

    report: dict = {
        "schema_version": 1,
        "stage": 1,
        "description": "Qt event-loop tail: mouse event → python-osc send_message()",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "trials_requested": trials,
        "trials_measured": len(samples_us),
        "method": method,
        "method_note": method_note,
        "stage1_p50_us": round(p50, 1),
        "stage1_p95_us": round(p95, 1),
        "stage1_p99_us": round(p99, 1),
        "stage1_mean_us": round(mean, 1),
        "stage1_std_us": round(std, 1),
    }

    # Compose with stages 2-6 if baseline.json exists
    baseline_path = Path(__file__).parent.parent / "latency_harness" / "baseline.json"
    if baseline_path.exists():
        try:
            baseline = json.loads(baseline_path.read_text())
            stages2_6_p99 = float(baseline.get("p99_us", 0))
            composed = p99 + stages2_6_p99
            report["stages2_6_p99_us"] = stages2_6_p99
            report["composed_p99_us"] = round(composed, 1)
            report["composed_note"] = "stage1_p99 + stages2_6_p99"
        except (json.JSONDecodeError, KeyError, TypeError) as exc:
            report["baseline_load_error"] = str(exc)
    else:
        report["composed_p99_us"] = None
        report["composed_note"] = "baseline.json not found; composed latency unavailable"

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description="Stage-1 latency harness")
    parser.add_argument("--trials", type=int, default=100, help="Number of trials")
    parser.add_argument(
        "--output", type=Path, default=Path("stage1_report.json"),
        help="Output JSON path",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Use synthetic samples (CI-safe, no xdotool required)",
    )
    args = parser.parse_args()

    report = run(trials=args.trials, dry_run=args.dry_run, output_path=args.output)

    print(f"Stage-1 latency harness complete")
    print(f"  method      : {report['method']}")
    print(f"  trials      : {report['trials_measured']}/{report['trials_requested']}")
    print(f"  p50         : {report['stage1_p50_us']} us")
    print(f"  p95         : {report['stage1_p95_us']} us")
    print(f"  p99         : {report['stage1_p99_us']} us")
    if report.get("composed_p99_us") is not None:
        print(f"  composed p99: {report['composed_p99_us']} us (stage1 + stages2-6)")
    print(f"  report      : {args.output}")
    if report.get("method_note"):
        print(f"  note        : {report['method_note']}")


if __name__ == "__main__":
    main()
