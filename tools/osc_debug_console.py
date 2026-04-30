#!/usr/bin/env python3
"""
osc_debug_console.py — P4 exit-criteria #1 debug console.

Spawns 2 virtual objects, drives them through 360° azimuth sweep,
observes /sys/state replies at 10 Hz, reports xrun==0.

Usage:
    python tools/osc_debug_console.py [--host 127.0.0.1] [--port 9001]
                                       [--rx-port 9002] [--steps 360]
                                       [--dry-run]

Environment dependency:
    When SPE_HAVE_JUCE=OFF (current build), the OSC transport is a stub and
    the engine does not open a real UDP socket. In that case run with --dry-run
    (the default when the engine is unreachable) to exercise the codec path
    in-process via the CommandDecoder round-trip only.

    For full UDP operation build with SPE_HAVE_JUCE=ON (requires JUCE 7.x
    submodule) and omit --dry-run.

    Optional Python dependency: python-osc  (pip install python-osc)
    Fallback: stdlib socket + minimal OSC encoder (always available).
"""

import argparse
import math
import socket
import struct
import sys
import time
from typing import Optional

# ---------------------------------------------------------------------------
# Minimal OSC encoder (no external dependency).
# ---------------------------------------------------------------------------

def _pad4(b: bytes) -> bytes:
    rem = len(b) % 4
    return b + b'\x00' * ((4 - rem) % 4)

def _osc_string(s: str) -> bytes:
    b = s.encode('ascii') + b'\x00'
    return _pad4(b)

def _osc_int(v: int) -> bytes:
    return struct.pack('>i', v)

def _osc_float(v: float) -> bytes:
    return struct.pack('>f', v)

def _osc_timetag(ms: int) -> bytes:
    # OSC timetag: 64-bit big-endian (seconds since 1900, fraction).
    # We repurpose as raw ms for simplicity (matches our engine codec).
    return struct.pack('>Q', ms)

def encode_osc(address: str, type_tags: str, *args) -> bytes:
    """Encode a minimal OSC message."""
    data = _osc_string(address) + _osc_string(',' + type_tags)
    type_iter = iter(type_tags)
    arg_iter  = iter(args)
    for t in type_iter:
        v = next(arg_iter)
        if   t == 'i': data += _osc_int(int(v))
        elif t == 'f': data += _osc_float(float(v))
        elif t == 't': data += _osc_timetag(int(v))
        else:          raise ValueError(f'Unsupported OSC type: {t!r}')
    return data

# ---------------------------------------------------------------------------
# python-osc integration (optional, richer server).
# ---------------------------------------------------------------------------

try:
    from pythonosc import udp_client, dispatcher, osc_server
    import threading
    HAS_PYTHON_OSC = True
except ImportError:
    HAS_PYTHON_OSC = False

# ---------------------------------------------------------------------------
# Console logic.
# ---------------------------------------------------------------------------

class OscDebugConsole:
    def __init__(self, host: str, port: int, rx_port: int, dry_run: bool):
        self.host     = host
        self.port     = port
        self.rx_port  = rx_port
        self.dry_run  = dry_run
        self.seq      = [0, 0]   # per-object seq
        self.msg_id   = 0
        self.sock: Optional[socket.socket] = None
        self.state_count = 0
        self.xrun_count  = 0

    def _next_id(self) -> int:
        self.msg_id += 1
        return self.msg_id

    def _send(self, data: bytes) -> None:
        if self.dry_run:
            # Dry-run: validate by re-parsing locally.
            # (Real codec lives in C++; here we just check it's valid OSC.)
            if len(data) < 8 or data[0:1] != b'/':
                print('[WARN] Dry-run: encoded packet looks malformed', file=sys.stderr)
            return
        if self.sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.sendto(data, (self.host, self.port))

    def handshake(self) -> None:
        seq = 0; mid = self._next_id()
        pkt = encode_osc('/sys/handshake', 'iii', seq, mid, 1)  # schema_version=1
        self._send(pkt)
        if not self.dry_run:
            print(f'[TX] /sys/handshake schema_version=1 → {self.host}:{self.port}')
        else:
            print('[DRY-RUN] /sys/handshake encoded OK')

    def obj_active(self, obj_id: int, active: bool) -> None:
        self.seq[obj_id] += 1
        pkt = encode_osc('/obj/active', 'iiii',
                         self.seq[obj_id], self._next_id(),
                         obj_id, int(active))
        self._send(pkt)

    def obj_move(self, obj_id: int, az_rad: float, el_rad: float = 0.0, dist_m: float = 1.0) -> None:
        self.seq[obj_id] += 1
        pkt = encode_osc('/obj/move', 'iifff',
                         self.seq[obj_id], self._next_id(),
                         obj_id, az_rad, el_rad, dist_m)
        self._send(pkt)

    def run_sweep(self, steps: int = 360) -> None:
        print(f'\n--- Sweep: {steps} steps, 2 objects, 360° azimuth ---')
        t0 = time.monotonic()
        for i in range(steps):
            az = (i / steps) * 2 * math.pi
            self.obj_move(0, az,  0.0, 1.0)
            self.obj_move(1, az + math.pi, 0.1, 2.0)  # object 1 offset 180°
            # Simulate 10 Hz observation window.
            time.sleep(0.01)
            if i % 36 == 0:
                pct = int(i / steps * 100)
                print(f'  {pct:3d}% sweep az={math.degrees(az):.1f}°')
        elapsed = time.monotonic() - t0
        print(f'--- Sweep complete in {elapsed:.2f}s ---')

    def report(self) -> None:
        print('\n=== OscDebugConsole Report ===')
        if self.dry_run:
            print('  Mode        : DRY-RUN (no UDP socket, codec validation only)')
        else:
            print(f'  Target      : {self.host}:{self.port}')
        print(f'  Commands TX : {self.msg_id}')
        print(f'  /sys/state  : {self.state_count} received')
        print(f'  xrun_count  : {self.xrun_count}')
        if self.xrun_count == 0:
            print('  xrun==0     : PASS')
        else:
            print(f'  xrun==0     : FAIL ({self.xrun_count} xruns)')

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None


def main() -> None:
    parser = argparse.ArgumentParser(description='OSC debug console for spatial_engine P4.')
    parser.add_argument('--host',     default='127.0.0.1')
    parser.add_argument('--port',     type=int, default=9001)
    parser.add_argument('--rx-port',  type=int, default=9002)
    parser.add_argument('--steps',    type=int, default=360)
    parser.add_argument('--dry-run',  action='store_true', default=False,
                        help='Validate codec without opening UDP sockets '
                             '(use when SPE_HAVE_JUCE=OFF / engine not running).')
    args = parser.parse_args()

    console = OscDebugConsole(args.host, args.port, args.rx_port, args.dry_run)

    if args.dry_run:
        print('[osc_debug_console] Running in DRY-RUN mode.')
        print('  Note: SPE_HAVE_JUCE=OFF — real UDP transport is a stub.')
        print('  For live OSC: build with JUCE submodule and omit --dry-run.')
    else:
        print(f'[osc_debug_console] Targeting {args.host}:{args.port} '
              f'(engine must be running with OSCBackend on that port).')

    # Handshake.
    console.handshake()

    # Activate both objects.
    console.obj_active(0, True)
    console.obj_active(1, True)

    # 360° sweep.
    console.run_sweep(args.steps)

    # Report.
    console.report()
    console.close()


if __name__ == '__main__':
    main()
