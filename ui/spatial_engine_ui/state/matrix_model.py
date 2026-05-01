"""Matrix routing model — read-only mirror of /sys/matrix payload."""
from __future__ import annotations


class MatrixModel:
    """Stores the raw /sys/matrix payload and parsed float grid."""

    def __init__(self) -> None:
        self._raw: bytes = b""
        self._rows: int = 0
        self._cols: int = 0
        self._data: list[list[float]] = []

    def load_payload(self, payload: bytes) -> None:
        """Load raw bytes from /sys/matrix OSC message.

        Expected format: 4-byte little-endian int rows, 4-byte int cols,
        then rows*cols IEEE-754 floats (little-endian).
        Falls back to storing raw bytes if parsing fails.
        """
        import struct

        self._raw = payload
        self._data = []
        try:
            if len(payload) < 8:
                return
            rows, cols = struct.unpack_from("<II", payload, 0)
            expected = 8 + rows * cols * 4
            if len(payload) < expected:
                return
            self._rows = rows
            self._cols = cols
            floats = struct.unpack_from(f"<{rows * cols}f", payload, 8)
            for r in range(rows):
                self._data.append(list(floats[r * cols:(r + 1) * cols]))
        except struct.error:
            pass

    @property
    def raw(self) -> bytes:
        return self._raw

    @property
    def rows(self) -> int:
        return self._rows

    @property
    def cols(self) -> int:
        return self._cols

    @property
    def data(self) -> list[list[float]]:
        return self._data

    def value(self, row: int, col: int) -> float:
        if 0 <= row < len(self._data) and 0 <= col < len(self._data[row]):
            return self._data[row][col]
        return 0.0
