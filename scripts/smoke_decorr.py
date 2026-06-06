#!/usr/bin/env python3
"""Real-binary smoke for ⑦ /decorr/* speaker decorrelation.

The spec's purpose (07.디코릴레이션) is to reduce inter-channel correlation (IACC)
for wider envelopment. We drive the live binary with a SINGLE object panned so its
energy lands on two adjacent speakers (highly correlated), then:
  * OFF  — /decorr disabled: the two channels are near-identical (corr ≈ 1).
  * ON   — /decorr enabled (mix 0.9, 6 stages, spread 12 ms): the per-channel
           allpass cascades decorrelate them → |corr| drops markedly.

PASS = enabling /decorr measurably lowers the adjacent-channel correlation, with
no xruns. Single-leading-tag wire (no ,ii header):
  /decorr/enable ,i 1
  /decorr/set    ,fffiii  mix spread ap | enabled stages seed
"""
import argparse, math, os, re, socket, struct, subprocess, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_ring(path, n=4):
    """Single horizontal ring of n speakers."""
    lines = ['version: "1.0"', 'name: "ring"', 'regularity_hint: "REGULAR"', "speakers:"]
    for i in range(n):
        az = -180.0 + 360.0 * i / n
        lines += [f"  - id: {i+1}", f"    channel: {i+1}",
                  f"    az_deg: {az}", "    el_deg: 0.0", "    dist_m: 1.0"]
    open(path, "w").write("\n".join(lines) + "\n")
    return n


def send_osc(sock, addr, types, args, port):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i":   msg += struct.pack(">i", a)
        elif t == "f": msg += struct.pack(">f", a)
        elif t == "s": msg += pad(a.encode() + b"\x00")
    sock.sendto(msg, ("127.0.0.1", port))


def send_osc_mixed(sock, addr, fargs, iargs, port):
    """Send ,fffiii: floats then ints (single-leading-tag, no ,ii header)."""
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    types = "f" * len(fargs) + "i" * len(iargs)
    msg = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for a in fargs: msg += struct.pack(">f", a)
    for a in iargs: msg += struct.pack(">i", a)
    sock.sendto(msg, ("127.0.0.1", port))


def run(bin_path, layout, channels, seconds, wav, decorr_on):
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
        # Pan an object between adjacent speakers → correlated pair on ch0/ch1.
        send_osc(sock, "/obj/move", "ifff", [0, 0.78, 0.0, 2.0], port)
        if decorr_on:
            # /decorr/set ,fffiii: mix=0.9 spread=24 ap=0.7 | enabled=1 stages=6 seed=7
            # Max spread (24 ms) → larger per-channel delay difference → bigger
            # inter-channel phase offset at the tone frequency (see threshold note).
            send_osc_mixed(sock, "/decorr/set", [0.9, 24.0, 0.7], [1, 6, 7], port)
        time.sleep(0.2)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    print("--- engine log tail ---"); print("\n".join(out.splitlines()[-3:]))
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))
    if not os.path.exists(wav):
        print("FAIL: no WAV"); return None, xruns
    w = wave.open(wav, "rb")
    nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    if nfr == 0: print("FAIL: empty WAV"); return None, xruns
    fmt = {2: "h", 4: "i"}.get(sw)
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    chans = [[] for _ in range(nch)]
    for i, v in enumerate(s):
        chans[i % nch].append(float(v))
    return chans, xruns


def pearson(a, b):
    n = min(len(a), len(b))
    if n == 0: return 0.0
    a = a[:n]; b = b[:n]
    ma = sum(a) / n; mb = sum(b) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db = math.sqrt(sum((x - mb) ** 2 for x in b))
    return num / (da * db) if da > 0 and db > 0 else 0.0


def most_correlated_pair(chans):
    """Return (i, j, |corr|) for the adjacent channel pair with highest |corr|."""
    best = (0, 1, 0.0)
    n = len(chans)
    for i in range(n):
        j = (i + 1) % n
        c = abs(pearson(chans[i], chans[j]))
        if c > best[2]:
            best = (i, j, c)
    return best


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()

    layout = "/tmp/ring_decorr.yaml"
    n = write_ring(layout, 4)
    print(f"[smoke] ring speakers={n}")

    cOff, xOff = run(a.bin, layout, n, a.seconds, "/tmp/decorr_off.wav", decorr_on=False)
    cOn,  xOn  = run(a.bin, layout, n, a.seconds, "/tmp/decorr_on.wav",  decorr_on=True)
    if cOff is None or cOn is None:
        print("SMOKE FAIL"); return 1

    i, j, corrOff = most_correlated_pair(cOff)
    corrOn = abs(pearson(cOn[i], cOn[j]))
    print(f"[smoke] most-correlated adjacent pair = ch{i}/ch{j}")
    print(f"[smoke] |corr| OFF={corrOff:.4f}  ON={corrOn:.4f}  reduction={corrOff-corrOn:.4f}")
    print(f"[smoke] xruns: off={xOff} on={xOn}")

    ok = True
    # Threshold note: the engine's per-object source is a PURE TONE (110 Hz sine).
    # Decorrelating two copies of one tone via allpass+delay only induces a phase
    # offset (|corr| = |cos Δφ|), so the achievable |corr| reduction on a tone is
    # inherently modest — the strong broadband proof (impulse, energy-preserving,
    # 68% bus change) is test_p_decorr_ctl / test_convergence_decorrelation. Here
    # we assert the real-binary effect is present and direction-correct: the pair
    # is no longer perfectly correlated and |corr| drops by a clear margin.
    if corrOff < 0.95:
        print(f"FAIL: OFF pair not correlated enough to test ({corrOff:.3f}) — bad pan/layout"); ok = False
    if corrOn >= 0.98 or corrOn >= corrOff - 0.03:
        print(f"FAIL: /decorr did not reduce inter-channel correlation "
              f"(OFF {corrOff:.3f} → ON {corrOn:.3f})"); ok = False
    if max(xOff, xOn) != 0:
        print("FAIL: xruns under /decorr path"); ok = False

    if ok:
        print(f"SMOKE PASS: /decorr/set drops adjacent-channel |corr| "
              f"{corrOff:.3f}→{corrOn:.3f} (Δ={corrOff-corrOn:.3f}) on a pure tone "
              f"(broadband decorr proven in unit tests), xruns=0")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
