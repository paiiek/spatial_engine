"""3D view placeholder."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import QWidget, QLabel, QVBoxLayout

    class View3D(QWidget):
        def __init__(self, object_model: object = None, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("[3D View — placeholder]", self))

        def refresh(self) -> None:
            self.update()

except ImportError:
    class View3D:  # type: ignore[no-redef]
        def __init__(self, object_model: object = None, parent: object = None) -> None:
            pass

        def refresh(self) -> None:
            pass
