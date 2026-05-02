# vid2spatial -> spatial_engine OSC Bridge (Demo Spike)

Translates vid2spatial_v2 OSC output to spatial_engine ADM-OSC format.

## Quick Start

```bash
# Install dependency if needed
pip install python-osc

# Run with defaults (listen 9000, send to 127.0.0.1:9100)
python3 bridge/spike_vid2spatial_osc.py

# Custom ports
python3 bridge/spike_vid2spatial_osc.py --listen-port 9000 --target-port 9100 --target-host 127.0.0.1

# With smoothing and rate options
python3 bridge/spike_vid2spatial_osc.py --alpha 0.3 --rate-hz 60

# With static object ID map (YAML)
python3 bridge/spike_vid2spatial_osc.py --config bridge/config.yaml
```

Stop with Ctrl+C.

## config.yaml format (optional)

```yaml
object_map:
  "default": 1     # vid2spatial tracking_id -> ADM obj number
  "person_0": 2
```

## Coordinate Transforms

| Parameter | vid2spatial | ADM-OSC | Transform |
|-----------|-------------|---------|-----------|
| Azimuth   | RIGHT=+az (deg) | LEFT=+az (deg) | `az_adm = -az_pipeline` |
| Elevation | +up (deg) | +up (deg) | identity |
| Distance  | near=1.0, far=0.0 (normalized) | near=0.0, far=1.0 | `dist_adm = 1.0 - dist_v2s` |

## Message Mapping

Receives from vid2spatial_v2 (port 9000):
- `/vid2spatial/azimuth` (float, degrees)
- `/vid2spatial/elevation` (float, degrees)
- `/vid2spatial/distance` (float, normalized 0-1)
- `/vid2spatial/spatial` (az, el, dist_m, velocity, timecode) — bundled

Sends to spatial_engine (port 9100):
- `/adm/obj/{N}/azim` (float)
- `/adm/obj/{N}/elev` (float)
- `/adm/obj/{N}/dist` (float)

## Known Limitations

- vid2spatial_v2 single-object mode: all messages map to obj 1 ("default" key).
- IIR smoothing (alpha=0.3) adds ~33ms lag at 180 deg/s pan speed — acceptable for demo.
- `/vid2spatial/spatial` bundle carries `dist_m` in metres; bridge normalises with max_dist=10m to match `_normalize_distance` in osc_sender.py.
- No OSC bundle (atomic) send — three separate messages per frame.
- Demo quality only: no reconnect logic, no metrics, no config hot-reload.
