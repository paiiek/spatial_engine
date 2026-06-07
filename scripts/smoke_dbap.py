#!/usr/bin/env python3
"""End-to-end smoke test for the DBAP algorithm in the real engine binary.

Drives spatial_engine_core (null input/output, WAV capture), selects DBAP for
object 0 via OSC and positions it hard-right, then verifies the captured WAV is
non-silent AND right-side speakers carry more energy than left-side ones, with
zero xruns. Primary purpose: confirm the Phase 1.3 DBAP no-alloc refactor
(dbap_gain_into) still renders correctly end-to-end through the live binary.

NOTE on the /obj/algo wire format: CommandDecoder consumes the first TWO ints of
any ,ii… message as a seq/id header (payload_int_offset=2). So /obj/algo must be
sent as [seq, id, obj, algo] = [0, 0, 0, 2] to actually route object 0 to DBAP
(algo=2). (Sending [obj, algo] silently falls back to VBAP — the latent bug in
smoke_vap/smoke_mdap.) /obj/move is ,ifff (1 leading int, no header eaten).

Usage: smoke_dbap.py [--bin PATH] [--layout PATH] [--port N]
Exit 0 = PASS.
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time, wave

def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def parse_layout_az(path):
    """Return {0-based wav channel: az_deg}. Channel c (1-based) -> wav ch c-1."""
    txt = open(path).read()
    chans = {}
    cur_ch = None
    for line in txt.splitlines():
        m = re.search(r"channel:\s*(\d+)", line)
        if m: cur_ch = int(m.group(1)); continue
        m = re.search(r"az_deg:\s*([-\d.]+)", line)
        if m and cur_ch is not None:
            chans[cur_ch - 1] = float(m.group(1)); cur_ch = None
    return chans

def send_osc(sock, addr, args, ip, port):
    """Minimal OSC encoder (address, then ,types, then args). Supports i,f."""
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00")
    types = ","
    for a in args: types += "i" if isinstance(a, int) else "f"
    msg += pad(types.encode() + b"\x00")
    for a in args:
        msg += struct.pack(">i", a) if isinstance(a, int) else struct.pack(">f", a)
    sock.sendto(msg, (ip, port))

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--seconds", type=int, default=3)
    ap.add_argument("--channels", type=int, default=8)
    ap.add_argument("--wav", default="/tmp/dbap_smoke.wav")
    a = ap.parse_args()
    port = a.port or free_port()
    az = parse_layout_az(a.layout)
    if len(az) != a.channels:
        print(f"WARN: parsed {len(az)} az entries, expected {a.channels}")

    if os.path.exists(a.wav): os.remove(a.wav)
    cmd = [a.bin, "--backend", "null", "--input-backend", "null",
           "--channels", str(a.channels), "--rate", "48000", "--block", "256",
           "--layout", a.layout, "--wav", a.wav, "--seconds", str(a.seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    print("RUN:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)  # startup + OSC bind
    # obj 0 -> DBAP (algo=2) via the 4-int [seq,id,obj,algo] header, then move
    # hard-right (az=+90deg) near a speaker (auto-activates).
    for _ in range(3):
        send_osc(sock, "/obj/algo", [0, 0, 0, 2], "127.0.0.1", port)
        send_osc(sock, "/obj/move", [0, math.pi / 2.0, 0.0, 0.9], "127.0.0.1", port)
        time.sleep(0.25)
    sock.close()

    try:
        out, _ = proc.communicate(timeout=a.seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    print("--- engine log tail ---");  print("\n".join(out.splitlines()[-8:]))
    xruns = None
    m = re.search(r"xruns=(\d+)", out)
    if m: xruns = int(m.group(1))

    if not os.path.exists(a.wav):
        print("FAIL: no WAV produced"); return 1
    w = wave.open(a.wav, "rb")
    nch, sw, fr, nfr = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    print(f"WAV: {nch}ch {sw*8}bit {fr}Hz {nfr}fr")
    if nfr == 0: print("FAIL: empty WAV"); return 1
    fmt = {2: "h", 4: "i"}.get(sw)
    if fmt is None: print(f"FAIL: unsupported sample width {sw}"); return 1
    samples = struct.unpack("<" + fmt * (len(raw)//sw), raw)
    energy = [0.0]*nch
    for i, v in enumerate(samples):
        energy[i % nch] += float(v)*v
    total = sum(energy)
    right = sum(e for c, e in enumerate(energy) if az.get(c, 0.0) > 0)
    left  = sum(e for c, e in enumerate(energy) if az.get(c, 0.0) < 0)
    nonzero = sum(1 for e in energy if e > 0.001 * (total / max(nch, 1) + 1e-9))
    print(f"per-channel energy: {[round(e,1) for e in energy]}")
    print(f"total={total:.1f}  right(az>0)={right:.1f}  left(az<0)={left:.1f}  "
          f"active_ch={nonzero}  xruns={xruns}")

    ok = True
    if total < 1.0:
        print("FAIL: WAV is silent (no DBAP output)"); ok = False
    if right <= left:
        print(f"FAIL: right-positioned object did not favor right speakers "
              f"(right={right:.1f} <= left={left:.1f}) — L/R inversion?"); ok = False
    if nonzero < 3:
        print(f"FAIL: DBAP did not distribute across multiple speakers "
              f"(active_ch={nonzero}) — distance panning not engaged?"); ok = False
    if xruns not in (0, None):
        print(f"FAIL: xruns={xruns} (expected 0)"); ok = False
    if ok:
        print(f"PASS: DBAP renders, distance-distributes across {nonzero} speakers, "
              f"right dominates (right/left = {right/max(left,1e-9):.2f}x), xruns={xruns}")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
