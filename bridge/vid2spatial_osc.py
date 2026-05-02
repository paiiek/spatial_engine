#!/usr/bin/env python3
"""
bridge/vid2spatial_osc.py — vid2spatial_v2 → spatial_engine ADM-OSC 프로덕션 브리지

변환 계약 (docs/adr/vid2spatial_osc_contract.md v1.0):
  port: 9000(수신) → 9100(송신)
  prefix: /vid2spatial → /adm/obj/N/
  az_adm = -az_pipeline   (RIGHT=+az → LEFT=+az)
  dist_adm = 1.0 - dist_v2s (near=1 → far=1)

이중 모드:
  AI 모드: vid2spatial 수신 → ADM-OSC 변환 → 9100 송신
  저레이턴시 모드: 수신 무시 (수동 OSC만)
"""

import argparse
import signal
import sys
import time
import threading
import logging
from typing import Dict, Optional, Any

logger = logging.getLogger(__name__)

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

# Default config values
_DEFAULTS = dict(
    listen_port=9000,
    target_port=9100,
    target_host="127.0.0.1",
    mode="ai",
    iir_alpha=0.3,
    rate_limit_hz=60.0,
    object_map={},
    max_objects=8,
)


# ---------------------------------------------------------------------------
# Coordinate transformation contract
# ---------------------------------------------------------------------------

class OscTranslator:
    """좌표 변환 계약 구현 (az_adm=-az, dist_adm=1-dist)."""

    @staticmethod
    def az_pipeline_to_adm(az_pipeline: float) -> float:
        """Invert azimuth sign: pipeline RIGHT=+az -> ADM LEFT=+az."""
        return -float(az_pipeline)

    @staticmethod
    def dist_v2s_to_adm(dist_v2s: float) -> float:
        """Invert distance: vid2spatial near=1 -> ADM far=1."""
        return 1.0 - float(dist_v2s)

    @staticmethod
    def elev_to_adm(elev: float) -> float:
        """Elevation: identity (both systems +up)."""
        return float(elev)


# ---------------------------------------------------------------------------
# IIR smoother
# ---------------------------------------------------------------------------

class IIRSmoother:
    """α=0.3 per-object 1차 IIR 저역통과 필터."""

    def __init__(self, alpha: float = 0.3):
        self.alpha = alpha
        self._state: Dict[str, float] = {}
        self._lock = threading.Lock()

    def smooth(self, key: str, value: float) -> float:
        with self._lock:
            if key not in self._state:
                self._state[key] = value
                return value
            self._state[key] = self.alpha * value + (1.0 - self.alpha) * self._state[key]
            return self._state[key]

    def reset(self, key: str) -> None:
        with self._lock:
            self._state.pop(key, None)


# ---------------------------------------------------------------------------
# Rate limiter
# ---------------------------------------------------------------------------

class RateLimiter:
    """60Hz 상한 per-object 레이트 리미터."""

    def __init__(self, hz: float = 60.0):
        self._interval = 1.0 / hz if hz > 0 else 0.0
        self._last: Dict[str, float] = {}
        self._lock = threading.Lock()

    def allow(self, key: str) -> bool:
        now = time.monotonic()
        with self._lock:
            if now - self._last.get(key, 0.0) >= self._interval:
                self._last[key] = now
                return True
            return False


# ---------------------------------------------------------------------------
# Object mapper
# ---------------------------------------------------------------------------

class ObjectMapper:
    """tracking_id → ADM obj 번호 (자동 할당, config.yaml 오버라이드)."""

    def __init__(
        self,
        static_map: Optional[Dict[str, int]] = None,
        max_objects: int = 8,
    ):
        self._map: Dict[str, int] = {str(k): int(v) for k, v in (static_map or {}).items()}
        self._next = max(self._map.values(), default=0) + 1
        self._max_objects = max_objects
        self._lock = threading.Lock()

    def get(self, tracking_id: str) -> Optional[int]:
        """Return ADM object number for tracking_id; None if max_objects exceeded."""
        with self._lock:
            if tracking_id not in self._map:
                if len(self._map) >= self._max_objects:
                    logger.warning(
                        "max_objects=%d reached, ignoring tracking_id=%s",
                        self._max_objects, tracking_id,
                    )
                    return None
                self._map[tracking_id] = self._next
                self._next += 1
            return self._map[tracking_id]


