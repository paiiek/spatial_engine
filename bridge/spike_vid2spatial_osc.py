#!/usr/bin/env python3
"""
vid2spatial_v2 -> spatial_engine ADM-OSC translator (demo spike)

Receives: UDP port 9000
  /vid2spatial/azimuth    (float, degrees, pipeline RIGHT=+az)
  /vid2spatial/elevation  (float, degrees)
  /vid2spatial/distance   (float, normalized 0-1, 1=near)
  /vid2spatial/spatial    (az, el, dist_m, velocity, timecode) bundled

Sends: UDP port 9100
  /adm/obj/{N}/azim   (float)
  /adm/obj/{N}/elev   (float)
  /adm/obj/{N}/dist   (float)

Coordinate transforms:
  az_adm   = -az_pipeline       (pipeline RIGHT=+az -> ADM LEFT=+az)
  dist_adm = 1.0 - dist_v2s     (vid2spatial near=1 -> ADM far=1)

Object ID mapping: vid2spatial has no per-object tracking_id in single-object
mode, so all messages map to obj 1 unless a config.yaml overrides.
"""

import argparse
import signal
import sys
import time
import threading
from typing import Dict, Optional

try:
    from pythonosc import dispatcher, osc_server, udp_client
    _PYTHONOSC_OK = True
except ImportError:
    _PYTHONOSC_OK = False
    dispatcher = osc_server = udp_client = None  # type: ignore

try:
    import yaml
    _YAML_OK = True
except ImportError:
    _YAML_OK = False

# ---------------------------------------------------------------------------
# Coordinate helpers
# ---------------------------------------------------------------------------

def az_pipeline_to_adm(az_pipeline: float) -> float:
    """Invert azimuth sign: pipeline RIGHT=+az -> ADM LEFT=+az."""
    return -az_pipeline


def dist_v2s_to_adm(dist_v2s: float) -> float:
    """Invert distance: vid2spatial near=1 -> ADM far=1."""
    return 1.0 - float(dist_v2s)


# ---------------------------------------------------------------------------
# IIR smoother
# ---------------------------------------------------------------------------

class IIRSmoother:
    """First-order IIR smoother per channel."""

    def __init__(self, alpha: float = 0.3):
        self.alpha = alpha
        self._state: Dict[str, float] = {}

    def smooth(self, key: str, value: float) -> float:
        if key not in self._state:
            self._state[key] = value
            return value
        self._state[key] = self.alpha * value + (1.0 - self.alpha) * self._state[key]
        return self._state[key]


# ---------------------------------------------------------------------------
# Rate limiter
# ---------------------------------------------------------------------------

class RateLimiter:
    def __init__(self, hz: float = 60.0):
        self._interval = 1.0 / hz
        self._last: Dict[str, float] = {}

    def allow(self, key: str) -> bool:
        now = time.monotonic()
        if now - self._last.get(key, 0.0) >= self._interval:
            self._last[key] = now
            return True
        return False


# ---------------------------------------------------------------------------
# Object ID mapper
# ---------------------------------------------------------------------------

class ObjectMapper:
    """Maps vid2spatial tracking IDs to ADM object numbers (1-based)."""

    def __init__(self, static_map: Optional[Dict[str, int]] = None):
        self._map: Dict[str, int] = static_map or {}
        self._next = max(self._map.values(), default=0) + 1
        self._lock = threading.Lock()

    def get(self, tracking_id: str) -> int:
        with self._lock:
            if tracking_id not in self._map:
                self._map[tracking_id] = self._next
                self._next += 1
            return self._map[tracking_id]


# ---------------------------------------------------------------------------
# Bridge
# ---------------------------------------------------------------------------

