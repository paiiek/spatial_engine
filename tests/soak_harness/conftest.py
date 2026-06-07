"""Soak-harness pytest plumbing — the opt-in `--run-soak` flag.

The 60 s full soak (`test_phase_c_shm_loopback.py::test_shm_loopback_full_soak`)
is gated behind `--run-soak` (or `SHM_SOAK_FULL=1`) so the default `pytest`
gate stays fast and deterministic (ADR 0019 PR6 §3 driver 2 / §6).
"""

from __future__ import annotations


def pytest_addoption(parser):
    parser.addoption(
        "--run-soak", action="store_true", default=False,
        help="run the opt-in 60 s shm cross-process soak (PR6 full mode)",
    )
