#!/usr/bin/env python3
"""End-to-end smoke test for the WFS full-mode renderer in the real engine binary.

Drives spatial_engine_core (null I/O, WAV capture) on a dense horizontal ring,
routes object 0 to WFS (/obj/algo ,ii = [0, 1]), and renders a right-of-centre
source. Verifies through the live binary that the ported reference WFS kernel
(render/ported/Wfs.cpp) produces, end-to-end:
  - a NON-silent, distributed wavefront (WFS drives MANY speakers, not a single
    VBAP triplet), AND
  - correct lateralisation (right source => right energy > left), AND
  - xruns=0 beyond the cold-start baseline.

NOTE on scope: the full-mode parameters (plane wave / wavefront curvature /
obliquity / gain+delay shaping / VBAP blend) are exercised in the unit test
(test_convergence_wfs) at the kernel level. Their OSC/session plumbing is a
later increment, so this real-binary smoke runs the renderer at default
(spherical) parameters — it proves the kernel is wired into the audio path and
renders a coherent wavefield. Companion to smoke_vap.py / smoke_vbap_elevation.py
/ smoke_mdap.py. Exit 0 = PASS.
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_dense_ring(path, n=24):
    """Dense horizontal ring (n speakers, 360/n° spacing) at el=0.

    A fine ring gives the WFS wavefront many secondary sources to drive, so the
    'distributed vs single-triplet' contrast against VBAP is unambiguous.
    """
    lines = ['version: "1.0"', 'name: "wfs_dense_ring24"', 'regularity_hint: "CIRCULAR"',
             "speakers:"]
    for i in range(n):
        az = -180.0 + 360.0 * i / n   # spans [-180, 180) — i=0 lands exactly on -180
        lines += [f"  - id: {i+1}", f"    channel: {i+1}",
                  f"    az_deg: {az}", "    el_deg: 0.0", "    dist_m: 1.0"]
    open(path, "w").write("\n".join(lines) + "\n")
    return path


def parse_layout(path):
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


def capture(bin_path, layout, channels, seconds, wav, az_rad, el_rad, dist_m=4.0, algo=1):
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
    for _ in range(3):
        # /obj/algo wire format is [seq, id, obj_id, algo]: CommandDecoder treats the
        # FIRST TWO ints of any ",ii…" message as a seq/id transaction header (see
        # CommandDecoder.cpp payload_int_offset). Sending only [obj_id, algo] makes
        # those two get eaten as seq/id, so the algo silently defaults to VBAP — the
        # object would never reach the WFS renderer. algo 1 = WFS.
        send_osc(sock, "/obj/algo", [0, 0, 0, algo], "127.0.0.1", port)
        send_osc(sock, "/obj/move", [0, az_rad, el_rad, dist_m], "127.0.0.1", port)
        time.sleep(0.25)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    print("--- engine log tail ---"); print("\n".join(out.splitlines()[-6:]))
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


def active_count(energy, frac=0.01):
    tot = sum(energy)
    if tot <= 0: return 0
    return sum(1 for e in energy if e > frac * tot)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default="")   # default: generated dense 24ch ring
    ap.add_argument("--seconds", type=int, default=3)
    ap.add_argument("--channels", type=int, default=0)
    a = ap.parse_args()

    if not a.layout:
        a.layout = write_dense_ring("/tmp/wfs_dense_ring.yaml", 24)
    geo = parse_layout(a.layout)
    if a.channels == 0:
        a.channels = len(geo)
    right = [c for c, (az, _) in geo.items() if az > 0]
    left = [c for c, (az, _) in geo.items() if az < 0]

    ok = True
    # Right-of-centre source (az=+40°) placed OUTSIDE the unit ring (dist=4 m, the
    # capture() default) so the secondary-source distances are comparable across
    # the source-facing arc — the WFS wavefront then genuinely spreads over many
    # speakers rather than collapsing onto the 1-2 nearest (which is what a near-
    # field source on the ring does).
    AZ, EL = math.radians(40.0), 0.0
    # VBAP baseline (same source, fresh cold start) to isolate the engine's 24ch
    # null-backend cold-start xrun from any WFS-introduced xrun.
    eB, xBase = capture(a.bin, a.layout, a.channels, a.seconds, "/tmp/wfs_base.wav",
                        AZ, EL, algo=0)
    if eB is None: return 1
    eR, xR = capture(a.bin, a.layout, a.channels, a.seconds, "/tmp/wfs_right.wav",
                     AZ, EL, algo=1)
    if eR is None: return 1
    # Mirror source on the left to confirm the lateralisation flips.
    eL, xL = capture(a.bin, a.layout, a.channels, a.seconds, "/tmp/wfs_left.wav",
                     -AZ, EL, algo=1)
    if eL is None: return 1

    nB = active_count(eB)

    nR = active_count(eR)
    rR = sum(eR[c] for c in right); lR = sum(eR[c] for c in left)
    rL = sum(eL[c] for c in right); lL = sum(eL[c] for c in left)
    print(f"[VBAP base] active={nB}  xruns={xBase}")
    print(f"[src R] per-ch energy: {[round(v,1) for v in eR]}  active={nR}  xruns={xR}")
    print(f"        right={rR:.1f} left={lR:.1f}")
    print(f"[src L] right={rL:.1f} left={lL:.1f}  xruns={xL}")

    if sum(eR) < 1.0:
        print("FAIL: WFS render is silent"); ok = False
    # WFS drives a distributed wavefront — strictly more speakers than the VBAP
    # baseline (VBAP collapses to a 2-3 speaker triplet; WFS spreads the arc).
    if nR <= 3 or nR <= nB:
        print(f"FAIL: WFS did not spread across the array "
              f"(active={nR}, VBAP baseline={nB})"); ok = False
    if rR <= lR:
        print(f"FAIL: right source not right-biased (right={rR:.1f} <= left={lR:.1f})"); ok = False
    if lL <= rL:
        print(f"FAIL: left source not left-biased (left={lL:.1f} <= right={rL:.1f})"); ok = False
    # WFS must add no xruns beyond the cold-start baseline (the 24ch null backend
    # emits one cold-start xrun identically for every algo, including VBAP).
    if xR > xBase or xL > xBase:
        print(f"FAIL: WFS added xruns over baseline "
              f"(base={xBase}, right={xR}, left={xL})"); ok = False

    if ok:
        print(f"PASS: WFS renders end-to-end — distributed wavefront ({nR} active "
              f"speakers vs VBAP {nB}), lateralisation tracks source side, "
              f"no xruns beyond cold-start baseline ({xBase})")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
