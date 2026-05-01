import sys
sys.modules.setdefault("PySide6.QtWidgets", None)


def test_elevation_view_import():
    from spatial_engine_ui.views.topdown import ElevationView
    view = ElevationView()
    assert view is not None


def test_update_objects():
    from spatial_engine_ui.views.topdown import ElevationView
    view = ElevationView()
    view.update_objects([(1.0, 0.0, 0.0), (0.0, 0.5, 0.0)])
    assert len(view._objects) == 2


def test_to_screen_coords_horizontal():
    from spatial_engine_ui.views.topdown import ElevationView
    # (x=1, y=0, z=0): r=1, el=0 -> sx > margin, sy == center_y
    sx, sy = ElevationView.to_screen_coords(1.0, 0.0, 0.0, 200, 200, 20)
    assert sx > 20
    center_y = 20 + int((1.0 - (0.0 + 1.0) * 0.5) * (200 - 40))
    assert sy == center_y


def test_to_screen_coords_overhead():
    from spatial_engine_ui.views.topdown import ElevationView
    # (x=0, y=1, z=0): r=0 -> sx == margin (overhead collapses to r=0)
    sx, sy = ElevationView.to_screen_coords(0.0, 1.0, 0.0, 200, 200, 20)
    assert sx == 20  # r=0 -> sx = margin
