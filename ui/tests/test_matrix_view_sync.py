"""Test: matrix_model mirrors /sys/matrix payload byte-for-byte after load."""
from __future__ import annotations

import struct

import pytest

from spatial_engine_ui.state.matrix_model import MatrixModel


def build_matrix_payload(rows: int, cols: int, values: list[float]) -> bytes:
    """Build the binary payload as expected by MatrixModel.load_payload."""
    header = struct.pack("<II", rows, cols)
    floats = struct.pack(f"<{rows * cols}f", *values)
    return header + floats


def test_matrix_model_raw_bytes_preserved() -> None:
    """Raw bytes stored in model must equal original payload byte-for-byte."""
    rows, cols = 4, 4
    values = [float(i) for i in range(rows * cols)]
    payload = build_matrix_payload(rows, cols, values)

    model = MatrixModel()
    model.load_payload(payload)

    assert model.raw == payload, "raw bytes do not match original payload"


def test_matrix_model_dimensions() -> None:
    rows, cols = 3, 5
    values = [1.0] * (rows * cols)
    payload = build_matrix_payload(rows, cols, values)

    model = MatrixModel()
    model.load_payload(payload)

    assert model.rows == rows
    assert model.cols == cols


def test_matrix_model_float_values() -> None:
    rows, cols = 2, 3
    values = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]
    payload = build_matrix_payload(rows, cols, values)

    model = MatrixModel()
    model.load_payload(payload)

    # Round-trip through IEEE-754 float — use struct to get exact expected values
    expected = struct.unpack(f"<{rows * cols}f", struct.pack(f"<{rows * cols}f", *values))
    for r in range(rows):
        for c in range(cols):
            assert model.value(r, c) == pytest.approx(expected[r * cols + c], rel=1e-5)


def test_matrix_model_reload_replaces_previous() -> None:
    """A second load_payload must fully replace the previous state."""
    model = MatrixModel()

    payload1 = build_matrix_payload(2, 2, [1.0, 2.0, 3.0, 4.0])
    model.load_payload(payload1)
    assert model.rows == 2

    payload2 = build_matrix_payload(1, 1, [9.0])
    model.load_payload(payload2)

    assert model.raw == payload2
    assert model.rows == 1
    assert model.cols == 1
    assert model.value(0, 0) == pytest.approx(9.0)


def test_matrix_model_empty_payload() -> None:
    """Empty/malformed payload must not raise — raw stored, data empty."""
    model = MatrixModel()
    model.load_payload(b"")
    assert model.raw == b""
    assert model.data == []