# ---------------------------------------------------------------------------
# Bridge server
# ---------------------------------------------------------------------------

class BridgeServer:
    """OSC 9000 수신 → 변환 → 9100 송신.

    mode: 'ai' | 'low_latency'
    switch_mode(mode): 모드 전환, 전환 시 마지막 위치 유지
    """

    def __init__(
        self,
        listen_port: int = 9000,
        target_host: str = "127.0.0.1",
        target_port: int = 9100,
        mode: str = "ai",
        iir_alpha: float = 0.3,
        rate_limit_hz: float = 60.0,
        static_map: Optional[Dict[str, int]] = None,
        max_objects: int = 8,
    ):
        self.listen_port = listen_port
        self.target_host = target_host
        self.target_port = target_port

        self._mode = mode
        self._mode_lock = threading.Lock()

        self.translator = OscTranslator()
        self.smoother = IIRSmoother(alpha=iir_alpha)
        self.rate = RateLimiter(hz=rate_limit_hz)
        self.mapper = ObjectMapper(static_map=static_map, max_objects=max_objects)

        # Per-object state accumulator
        self._state: Dict[str, Dict[str, float]] = {}
        self._state_lock = threading.Lock()

        self._client = (
            udp_client.SimpleUDPClient(target_host, target_port)
            if _PYTHONOSC_OK else None
        )
        self._server: Any = None
        self._running = False

    # ------------------------------------------------------------------
    # Mode management
    # ------------------------------------------------------------------

    @property
    def mode(self) -> str:
        with self._mode_lock:
            return self._mode

    def switch_mode(self, mode: str) -> None:
        """モード転換 — 마지막 위치 유지, 전환 시간 < 500ms 보장."""
        if mode not in ("ai", "low_latency"):
            raise ValueError(f"Invalid mode: {mode!r}. Must be 'ai' or 'low_latency'.")
        with self._mode_lock:
            old = self._mode
            self._mode = mode
        logger.info("[bridge] mode switched %s -> %s", old, mode)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _update_state(self, tracking_id: str, **kwargs: float) -> None:
        with self._state_lock:
            if tracking_id not in self._state:
                self._state[tracking_id] = {}
            self._state[tracking_id].update(kwargs)

    def _flush(self, tracking_id: str) -> None:
        """Send current state for tracking_id if rate allows and mode is ai."""
        if self.mode != "ai":
            return
        if self._client is None:
            return

        with self._state_lock:
            s = self._state.get(tracking_id, {})
            az_raw   = s.get("az",   0.0)
            el_raw   = s.get("el",   0.0)
            dist_raw = s.get("dist", 0.5)

        obj_n = self.mapper.get(tracking_id)
        if obj_n is None:
            return

        key = f"obj_{obj_n}"

        az_adm   = self.smoother.smooth(f"{key}_az",   OscTranslator.az_pipeline_to_adm(az_raw))
        el_adm   = self.smoother.smooth(f"{key}_el",   OscTranslator.elev_to_adm(el_raw))
        dist_adm = self.smoother.smooth(f"{key}_dist", OscTranslator.dist_v2s_to_adm(dist_raw))

        if not self.rate.allow(key):
            return

        prefix = f"/adm/obj/{obj_n}"
        self._client.send_message(f"{prefix}/azim", float(az_adm))
        self._client.send_message(f"{prefix}/elev", float(el_adm))
        self._client.send_message(f"{prefix}/dist", float(dist_adm))

    # ------------------------------------------------------------------
    # OSC handlers
    # ------------------------------------------------------------------

    def _handle_azimuth(self, address: str, *args: Any) -> None:
        if self.mode != "ai":
            return
        val = float(args[0]) if args else 0.0
        self._update_state("default", az=val)
        self._flush("default")

    def _handle_elevation(self, address: str, *args: Any) -> None:
        if self.mode != "ai":
            return
        val = float(args[0]) if args else 0.0
        self._update_state("default", el=val)
        self._flush("default")

    def _handle_distance(self, address: str, *args: Any) -> None:
        if self.mode != "ai":
            return
        val = float(args[0]) if args else 0.5
        self._update_state("default", dist=val)
        self._flush("default")

    def _handle_spatial(self, address: str, *args: Any) -> None:
        # /vid2spatial/spatial [az_deg, el_deg, dist_m, velocity, timecode]
        if self.mode != "ai":
            return
        if len(args) >= 3:
            az_deg  = float(args[0])
            el_deg  = float(args[1])
            dist_m  = float(args[2])
            # dist_m in metres; normalise with max_dist=10m
            dist_norm = max(0.0, min(1.0, 1.0 - dist_m / 10.0))
            self._update_state("default", az=az_deg, el=el_deg, dist=dist_norm)
            self._flush("default")

    def _handle_fallback(self, address: str, *args: Any) -> None:
        pass  # ignore /vid2spatial/velocity, /timecode, /frame

    # ------------------------------------------------------------------
    # Server lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
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
            f"[bridge] mode={self.mode}  "
            f"Listening on 0.0.0.0:{self.listen_port} -> "
            f"{self.target_host}:{self.target_port}"
        )
        self._server.serve_forever()

    def stop(self) -> None:
        if self._server and self._running:
            self._server.shutdown()
            self._running = False
            print("[bridge] Stopped.")


