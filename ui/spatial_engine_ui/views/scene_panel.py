# ui/spatial_engine_ui/views/scene_panel.py
# Scene snapshot panel — save / load / list scenes via OSC.
# PySide6 import is optional so this module can be imported in headless test environments.

# OSC address constants (used by tests and by the panel widget).
OSC_SCENE_SAVE = "/scene/save"
OSC_SCENE_LOAD = "/scene/load"
OSC_SCENE_LIST = "/scene/list"

try:
    from PySide6.QtWidgets import (
        QWidget, QVBoxLayout, QHBoxLayout,
        QPushButton, QLineEdit, QListWidget, QLabel,
    )
    _HAS_QT = True
except ImportError:
    _HAS_QT = False


if _HAS_QT:
    class ScenePanel(QWidget):
        """Simple panel: text field for scene name + Save / Load / List buttons."""

        def __init__(self, osc_sender=None, parent=None):
            super().__init__(parent)
            self._osc = osc_sender  # callable(address: str, *args) or None

            self._name_edit = QLineEdit()
            self._name_edit.setPlaceholderText("Scene name…")

            btn_save = QPushButton("Save")
            btn_load = QPushButton("Load")
            btn_list = QPushButton("List")

            btn_save.clicked.connect(self._on_save)
            btn_load.clicked.connect(self._on_load)
            btn_list.clicked.connect(self._on_list)

            self._scene_list = QListWidget()

            row = QHBoxLayout()
            row.addWidget(self._name_edit)
            row.addWidget(btn_save)
            row.addWidget(btn_load)
            row.addWidget(btn_list)

            layout = QVBoxLayout(self)
            layout.addWidget(QLabel("Scenes"))
            layout.addLayout(row)
            layout.addWidget(self._scene_list)

        def _send(self, addr, *args):
            if self._osc:
                self._osc(addr, *args)

        def _on_save(self):
            name = self._name_edit.text().strip()
            if name:
                self._send(OSC_SCENE_SAVE, name)

        def _on_load(self):
            name = self._name_edit.text().strip()
            if name:
                self._send(OSC_SCENE_LOAD, name)

        def _on_list(self):
            self._send(OSC_SCENE_LIST)
