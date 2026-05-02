"""Object parameter inspector panels.

Per-object DSP parameter controls (Phase v0 acceptance #9):
  - Position read-out (id / x / z) — for inspection only.
  - Algorithm selector (VBAP / WFS / DBAP) → /obj/algo
  - 4-band EQ (low / lowmid / highmid / high), -24..+24 dB → /obj/dsp param 0..3
  - User delay (0..1000 ms)                                → /obj/dsp param 4
  - Distance HF rolloff k_hf (0..1)                        → /obj/dsp param 5
  - Reverb send (0..1)                                     → /obj/dsp param 6

Headless stub: importable without PySide6.
"""
from __future__ import annotations

try:
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import (
        QWidget, QVBoxLayout, QLabel, QFormLayout, QLineEdit,
        QSlider, QHBoxLayout, QComboBox,
    )

    class _LabeledSlider(QWidget):
        """Slider with a numeric read-out label, integer range, fixed scale factor."""

        def __init__(self, label: str, mn: int, mx: int, init: int,
                     scale: float, suffix: str, on_change, parent=None) -> None:
            super().__init__(parent)
            row = QHBoxLayout(self)
            row.setContentsMargins(0, 0, 0, 0)
            self._lbl = QLabel(label, self)
            self._lbl.setFixedWidth(70)
            row.addWidget(self._lbl)
            self._slider = QSlider(Qt.Orientation.Horizontal, self)
            self._slider.setRange(mn, mx)
            self._slider.setValue(init)
            row.addWidget(self._slider)
            self._read = QLabel("", self)
            self._read.setFixedWidth(60)
            row.addWidget(self._read)
            self._scale = scale
            self._suffix = suffix
            self._on_change = on_change
            self._slider.valueChanged.connect(self._handle)
            self._handle(init)

        def _handle(self, v: int) -> None:
            f = float(v) * self._scale
            self._read.setText(f"{f:.2f}{self._suffix}")
            if self._on_change is not None:
                self._on_change(f)

        def set_value_silent(self, f: float) -> None:
            self._slider.blockSignals(True)
            self._slider.setValue(int(round(f / self._scale)))
            self._read.setText(f"{f:.2f}{self._suffix}")
            self._slider.blockSignals(False)

    class ObjectInspector(QWidget):
        """Full per-object DSP inspector. OSC routes via osc_client (optional)."""

        def __init__(self, parent: QWidget | None = None,
                     osc_client: object | None = None) -> None:
            super().__init__(parent)
            self._client = osc_client
            self._obj_id: int = -1

            root = QVBoxLayout(self)
            root.addWidget(QLabel("Object Inspector", self))

            form = QFormLayout()
            self._id_field = QLineEdit(self); self._id_field.setReadOnly(True)
            self._x_field  = QLineEdit(self); self._x_field.setReadOnly(True)
            self._z_field  = QLineEdit(self); self._z_field.setReadOnly(True)
            form.addRow("ID:", self._id_field)
            form.addRow("X:",  self._x_field)
            form.addRow("Z:",  self._z_field)
            root.addLayout(form)

            # Algorithm
            algo_row = QHBoxLayout()
            algo_row.addWidget(QLabel("Algorithm:", self))
            self._algo = QComboBox(self)
            self._algo.addItems(["VBAP", "WFS", "DBAP"])
            self._algo.currentIndexChanged.connect(self._on_algo)
            algo_row.addWidget(self._algo)
            root.addLayout(algo_row)

            # 4-band EQ (slider value scaled by 0.5 dB; range -48..+48 → -24..+24 dB)
            self._eq_low      = _LabeledSlider("EQ Low (dB)",   -48, 48, 0, 0.5, "dB",
                                                lambda v: self._send_dsp(0, v))
            self._eq_lowmid   = _LabeledSlider("EQ LowMid",     -48, 48, 0, 0.5, "dB",
                                                lambda v: self._send_dsp(1, v))
            self._eq_highmid  = _LabeledSlider("EQ HighMid",    -48, 48, 0, 0.5, "dB",
                                                lambda v: self._send_dsp(2, v))
            self._eq_high     = _LabeledSlider("EQ High (dB)",  -48, 48, 0, 0.5, "dB",
                                                lambda v: self._send_dsp(3, v))
            for w in (self._eq_low, self._eq_lowmid, self._eq_highmid, self._eq_high):
                root.addWidget(w)

            # User delay (0..1000 ms; slider 0..1000 step 1)
            self._delay = _LabeledSlider("Delay", 0, 1000, 0, 1.0, "ms",
                                         lambda v: self._send_dsp(4, v))
            root.addWidget(self._delay)

            # Distance HF (0..100 → 0..1)
            self._hf = _LabeledSlider("HF k", 0, 100, 50, 0.01, "",
                                       lambda v: self._send_dsp(5, v))
            root.addWidget(self._hf)

            # Reverb send (0..100 → 0..1)
            self._rev = _LabeledSlider("Reverb send", 0, 100, 0, 0.01, "",
                                        lambda v: self._send_dsp(6, v))
            root.addWidget(self._rev)

            self.setEnabled(False)  # disabled until an object is selected

        # ------------------------------------------------------------------
        # OSC dispatch helpers
        # ------------------------------------------------------------------
        def _on_algo(self, idx: int) -> None:
            if self._client is not None and self._obj_id >= 0 and hasattr(self._client, "send_object_algo"):
                self._client.send_object_algo(self._obj_id, int(idx))

        def _send_dsp(self, param: int, value: float) -> None:
            if self._client is not None and self._obj_id >= 0 and hasattr(self._client, "send_object_dsp"):
                self._client.send_object_dsp(self._obj_id, int(param), float(value))

        # ------------------------------------------------------------------
        # Selection bridge
        # ------------------------------------------------------------------
        def show_object(self, obj: object) -> None:
            if obj is None:
                self._obj_id = -1
                self._id_field.setText("")
                self._x_field.setText("")
                self._z_field.setText("")
                self.setEnabled(False)
                return
            self._obj_id = int(getattr(obj, "obj_id", -1))
            self._id_field.setText(str(self._obj_id))
            self._x_field.setText(f"{float(getattr(obj, 'x', 0.0)):.3f}")
            self._z_field.setText(f"{float(getattr(obj, 'z', 0.0)):.3f}")
            self.setEnabled(True)

except ImportError:
    # Headless stub
    class ObjectInspector:  # type: ignore[no-redef]
        def __init__(self, parent: object = None, osc_client: object | None = None) -> None:
            self._client = osc_client
            self._obj_id = -1

        def show_object(self, obj: object) -> None:
            self._obj_id = int(getattr(obj, "obj_id", -1)) if obj is not None else -1

        # Stub for tests that exercise the OSC routing path
        def _send_dsp(self, param: int, value: float) -> None:
            if self._client is not None and self._obj_id >= 0 and hasattr(self._client, "send_object_dsp"):
                self._client.send_object_dsp(self._obj_id, int(param), float(value))

        def _on_algo(self, idx: int) -> None:
            if self._client is not None and self._obj_id >= 0 and hasattr(self._client, "send_object_algo"):
                self._client.send_object_algo(self._obj_id, int(idx))
