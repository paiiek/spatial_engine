#!/usr/bin/env python3
"""Real-binary smoke for Phase 2.5 — binaural monitor 5-band peak EQ.

Drives the live engine with a binaural SOFA loaded and captures the binaural
L/R bus to WAV (--wav-binaural). Two objects emit the engine's per-object test
tones: object 0 = 110 Hz, object 4 = 330 Hz (freq = 110*(1+0.5*i)). The EQ is
exercised over the UDP wire (/sys/binaural_eq/...):

  * baseline run (EQ off)            -> E110/E330 = r0
  * cut run (-18 dB @ 110 Hz, EQ on) -> E110/E330 = r1

A frequency-selective EQ must drop the 110 Hz tone while leaving 330 Hz (an
octave+ away) essentially intact, so r1 << r0 and E330 is ~unchanged.

Validates the full wire path /sys/binaural_eq -> decode -> FIFO -> applyBinauralEq
-> post-chain EQ on the binaural bus, end to end, xruns==0.

Usage: smoke_binaural_eq.py [--bin PATH] [--sofa PATH] [--layout PATH]
Exit 0 = PASS.
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time, wave

SR = 48000


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        m += struct.pack(">i", a) if t == "i" else struct.pack(">f", a)
    return m


def run_capture(binp, sofa, layout, channels, eq_on, wav):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", str(SR), "--block", "256",
           "--layout", layout, "--wav", wav, "--wav-binaural",
           "--binaural-sofa", sofa, "--binaural-enable",
           "--seconds", "2", "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    # obj0 (110 Hz) + obj4 (330 Hz) both front; optional EQ cut at 110 Hz.
    # Repeat so a dropped startup datagram does not desync the run.
    for _ in range(3):
        sock.sendto(osc("/adm/obj/0/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/0/active", "i", 1), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/4/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/4/active", "i", 1), ("127.0.0.1", port))
        if eq_on:
            sock.sendto(osc("/sys/binaural_eq/enable", "i", 1), ("127.0.0.1", port))
            sock.sendto(osc("/sys/binaural_eq/band", "ifff", 0, 110.0, -18.0, 4.0),
                        ("127.0.0.1", port))
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return (int(xr.group(1)) if xr else None)


def goertzel(x, freq):
    w = 2.0 * math.pi * freq / SR
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + coeff * s1 - s2
        s2 = s1; s1 = s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def read_L(wav):
    w = wave.open(wav, "rb"); nch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    if nch != 2:
        raise RuntimeError(f"expected 2-channel binaural WAV, got {nch}")
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    L = list(s[0::2])
    lo = min(len(L) - 1, int(SR * 0.8))           # steady-state window past xfade
    return L[lo:lo + SR]


def tones(wav):
    L = read_L(wav)
    return goertzel(L, 110.0), goertzel(L, 330.0)


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--sofa", default=os.path.join(root, "core/tests/fixtures/synthetic_itd_pm90.speh"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()

    ok = True
    xr0 = run_capture(a.bin, a.sofa, a.layout, a.channels, False, "/tmp/bineq_off.wav")
    e110_0, e330_0 = tones("/tmp/bineq_off.wav")
    xr1 = run_capture(a.bin, a.sofa, a.layout, a.channels, True, "/tmp/bineq_on.wav")
    e110_1, e330_1 = tones("/tmp/bineq_on.wav")

    r0 = e110_0 / max(e330_0, 1e-30)
    r1 = e110_1 / max(e330_1, 1e-30)
    print(f"[bineq] OFF: E110={e110_0:.4g} E330={e330_0:.4g} (110/330={r0:.3g}) xruns={xr0}")
    print(f"[bineq] ON : E110={e110_1:.4g} E330={e330_1:.4g} (110/330={r1:.3g}) xruns={xr1}")

    if not (e110_0 > 0 and e330_0 > 0):
        print("FAIL: baseline bus is missing a tone"); ok = False
    if not (e110_1 < 0.25 * e110_0):
        print("FAIL: EQ -18dB@110 did not cut the 110 Hz tone (>~6 dB)"); ok = False
    if not (e330_1 > 0.6 * e330_0):
        print("FAIL: EQ cut at 110 disturbed the 330 Hz tone (should be ~untouched)"); ok = False
    if not (r1 < 0.5 * r0):
        print("FAIL: EQ did not shift the 110/330 balance toward 330"); ok = False
    if xr0 not in (0, None) or xr1 not in (0, None):
        print(f"FAIL: xruns ({xr0},{xr1})"); ok = False

    if ok:
        print(f"PASS: /sys/binaural_eq -18dB@110 cut the 110 Hz tone "
              f"(110/330 {r0:.3g} -> {r1:.3g}) while 330 Hz held, xruns=0")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
