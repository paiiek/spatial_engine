"""tests/accuracy_harness/test_accuracy.py
pytest wrapper for the numerical accuracy harness.

Imports run_accuracy.main() and asserts exit code 0.
Passes when all CI gates in run_accuracy.py pass.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

import run_accuracy


def test_accuracy_harness_passes():
    """Run the full accuracy harness; assert all CI gates pass (exit 0)."""
    rc = run_accuracy.main()
    assert rc == 0, "run_accuracy.main() returned non-zero — CI accuracy gates failed"
