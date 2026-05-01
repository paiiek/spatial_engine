"""Status indicator widget — green/yellow/red dot with label."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import QWidget, QHBoxLayout, QLabel
    from PySide6.QtGui import QColor, QPainter, QBrush
    from PySide6.QtCore import Qt, QSize

    class _Dot(QWidget):
        def __init__(self, color: QColor, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            self._color = color
            self.setFixedSize(QSize(14, 14))

        def set_color(self, color: QColor) -> None:
            self._color = color
            self.update()

        def paintEvent(self, _event: object) -> None:
            p = QPainter(self)
            p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setBrush(QBrush(self._color))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(1, 1, 12, 12)

    class StatusIndicator(QWidget):
        GREEN = QColor(0x4C, 0xAF, 0x50)
        YELLOW = QColor(0xFF, 0xC1, 0x07)
        RED = QColor(0xF4, 0x43, 0x36)

        def __init__(self, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            layout = QHBoxLayout(self)
            layout.setContentsMargins(2, 2, 2, 2)
            self._dot = _Dot(self.GREEN, self)
            self._label = QLabel("OK", self)
            layout.addWidget(self._dot)
            layout.addWidget(self._label)

        def set_ok(self) -> None:
            self._dot.set_color(self.GREEN)
            self._label.setText("OK")

        def set_warning(self, msg: str = "Warning") -> None:
            self._dot.set_color(self.YELLOW)
            self._label.setText(msg)

        def set_error(self, msg: str = "Error") -> None:
            self._dot.set_color(self.RED)
            self._label.setText(msg)

except ImportError:
    # Headless stub for test environments without a display
    class StatusIndicator:  # type: ignore[no-redef]
        def __init__(self, parent: object = None) -> None:
            self.state = "ok"
            self.msg = "OK"

        def set_ok(self) -> None:
            self.state = "ok"; self.msg = "OK"

        def set_warning(self, msg: str = "Warning") -> None:
            self.state = "warning"; self.msg = msg

        def set_error(self, msg: str = "Error") -> None:
            self.state = "error"; self.msg = msg
