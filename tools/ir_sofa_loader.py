#!/usr/bin/env python3
"""Load SOFA IR files as float32 numpy arrays."""
import numpy as np
import h5py
from pathlib import Path


def load_sofa_ir(path: str, measurement_idx: int = 0) -> np.ndarray:
    """
    Load IR from SOFA file.
    Returns shape [receiver_count, ir_length] float32 array.
    measurement_idx: which measurement (source position) to load.
    """
    with h5py.File(path, 'r') as f:
        data_ir = f['Data.IR'][measurement_idx]  # shape [receiver, ir_len]
        return data_ir.astype(np.float32)


def sofa_info(path: str) -> dict:
    """Return metadata dict: sr, ir_len, n_measurements, n_receivers."""
    with h5py.File(path, 'r') as f:
        sr = float(f['Data.SamplingRate'][0])
        shape = f['Data.IR'].shape  # [n_meas, n_recv, ir_len]
        return {
            'sample_rate_hz': sr,
            'ir_length_samples': shape[2],
            'measurement_count': shape[0],
            'receiver_count': shape[1],
        }


if __name__ == '__main__':
    import sys
    path = sys.argv[1] if len(sys.argv) > 1 else '/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa'
    info = sofa_info(path)
    print(f"SOFA: {info}")
    ir = load_sofa_ir(path, measurement_idx=0)
    print(f"IR shape: {ir.shape}, dtype: {ir.dtype}, max: {ir.max():.4f}")
