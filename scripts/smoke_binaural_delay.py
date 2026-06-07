#!/usr/bin/env python3
"""Real-binary smoke for Phase 2.4 — binaural monitor stereo delay ring.

The engine delays the binaural L/R bus through a stereo ring, tap =
binauralDelayMs (BinauralMonitorChain.cpp:132-154), tunable via
/sys/binaural_delay. With a tap engaged, the whole bus output is shifted later
by the tap. This drives object 0 (110 Hz) front and captures the binaural bus
from the start of the WAV; the per-run onset (first envelope hop above 20 % of
that run's peak) lands at ~activation_time + tap. Comparing tap=0 vs tap=200 ms,
the onset must move later by ~200 ms (9600 samples @ 48 kHz).

Per-run onset detection makes the measurement independent of cross-run OSC
timing jitter. Validates /sys/binaural_delay -> engine atomic -> audioBlock
delay ring, end to end, xruns==0.

Usage: smoke_binaural_delay.py [--bin PATH] [--sofa PATH] [--layout PATH]
Exit 0 = PASS.
"""
import argparse, os, re, socket, struct, subprocess, sys, time, wave

SR = 48000
TAP_MS = 200.0


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        m += struct.pack(">i", a) if t == "i" else struct.pack(">f", a)
    return m


def run_capture(binp, sofa, layout, channels, tap_ms, wav):
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
    # Set the tap BEFORE activating so the very first output is already delayed,
    # then activate object 0. (Send once — extra repeats would re-stamp onset.)
    sock.sendto(osc("/sys/binaural_delay", "f", tap_ms), ("127.0.0.1", port))
    sock.sendto(osc("/adm/obj/0/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
    sock.sendto(osc("/adm/obj/0/active", "i", 1), ("127.0.0.1", port))
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return (int(xr.group(1)) if xr else None)


def onset_sample(wav, hop=128):
    w = wave.open(wav, "rb"); nch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    L = s[0::2]
    env = []
    for k in range(0, len(L) - hop, hop):
        e = 0.0
        for v in L[k:k + hop]:
            e += float(v) * v
        env.append(e)
    if not env:
        return -1
    peak = max(env)
    if peak <= 0:
        return -1
    th = 0.2 * peak
    for i, e in enumerate(env):
        if e > th:
            return i * hop
    return -1


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--sofa", default=os.path.join(root, "core/tests/fixtures/synthetic_itd_pm90.speh"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()

    ok = True
    xr0 = run_capture(a.bin, a.sofa, a.layout, a.channels, 0.0, "/tmp/dl_0.wav")
    on0 = onset_sample("/tmp/dl_0.wav")
    xr1 = run_capture(a.bin, a.sofa, a.layout, a.channels, TAP_MS, "/tmp/dl_tap.wav")
    on1 = onset_sample("/tmp/dl_tap.wav")

    shift = on1 - on0 if (on0 >= 0 and on1 >= 0) else -1
    expect = TAP_MS * 0.001 * SR
    print(f"[delay] tap=0 onset={on0} smp | tap={TAP_MS:.0f}ms onset={on1} smp | "
          f"shift={shift} smp (expect ~{expect:.0f}) xruns={xr0},{xr1}")

    if on0 < 0 or on1 < 0:
        print("FAIL: could not locate onset"); ok = False
    elif abs(shift - expect) > 0.001 * 60 * SR:   # 60 ms tolerance (timing jitter)
        print(f"FAIL: onset shift {shift} != ~{expect:.0f} (delay not engaged / wrong tap)"); ok = False
    if xr0 not in (0, None) or xr1 not in (0, None):
        print(f"FAIL: xruns ({xr0},{xr1})"); ok = False

    if ok:
        print(f"PASS: /sys/binaural_delay {TAP_MS:.0f}ms shifted the binaural onset by "
              f"{shift} samples (~{shift/SR*1000:.0f}ms), xruns=0")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
