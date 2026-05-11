"""Playwright-based browser harness for WebGUI fps gates (G2 / G4).

This package is opt-in: tests are marked ``@pytest.mark.playwright`` and are
skipped unless the marker is selected explicitly (or chromium is installed
and discoverable).

Gates implemented here:

* G2 desktop — `test_fps_desktop.py` — min-of-5-windows-p10 >= 60 fps with
  64 active objects + concurrent drag synth.

See ``.omc/plans/spatial-engine-webgui-v1.md`` §2 G2 for the contract.
"""
