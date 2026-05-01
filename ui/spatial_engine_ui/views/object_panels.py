"""Object parameter inspector panels."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel, QFormLayout, QLineEdit

    class ObjectInspector(QWidget):
        def __init__(self, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("Object Inspector", self))
            form = QFormLayout()
            self._id_field = QLineEdit(self)
            self._id_field.setReadOnly(True)
            self._x_field = QLineEdit(self)
            self._x_field.setReadOnly(True)
            self._z_field = QLineEdit(self)
            self._z_field.setReadOnly(True)
            form.addRow("ID:", self._id_field)
            form.addRow("X:", self._x_field)
            form.addRow("Z:", self._z_field)
            layout.addLayout(form)

        def show_object(self, obj: object) -> None:
            if obj is None:
                self._id_field.setText("")
                self._x_field.setText("")
                self._z_field.setText("")
            else:
                self._id_field.setText(str(obj.obj_id))
                self._x_field.setText(f"{obj.x:.3f}")
                self._z_field.setText(f"{obj.z:.3f}")

except ImportError:
    class ObjectInspector:  # type: ignore[no-redef]
        def __init__(self, parent: object = None) -> None:
            pass

        def show_object(self, obj: object) -> None:
            pass
