"""P0 smoke: package imports and CLI parses."""

from __future__ import annotations

import spatial_engine_ui
from spatial_engine_ui.app import main, parse_args


def test_version_pinned():
    assert spatial_engine_ui.__version__ == "0.1.0"
    assert spatial_engine_ui.SCHEMA_VERSION == 1


def test_default_ports():
    args = parse_args([])
    assert args.osc_cmd_port == 9100
    assert args.osc_state_port == 9101
    assert args.core_host == "127.0.0.1"


def test_main_returns_zero(capsys):
    rc = main([])
    captured = capsys.readouterr()
    assert rc == 0
    assert "spatial_engine_ui" in captured.out
