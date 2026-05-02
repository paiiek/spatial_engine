#!/usr/bin/env python3
"""
demo_e2e.py — 엔드투엔드 데모 (시각 레이어).

OSC 9100 → osc_bridge intercept → WebSocket → WebGUI canvas 업데이트

Usage:
    cd /home/seung/mmhoa/spatial_engine
    python3 tools/demo_e2e.py              # 5개 오브젝트 회전 데모
    python3 tools/demo_e2e.py --traj PATH  # vid2spatial traj.json 재생 (스케일 보정)
"""

import argparse
import json
import math
import socket
import struct
import sys
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# Minimal OSC encoder
# ---------------------------------------------------------------------------

def _pad4(b: bytes) -> bytes:
    r = len(b) % 4
    return b + b'\x00' * ((4 - r) % 4)

def encode_osc(addr: str, *floats: float) -> bytes:
    tags = ',' + 'f' * len(floats)
    data = _pad4(addr.encode('ascii') + b'\x00')
    data += _pad4(tags.encode('ascii') + b'\x00')
    for f in floats:
        data += struct.pack('>f', f)
    return data


# ---------------------------------------------------------------------------
# Demo patterns
# ---------------------------------------------------------------------------

def multi_object_demo(sock, host, port, n_objs=5, duration=60.0, fps=30.0):
    """N objects on independent orbit paths."""
    dt = 1.0 / fps
    t = 0.0
    addr = (host, port)

    # Each object: base_az, az_speed (°/s), base_dist, dist_amp, base_el, el_speed
    patterns = [
        (  0.0, 45.0,  0.6, 0.2,  10.0,  20.0),
        ( 72.0, 30.0,  0.4, 0.3,  -5.0,  15.0),
        (144.0, 60.0,  0.7, 0.15,  20.0, -25.0),
        (216.0, 20.0,  0.5, 0.25, -15.0,  30.0),
        (288.0, 50.0,  0.3, 0.35,   0.0,  10.0),
    ]
    patterns = patterns[:n_objs]

    print(f"[demo] {n_objs} objects  fps={fps:.0f}  duration={duration:.0f}s")
    print("[demo] http://147.47.120.128:8000  (Ctrl-C to stop)")

    deadline = time.time() + duration
    while time.time() < deadline:
        for i, (base_az, az_spd, base_dist, dist_amp, base_el, el_spd) in enumerate(patterns):
            az   = (base_az + az_spd * t) % 360 - 180
            dist = base_dist + dist_amp * math.sin(2 * math.pi * t / 4.0 + i)
            dist = max(0.05, min(0.95, dist))
            el   = base_el + 20.0 * math.sin(2 * math.pi * t / 6.0 + i * 0.7)
            el   = max(-80.0, min(80.0, el))
            pkt  = encode_osc(f"/adm/obj/{i+1}/aed", az, el, dist)
            sock.sendto(pkt, addr)
        t += dt
        time.sleep(dt)


def traj_demo(sock, host, port, traj_path: Path, obj_id=1, fps=30.0, loop=False, scale=60.0):
    """Stream traj.json, scaling azimuth for visibility (real data is <1°)."""
    data   = json.loads(traj_path.read_text())
    frames = data.get("frames", [])
    fps    = float(data.get("fps", fps))
    dt     = 1.0 / fps
    addr   = (host, port)
    n      = obj_id

    # Centre + scale azimuth so ±span → ±scale/2 degrees
    azs = [float(f.get("az", 0)) for f in frames]
    az_centre = (max(azs) + min(azs)) / 2
    az_span   = max(1e-3, max(azs) - min(azs))
    az_gain   = scale / az_span

    print(f"[demo] {traj_path.name}  frames={len(frames)}  fps={fps:.0f}  az_gain={az_gain:.0f}x")
    print(f"[demo] http://147.47.120.128:8000  obj {n}  (Ctrl-C to stop)")

    run = True
    while run:
        for i, fr in enumerate(frames):
            raw_az = float(fr.get("az", 0))
            el     = float(fr.get("el", 0))
            dist_m = float(fr.get("dist_m", 5.0))

            az_adm   = -(raw_az - az_centre) * az_gain
            dist_adm = max(0.0, min(1.0, 1.0 - dist_m / 20.0))

            pkt = encode_osc(f"/adm/obj/{n}/aed", az_adm, el, dist_adm)
            sock.sendto(pkt, addr)

            if i % int(fps) == 0:
                print(f"  t={i/fps:.1f}s  az={az_adm:+7.2f}°  el={el:+6.2f}°  dist={dist_adm:.2f}")
            time.sleep(dt)
        run = loop


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--traj", default=None, help="traj.json path (omit for synthetic)")
    ap.add_argument("--obj-id", type=int, default=1)
    ap.add_argument("--fps",   type=float, default=30.0)
    ap.add_argument("--duration", type=float, default=60.0, help="Synthetic demo duration (s)")
    ap.add_argument("--loop",  action="store_true")
    ap.add_argument("--host",  default="127.0.0.1")
    ap.add_argument("--port",  type=int, default=9100)
    ap.add_argument("--n-objs", type=int, default=5, help="Objects for synthetic demo")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    if args.traj:
        traj_path = Path(args.traj)
        if not traj_path.exists():
            print(f"[demo] File not found: {traj_path}")
            sys.exit(1)
        traj_demo(sock, args.host, args.port, traj_path,
                  obj_id=args.obj_id, fps=args.fps, loop=args.loop)
    else:
        multi_object_demo(sock, args.host, args.port,
                          n_objs=args.n_objs, duration=args.duration, fps=args.fps)

if __name__ == "__main__":
    main()
