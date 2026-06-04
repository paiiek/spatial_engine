#!/usr/bin/env python3
"""Real-binary smoke for ⑥e-3b — early predelay + absorption EQ.

Isolates the NEW predelay via TAIL PERSISTENCE at a deactivation edge.

The early reflections (upper ring, azimuth-dependent) now pass through a per-
object predelay (~20 ms @ default) + absorption EQ BEFORE the image rings; the
direct/dry sound (lower ring) does not. So when the object is DEACTIVATED:
  - the direct sound (and its small VBAP spill) stop within ~one block;
  - the early reflections keep arriving for predelay (~20 ms) + the image ring
    drain (<= kErRingLen=512 = 10.7 ms) AFTER the cut.
The late FDN also rings on, but it fans a MONO send onto fixed symmetric corners,
so it carries ~no left/right ASYMMETRY. Upper-ring L/R asymmetry is therefore an
early-only signature, and its persistence past the direct cut measures the
predelay: a tail > the ring-drain ceiling (~11 ms) can only come from predelay.

PASS: upper-asymmetry tail outlasts the direct cut by >= 15 ms (predelay
engaged), early reflections track azimuth before the cut (⑥d regression), xruns==0.
"""
import argparse, os, re, socket, struct, subprocess, time, wave

SR = 48000


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


def capture(bin_path, layout, channels, seconds, wav, az, off_at):
    """Activate object 0, deactivate it at `off_at` s, return (channels, xruns, off_at)."""
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [bin_path, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", str(SR), "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", str(seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.5)
    t0 = time.time()
    send_osc(sock, "/reverb/select", "s", ["room"], "127.0.0.1", port)
    send_osc(sock, "/obj/move", "ifff", [0, az, -0.349, 2.0], "127.0.0.1", port)  # lower ring
    send_osc(sock, "/obj/dsp", "iiiif", [0, 0, 0, 6, 0.9], "127.0.0.1", port)     # reverb send
    # Hold to steady state, then deactivate object 0 ([seq,id,obj,active]).
    time.sleep(max(0.05, off_at - (time.time() - t0)))
    send_osc(sock, "/obj/active", "iiii", [0, 0, 0, 0], "127.0.0.1", port)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))
    if not os.path.exists(wav): return None, xruns
    w = wave.open(wav, "rb"); nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    flat = struct.unpack("<" + {2: "h", 4: "i"}[sw] * (len(raw) // sw), raw)
    chans = [flat[c::nch] for c in range(nch)]
    return chans, xruns


def hop_env(chans, idxs, hop):
    n = len(chans[0]); out = []
    for s in range(0, n - hop, hop):
        e = 0.0
        for c in idxs:
            seg = chans[c][s:s + hop]
            e += sum(float(v) * v for v in seg)
        out.append(e)
    return out


def fall_time(env, ref_lo, ref_hi, thresh_frac):
    """Last hop in [ref_lo,ref_hi) where env is above thresh_frac*peak, then the
    first hop after it where env stays below — i.e. the cut/drain time."""
    seg = env[ref_lo:ref_hi]
    if not seg: return -1
    peak = max(seg)
    th = peak * thresh_frac
    last_above = -1
    for k in range(ref_lo, ref_hi):
        if env[k] > th:
            last_above = k
    return last_above


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--off-at", type=float, default=0.8)
    a = ap.parse_args()

    n = write_dome("/tmp/dome_predelay.yaml", 8)
    chans, xr = capture(a.bin, "/tmp/dome_predelay.yaml", n, a.seconds, "/tmp/predelay.wav",
                        az=-1.0, off_at=a.off_at)
    if chans is None: print("SMOKE FAIL: no WAV"); return 1

    HOP = 32  # 0.67 ms resolution
    lower = hop_env(chans, range(0, 8), HOP)
    upL   = hop_env(chans, (9, 10, 11), HOP)
    upR   = hop_env(chans, (13, 14, 15), HOP)
    asym  = [abs(l - r) for l, r in zip(upL, upR)]
    nH = len(lower)

    # Locate the deactivation edge robustly: the largest drop between the mean of
    # a few hops BEFORE vs AFTER k (windowed, so block granularity / VBAP spill
    # splitting the cut across hops can't shift the edge by a single hop).
    win_lo = int(0.45 * SR / HOP)
    W = 4
    def drop(k):
        pre = sum(lower[k - W:k]) / W
        post = sum(lower[k + 1:k + 1 + W]) / W
        return pre - post
    edge = max(range(win_lo + W, nH - W - 1), key=drop)

    # Direct cut: last hop the lower ring is loud (>=10% of its pre-edge peak).
    search_hi = min(nH, edge + int(0.10 * SR / HOP))
    t_dir = fall_time(lower, win_lo, edge + 2, 0.10)
    # Early tail: last hop upper-asym is loud, into the post-edge window.
    t_early = fall_time(asym, win_lo, search_hi, 0.10)

    tail_ms = (t_early - t_dir) * HOP / SR * 1000.0 if (t_dir >= 0 and t_early >= 0) else -1.0
    ring_ceiling_ms = 512.0 / SR * 1000.0

    # ⑥d regression: before the edge, object-left biases upper-LEFT.
    pre = slice(win_lo, edge)
    eL = sum(upL[pre]); eR = sum(upR[pre])

    print(f"[smoke] edge~{edge*HOP/SR*1000:.0f}ms  direct_cut={t_dir*HOP/SR*1000:.1f}ms  "
          f"early_tail_end={t_early*HOP/SR*1000:.1f}ms  tail={tail_ms:.1f}ms "
          f"(ring ceiling={ring_ceiling_ms:.1f}ms)  xruns={xr}")
    print(f"[smoke] pre-cut upper L={eL:.4g} R={eR:.4g} (obj LEFT -> L/R={eL/max(eR,1e-9):.2f})")

    ok = True
    if t_dir < 0 or t_early < 0:
        print("FAIL: could not locate cut/tail"); ok = False
    elif tail_ms < 15.0:
        print(f"FAIL: early tail {tail_ms:.1f}ms within ring ceiling -> predelay not engaged"); ok = False
    elif tail_ms > 120.0:
        # The asym tail must end well before the FDN T60 (~1.2 s) — proving it is
        # the EARLY drain (predelay + ring), not a symmetric late-FDN artifact.
        print(f"FAIL: tail {tail_ms:.1f}ms implausibly long (FDN contamination?)"); ok = False
    if eL <= eR:
        print("FAIL: object-left did not bias upper-LEFT (⑥d early path broken)"); ok = False
    if xr != 0:
        print(f"FAIL: xruns={xr}"); ok = False

    if ok:
        print(f"SMOKE PASS: early-reflection asymmetry outlasts the direct cut by {tail_ms:.1f}ms "
              f"(> {ring_ceiling_ms:.1f}ms ring ceiling => ~20ms predelay engaged), "
              f"azimuth tracking intact, xruns=0")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
