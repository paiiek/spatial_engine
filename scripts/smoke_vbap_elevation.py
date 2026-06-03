#!/usr/bin/env python3
"""End-to-end smoke test for VBAP 3D 5-tier elevation layering in the real binary.

Drives spatial_engine_core (null I/O, WAV capture) on a 3D rig (lab_8ch.yaml:
lower ring ch1-4 at el=0, upper ring ch5-8 at el=30), selects VBAP for object 0,
then verifies through the live binary:
  1. an elevated right object (az=+45, el=+30) is non-silent, favors the RIGHT
     speakers (L/R invariant), AND routes meaningful energy to the UPPER ring
     (elevation actually lifts the image — the whole point of this increment);
  2. a flat object (az=+45, el=0) keeps its energy on the LOWER ring (upper ring
     stays comparatively quiet) — the 5-tier mask's horizontal-layer rule.

Companion to smoke_vap.py; same OSC/WAV harness. Exit 0 = PASS.
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def parse_layout(path):
    """Return {0-based wav channel: (az_deg, el_deg)}."""
    txt = open(path).read()
    chans = {}
    cur_ch = cur_az = None
    for line in txt.splitlines():
        m = re.search(r"channel:\s*(\d+)", line)
        if m: cur_ch = int(m.group(1)); cur_az = None; continue
        m = re.search(r"az_deg:\s*([-\d.]+)", line)
        if m and cur_ch is not None: cur_az = float(m.group(1)); continue
        m = re.search(r"el_deg:\s*([-\d.]+)", line)
        if m and cur_ch is not None and cur_az is not None:
            chans[cur_ch - 1] = (cur_az, float(m.group(1))); cur_ch = None
    return chans


def send_osc(sock, addr, args, ip, port):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00")
    types = ","
    for a in args: types += "i" if isinstance(a, int) else "f"
    msg += pad(types.encode() + b"\x00")
    for a in args:
        msg += struct.pack(">i", a) if isinstance(a, int) else struct.pack(">f", a)
    sock.sendto(msg, (ip, port))


def capture(bin_path, layout, channels, seconds, wav, az_rad, el_rad):
    """Run the engine once with object 0 -> VBAP at (az_rad, el_rad). Return per-ch energy."""
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [bin_path, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", "48000", "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", str(seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    print("RUN:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    # obj 0 -> VBAP (algo=0), positioned at (az,el) near wall (auto-activates).
    for _ in range(3):
        send_osc(sock, "/obj/algo", [0, 0], "127.0.0.1", port)
        send_osc(sock, "/obj/move", [0, az_rad, el_rad, 0.9], "127.0.0.1", port)
        time.sleep(0.25)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    tail = out.splitlines()[-8:]
    print("--- engine log tail ---"); print("\n".join(tail))
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))

    if not os.path.exists(wav):
        print("FAIL: no WAV produced"); return None, xruns
    w = wave.open(wav, "rb")
    nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    if nfr == 0: print("FAIL: empty WAV"); return None, xruns
    fmt = {2: "h", 4: "i"}.get(sw)
    if fmt is None: print(f"FAIL: unsupported sample width {sw}"); return None, xruns
    samples = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    energy = [0.0] * nch
    for i, v in enumerate(samples):
        energy[i % nch] += float(v) * v
    return energy, xruns


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--seconds", type=int, default=3)
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()

    geo = parse_layout(a.layout)  # {ch: (az_deg, el_deg)}
    upper = [c for c, (_, el) in geo.items() if el > 1.0]
    lower = [c for c, (_, el) in geo.items() if el <= 1.0]
    right = [c for c, (az, _) in geo.items() if az > 0]
    left = [c for c, (az, _) in geo.items() if az < 0]
    print(f"layout: lower={sorted(lower)} upper={sorted(upper)} "
          f"right={sorted(right)} left={sorted(left)}")
    if not upper or not lower:
        print("FAIL: layout is not 3D (need both lower and upper rings)"); return 1

    ok = True

    # --- Case 1: elevated right object (az=+45, el=+30) ---
    e1, x1 = capture(a.bin, a.layout, a.channels, a.seconds, "/tmp/vbap_elev_up.wav",
                     math.radians(45.0), math.radians(30.0))
    if e1 is None: return 1
    print(f"[elevated] per-ch energy: {[round(v,1) for v in e1]}  xruns={x1}")
    tot1 = sum(e1)
    up1 = sum(e1[c] for c in upper); lo1 = sum(e1[c] for c in lower)
    r1 = sum(e1[c] for c in right); l1 = sum(e1[c] for c in left)
    if tot1 < 1.0:
        print("FAIL: elevated object produced silence"); ok = False
    if r1 <= l1:
        print(f"FAIL: elevated right object did not favor right (r={r1:.1f} <= l={l1:.1f})"); ok = False
    if up1 <= 0.10 * tot1:
        print(f"FAIL: elevation did not lift image to upper ring "
              f"(upper={up1:.1f} <= 10% of total={tot1:.1f})"); ok = False
    if x1 != 0:
        print(f"FAIL: xruns during elevated render ({x1})"); ok = False

    # --- Case 2: flat object (az=+45, el=0) stays on lower ring ---
    e2, x2 = capture(a.bin, a.layout, a.channels, a.seconds, "/tmp/vbap_elev_flat.wav",
                     math.radians(45.0), 0.0)
    if e2 is None: return 1
    print(f"[flat] per-ch energy: {[round(v,1) for v in e2]}  xruns={x2}")
    tot2 = sum(e2)
    up2 = sum(e2[c] for c in upper); lo2 = sum(e2[c] for c in lower)
    if tot2 < 1.0:
        print("FAIL: flat object produced silence"); ok = False
    if lo2 <= up2:
        print(f"FAIL: flat object did not stay on lower ring (lower={lo2:.1f} <= upper={up2:.1f})"); ok = False
    if x2 != 0:
        print(f"FAIL: xruns during flat render ({x2})"); ok = False

    # Elevation must visibly raise the upper/total ratio vs the flat object.
    if ok and (up1 / max(tot1, 1e-9)) <= (up2 / max(tot2, 1e-9)):
        print(f"FAIL: elevated upper-ratio ({up1/max(tot1,1e-9):.2f}) not greater than "
              f"flat upper-ratio ({up2/max(tot2,1e-9):.2f})"); ok = False

    if ok:
        print(f"PASS: VBAP elevation layering works — elevated lifts to upper ring "
              f"(upper {up1/max(tot1,1e-9):.0%} of energy vs flat {up2/max(tot2,1e-9):.0%}), "
              f"right>left, lower ring holds flat object, xruns=0")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
