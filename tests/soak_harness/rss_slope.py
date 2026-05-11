"""G7 sentinel — RSS slope (MB/h).

Reads a JSON soak report produced by ``run_soak_webgui.py``. Three modes:

  1. Default: compute least-squares RSS slope (MB/h) and exit 0 iff
     ``slope ≤ --threshold-mb-h``. Plan §2 G7 specifies the day-2
     threshold = ``max(day1_median × 3, 50)``.

  2. ``--print-median``: print median RSS value and the computed slope, do
     NOT enforce a threshold (used at day-1 → day-2 handoff to derive the
     fixed threshold value).

  3. ``--derive-day2-threshold``: print the recommended day-2 threshold
     using the formula ``max(slope × 3, 50)`` based on the day-1 report,
     where ``slope`` is the day-1 measured slope. The result is the
     "× 3 conservative bound" plan §10 mandates, with the 50 MB/h
     hard upper-cap applied.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

# Hard ceiling from plan §10 G7 ("보수 상한 50 MB/h").
DAY2_HARD_CEILING_MB_H = 50.0


def _linear_slope_per_h(times_s: list[float], values: list[float]) -> float:
    n = len(times_s)
    if n < 2:
        return 0.0
    sum_x = sum(times_s)
    sum_y = sum(values)
    sum_xx = sum(x * x for x in times_s)
    sum_xy = sum(x * y for x, y in zip(times_s, values))
    denom = n * sum_xx - sum_x * sum_x
    if denom == 0:
        return 0.0
    slope_per_s = (n * sum_xy - sum_x * sum_y) / denom
    return slope_per_s * 3600.0


def derive_day2_threshold(day1_slope_mb_h: float) -> float:
    """Plan §10 G7: day-2 threshold = ``median × 3`` with 50 MB/h hard cap.

    Korean source (plan §3 S5, §10 G7):
        "threshold = max(median × 3, 50MB/h 상한)" + "(보수 상한 50MB/h)"

    Interpretation: the conservative upper cap is 50 MB/h — under no
    circumstances should day-2 permit > 50 MB/h drift, regardless of how
    noisy day-1 was. The base value is ``day1_slope × 3``. Negative or
    near-zero day-1 slope is clamped to a minimum of 1 MB/h so a flat
    day-1 does not yield a degenerate zero threshold (which would make
    day-2 fail on measurement noise).
    """
    candidate = max(day1_slope_mb_h * 3.0, 1.0)  # min 1 MB/h floor for noise
    return min(candidate, DAY2_HARD_CEILING_MB_H)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RSS slope sentinel (MB/h)")
    ap.add_argument("--report", required=True, help="JSON report path")
    ap.add_argument("--threshold-mb-h", type=float, default=None,
                    help="Fail if slope > threshold MB/h (default: none, "
                         "diagnostic only).")
    ap.add_argument("--print-median", action="store_true",
                    help="Print median RSS + slope, do NOT enforce.")
    ap.add_argument(
        "--derive-day2-threshold", action="store_true",
        help="Print recommended day-2 threshold (max(slope×3, 50)).",
    )
    ap.add_argument(
        "--skip-warmup-s", type=float, default=0.0,
        help="Drop samples with t < this value before regression. "
             "For 48h soak, 60s is a reasonable warmup discard.",
    )
    args = ap.parse_args(argv)

    report_path = Path(args.report)
    if not report_path.exists():
        print(f"ERROR: report not found: {report_path}", file=sys.stderr)
        return 2
    try:
        data = json.loads(report_path.read_text())
    except Exception as exc:
        print(f"ERROR: cannot parse {report_path}: {exc!r}", file=sys.stderr)
        return 2

    series = data.get("rss_series", [])
    if not series:
        print("ERROR: rss_series empty", file=sys.stderr)
        return 2

    if args.skip_warmup_s > 0:
        filtered = [s for s in series if float(s["t"]) >= args.skip_warmup_s]
        if len(filtered) < 2:
            print(
                f"ERROR: only {len(filtered)} samples after "
                f"--skip-warmup-s={args.skip_warmup_s}",
                file=sys.stderr,
            )
            return 2
        series = filtered

    times = [float(s["t"]) for s in series]
    values = [float(s["rss_mb"]) for s in series]
    slope = _linear_slope_per_h(times, values)

    if args.print_median:
        median = statistics.median(values)
        print(f"rss_slope_mb_per_h={slope:.4f} median_rss_mb={median:.2f} "
              f"n_samples={len(values)}")
        return 0

    if args.derive_day2_threshold:
        day2 = derive_day2_threshold(slope)
        print(f"day2_threshold_mb_per_h={day2:.2f} "
              f"(formula=min(max(day1_slope*3,1),{DAY2_HARD_CEILING_MB_H}) "
              f"hard_cap={DAY2_HARD_CEILING_MB_H}; "
              f"day1_slope={slope:.4f})")
        return 0

    if args.threshold_mb_h is None:
        # Diagnostic only.
        print(f"rss_slope_mb_per_h={slope:.4f} n_samples={len(values)} "
              f"threshold=<unset>")
        return 0

    print(f"rss_slope_mb_per_h={slope:.4f} n_samples={len(values)} "
          f"threshold={args.threshold_mb_h}")
    return 0 if slope <= args.threshold_mb_h else 1


if __name__ == "__main__":
    sys.exit(main())
