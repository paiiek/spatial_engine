"""Noise generator panel — white/pink noise routed to selected channel."""
from __future__ import annotations

try:
    from PySide6.QtWidgets import (
        QWidget, QVBoxLayout, QHBoxLayout, QLabel,
        QComboBox, QSlider, QPushButton, QSpinBox,
    )
    from PySide6.QtCore import Qt

    class NoisePanel(QWidget):
        def __init__(self, osc_client: object = None, parent: QWidget | None = None) -> None:
            super().__init__(parent)
            self._client = osc_client
            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("Noise Generator", self))

            row1 = QHBoxLayout()
            row1.addWidget(QLabel("Channel:", self))
            self._channel = QSpinBox(self)
            self._channel.setRange(0, 63)
            row1.addWidget(self._channel)
            layout.addLayout(row1)

            row2 = QHBoxLayout()
            row2.addWidget(QLabel("Type:", self))
            self._type = QComboBox(self)
            self._type.addItems(["white", "pink"])
            row2.addWidget(self._type)
            layout.addLayout(row2)

            row3 = QHBoxLayout()
            row3.addWidget(QLabel("Gain (dB):", self))
            self._gain = QSlider(Qt.Orientation.Horizontal, self)
            self._gain.setRange(-60, 0)
            self._gain.setValue(-12)
            row3.addWidget(self._gain)
            layout.addLayout(row3)

            btn = QPushButton("Apply", self)
            btn.clicked.connect(self._apply)
            layout.addWidget(btn)

        def _apply(self) -> None:
            if self._client is None:
                return
            ch = self._channel.value()
            noise_type = self._type.currentText()
            gain_db = float(self._gain.value())
            self._client.send_noise_type(ch, noise_type)
            self._client.send_noise_gain(ch, gain_db)

except ImportError:
    class NoisePanel:  # type: ignore[no-redef]
        def __init__(self, osc_client: object = None, parent: object = None) -> None:
            self._client = osc_client
