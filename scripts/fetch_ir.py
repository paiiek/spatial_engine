#!/usr/bin/env python3
"""fetch_ir.py — 외부 CC-BY IR 다운로드 시도 → 실패 시 합성 fallback.

출력: assets/ir/sample_ir.wav (mono, 48kHz, float32 WAV)
"""
import os
import struct
import sys
import urllib.error
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUT_DIR = os.path.join(REPO_ROOT, "assets", "ir")
OUT_PATH = os.path.join(OUT_DIR, "sample_ir.wav")

# Public domain / CC-BY small IR candidates (tried in order).
CANDIDATE_URLS = [
    "https://github.com/SoundZen/public-irs/raw/main/small_room_48k_mono.wav",
    "https://www.openair.hosted.york.ac.uk/IRs/innocent-railway-tunnel/Audio/Mono/IR_Innocent_Railway_Tunnel_Mono.wav",
]


def _write_float32_wav(path: str, samples, sr: int) -> None:
    """Write mono float32 WAV (IEEE 754, format tag 3)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    num_samples = len(samples)
    byte_data = struct.pack(f"<{num_samples}f", *samples)
    data_size = len(byte_data)
    fmt_size = 18  # PCM ext header for float
    riff_size = 4 + (8 + fmt_size) + (8 + data_size)

    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", riff_size))
        f.write(b"WAVE")
        # fmt chunk (IEEE float)
        f.write(b"fmt ")
        f.write(struct.pack("<I", fmt_size))
        f.write(struct.pack("<H", 3))      # audio format: IEEE float
        f.write(struct.pack("<H", 1))      # num channels: mono
        f.write(struct.pack("<I", sr))     # sample rate
        f.write(struct.pack("<I", sr * 4)) # byte rate
        f.write(struct.pack("<H", 4))      # block align
        f.write(struct.pack("<H", 32))     # bits per sample
        f.write(struct.pack("<H", 0))      # extra param size
        # data chunk
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        f.write(byte_data)


def _synthesize_ir(sr: int = 48000) -> list:
    """300ms RT60 synthetic reverb IR, 1 sec long."""
    import math
    import random

    N = sr  # 1 sec
    rng = random.Random(42)
    ir = [0.0] * N
    for i in range(N):
        noise = rng.gauss(0.0, 1.0)
        envelope = math.exp(-i / (sr * 0.3))
        ir[i] = noise * envelope
    ir[0] = 1.0  # direct impulse
    peak = max(abs(x) for x in ir)
    if peak > 0.0:
        ir = [x / peak for x in ir]
    return ir


def _try_download(url: str, timeout: int = 5) -> bytes | None:
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "fetch_ir/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
        print(f"  Downloaded {len(data)} bytes from {url}")
        return data
    except Exception as exc:
        print(f"  Skip {url}: {exc}")
        return None


def main() -> int:
    os.makedirs(OUT_DIR, exist_ok=True)

    # 1) Try external download
    for url in CANDIDATE_URLS:
        print(f"Trying {url} ...")
        raw = _try_download(url)
        if raw and raw[:4] == b"RIFF" and len(raw) > 44:
            with open(OUT_PATH, "wb") as f:
                f.write(raw)
            print(f"Saved downloaded IR -> {OUT_PATH}")
            return 0

    # 2) Fallback: synthesize
    print("All downloads failed — synthesising 300ms RT60 IR ...")
    sr = 48000
    ir = _synthesize_ir(sr)
    _write_float32_wav(OUT_PATH, ir, sr)
    print(f"Saved synthetic IR -> {OUT_PATH}  ({len(ir)} samples @ {sr} Hz)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
