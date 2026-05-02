#!/usr/bin/env python3
"""Convert a SimpleFreeFieldHRIR SOFA file to a .speh binary for the C++ HRTF engine.

Binary layout (.speh):
  magic[4]       "SPEH"
  n_positions    uint32  (number of measurement positions)
  ir_length      uint32  (samples per IR)
  n_receivers    uint32  (always 2 for binaural: L, R)
  sample_rate    float32
  reserved       uint32  (= 0)
  positions      float32[n_positions][3]  (az_deg, el_deg, dist_m)
  ir_data        float32[n_positions][n_receivers][ir_length]

Usage:
  python3 tools/sofa_to_bin.py kemar.sofa kemar.speh
"""

import struct
import sys
import numpy as np

try:
    import h5py
except ImportError:
    sys.exit("h5py required: pip install h5py")


def convert(sofa_path: str, out_path: str) -> None:
    with h5py.File(sofa_path, "r") as f:
        sr = float(np.asarray(f["Data.SamplingRate"]).flat[0])
        ir = np.asarray(f["Data.IR"], dtype=np.float32)     # (M, 2, N)
        pos = np.asarray(f["SourcePosition"], dtype=np.float32)  # (M, 3)

    n_pos, n_recv, ir_len = ir.shape
    assert pos.shape == (n_pos, 3), "SourcePosition shape mismatch"

    print(f"  SR={sr}  positions={n_pos}  receivers={n_recv}  ir_len={ir_len}")
    print(f"  Output size ~ {(24 + n_pos*12 + n_pos*n_recv*ir_len*4) / 1e6:.1f} MB")

    header = struct.pack("<4sIIIfI",
                         b"SPEH", n_pos, ir_len, n_recv, sr, 0)
    with open(out_path, "wb") as fout:
        fout.write(header)
        fout.write(pos.tobytes())          # (M, 3) float32
        fout.write(ir.tobytes())           # (M, 2, N) float32 row-major


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(f"Usage: {sys.argv[0]} <input.sofa> <output.speh>")
    convert(sys.argv[1], sys.argv[2])
    print(f"Written: {sys.argv[2]}")
