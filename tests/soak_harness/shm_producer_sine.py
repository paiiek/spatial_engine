#!/usr/bin/env python3
"""ADR 0019 PR6 — synthetic shm-ring PRODUCER driver for the cross-process soak.

Standalone helper that drives `adm_player.ipc_sink.IpcRingSink` (the PR5 Python
producer) with a DETERMINISTIC ramp payload so the soak harness
(`test_phase_c_shm_loopback.py`) can prove drop-free streaming against the real
`spatial_engine_core --input-backend shm:/<name> --backend null` consumer.

The ramp is the assertion-3c PRODUCER-SIDE write-integrity witness: for global
frame index `i`, every channel's sample at that frame is set to `float(i)`. The
harness reconstructs the expected value at any wire slot `s` (a power-of-two ring)
with NO out-of-band state:

    f = (write_idx - 1) - ((write_idx - 1 - s) mod cap)

i.e. the most-recently-written frame index resident at slot `s`. float32 is exact
for integer frame indices up to 2**24 (~349 s @ 48 kHz), well beyond the 60 s run.

NOTE (REV3/REV4 honest scope): this witnesses the PRODUCER's de-interleave /
placement correctness only. It does NOT witness a CONSUMER torn/stale read — the
consumer's reads are never re-published on the wire (`--backend null`). The
memory-ordering proof (ARM64) and a consumer read-integrity exposure path are
PR7, not PR6.

Discovery: the producer package lives in a sibling repo and is NOT installed.
`ADM_PLAYER_ROOT` (default `/home/seung/mmhoa/adm_player/dreamscape`) is prepended
to `sys.path`. If `IpcRingSink` cannot be imported, the script exits
`EXIT_NO_ADM_PLAYER` (3) so the harness can `pytest.skip` (not fail).

Usage:
    PYTHONPATH=<root> python3 shm_producer_sine.py \
        --name spe-pr6-smoke --channels 8 --rate 48000 \
        --block-size 256 --ring-frames 8192 --duration 2 --drain-dwell-s 2.0
"""

from __future__ import annotations

import argparse
import os
import sys
import time

EXIT_OK = 0
EXIT_NO_ADM_PLAYER = 3

DEFAULT_ADM_PLAYER_ROOT = "/home/seung/mmhoa/adm_player/dreamscape"


def _discover_ipc_sink():
    """Prepend ADM_PLAYER_ROOT to sys.path and import IpcRingSink.

    Returns the IpcRingSink class, or None if the producer package is absent.
    """
    root = os.environ.get("ADM_PLAYER_ROOT", DEFAULT_ADM_PLAYER_ROOT)
    if root and root not in sys.path:
        sys.path.insert(0, root)
    try:
        from adm_player.ipc_sink import IpcRingSink  # type: ignore
    except Exception as exc:  # noqa: BLE001 — any import failure → skip signal
        print(f"shm_producer_sine: cannot import adm_player.ipc_sink: {exc}",
              file=sys.stderr)
        return None
    return IpcRingSink


def _build_ramp_block(numpy, start_frame: int, block_size: int, channels: int):
    """(block_size, channels) float32 block where row f == float(start_frame + f).

    Identical across channels so the wire reader can assert at any single slot.
    """
    frames = numpy.arange(start_frame, start_frame + block_size, dtype=numpy.float64)
    block = numpy.empty((block_size, channels), dtype=numpy.float32)
    block[:] = frames.astype(numpy.float32)[:, None]
    return block


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="ADR 0019 PR6 synthetic shm producer")
    ap.add_argument("--name", required=True, help="POSIX shm name (→ /dev/shm/<name>)")
    ap.add_argument("--channels", type=int, default=8)
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--block-size", type=int, default=256)
    ap.add_argument("--ring-frames", type=int, default=8192)
    ap.add_argument("--duration", type=float, default=2.0, help="seconds of streaming")
    ap.add_argument("--drain-dwell-s", type=float, default=2.0,
                    help="DRAINING dwell on close(); >=2.0 in smoke de-flakes assertion 5")
    args = ap.parse_args(argv)

    IpcRingSink = _discover_ipc_sink()
    if IpcRingSink is None:
        return EXIT_NO_ADM_PLAYER

    import numpy as np  # adm_player depends on numpy; safe after discovery

    sink = IpcRingSink(
        args.name,
        sample_rate=args.rate,
        channels=args.channels,
        block_size=args.block_size,
        ring_frames=args.ring_frames,
        drain_dwell_s=args.drain_dwell_s,
    )

    block_seconds = args.block_size / float(args.rate)
    n_blocks = max(1, int(round(args.duration / block_seconds)))
    start = time.monotonic()
    try:
        frame = 0
        for b in range(n_blocks):
            block = _build_ramp_block(np, frame, args.block_size, args.channels)
            sink.write(block)
            frame += args.block_size
            # Pace to wall-clock block cadence (real-time streaming, not a burst).
            target = start + (b + 1) * block_seconds
            slack = target - time.monotonic()
            if slack > 0:
                time.sleep(slack)
    finally:
        sink.close()  # DRAINING(drain_dwell_s) → CLOSED → unlink
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
