#!/usr/bin/env python3
"""e2e test: SOFA IR loader via ir_sofa_loader.py"""
import sys
import os
import pytest

SOFA_PATH = '/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa'

# Skip entire module if h5py is unavailable
try:
    import h5py  # noqa: F401
except ImportError:
    pytest.skip("h5py not available", allow_module_level=True)

# Add tools dir to path so we can import ir_sofa_loader
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../tools'))
from ir_sofa_loader import sofa_info, load_sofa_ir  # noqa: E402


@pytest.fixture(scope='module')
def info():
    if not os.path.exists(SOFA_PATH):
        pytest.skip(f"SOFA file not found: {SOFA_PATH}")
    return sofa_info(SOFA_PATH)


def test_sofa_sample_rate(info):
    assert int(info['sample_rate_hz']) == 48000


def test_sofa_ir_length(info):
    assert info['ir_length_samples'] == 384


def test_sofa_receiver_count(info):
    assert info['receiver_count'] == 2


def test_sofa_measurement_count(info):
    assert info['measurement_count'] == 64800


def test_load_ir_shape():
    if not os.path.exists(SOFA_PATH):
        pytest.skip(f"SOFA file not found: {SOFA_PATH}")
    import numpy as np
    ir = load_sofa_ir(SOFA_PATH, measurement_idx=0)
    assert ir.shape == (2, 384)
    assert ir.dtype == np.float32


def test_load_ir_not_all_zeros():
    if not os.path.exists(SOFA_PATH):
        pytest.skip(f"SOFA file not found: {SOFA_PATH}")
    ir = load_sofa_ir(SOFA_PATH, measurement_idx=0)
    assert ir.max() != 0.0, "IR data appears to be all zeros — invalid SOFA file"
