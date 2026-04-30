# Coordinate Convention — Single Source of Truth (v0)

All coordinate transformations in `spatial_engine` must go through
`core/src/coords/Coords.h`. No sign-flips anywhere else.

---

## Frames

### 1. Pipeline / vid2spatial-native

| Symbol | Meaning |
|--------|---------|
| `az`   | Azimuth in **radians**. `az = atan2(x_listener, z_listener)`. **RIGHT = +az**. |
| `el`   | Elevation in **radians**. `el = arcsin(-y_image_normalized)`. **UP = +el**. |
| Origin | Listener position. z-axis points forward (away from listener). |

Example: a speaker 45° to the right, at ear height, distance 1 m:
- `az = +π/4`, `el = 0.0`, `dist = 1.0 m`
- Cartesian: `x = sin(π/4) = 0.707`, `y = 0`, `z = cos(π/4) = 0.707`

### 2. AmbiX / SOFA

| Symbol | Meaning |
|--------|---------|
| `az`   | Azimuth in **radians**. **LEFT = +az** (counterclockwise from front). |
| `el`   | Elevation in **radians**. **UP = +el** (same as Pipeline). |

Conversion from Pipeline:
```
pipeline_to_ambix(az, el) = (-az, el)
```

Example: speaker at `az_pipeline = +π/4` (right) maps to `az_ambix = -π/4`.

### 3. VBAP Layout-Frame (engine internal)

Speaker positions stored in YAML as `(az_deg, el_deg, dist_m)` or Cartesian `(x, y, z)`.

| Convention | Definition |
|-----------|-----------|
| `az_deg`  | Degrees from front. **RIGHT = +az**. Range [-180, +180]. |
| `el_deg`  | Degrees above horizon. **UP = +el**. |
| `dist_m`  | Distance in metres from origin. |

Conversion to Cartesian (x=right, y=up, z=front):
```
x = dist * cos(el) * sin(az)
y = dist * sin(el)
z = dist * cos(el) * cos(az)
```

Example: speaker at `az_deg=+90, el_deg=0, dist_m=1.0`:
- `x = 1.0, y = 0.0, z = 0.0`  (purely to the right)

### 4. Image-y-down

Pixel y-coordinate grows **downward** in the image.

| Symbol              | Meaning |
|--------------------|---------|
| `y_image_normalized` | `(pixel_y - height/2) / (height/2)`. Range [-1, +1]. |
| `el`               | `arcsin(-y_image_normalized)` → UP = +el in listener frame. |

Example: object at bottom of frame (`y_image_normalized = +1.0`):
- `el = arcsin(-1.0) = -π/2`  (below horizon, correct)

---

## Conversion Table

| From            | To              | Formula                              |
|-----------------|-----------------|--------------------------------------|
| Pipeline        | AmbiX/SOFA      | `(-az, el)`                         |
| AmbiX/SOFA      | Pipeline        | `(-az, el)`  (same, symmetric)      |
| Cartesian       | Pipeline        | `az=atan2(x,z)`, `el=atan2(y,sqrt(x²+z²))`, `dist=norm` |
| YAML speaker    | Cartesian       | `x=d·cos(el)·sin(az)`, `y=d·sin(el)`, `z=d·cos(el)·cos(az)` |
| Image-y-down    | Listener el     | `arcsin(-y_image_normalized)`        |
| Pipeline az     | Stereo pan      | `sin(az_pipe)` — RIGHT louder for +az |

---

## Stereo Pan Anti-Regression Lock (2026-03-01)

The 2026-03-01 `baseline_pan` inversion bug used `sin(-az)` which inverted L/R.
The correct formula is always `sin(az_pipe)`:

```cpp
// CORRECT — locked in stereo_pan_from_pipeline_az()
float pan = sin(az_pipe);   // +az (RIGHT) -> sin > 0 -> R louder
```

```cpp
// WRONG — DO NOT USE. This was the 2026-03-01 inversion bug.
float pan = sin(-az_pipe);  // inverts L/R
```

Test assertion that locks this regression:
```cpp
assert(stereo_pan_from_pipeline_az(+π/2) > stereo_pan_from_pipeline_az(0));
assert(stereo_pan_from_pipeline_az(0)    > stereo_pan_from_pipeline_az(-π/2));
```

---

## All Helpers (namespace `spe::coords`)

```cpp
// Coords.h — header-only inline functions

std::pair<float,float> pipeline_to_ambix(float az_pipe, float el_pipe);
std::pair<float,float> ambix_to_pipeline(float az_ambix, float el_ambix);
std::tuple<float,float,float> cartesian_to_pipeline(float x, float y, float z);
float image_y_to_listener_el(float y_image_normalized);
std::array<float,3> yaml_speaker_to_cartesian(float az_deg, float el_deg, float dist_m=1.0f);
float stereo_pan_from_pipeline_az(float az_pipe);
```

---

## See Also

- `core/src/coords/CoordsTests.h` — hand-computed expected values for unit tests
- `core/tests/core_unit/test_p2_coords.cpp` — full test suite
- `configs/lab_*.yaml` — speaker layout examples
- `proto/geometry_schema.json` — JSON schema for YAML layouts
