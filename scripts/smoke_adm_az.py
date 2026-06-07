#!/usr/bin/env python3
"""Real-binary smoke for Phase 3.1 ADM-OSC azimuth L/R correctness.

Sends /adm/obj/0/aed over the UDP wire to the live engine at az=+30 (ADM LEFT)
and az=-30 (ADM RIGHT), captures the WAV, and asserts the LEFT-positioned ADM
object lights the LEFT speakers (and RIGHT->RIGHT). Validates the -az decode
through the actual network path (not just in-process like the golden unit test).

Usage: smoke_adm_az.py [--bin PATH] [--layout PATH]
Exit 0 = PASS.
"""
import argparse, os, re, socket, struct, subprocess, sys, time, wave

def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def parse_layout_az(path):
    chans = {}; cur = None
    for line in open(path):
        m = re.search(r"channel:\s*(\d+)", line)
        if m: cur = int(m.group(1)); continue
        m = re.search(r"az_deg:\s*([-\d.]+)", line)
        if m and cur is not None: chans[cur - 1] = float(m.group(1)); cur = None
    return chans

def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        m += struct.pack(">i", a) if t == "i" else struct.pack(">f", a)
    return m

def run_capture(binp, layout, channels, az_deg, wav):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", "48000", "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", "2",
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    for _ in range(3):
        sock.sendto(osc("/adm/obj/0/aed", "fff", az_deg, 0.0, 0.05), ("127.0.0.1", port))
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return (int(xr.group(1)) if xr else None)

def lr_energy(wav, az_map):
    w = wave.open(wav, "rb"); nch, sw = w.getnchannels(), w.getsampwidth()
    raw = w.readframes(w.getnframes()); w.close()
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    e = [0.0] * nch
    for i, v in enumerate(s): e[i % nch] += float(v) * v
    left = sum(en for c, en in enumerate(e) if az_map.get(c, 0.0) < 0)
    right = sum(en for c, en in enumerate(e) if az_map.get(c, 0.0) > 0)
    return left, right

def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()
    az_map = parse_layout_az(a.layout)

    ok = True
    # ADM +30 = LEFT
    xr1 = run_capture(a.bin, a.layout, a.channels, 30.0, "/tmp/adm_left.wav")
    l1, r1 = lr_energy("/tmp/adm_left.wav", az_map)
    print(f"[adm-az] ADM +30 (LEFT):  left={l1:.4g} right={r1:.4g} xruns={xr1}")
    if not (l1 > 4.0 * max(r1, 1e-12)):
        print("FAIL: ADM az=+30 (LEFT) did not favor LEFT speakers (L/R inversion?)"); ok = False

    # ADM -30 = RIGHT
    xr2 = run_capture(a.bin, a.layout, a.channels, -30.0, "/tmp/adm_right.wav")
    l2, r2 = lr_energy("/tmp/adm_right.wav", az_map)
    print(f"[adm-az] ADM -30 (RIGHT): left={l2:.4g} right={r2:.4g} xruns={xr2}")
    if not (r2 > 4.0 * max(l2, 1e-12)):
        print("FAIL: ADM az=-30 (RIGHT) did not favor RIGHT speakers"); ok = False
    if xr1 not in (0, None) or xr2 not in (0, None):
        print(f"FAIL: xruns ({xr1},{xr2})"); ok = False

    if ok:
        print(f"PASS: ADM-OSC azimuth L/R correct through the wire "
              f"(+30->L {l1/max(r1,1e-12):.1f}x, -30->R {r2/max(l2,1e-12):.1f}x)")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
