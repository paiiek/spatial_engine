"""G7 sentinel — fd leak (delta).

Reads a JSON soak report produced by ``run_soak_webgui.py`` and verifies
that the file-descriptor count did not drift upward beyond
``--threshold-fd`` between the first and last sample in ``fd_series``.

Plan §10 G7 acceptance: ``fd leak ≤ 5``. Exit 0 iff
``last_fd - first_fd ≤ threshold``.

Note: ``fd_series`` is sampled during the soak from the uvicorn process
PID, not from the long-running test harness itself. SIGKILL fault
injections restart uvicorn with a fresh PID; the harness records the new
PID's fd count seamlessly. The delta therefore measures fd growth in the
*latest* uvicorn lifetime, plus inter-PID drift. For day-1/day-2 final
acceptance, prefer a soak window without restarts in the last 60 s.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="fd leak sentinel")
    ap.add_argument("--report", required=True, help="JSON report path")
    ap.add_argument("--threshold-fd", type=int, default=5,
                    help="Max permitted last_fd - first_fd (default 5).")
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

    series = data.get("fd_series", [])
    if not series:
        print("ERROR: fd_series empty", file=sys.stderr)
        return 2

    first_fd = int(series[0]["fd"])
    last_fd = int(series[-1]["fd"])
    delta = last_fd - first_fd

    print(f"fd_first={first_fd} fd_last={last_fd} fd_delta={delta} "
          f"n_samples={len(series)} threshold={args.threshold_fd}")

    return 0 if delta <= args.threshold_fd else 1


if __name__ == "__main__":
    sys.exit(main())