# ---------------------------------------------------------------------------
# Config loader
# ---------------------------------------------------------------------------

def load_config(path: str) -> dict:
    cfg = dict(_DEFAULTS)
    if not path:
        return cfg
    if not _YAML_OK:
        logger.warning("[bridge] PyYAML not installed; using defaults.")
        return cfg
    try:
        with open(path) as f:
            data = yaml.safe_load(f) or {}
        cfg.update({k: v for k, v in data.items() if k in cfg})
        logger.info("[bridge] Loaded config from %s", path)
    except Exception as exc:
        logger.warning("[bridge] Could not load config %s: %s", path, exc)
    return cfg


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="vid2spatial_v2 -> spatial_engine ADM-OSC 프로덕션 브리지"
    )
    parser.add_argument("--listen-port",  type=int,   default=None,        help="UDP port to receive vid2spatial OSC (default 9000)")
    parser.add_argument("--target-port",  type=int,   default=None,        help="UDP port to send ADM-OSC (default 9100)")
    parser.add_argument("--target-host",  type=str,   default=None,        help="Host to send ADM-OSC (default 127.0.0.1)")
    parser.add_argument("--config",       type=str,   default="bridge/config.yaml", help="config.yaml path (default bridge/config.yaml)")
    parser.add_argument("--mode",         type=str,   default=None,        choices=["ai", "low_latency"], help="Initial mode (default ai)")
    args = parser.parse_args()

    cfg = load_config(args.config)

    listen_port  = args.listen_port  if args.listen_port  is not None else cfg["listen_port"]
    target_port  = args.target_port  if args.target_port  is not None else cfg["target_port"]
    target_host  = args.target_host  if args.target_host  is not None else cfg["target_host"]
    mode         = args.mode         if args.mode         is not None else cfg["mode"]
    static_map   = {str(k): int(v) for k, v in (cfg.get("object_map") or {}).items()}

    server = BridgeServer(
        listen_port=listen_port,
        target_host=target_host,
        target_port=target_port,
        mode=mode,
        iir_alpha=float(cfg["iir_alpha"]),
        rate_limit_hz=float(cfg["rate_limit_hz"]),
        static_map=static_map,
        max_objects=int(cfg["max_objects"]),
    )

    def _sig(sig, frame):
        print("\n[bridge] Shutting down...")
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT,  _sig)
    signal.signal(signal.SIGTERM, _sig)

    server.start()


if __name__ == "__main__":
    main()
