#!/usr/bin/env python3
"""Generate a tiny synthetic .speh binary for binaural tests.

Layout (matches tools/sofa_to_bin.py):
  magic[4]       "SPEH"
  n_positions    uint32
  ir_length      uint32   (must be in {64,128,256,384,512,1024})
  n_receivers    uint32   (always 2)
  sample_rate    float32
  reserved       uint32
  positions      float32[n_positions][3]   (az_deg, el_deg, dist_m)
  ir_data        float32[n_positions][2][ir_length]

The default fixture writes 4 positions covering the cardinal directions
on the horizontal plane (front, left, back, right) with distinguishable
HRIRs:
  pos 0 az=0   el=0  → IR_L = δ at 0, IR_R = δ at 1
  pos 1 az=90  el=0  → IR_L = δ at 0, IR_R = δ at 2
  pos 2 az=180 el=0  → IR_L = δ at 0, IR_R = δ at 3
  pos 3 az=-90 el=0  → IR_L = δ at 0, IR_R = δ at 4

ir_length = 64 (smallest supported), sample_rate = 48000 Hz.
"""

import argparse
import struct
from pathlib import Path

# Default fixture parameters.
IR_LEN = 64
SR     = 48000.0
N_RECV = 2

POSITIONS = [
    # (az_deg, el_deg, dist_m, L_delay_samples, R_delay_samples)
    (0.0,     0.0, 1.0, 0, 1),
    (90.0,    0.0, 1.0, 0, 2),
    (180.0,   0.0, 1.0, 0, 3),
    (-90.0,   0.0, 1.0, 0, 4),
]


def write_speh(out: Path) -> None:
    n_pos = len(POSITIONS)
    header = struct.pack("<4sIIIfI",
                         b"SPEH", n_pos, IR_LEN, N_RECV, SR, 0)
    with out.open("wb") as f:
        f.write(header)
        # Positions
        for (az, el, dist, _, _) in POSITIONS:
            f.write(struct.pack("<fff", az, el, dist))
        # IR data — float32[n_pos][2][ir_len]
        for (_, _, _, lL, lR) in POSITIONS:
            for delay in (lL, lR):
                ir = [0.0] * IR_LEN
                if 0 <= delay < IR_LEN:
                    ir[delay] = 1.0
                f.write(struct.pack(f"<{IR_LEN}f", *ir))
    print(f"Wrote: {out} ({out.stat().st_size} bytes, n_pos={n_pos}, ir_len={IR_LEN})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output .speh path")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_speh(args.output)
