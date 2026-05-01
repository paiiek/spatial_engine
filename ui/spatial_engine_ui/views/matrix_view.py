"""Read-only matrix routing view."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import QWidget, QTableWidget, QTableWidgetItem, QVBoxLayout, QLabel
    from PySide6.QtCore import Qt

    class MatrixView(QWidget):
        def __init__(self, matrix_model: object = None, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            self._model = matrix_model
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("Matrix Routing (read-only)", self))
            self._table = QTableWidget(0, 0, self)
            self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
            layout.addWidget(self._table)

        def sync_from_model(self) -> None:
            if self._model is None:
                return
            rows = self._model.rows
            cols = self._model.cols
            self._table.setRowCount(rows)
            self._table.setColumnCount(cols)
            for r in range(rows):
                for c in range(cols):
                    val = self._model.value(r, c)
                    item = QTableWidgetItem(f"{val:.3f}")
                    item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                    self._table.setItem(r, c, item)

except ImportError:
    class MatrixView:  # type: ignore[no-redef]
        def __init__(self, matrix_model: object = None, parent: object = None) -> None:
            self._model = matrix_model

        def sync_from_model(self) -> None:
            pass
