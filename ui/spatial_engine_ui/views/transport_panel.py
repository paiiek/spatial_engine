"""Transport panel — Play / Stop buttons routed via OSC.

Headless stub: importable without PySide6. Tests verify OSC routing through
either backend.
"""
from __future__ import annotations

OSC_TRANSPORT_PLAY = "/transport/play"
OSC_TRANSPORT_STOP = "/transport/stop"

try:
    from PySide6.QtWidgets import QWidget, QHBoxLayout, QPushButton, QLabel
    _HAS_QT = True
except ImportError:
    _HAS_QT = False


if _HAS_QT:
    class TransportPanel(QWidget):
        """Two-button transport bar (Play / Stop). Sends raw OSC addresses."""

        def __init__(self, osc_sender=None, parent=None) -> None:
            super().__init__(parent)
            self._osc = osc_sender  # callable(address, *args) OR an osc_client.OscClient

            row = QHBoxLayout(self)
            row.addWidget(QLabel("Transport:", self))
            self._btn_play = QPushButton("▶ Play", self)
            self._btn_stop = QPushButton("■ Stop", self)
            self._btn_play.clicked.connect(self._on_play)
            self._btn_stop.clicked.connect(self._on_stop)
            row.addWidget(self._btn_play)
            row.addWidget(self._btn_stop)

        def _send(self, addr: str, *args: object) -> None:
            if self._osc is None:
                return
            if hasattr(self._osc, "send_transport_play") and addr == OSC_TRANSPORT_PLAY:
                self._osc.send_transport_play()
                return
            if hasattr(self._osc, "send_transport_stop") and addr == OSC_TRANSPORT_STOP:
                self._osc.send_transport_stop()
                return
            if callable(self._osc):
                self._osc(addr, *args)
            elif hasattr(self._osc, "send"):
                self._osc.send(addr, *args)

        def _on_play(self) -> None:
            self._send(OSC_TRANSPORT_PLAY)

        def _on_stop(self) -> None:
            self._send(OSC_TRANSPORT_STOP)
else:
    class TransportPanel:  # type: ignore[no-redef]
        """Headless stub mirroring TransportPanel's OSC routing."""

        def __init__(self, osc_sender=None, parent=None) -> None:
            self._osc = osc_sender

        def _send(self, addr: str, *args: object) -> None:
            if self._osc is None:
                return
            if hasattr(self._osc, "send_transport_play") and addr == OSC_TRANSPORT_PLAY:
                self._osc.send_transport_play()
                return
            if hasattr(self._osc, "send_transport_stop") and addr == OSC_TRANSPORT_STOP:
                self._osc.send_transport_stop()
                return
            if callable(self._osc):
                self._osc(addr, *args)
            elif hasattr(self._osc, "send"):
                self._osc.send(addr, *args)

        def _on_play(self) -> None:
            self._send(OSC_TRANSPORT_PLAY)

        def _on_stop(self) -> None:
            self._send(OSC_TRANSPORT_STOP)