class Vid2SpatialBridge:
    def __init__(
        self,
        listen_port: int = 9000,
        target_host: str = "127.0.0.1",
        target_port: int = 9100,
        alpha: float = 0.3,
        rate_hz: float = 60.0,
        config_path: Optional[str] = None,
    ):
        self.listen_port = listen_port
        self.target_host = target_host
        self.target_port = target_port

        static_map: Dict[str, int] = {}
        if config_path and _YAML_OK:
            try:
                with open(config_path) as f:
                    cfg = yaml.safe_load(f)
                static_map = {str(k): int(v) for k, v in (cfg.get("object_map") or {}).items()}
                print(f"[bridge] Loaded {len(static_map)} static mappings from {config_path}")
            except Exception as e:
                print(f"[bridge] Warning: could not load config: {e}")

        self.smoother = IIRSmoother(alpha=alpha)
        self.rate = RateLimiter(hz=rate_hz)
        self.mapper = ObjectMapper(static_map)

        # Per-object state accumulator (az/el/dist arrive as separate messages)
        self._state: Dict[str, Dict[str, float]] = {}
        self._state_lock = threading.Lock()

        self._client = udp_client.SimpleUDPClient(target_host, target_port) if _PYTHONOSC_OK else None
        self._server: Optional[osc_server.ThreadingOSCUDPServer] = None
        self._running = False

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _obj_key(self, tracking_id: str) -> str:
        return tracking_id

    def _update_state(self, tracking_id: str, **kwargs):
        with self._state_lock:
            if tracking_id not in self._state:
                self._state[tracking_id] = {}
            self._state[tracking_id].update(kwargs)

    def _flush(self, tracking_id: str):
        """Send current state for a tracking_id if rate allows."""
        with self._state_lock:
            s = self._state.get(tracking_id, {})
            az_raw  = s.get("az",   0.0)
            el_raw  = s.get("el",   0.0)
            dist_raw = s.get("dist", 0.5)

        obj_n = self.mapper.get(tracking_id)
        key   = f"obj_{obj_n}"

        az_adm   = self.smoother.smooth(f"{key}_az",   az_pipeline_to_adm(az_raw))
        el_adm   = self.smoother.smooth(f"{key}_el",   el_raw)
        dist_adm = self.smoother.smooth(f"{key}_dist", dist_v2s_to_adm(dist_raw))

        if not self.rate.allow(key):
            return

        prefix = f"/adm/obj/{obj_n}"
        self._client.send_message(f"{prefix}/azim", float(az_adm))
        self._client.send_message(f"{prefix}/elev", float(el_adm))
        self._client.send_message(f"{prefix}/dist", float(dist_adm))

    # ------------------------------------------------------------------
    # OSC handlers  (vid2spatial sends /vid2spatial/<param> flat messages
    #                or /vid2spatial/spatial bundle)
    # ------------------------------------------------------------------

    def _handle_azimuth(self, address, *args):
        val = float(args[0]) if args else 0.0
        self._update_state("default", az=val)
        self._flush("default")

    def _handle_elevation(self, address, *args):
        val = float(args[0]) if args else 0.0
        self._update_state("default", el=val)
        self._flush("default")

    def _handle_distance(self, address, *args):
        val = float(args[0]) if args else 0.5
        self._update_state("default", dist=val)
        self._flush("default")

    def _handle_spatial(self, address, *args):
        # /vid2spatial/spatial [az_deg, el_deg, dist_m, velocity, timecode]
        if len(args) >= 3:
            az_deg  = float(args[0])
            el_deg  = float(args[1])
            dist_m  = float(args[2])
            # dist_m is in metres here; convert to normalized (same formula as osc_sender._normalize_distance)
            dist_norm = max(0.0, min(1.0, 1.0 - dist_m / 10.0))
            self._update_state("default", az=az_deg, el=el_deg, dist=dist_norm)
            self._flush("default")

    def _handle_fallback(self, address, *args):
        pass  # ignore /vid2spatial/velocity, /timecode, /frame

    # ------------------------------------------------------------------
    # Server lifecycle
    # ------------------------------------------------------------------

    def start(self):
        if not _PYTHONOSC_OK:
            print("[bridge] python-osc not installed. Run: pip install python-osc")
            sys.exit(1)
        disp = dispatcher.Dispatcher()
        disp.map("/vid2spatial/azimuth",   self._handle_azimuth)
        disp.map("/vid2spatial/elevation", self._handle_elevation)
        disp.map("/vid2spatial/distance",  self._handle_distance)
        disp.map("/vid2spatial/spatial",   self._handle_spatial)
        disp.set_default_handler(self._handle_fallback)

        self._server = osc_server.ThreadingOSCUDPServer(
            ("0.0.0.0", self.listen_port), disp
        )
        self._running = True
        print(
            f"[bridge] Listening on 0.0.0.0:{self.listen_port} -> "
            f"{self.target_host}:{self.target_port}"
        )
        self._server.serve_forever()

    def stop(self):
        if self._server and self._running:
            self._server.shutdown()
            self._running = False
            print("[bridge] Stopped.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="vid2spatial_v2 -> spatial_engine ADM-OSC bridge (demo spike)"
    )
    parser.add_argument("--listen-port",  type=int,   default=9000,        help="UDP port to receive vid2spatial OSC (default 9000)")
    parser.add_argument("--target-port",  type=int,   default=9100,        help="UDP port to send ADM-OSC (default 9100)")
    parser.add_argument("--target-host",  type=str,   default="127.0.0.1", help="Host to send ADM-OSC (default 127.0.0.1)")
    parser.add_argument("--alpha",        type=float, default=0.3,         help="IIR smoothing factor 0-1 (default 0.3)")
    parser.add_argument("--rate-hz",      type=float, default=60.0,        help="Output rate limit in Hz (default 60)")
    parser.add_argument("--config",       type=str,   default=None,        help="Optional config.yaml for static object_map")
    args = parser.parse_args()

    bridge = Vid2SpatialBridge(
        listen_port=args.listen_port,
        target_host=args.target_host,
        target_port=args.target_port,
        alpha=args.alpha,
        rate_hz=args.rate_hz,
        config_path=args.config,
    )

    def _sigint(sig, frame):
        print("\n[bridge] Ctrl+C received, shutting down...")
        bridge.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, _sigint)
    signal.signal(signal.SIGTERM, _sigint)

    bridge.start()


if __name__ == "__main__":
    main()
