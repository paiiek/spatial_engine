"""Headless tests for ElevationControl and adm_obj_elev — no Qt required."""
from __future__ import annotations

import sys
import types


def _stub_pyside6() -> None:
    """Insert minimal PySide6 stubs so topdown.py import path takes the stub branch."""
    if "PySide6" not in sys.modules:
        pyside6 = types.ModuleType("PySide6")
        sys.modules["PySide6"] = pyside6

        # Raise ImportError when QtWidgets is imported so the except branch fires
        class _FailLoader:
            def __getattr__(self, name: str):  # noqa: ANN001
                raise ImportError("headless stub")

        qtwidgets = types.ModuleType("PySide6.QtWidgets")
        # Make importing any name from it raise AttributeError-like failure
        # The try block in topdown.py does `from PySide6.QtWidgets import ...`
        # which will raise ImportError if the module raises on attribute access.
        sys.modules["PySide6.QtWidgets"] = None  # type: ignore[assignment]


# Ensure stub branch is used before importing the module
_stub_pyside6()

from spatial_engine_ui.ipc.protocol import adm_obj_elev  # noqa: E402
from spatial_engine_ui.views.topdown import ElevationControl  # noqa: E402


# ---------------------------------------------------------------------------
# Test 1: adm_obj_elev address format
# ---------------------------------------------------------------------------
def test_adm_obj_elev_address():
    assert adm_obj_elev(3) == "/adm/obj/3/elev"
    assert adm_obj_elev(1) == "/adm/obj/1/elev"
    assert adm_obj_elev(99) == "/adm/obj/99/elev"


# ---------------------------------------------------------------------------
# Test 2: ElevationControl stub is importable
# ---------------------------------------------------------------------------
def test_elevation_control_importable():
    ctrl = ElevationControl()
    assert ctrl is not None


# ---------------------------------------------------------------------------
# Test 3: slider range constants
# ---------------------------------------------------------------------------
def test_elevation_range_constants():
    assert ElevationControl.ELEV_MIN == -90
    assert ElevationControl.ELEV_MAX == 90
    assert ElevationControl.ELEV_DEFAULT == 0


# ---------------------------------------------------------------------------
# Test 4: value conversion — degrees=45.0 passed as float
# ---------------------------------------------------------------------------
def test_elevation_value_conversion():
    received: list[tuple[str, float]] = []

    def mock_send(addr: str, val: float) -> None:
        received.append((addr, val))

    ctrl = ElevationControl(send_osc=mock_send)
    ctrl.set_object(3)
    ctrl.on_value_changed(45.0)

    assert len(received) == 1
    addr, val = received[0]
    assert addr == "/adm/obj/3/elev"
    assert isinstance(val, float)
    assert val == 45.0


# ---------------------------------------------------------------------------
# Test 5: no send when obj_id not set
# ---------------------------------------------------------------------------
def test_no_send_without_object():
    received: list = []
    ctrl = ElevationControl(send_osc=lambda a, v: received.append((a, v)))
    ctrl.on_value_changed(30.0)
    assert received == []


# ---------------------------------------------------------------------------
# Test 6: set_object resets default value
# ---------------------------------------------------------------------------
def test_set_object_resets_value():
    ctrl = ElevationControl()
    ctrl.set_object(5)
    assert ctrl._value == float(ElevationControl.ELEV_DEFAULT)
