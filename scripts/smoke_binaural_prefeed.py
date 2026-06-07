#!/usr/bin/env python3
"""Real-binary smoke for Phase 2.1 — binaural HRTF prefeed low-pass.

The engine filters each active object's dry signal through a one-pole LP (default
4200 Hz) before the HRTF (BinauralMonitorChain.cpp:106-125); the corner is
tunable via /sys/binaural_prefeed. Two object tones are driven: object 0 =
110 Hz, object 63 = 3575 Hz (freq = 110*(1+0.5*i)). Comparing the default
4200 Hz corner against a bypass corner (both runs share the SAME HRTF, so its
colouration cancels), the LP must attenuate the 3575 Hz tone while leaving the
110 Hz tone essentially intact.

Validates the full wire path /sys/binaural_prefeed -> decode -> engine atomic ->
audioBlock prefilter -> HRTF input, end to end, xruns==0.

Usage: smoke_binaural_prefeed.py [--bin PATH] [--sofa PATH] [--layout PATH]
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


def run_capture(binp, sofa, layout, channels, cutoff, wav):
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
    for _ in range(3):
        sock.sendto(osc("/adm/obj/0/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/0/active", "i", 1), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/63/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/63/active", "i", 1), ("127.0.0.1", port))
        sock.sendto(osc("/sys/binaural_prefeed", "f", cutoff), ("127.0.0.1", port))
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
    lo = min(len(L) - 1, int(SR * 0.8))
    return L[lo:lo + SR]


def tones(wav):
    L = read_L(wav)
    return goertzel(L, 110.0), goertzel(L, 3575.0)


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--sofa", default=os.path.join(root, "core/tests/fixtures/synthetic_itd_pm90.speh"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()

    ok = True
    xr0 = run_capture(a.bin, a.sofa, a.layout, a.channels, 4200.0, "/tmp/pf_lp.wav")
    e110_lp, e3575_lp = tones("/tmp/pf_lp.wav")
    xr1 = run_capture(a.bin, a.sofa, a.layout, a.channels, 30000.0, "/tmp/pf_by.wav")
    e110_by, e3575_by = tones("/tmp/pf_by.wav")

    r_lp = e3575_lp / max(e110_lp, 1e-30)
    r_by = e3575_by / max(e110_by, 1e-30)
    print(f"[prefeed] LP@4200: E110={e110_lp:.4g} E3575={e3575_lp:.4g} (3575/110={r_lp:.3g}) xruns={xr0}")
    print(f"[prefeed] bypass : E110={e110_by:.4g} E3575={e3575_by:.4g} (3575/110={r_by:.3g}) xruns={xr1}")

    if not (e110_by > 0 and e3575_by > 0):
        print("FAIL: bypass run missing a tone"); ok = False
    if not (e3575_lp < 0.8 * e3575_by):
        print("FAIL: prefeed LP did not attenuate the 3575 Hz tone"); ok = False
    if not (e110_lp > 0.9 * e110_by):
        print("FAIL: prefeed LP disturbed the 110 Hz tone (should be ~untouched)"); ok = False
    if not (r_lp < 0.85 * r_by):
        print("FAIL: prefeed did not bend the HF/LF balance down"); ok = False
    if xr0 not in (0, None) or xr1 not in (0, None):
        print(f"FAIL: xruns ({xr0},{xr1})"); ok = False

    if ok:
        print(f"PASS: /sys/binaural_prefeed LP@4200 rolled off 3575 Hz "
              f"(3575/110 {r_by:.3g} -> {r_lp:.3g}) while 110 Hz held, xruns=0")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
