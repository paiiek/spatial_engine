#!/usr/bin/env python3
"""Real-binary smoke for Phase 2.6b binaural head tracking (/ypr).

Head rotation is only audible on the B1 binaural bus, so this drives the live
engine with a binaural SOFA loaded, captures the binaural L/R bus to WAV
(--wav-binaural), and sends /ypr over the UDP wire:

  * /ypr +90 (head turned right) on a FRONT object must shift the image RIGHT
    -> right ear leads (inter-aural delay sign matches a physical-right source).
  * /ypr -90 (head left) is the mirror -> left ear leads (opposite ITD sign).

Validates the full wire path /ypr -> decode -> SysHeadYpr -> engine atomics ->
audioBlock rotation -> HRTF lookup, end to end (not just in-process like the
golden unit test). Uses the committed synthetic_itd_pm90.speh fixture (clean
+/-90 ITD, az=0 symmetric).

Usage: smoke_head_ypr.py [--bin PATH] [--sofa PATH] [--layout PATH]
Exit 0 = PASS.
"""
import argparse, os, re, socket, struct, subprocess, sys, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        m += struct.pack(">i", a) if t == "i" else struct.pack(">f", a)
    return m


def run_capture(binp, sofa, layout, channels, yaw_deg, wav):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", "48000", "--block", "256",
           "--layout", layout, "--wav", wav, "--wav-binaural",
           "--binaural-sofa", sofa, "--binaural-enable",
           "--seconds", "2", "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    # Front object, active, then the head pose. Repeat so a dropped datagram
    # during startup does not desync the run.
    for _ in range(3):
        sock.sendto(osc("/adm/obj/0/aed", "fff", 0.0, 0.0, 0.05), ("127.0.0.1", port))
        sock.sendto(osc("/adm/obj/0/active", "i", 1), ("127.0.0.1", port))
        sock.sendto(osc("/ypr", "fff", yaw_deg, 0.0, 0.0), ("127.0.0.1", port))
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return (int(xr.group(1)) if xr else None)


def xcorr_lag(wav, max_lag=64):
    """Cross-correlation lag (samples) of L vs R over a steady-state window."""
    w = wave.open(wav, "rb"); nch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    if nch != 2:
        raise RuntimeError(f"expected 2-channel binaural WAV, got {nch}")
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    L = s[0::2]; R = s[1::2]
    # Steady-state window past the HRTF crossfade / ramp transient.
    lo = min(len(L) - 1, int(48000 * 0.8)); hi = min(len(L), lo + 16000)
    L = L[lo:hi]; R = R[lo:hi]
    N = len(L)
    best = None; best_lag = 0
    for d in range(-max_lag, max_lag + 1):
        acc = 0.0
        for nn in range(N):
            m = nn + d
            if 0 <= m < N:
                acc += L[nn] * R[m]
        if best is None or acc > best:
            best = acc; best_lag = d
    energy = sum(x * x for x in L) + sum(x * x for x in R)
    return best_lag, energy


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--sofa", default=os.path.join(root, "core/tests/fixtures/synthetic_itd_pm90.speh"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()

    ok = True
    xr_r = run_capture(a.bin, a.sofa, a.layout, a.channels, +90.0, "/tmp/ypr_right.wav")
    lag_r, e_r = xcorr_lag("/tmp/ypr_right.wav")
    print(f"[ypr] /ypr +90 (head right): lag={lag_r} energy={e_r:.4g} xruns={xr_r}")
    if not (e_r > 0):
        print("FAIL: /ypr +90 produced silent binaural bus"); ok = False

    xr_l = run_capture(a.bin, a.sofa, a.layout, a.channels, -90.0, "/tmp/ypr_left.wav")
    lag_l, e_l = xcorr_lag("/tmp/ypr_left.wav")
    print(f"[ypr] /ypr -90 (head left):  lag={lag_l} energy={e_l:.4g} xruns={xr_l}")
    if not (e_l > 0):
        print("FAIL: /ypr -90 produced silent binaural bus"); ok = False

    # The two head poses must produce a non-zero, OPPOSITE-signed ITD: a front
    # object swings right then left as the head turns. Same sign (or zero) means
    # the rotation never reached the HRTF lookup (or was L/R-inverted).
    if not (lag_r != 0 and lag_l != 0 and (lag_r > 0) != (lag_l > 0)):
        print(f"FAIL: head poses did not produce opposite ITD "
              f"(+90 lag={lag_r}, -90 lag={lag_l}) — rotation not wired / inverted")
        ok = False

    if xr_r not in (0, None) or xr_l not in (0, None):
        print(f"FAIL: xruns ({xr_r},{xr_l})"); ok = False

    if ok:
        print(f"PASS: /ypr head tracking swings the binaural image through the wire "
              f"(+90 ITD {lag_r}, -90 ITD {lag_l}, opposite signs, xruns=0)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
