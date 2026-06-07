#!/usr/bin/env python3
"""Real-binary smoke for Phase 4.2b — log-sweep calibration signal.

Drives one speaker channel's noise generator in SWEEP mode over the wire and
captures the speaker bus (--wav):
  /noise/{ch}/type ,s sweep    (channel in the ADDRESS, no seq/id trap)
  /noise/{ch}/gain ,f
The engine emits a repeating 20 Hz→20 kHz exponential sine sweep (1 s period).
Two properties distinguish it from white/pink noise:
  1. It is a swept SINE — at any instant a single tone — so its crest factor
     (peak/RMS) is ≈ √2 (~1.4), far below broadband noise (~3-4).
  2. It spans the band: across short windows the local frequency (zero-crossing
     rate) ranges from tens of Hz up to many kHz.

PASS: crest factor ≈ √2, the per-window local frequency spans low→high, xruns==0.
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


def run(binp, layout, channels, ntype, ch, wav):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", str(SR), "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", "3",
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    for _ in range(3):
        sock.sendto(osc(f"/noise/{ch}/type", "s", ntype), ("127.0.0.1", port))
        sock.sendto(osc(f"/noise/{ch}/gain", "f", 0.5), ("127.0.0.1", port))
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=14)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return int(xr.group(1)) if xr else None


def read_ch(wav, ch_index, channels):
    w = wave.open(wav, "rb"); sw, n = w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    lo = min(n - 1, int(SR * 1.0)); hi = min(n, lo + int(SR * 1.5))   # ~1.5 s steady
    norm = 32768.0 if sw == 2 else 2147483648.0
    return [v / norm for v in s[ch_index::channels]][lo:hi]


def crest_factor(x):
    peak = max(abs(v) for v in x)
    rms = math.sqrt(sum(v * v for v in x) / len(x)) or 1e-12
    return peak / rms


def window_freqs(x, win=480):
    """local frequency (Hz) per non-overlapping window, via zero-crossing rate."""
    freqs = []
    for i in range(0, len(x) - win, win):
        z = sum(1 for j in range(i + 1, i + win)
                if (x[j] >= 0) != (x[j - 1] >= 0))
        freqs.append(z * 0.5 * SR / win)   # crossings/2 per window → cycles → Hz
    return freqs


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    ap.add_argument("--ch", type=int, default=1, help="1-based YAML channel")
    a = ap.parse_args()

    xs = run(a.bin, a.layout, a.channels, "sweep", a.ch, "/tmp/noise_sweep.wav")
    sig = read_ch("/tmp/noise_sweep.wav", a.ch - 1, a.channels)
    cf = crest_factor(sig)
    fr = window_freqs(sig)
    fmin, fmax = min(fr), max(fr)
    print(f"[sweep] crest factor = {cf:.2f} (sine ~1.41, noise ~3-4) xruns={xs}")
    print(f"[sweep] local freq span = {fmin:.0f}..{fmax:.0f} Hz over 10 ms windows")
    # tonal (crest near sqrt2), and spans clearly low -> high frequency.
    ok = (cf < 2.0) and (fmin < 600) and (fmax > 5000) and xs == 0
    print(("PASS" if ok else "FAIL") +
          f": /noise sweep is a tonal 20->20k log sweep (crest {cf:.2f}, {fmin:.0f}..{fmax:.0f} Hz), xruns=0")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
