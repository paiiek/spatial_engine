"""spatial_engine_ui.app — Qt entry point (P0 stub).

Real UI lands at P8 (top-down + drag + matrix view + noise panel).
This file proves the package imports and the CLI surface is wired.
"""

from __future__ import annotations

import argparse
import sys

from . import SCHEMA_VERSION, __version__


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="spatial_engine_ui")
    parser.add_argument("--osc-cmd-port", type=int, default=9100)
    parser.add_argument("--osc-state-port", type=int, default=9101)
    parser.add_argument(
        "--core-host", type=str, default="127.0.0.1",
        help="Native core host (loopback by default; LAN OK).",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    print(
        f"spatial_engine_ui v{__version__} (P0 stub)\n"
        f"  schema_version={SCHEMA_VERSION}\n"
        f"  core={args.core_host}  cmd_port={args.osc_cmd_port}  state_port={args.osc_state_port}\n"
        "(P0 stub: no QApplication started. Real UI lands at P8.)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
