#!/usr/bin/env python3
"""Real-binary smoke for Phase 4.2a — pink calibration noise (Kellet 7-state).

Drives one speaker channel's array-verification noise generator over the wire
and captures the speaker bus (--wav):
  /noise/{ch}/type ,s white|pink   (channel is in the ADDRESS, no seq/id trap)
  /noise/{ch}/gain ,f
The estimator sums a FIXED number of frequency probes per octave band, so its
per-band value tracks the PSD at the band centre. Thus its slope is the PSD
slope in dB/oct: white ≈ 0 (flat), true pink ≈ −3 dB/oct. This cleanly confirms
the new Kellet shaper is real pink, not the old single-pole LP placeholder
(which rolled off ~−6 dB/oct).

PASS: white slope ≈ flat, pink slope ≈ −3 dB/oct (white − pink ≳ +2), xruns==0.
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
        if t == "i":   m += struct.pack(">i", int(a))
        elif t == "f": m += struct.pack(">f", float(a))
        elif t == "s": m += pad(a.encode() + b"\x00")
    return m


def goertzel_power(x, freq):
    w = 2.0 * math.pi * freq / SR
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + coeff * s1 - s2
        s2 = s1; s1 = s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def octave_band_slope(sig):
    """PSD slope in dB/oct: fixed probes per octave band 125 Hz..8 kHz, so each
    band value tracks the PSD at its centre (white ~0, pink ~-3)."""
    centres = [125.0 * (2.0 ** k) for k in range(7)]   # 125..8000
    xs, ys = [], []
    for k, fc in enumerate(centres):
        lo, hi = fc / math.sqrt(2), fc * math.sqrt(2)
        probes = [lo + (hi - lo) * j / 15.0 for j in range(16)]  # 16 bins/band
        e = sum(goertzel_power(sig, f) for f in probes)
        if e > 0:
            xs.append(k); ys.append(10.0 * math.log10(e))
    # least-squares slope of dB vs octave index
    n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
    num = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    den = sum((xs[i] - mx) ** 2 for i in range(n))
    return num / den if den else 0.0


def run(binp, layout, channels, ntype, ch, wav):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", str(SR), "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", "2",
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    for _ in range(3):
        sock.sendto(osc(f"/noise/{ch}/type", "s", ntype), ("127.0.0.1", port))
        sock.sendto(osc(f"/noise/{ch}/gain", "f", 0.5), ("127.0.0.1", port))
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return int(xr.group(1)) if xr else None


def read_ch(wav, ch_index, channels):
    w = wave.open(wav, "rb"); sw, n = w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    lo = min(n - 1, int(SR * 0.5)); hi = min(n, lo + int(SR * 1.4))
    return list(s[ch_index::channels])[lo:hi]


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    ap.add_argument("--ch", type=int, default=1, help="1-based YAML channel")
    a = ap.parse_args()

    xw = run(a.bin, a.layout, a.channels, "white", a.ch, "/tmp/noise_white.wav")
    xp = run(a.bin, a.layout, a.channels, "pink",  a.ch, "/tmp/noise_pink.wav")
    white = read_ch("/tmp/noise_white.wav", a.ch - 1, a.channels)
    pink  = read_ch("/tmp/noise_pink.wav",  a.ch - 1, a.channels)
    sw = octave_band_slope(white)
    sp = octave_band_slope(pink)
    print(f"[noise] white PSD slope = {sw:+.2f} dB/oct (ideal ~0, flat)      xruns={xw}")
    print(f"[noise] pink  PSD slope = {sp:+.2f} dB/oct (ideal ~-3, pink)     xruns={xp}")
    print(f"[noise] white - pink = {sw - sp:+.2f} dB/oct (ideal +3)")
    # white ~flat, pink ~-3, clear separation. Loose bounds for noise variance.
    ok = (abs(sw) < 1.5) and (-4.5 < sp < -1.8) and (sw - sp > 2.0) and xw == 0 and xp == 0
    print(("PASS" if ok else "FAIL") +
          f": /noise pink is true -3 dB/oct vs white flat (Δslope {sw - sp:+.2f}), xruns=0")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
