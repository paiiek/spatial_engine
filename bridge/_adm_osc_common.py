#!/usr/bin/env python3
"""
_adm_osc_common.py — Shared ADM-OSC v1.0 helpers for spatial_engine bridges.

Provides:
  - Coordinate conversion (az sign flip, distance inversion)
  - IIRSmoother: first-order IIR low-pass per keyed channel
  - RateLimiter: token-bucket per keyed channel at configurable Hz
  - ObjectMapper: vid2spatial tracking-ID -> ADM obj_id (1-based)
  - vendor_quirk stubs: DiGiCo / Avid / Yamaha placeholder transforms

ADM-OSC spec reference: https://immersive-audio-live.github.io/ADM-OSC/
MAX_DIST contract: 20.0 metres (mirrors core/src/ipc/AdmOscConstants.h)

Phase C3 note: Direct console traffic hits core/ on port 9100 directly.
This module is used by bridge/ translators that sit between non-ADM sources
(e.g. vid2spatial) and the core engine.
"""

import time
from typing import Dict, Optional

# ---------------------------------------------------------------------------
# ADM-OSC v1.0 constants — must match core/src/ipc/AdmOscConstants.h
# ---------------------------------------------------------------------------

ADM_OSC_MAX_DIST: float = 20.0  # metres; v0 contract, do not change without ADR


# ---------------------------------------------------------------------------
# Coordinate helpers (vid2spatial_osc_contract.md)
# ---------------------------------------------------------------------------

def az_pipeline_to_adm(az_pipeline: float) -> float:
    """Invert azimuth sign: pipeline RIGHT=+az -> ADM LEFT=+az.
    See: docs/adr/vid2spatial_osc_contract.md:45  az_adm = -az_pipeline
    """
    return -az_pipeline


def dist_v2s_to_adm(dist_v2s: float) -> float:
    """Invert distance: vid2spatial near=1 -> ADM far=1 (normalised [0..1])."""
    return 1.0 - float(dist_v2s)


def adm_dist_to_metres(dist_norm: float) -> float:
    """ADM normalised distance -> metres using v0 MAX_DIST contract."""
    return dist_norm * ADM_OSC_MAX_DIST


def metres_to_adm_dist(dist_m: float) -> float:
    """Metres -> ADM normalised distance [0..1]."""
    return dist_m / ADM_OSC_MAX_DIST if ADM_OSC_MAX_DIST > 0.0 else 0.0


# ---------------------------------------------------------------------------
# IIR smoother
# ---------------------------------------------------------------------------

class IIRSmoother:
    """First-order IIR smoother per keyed channel.

    alpha=1.0 => no smoothing (pass-through)
    alpha=0.1 => heavy smoothing
    """

    def __init__(self, alpha: float = 0.3):
        if not (0.0 < alpha <= 1.0):
            raise ValueError(f"IIRSmoother alpha must be in (0, 1], got {alpha}")
        self.alpha = alpha
        self._state: Dict[str, float] = {}

    def smooth(self, key: str, value: float) -> float:
        if key not in self._state:
            self._state[key] = value
            return value
        self._state[key] = self.alpha * value + (1.0 - self.alpha) * self._state[key]
        return self._state[key]

    def reset(self, key: Optional[str] = None) -> None:
        if key is None:
            self._state.clear()
        else:
            self._state.pop(key, None)


# ---------------------------------------------------------------------------
# Rate limiter
# ---------------------------------------------------------------------------

class RateLimiter:
    """Per-key token-bucket rate limiter at configurable Hz.

    Default 60 Hz matches bridge/spike_vid2spatial_osc.py:81-91 rate cap.
    """

    def __init__(self, hz: float = 60.0):
        if hz <= 0.0:
            raise ValueError(f"RateLimiter hz must be > 0, got {hz}")
        self._interval = 1.0 / hz
        self._last: Dict[str, float] = {}

    def allow(self, key: str) -> bool:
        now = time.monotonic()
        if now - self._last.get(key, 0.0) >= self._interval:
            self._last[key] = now
            return True
        return False

    def reset(self, key: Optional[str] = None) -> None:
        if key is None:
            self._last.clear()
        else:
            self._last.pop(key, None)


# ---------------------------------------------------------------------------
# Object ID mapper
# ---------------------------------------------------------------------------

class ObjectMapper:
    """Maps external tracking IDs to ADM object numbers (1-based, 1..MAX_OBJECTS).

    MAX_OBJECTS = 64 mirrors core/src/core/Constants.h:12.
    """

    MAX_OBJECTS: int = 64

    def __init__(self, default_obj_id: int = 1):
        self._map: Dict[str, int] = {}
        self._next_id: int = 1
        self._default_obj_id = default_obj_id

    def get_or_assign(self, tracking_id: str) -> int:
        """Return existing ADM obj_id for tracking_id, or assign a new one."""
        if tracking_id not in self._map:
            if self._next_id > self.MAX_OBJECTS:
                # Wrap around — oldest mapping is overwritten implicitly
                self._next_id = 1
            self._map[tracking_id] = self._next_id
            self._next_id += 1
        return self._map[tracking_id]

    def default(self) -> int:
        return self._default_obj_id

    def reset(self) -> None:
        self._map.clear()
        self._next_id = 1


# ---------------------------------------------------------------------------
# Vendor quirk stubs
# Korean console compatibility: DiGiCo / Avid / Yamaha
# Phase C3: placeholder transforms. Real deviations go here, NOT in core/.
# ---------------------------------------------------------------------------

def apply_digico_quirk(az_deg: float, el_deg: float, dist_norm: float):
    """DiGiCo SD-series ADM-OSC quirk placeholder.

    Known deviations (Phase C4 follow-up — replace with real capture data):
    - DiGiCo may send az in [0..360] instead of [-180..180].
    - No confirmed deviation from v1.0 spec in public SDK docs.
    """
    # Normalise az to [-180..180] if DiGiCo sends [0..360]
    if az_deg > 180.0:
        az_deg -= 360.0
    return az_deg, el_deg, dist_norm


def apply_avid_quirk(az_deg: float, el_deg: float, dist_norm: float):
    """Avid S6L ADM-OSC quirk placeholder.

    Known deviations (Phase C4 follow-up):
    - Avid may send dist in absolute metres rather than normalised [0..1].
    - Placeholder: if dist > 1.0, assume metres and normalise.
    """
    if dist_norm > 1.0:
        dist_norm = min(dist_norm / ADM_OSC_MAX_DIST, 1.0)
    return az_deg, el_deg, dist_norm


def apply_yamaha_quirk(az_deg: float, el_deg: float, dist_norm: float):
    """Yamaha Rivage PM / QL ADM-OSC quirk placeholder.

    Known deviations (Phase C4 follow-up):
    - No confirmed spec deviation found in public documentation.
    - Stub passes through unchanged.
    """
    return az_deg, el_deg, dist_norm
