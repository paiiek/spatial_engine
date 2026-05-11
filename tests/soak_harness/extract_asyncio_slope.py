"""G7 sentinel — asyncio.all_tasks() slope (tasks/h).

Reads a JSON soak report produced by ``run_soak_webgui.py`` and computes
the least-squares slope of ``asyncio_series[*].n_tasks`` over time. Exit 0
iff slope ≤ ``--threshold-tasks-h`` (default 1 task/h, plan §10 G7).

Optional ``--print-median`` prints the median ``n_tasks`` value over the
series (diagnostic only; does not affect exit code).
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path


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


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="asyncio task slope sentinel")
    ap.add_argument("--report", required=True, help="JSON report path")
    ap.add_argument("--threshold-tasks-h", type=float, default=1.0,
                    help="Fail if slope > threshold (default 1.0).")
    ap.add_argument("--print-median", action="store_true",
                    help="Print median n_tasks (diagnostic).")
    ap.add_argument(
        "--skip-warmup-s", type=float, default=0.0,
        help="Drop samples with t < this value before regression. Useful "
             "to exclude the WS-connection ramp (defaults 0 = no skip). "
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

    series = data.get("asyncio_series", [])
    if not series:
        print("ERROR: asyncio_series empty (no samples collected)",
              file=sys.stderr)
        return 2

    if args.skip_warmup_s > 0:
        filtered = [s for s in series if float(s["t"]) >= args.skip_warmup_s]
        if len(filtered) < 2:
            print(
                f"ERROR: only {len(filtered)} samples after "
                f"--skip-warmup-s={args.skip_warmup_s} (need ≥2)",
                file=sys.stderr,
            )
            return 2
        series = filtered

    times = [float(s["t"]) for s in series]
    values = [float(s["n_tasks"]) for s in series]
    slope = _linear_slope_per_h(times, values)

    if args.print_median:
        # Diagnostic mode — emit slope + median, do NOT enforce threshold.
        median = statistics.median(values)
        print(f"asyncio_slope_tasks_per_h={slope:.4f} "
              f"median_n_tasks={median:.2f} n_samples={len(values)}")
        return 0

    print(f"asyncio_slope_tasks_per_h={slope:.4f} "
          f"n_samples={len(values)} threshold={args.threshold_tasks_h}")

    return 0 if slope <= args.threshold_tasks_h else 1


if __name__ == "__main__":
    sys.exit(main())
