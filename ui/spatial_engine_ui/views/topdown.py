"""Top-down 2D view of spatial objects (placeholder widget)."""
from __future__ import annotations

from typing import Callable, Optional

try:
    from PySide6.QtWidgets import QWidget, QLabel, QVBoxLayout, QSlider, QHBoxLayout
    from PySide6.QtCore import Qt

    class TopDownView(QWidget):
        def __init__(self, object_model: object = None, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            self._model = object_model
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("[Top-Down View — drag objects here]", self))

        def refresh(self) -> None:
            self.update()

    class ElevationControl(QWidget):
        """Elevation slider -90..+90 degrees for a selected object."""

        ELEV_MIN: int = -90
        ELEV_MAX: int = 90
        ELEV_DEFAULT: int = 0

        def __init__(
            self,
            send_osc: Optional[Callable[[str, float], None]] = None,
            parent: QWidget | None = None,
        ) -> None:
            super().__init__(parent)
            self._obj_id: Optional[int] = None
            self._send_osc = send_osc

            layout = QHBoxLayout(self)
            self._label = QLabel("Elev: 0°", self)
            self._slider = QSlider(Qt.Orientation.Horizontal, self)
            self._slider.setMinimum(self.ELEV_MIN)
            self._slider.setMaximum(self.ELEV_MAX)
            self._slider.setValue(self.ELEV_DEFAULT)
            self._slider.valueChanged.connect(self._on_slider_changed)
            layout.addWidget(self._label)
            layout.addWidget(self._slider)

        def set_object(self, obj_id: int) -> None:
            self._obj_id = obj_id
            self._slider.setValue(self.ELEV_DEFAULT)

        def _on_slider_changed(self, value: int) -> None:
            self._label.setText(f"Elev: {value}°")
            self.on_value_changed(float(value))

        def on_value_changed(self, degrees: float) -> None:
            if self._obj_id is None or self._send_osc is None:
                return
            from spatial_engine_ui.ipc.protocol import adm_obj_elev
            self._send_osc(adm_obj_elev(self._obj_id), degrees)

        @property
        def slider(self) -> QSlider:
            return self._slider

    # --- Elevation side-view (read-only scatter: horizontal-distance r vs height y) ---

    class ElevationView(QWidget):
        """Side-view showing object r (horizontal dist) vs elevation y."""
        MARGIN = 20
        RADIUS = 5

        def __init__(self, parent: "QWidget | None" = None):
            super().__init__(parent)
            self._objects: list[tuple[float, float, float]] = []  # (x, y, z)

        def update_objects(self, objects: list[tuple[float, float, float]]) -> None:
            self._objects = list(objects)
            self.update()

        @staticmethod
        def to_screen_coords(x: float, y: float, z: float,
                             w: int, h: int, margin: int) -> tuple[int, int]:
            """Map (x,y,z) -> screen (sx, sy). r=sqrt(x^2+z^2), el=y."""
            r = (x*x + z*z) ** 0.5
            usable_w = w - 2 * margin
            usable_h = h - 2 * margin
            sx = margin + int(r * usable_w * 0.5)
            sy = margin + int((1.0 - (y + 1.0) * 0.5) * usable_h)
            return sx, sy

        def paintEvent(self, _event) -> None:  # type: ignore[override]
            from PySide6.QtGui import QPainter, QColor
            p = QPainter(self)
            p.fillRect(self.rect(), QColor(30, 30, 30))
            p.setPen(QColor(100, 200, 100))
            for (x, y, z) in self._objects:
                sx, sy = self.to_screen_coords(x, y, z, self.width(), self.height(), self.MARGIN)
                p.drawEllipse(sx - self.RADIUS, sy - self.RADIUS,
                              self.RADIUS * 2, self.RADIUS * 2)

except ImportError:
    class TopDownView:  # type: ignore[no-redef]
        def __init__(self, object_model: object = None, parent: object = None) -> None:
            self._model = object_model

        def refresh(self) -> None:
            pass

    class ElevationControl:  # type: ignore[no-redef]
        """Headless stub — no PySide6."""

        ELEV_MIN: int = -90
        ELEV_MAX: int = 90
        ELEV_DEFAULT: int = 0

        def __init__(
            self,
            send_osc: Optional[Callable[[str, float], None]] = None,
            parent: object = None,
        ) -> None:
            self._obj_id: Optional[int] = None
            self._send_osc = send_osc
            self._value: float = float(self.ELEV_DEFAULT)

        def set_object(self, obj_id: int) -> None:
            self._obj_id = obj_id
            self._value = float(self.ELEV_DEFAULT)

        def on_value_changed(self, degrees: float) -> None:
            self._value = degrees
            if self._obj_id is None or self._send_osc is None:
                return
            from spatial_engine_ui.ipc.protocol import adm_obj_elev
            self._send_osc(adm_obj_elev(self._obj_id), degrees)

    class ElevationView:  # type: ignore[no-redef]
        """Headless stub for ElevationView."""
        MARGIN = 20
        RADIUS = 5

        def __init__(self, parent=None):
            self._objects: list[tuple[float, float, float]] = []

        def update_objects(self, objects: list[tuple[float, float, float]]) -> None:
            self._objects = list(objects)

        @staticmethod
        def to_screen_coords(x: float, y: float, z: float,
                             w: int, h: int, margin: int) -> tuple[int, int]:
            r = (x*x + z*z) ** 0.5
            usable_w = w - 2 * margin
            usable_h = h - 2 * margin
            sx = margin + int(r * usable_w * 0.5)
            sy = margin + int((1.0 - (y + 1.0) * 0.5) * usable_h)
            return sx, sy
