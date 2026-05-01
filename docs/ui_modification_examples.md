# UI Modification Examples

Three worked examples for new contributors. Each is a self-contained UI-only change
that does not require touching the C++ core or OSC schema. Use these to verify your
toolchain end-to-end after onboarding.

---

## Example 1: Per-Object Peak Meter Widget

**File to modify**: `ui/views/object_panels.py`

Add a peak-meter bar next to the gain slider for each spatial object.

### What to add

```python
# ui/views/object_panels.py

from PySide6.QtWidgets import QProgressBar

class ObjectPanel(QWidget):
    def __init__(self, obj_id: int, parent=None):
        super().__init__(parent)
        # ... existing init ...

        # NEW: peak meter
        self._peak_meter = QProgressBar()
        self._peak_meter.setRange(0, 100)          # 0 = -60 dBFS, 100 = 0 dBFS
        self._peak_meter.setValue(0)
        self._peak_meter.setOrientation(Qt.Vertical)
        self._peak_meter.setTextVisible(False)
        self._peak_meter.setFixedWidth(12)
        self._layout.addWidget(self._peak_meter)   # add to existing HBoxLayout

    def update_peak(self, linear_gain: float) -> None:
        """Call from OSC state handler when /sys/metrics arrives."""
        import math
        db = 20.0 * math.log10(max(linear_gain, 1e-6))
        pct = int(max(0.0, min(100.0, (db + 60.0))))
        self._peak_meter.setValue(pct)
```

### Where to wire it

In the OSC state handler (typically `ui/controllers/state_receiver.py`), find where
`/sys/metrics` is parsed and call `panel.update_peak(level)` for the matching object ID.

### Test

Run `just test` — no new test required for a display-only widget. Visually verify the
meter moves when a test signal is playing.

---

## Example 2: Drag Sensitivity / Smoothing Tau

**File to modify**: `ui/controllers/drag.py`

Adjust how quickly the top-down view follows mouse drags (coalescer rate / smoothing tau).

### Current pattern (approximate)

```python
# ui/controllers/drag.py

DRAG_COALESCE_RATE_HZ = 30      # send OSC at most 30 times/sec
SMOOTHING_TAU_MS = 50           # exponential smoothing time constant

class DragCoalescer:
    def __init__(self):
        self._timer = QTimer()
        self._timer.setInterval(int(1000 / DRAG_COALESCE_RATE_HZ))
        self._timer.timeout.connect(self._flush)
        self._alpha = 1.0 - math.exp(-self._timer.interval() / SMOOTHING_TAU_MS)
```

### Modification

To make dragging feel more responsive (less lag), decrease `SMOOTHING_TAU_MS`:

```python
SMOOTHING_TAU_MS = 20   # was 50; snappier feel
```

To reduce OSC traffic on slow networks, decrease `DRAG_COALESCE_RATE_HZ`:

```python
DRAG_COALESCE_RATE_HZ = 15   # was 30; half the OSC rate
```

### Test

Use `tools/osc_debug_console.py --listen 9100` to observe OSC packet rate while dragging
an object. Verify the rate matches your configured `DRAG_COALESCE_RATE_HZ`.

---

## Example 3: Recolor / Reicon Objects by Category

**File to modify**: `ui/views/topdown.py`

Change the color or icon of objects rendered in the top-down spatial view.

### Current pattern (approximate)

```python
# ui/views/topdown.py

CATEGORY_COLORS = {
    "default":  QColor("#4A90D9"),
    "voice":    QColor("#7ED321"),
    "music":    QColor("#F5A623"),
    "effect":   QColor("#BD10E0"),
}

CATEGORY_ICONS = {
    "default": "circle",
    "voice":   "person",
    "music":   "note",
    "effect":  "star",
}

class TopDownView(QWidget):
    def _paint_object(self, painter: QPainter, obj: ObjectState) -> None:
        color = CATEGORY_COLORS.get(obj.category, CATEGORY_COLORS["default"])
        icon  = CATEGORY_ICONS.get(obj.category, "circle")
        painter.setBrush(QBrush(color))
        # ... draw icon shape ...
```

### Modification

Add a new category or change existing colors:

```python
CATEGORY_COLORS = {
    "default":  QColor("#4A90D9"),
    "voice":    QColor("#E74C3C"),   # changed: red for voice
    "music":    QColor("#2ECC71"),   # changed: green for music
    "effect":   QColor("#F39C12"),   # changed: amber for effect
    "ambience": QColor("#95A5A6"),   # NEW: grey for ambience
}

CATEGORY_ICONS = {
    "default":  "circle",
    "voice":    "person",
    "music":    "note",
    "effect":   "star",
    "ambience": "wave",             # NEW
}
```

### Test

Launch the UI (`just run`) and verify objects with each category show the correct color.
Category is set via the object panel dropdown; it is a UI-only label and does not affect
the OSC command stream.
