#!/usr/bin/env python3
"""C3 — Generate a small synthetic SimpleFreeFieldHRIR SOFA fixture.

Produces a checked-in test fixture that ir_sofa_loader.py / sofa_to_bin.py
can round-trip without depending on an external 100 MB+ KEMAR / CIPIC dataset.

Layout (HDF5 / SOFA 1.0 SimpleFreeFieldHRIR conventions):
  - Data.IR              float32  shape (M, R, N) = (2, 2, 64)
  - Data.SamplingRate    float64  shape (1,)       value 48000
  - Data.Delay           float64  shape (1, R)
  - SourcePosition       float64  shape (M, 3)     (az, el, dist) deg/m
  - ReceiverPosition     float64  shape (R, 3, 1)
  - ListenerPosition     float64  shape (1, 3)
  - ListenerView         float64  shape (1, 3)
  - ListenerUp           float64  shape (1, 3)
  - EmitterPosition      float64  shape (1, 3, 1)

IR content per measurement is a Dirac at index 0 followed by an exponential
decay tail with a per-receiver gain offset, so the loader can verify both
shape and value (max != 0, channel asymmetry survives the round-trip).

Total file size after HDF5 framing: ~12 KB. Plan target: ~5 KB synthetic
fixture; HDF5 metadata overhead pushes us slightly above, but it's still
two orders of magnitude smaller than a real SOFA HRIR set.

Usage:
  python3 tools/gen_synthetic_sofa.py tests/fixtures/synthetic_min.sofa
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import h5py
import numpy as np


def write_synthetic_sofa(out_path: str | Path,
                         n_measurements: int = 2,
                         n_receivers: int = 2,
                         ir_length: int = 64,
                         sample_rate: float = 48000.0,
                         itd_samples: int = 0) -> None:
    """Write a synthetic SimpleFreeFieldHRIR SOFA fixture.

    itd_samples > 0 embeds a per-receiver onset delay so the L/R IRs differ
    in their leading-edge index (a detectable inter-aural time difference).
    For an az=+90 source (right-side in pipeline convention) the right ear
    (receiver index 1) leads — its onset stays at 0 while the contralateral
    (left, receiver index 0) onset is shifted by itd_samples. This is used by
    B-M2's swap-detection fixture, where fixtureB must differ from
    synthetic_min in BOTH ITD and ir_length so an "active table changed"
    assertion is unambiguous.
    """
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)

    # Synthetic IR: dirac at the per-receiver onset + exponential decay tail;
    # per-receiver gain ramps so the two channels are distinguishable
    # post-round-trip.
    n = np.arange(ir_length, dtype=np.float32)
    decay = np.exp(-n / 8.0).astype(np.float32)
    decay[0] = 1.0  # dirac dominant
    ir = np.zeros((n_measurements, n_receivers, ir_length), dtype=np.float32)
    for m in range(n_measurements):
        for r in range(n_receivers):
            base = decay * (0.5 + 0.25 * r) * (1.0 - 0.1 * m)
            # Per-receiver onset shift: contralateral (far) ear is delayed.
            # For a source at az=+90 (m==1, right side), the LEFT ear (r==0)
            # is contralateral and gets the delay; the RIGHT ear (r==1) leads.
            shift = 0
            if itd_samples > 0 and m == 1 and r == 0:
                shift = itd_samples
            if shift > 0:
                shifted = np.zeros(ir_length, dtype=np.float32)
                shifted[shift:] = base[:ir_length - shift]
                ir[m, r, :] = shifted
            else:
                ir[m, r, :] = base

    # Source positions: two measurements at (az=0, el=0) and (az=90, el=0), 1m.
    src_pos = np.zeros((n_measurements, 3), dtype=np.float64)
    for m in range(n_measurements):
        src_pos[m] = [m * 90.0, 0.0, 1.0]

    # Receivers offset ±0.09 m along the X axis (head radius).
    recv = np.zeros((n_receivers, 3, 1), dtype=np.float64)
    for r in range(n_receivers):
        recv[r, 0, 0] = -0.09 if r == 0 else 0.09

    # Listener / emitter (single positions per SOFA convention).
    listener_pos  = np.zeros((1, 3), dtype=np.float64)
    listener_view = np.array([[1.0, 0.0, 0.0]], dtype=np.float64)
    listener_up   = np.array([[0.0, 0.0, 1.0]], dtype=np.float64)
    emitter_pos   = np.zeros((1, 3, 1), dtype=np.float64)

    delay = np.zeros((1, n_receivers), dtype=np.float64)

    with h5py.File(out, "w") as f:
        f.create_dataset("Data.IR",           data=ir)
        f.create_dataset("Data.SamplingRate", data=np.array([sample_rate], dtype=np.float64))
        f.create_dataset("Data.Delay",        data=delay)
        f.create_dataset("SourcePosition",    data=src_pos)
        f.create_dataset("ReceiverPosition",  data=recv)
        f.create_dataset("ListenerPosition",  data=listener_pos)
        f.create_dataset("ListenerView",      data=listener_view)
        f.create_dataset("ListenerUp",        data=listener_up)
        f.create_dataset("EmitterPosition",   data=emitter_pos)

        # Minimum SOFA conventions metadata so SOFA-aware tools recognise the file.
        f.attrs["Conventions"]        = "SOFA"
        f.attrs["SOFAConventions"]    = "SimpleFreeFieldHRIR"
        f.attrs["SOFAConventionsVersion"] = "1.0"
        f.attrs["Version"]            = "1.0"
        f.attrs["DataType"]           = "FIR"
        f.attrs["RoomType"]           = "free field"
        f.attrs["Title"]              = "spatial_engine synthetic minimal HRIR fixture (C3)"
        f.attrs["DateCreated"]        = "2026-05-04"

    size_bytes = os.path.getsize(out)
    print(f"Wrote {out} (M={n_measurements}, R={n_receivers}, N={ir_length}, "
          f"SR={sample_rate}, size={size_bytes} bytes)")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/synthetic_min.sofa"
    # Optional positional overrides for the B-M2 swap fixture:
    #   gen_synthetic_sofa.py <out> <ir_length> <itd_samples>
    ir_length   = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    itd_samples = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    write_synthetic_sofa(out, ir_length=ir_length, itd_samples=itd_samples)
