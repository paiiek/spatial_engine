#!/usr/bin/env python3
"""Real-binary smoke for ⑥d Shoebox early reflections.

Isolates the EARLY reflections from the late FDN by exploiting position
dependence. With a lower-hemisphere object the dry direct sound stays in the
lower ring (≈0 upper). The late FDN fans onto FIXED cube corners from a MONO
send, so its upper-ring contribution is position-INDEPENDENT and left/right
symmetric. Only the early reflections are position-dependent: the +y (ceiling)
image of a lower-LEFT object points up-LEFT, and of a lower-RIGHT object up-RIGHT.

So if the UPPER-ring left/right energy balance FLIPS when the object moves from
left to right, that asymmetry can only come from the early reflections — the
late FDN and the (lower) dry cannot produce it.
"""
import argparse, math, os, re, socket, struct, subprocess, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_dome(path, per_ring=8):
    lines = ['version: "1.0"', 'name: "dome"', 'regularity_hint: "IRREGULAR"', "speakers:"]
    ch = 1
    for ring_el in (-20.0, 30.0):
        for i in range(per_ring):
            az = -180.0 + 360.0 * i / per_ring
            lines += [f"  - id: {ch}", f"    channel: {ch}",
                      f"    az_deg: {az}", f"    el_deg: {ring_el}", "    dist_m: 1.0"]
            ch += 1
    open(path, "w").write("\n".join(lines) + "\n")
    return ch - 1


def send_osc(sock, addr, types, args, ip, port):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i":   msg += struct.pack(">i", a)
        elif t == "f": msg += struct.pack(">f", a)
        elif t == "s": msg += pad(a.encode() + b"\x00")
    sock.sendto(msg, (ip, port))


def capture(bin_path, layout, channels, seconds, wav, az):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [bin_path, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", "48000", "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", str(seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    for _ in range(3):
        send_osc(sock, "/reverb/select", "s", ["room"], "127.0.0.1", port)
        send_osc(sock, "/obj/move", "ifff", [0, az, -0.349, 2.0], "127.0.0.1", port)  # lower ring
        send_osc(sock, "/obj/dsp", "iiiif", [0, 0, 0, 6, 0.9], "127.0.0.1", port)     # reverb send
        time.sleep(0.25)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))
    if not os.path.exists(wav): print("FAIL: no WAV"); return None, xruns
    w = wave.open(wav, "rb"); nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    samples = struct.unpack("<" + {2: "h", 4: "i"}[sw] * (len(raw) // sw), raw)
    energy = [0.0] * nch
    for i, v in enumerate(samples):
        energy[i % nch] += float(v) * v
    return energy, xruns


def upper_lr(energy):
    # Dome: idx 8..15 = upper ring, az = -180 + 45*(idx-8).
    # left  = az in (-180,0) exclusive -> idx 9,10,11 ; right = az>0 -> idx 13,14,15.
    left  = sum(energy[i] for i in (9, 10, 11))
    right = sum(energy[i] for i in (13, 14, 15))
    return left, right


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()

    n = write_dome("/tmp/dome_early.yaml", 8)
    eL, xL = capture(a.bin, "/tmp/dome_early.yaml", n, a.seconds, "/tmp/early_left.wav",  az=-1.0)
    eR, xR = capture(a.bin, "/tmp/dome_early.yaml", n, a.seconds, "/tmp/early_right.wav", az=+1.0)
    if eL is None or eR is None: print("SMOKE FAIL"); return 1

    lL, rL = upper_lr(eL)   # object LEFT
    lR, rR = upper_lr(eR)   # object RIGHT
    print(f"[smoke] object LEFT : upper L={lL:.4g} R={rL:.4g}  (L/R={lL/max(rL,1e-9):.2f})  xruns={xL}")
    print(f"[smoke] object RIGHT: upper L={lR:.4g} R={rR:.4g}  (L/R={lR/max(rR,1e-9):.2f})  xruns={xR}")

    ok = True
    if lL <= 0 or rR <= 0: print("FAIL: upper ring silent (early reflections not firing?)"); ok = False
    # Position-dependent signature: object-left biases upper-LEFT, object-right
    # biases upper-RIGHT. The late FDN (fixed corners, mono send) cannot do this.
    if not (lL > rL): print("FAIL: object-left did not bias upper-LEFT"); ok = False
    if not (rR > lR): print("FAIL: object-right did not bias upper-RIGHT"); ok = False

    if ok:
        print(f"SMOKE PASS: early reflections track object azimuth on the ceiling "
              f"(L-obj L/R={lL/max(rL,1e-9):.2f} > 1 > R-obj L/R={lR/max(rR,1e-9):.2f}), "
              f"xruns={max(xL,xR)}")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
