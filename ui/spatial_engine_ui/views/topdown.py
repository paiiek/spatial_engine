"""Top-down 2D view of spatial objects (placeholder widget)."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import QWidget, QLabel, QVBoxLayout

    class TopDownView(QWidget):
        def __init__(self, object_model: object = None, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            self._model = object_model
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("[Top-Down View — drag objects here]", self))

        def refresh(self) -> None:
            self.update()

except ImportError:
    class TopDownView:  # type: ignore[no-redef]
        def __init__(self, object_model: object = None, parent: object = None) -> None:
            self._model = object_model

        def refresh(self) -> None:
            pass
