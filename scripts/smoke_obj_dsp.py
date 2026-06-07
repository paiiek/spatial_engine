#!/usr/bin/env python3
"""Real-binary smoke for Phase 4.1 — per-object DSP chain (/obj/dsp routing).

Validates that the *already-wired* /obj/dsp path (decode -> FIFO -> drain ->
obj_cache_ -> PerObjectChain::setParams -> processSample, SpatialEngine.cpp:
1234/1367/1380) actually does what it claims, end to end over the UDP wire, on
the speaker bus (--wav, N channels). The plan called Phase 4.1 "complete the
routing", but the routing already exists; this smoke turns that claim into
evidence and would surface any latent bug (as the binaural EQ work did).

Wire: /obj/dsp ,iiiif <seq> <id> <obj_id> <param 0..7> <value>
  param 0..3 = 4-band EQ gain dB (band centres 100/500/2000/8000 Hz, EQ4Band.h)
  param 7    = source width radians (MDAP spread, VBAP/DBAPRenderer)
  /obj/dsp carries the reliable-delivery seq/id envelope: the decoder strips the
  first two ints (CommandDecoder.cpp:303-314, same convention smoke_dbap uses for
  /obj/algo), so the payload MUST be prefixed with seq,id. Sending a bare
  ,iif obj param value makes the decoder eat obj_id+param as seq/id (both read 0)
  -- which silently routes width to EQ band 0 and looks like "width does nothing".
Each active object emits the engine per-object test tone freq = 110*(1+0.5*i) Hz.

Sub-test A — per-object frequency-selective EQ:
  obj 0 (110 Hz) + obj 7 (495 Hz) front. Cut obj 0 band 0 (@100 Hz) by -18 dB.
  The 110 Hz tone (obj 0, near band-0 centre) must drop hard, while the 495 Hz
  tone (obj 7, a SEPARATE object whose own EQ is untouched) holds essentially
  unchanged -- proving both frequency selectivity AND per-object isolation.

Sub-test B — per-object width (param 7 -> MDAP spread):
  obj 0 (110 Hz) placed OFF-GRID in both az and el (az=20, el=20 deg) on the
  lab_8ch DOME (lower ring el=0 + upper ring el=30 at +-45/+-135). At that
  direction VBAP-3D already spans an elevation triplet, so width=1.2 rad (MDAP,
  spread clamped to 40 deg) recruits MORE speakers (active_ch 3 -> 4) and shifts
  the normalised energy distribution (L1 well above the noise floor).
  NB: el MUST be off the ring -- at el=0 the lab layouts are horizontal-coplanar
  and VBAP-3D triangulation is degenerate, so MDAP only rescales without
  redistributing (this mirrors the golden test_convergence_mdap rig: az/el=20).
  Concentration (max/total) is NOT a good signal: MDAP energy-normalises (Σg²=1)
  so the nominal direction stays dominant even as neighbours are added.
  The smoke proves the /obj/dsp param-7 WIRE delivers width and spreads the
  render end to end; the MDAP math itself is covered by test_convergence_mdap.

Exit 0 = PASS (both sub-tests + xruns==0 in every run).
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
        m += struct.pack(">i", int(a)) if t == "i" else struct.pack(">f", float(a))
    return m


_seq = [1000]
def obj_dsp(sock, port, obj, param, value):
    # /obj/dsp carries the reliable-delivery seq/id envelope: the decoder strips
    # the first two ints (CommandDecoder.cpp:303-314), so the payload must be
    # ,iiiif <seq> <id> <obj> <param> <value>. Sending bare ,iif obj param value
    # makes the decoder eat obj_id+param as seq/id (both read back 0). seq is
    # bumped per send so the 3x robustness loop is not dropped as a duplicate.
    _seq[0] += 1
    sock.sendto(osc("/obj/dsp", "iiiif", _seq[0], 0, obj, param, value), ("127.0.0.1", port))


def goertzel(x, freq):
    w = 2.0 * math.pi * freq / SR
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + coeff * s1 - s2
        s2 = s1; s1 = s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def read_channels(wav, nch_expect):
    w = wave.open(wav, "rb")
    nch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    fmt = {2: "h", 4: "i"}[sw]
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    lo = min(n - 1, int(SR * 0.8))                # steady-state window past xfade
    hi = min(n, lo + SR)
    chans = [list(s[c::nch])[lo:hi] for c in range(nch)]
    return chans


def run(binp, layout, channels, drive, wav):
    """drive(sock, port) sends the OSC for this run. Returns xruns int."""
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    cmd = [binp, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", str(SR), "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", "2",
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    for _ in range(3):                            # repeat: a dropped startup datagram won't desync
        drive(sock, port)
        time.sleep(0.2)
    sock.close()
    try: out, _ = proc.communicate(timeout=12)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xr = re.search(r"xruns=(\d+)", out)
    return int(xr.group(1)) if xr else None


def mono_mix(chans):
    n = min(len(c) for c in chans)
    return [sum(c[i] for c in chans) for i in range(n)]


def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    # lab_8ch is a DOME (lower ring el=0 + upper ring el=30 at +-45/+-135): an
    # off-grid (az,el) source spans an elevation triplet that MDAP can widen --
    # the geometry the golden test_convergence_mdap also relies on.
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()
    F0, F7 = 110.0, 495.0                          # 110*(1+0.5*0), 110*(1+0.5*7)

    # ---- Sub-test A: per-object frequency-selective EQ ----
    def drive_a(eq_on):
        def d(sock, port):
            sock.sendto(osc("/obj/move", "ifff", 0, 0.0, 0.0, 0.3), ("127.0.0.1", port))
            sock.sendto(osc("/obj/move", "ifff", 7, 0.0, 0.0, 0.3), ("127.0.0.1", port))
            if eq_on:                              # obj 0, param 0 (band @100 Hz), -18 dB
                obj_dsp(sock, port, 0, 0, -18.0)
        return d

    xa0 = run(a.bin, a.layout, a.channels, drive_a(False), "/tmp/objdsp_eq_off.wav")
    xa1 = run(a.bin, a.layout, a.channels, drive_a(True),  "/tmp/objdsp_eq_on.wav")
    m0 = mono_mix(read_channels("/tmp/objdsp_eq_off.wav", a.channels))
    m1 = mono_mix(read_channels("/tmp/objdsp_eq_on.wav",  a.channels))
    e110_0, e495_0 = goertzel(m0, F0), goertzel(m0, F7)
    e110_1, e495_1 = goertzel(m1, F0), goertzel(m1, F7)
    r0 = e110_0 / e495_0 if e495_0 else float("inf")
    r1 = e110_1 / e495_1 if e495_1 else float("inf")
    held = e495_1 / e495_0 if e495_0 else 0.0      # obj 7 should be ~unchanged
    print(f"[objdsp-eq] OFF: E110={e110_0:.3e} E495={e495_0:.3e} (110/495={r0:.3f}) xruns={xa0}")
    print(f"[objdsp-eq] CUT: E110={e110_1:.3e} E495={e495_1:.3e} (110/495={r1:.3f}) "
          f"obj7_held={held:.3f} xruns={xa1}")
    eq_pass = (r1 < 0.25 * r0) and (0.7 < held < 1.4) and xa0 == 0 and xa1 == 0

    # ---- Sub-test B: per-object width -> MDAP spread ----
    AZ_OFF, EL_OFF = 20.0 * math.pi / 180.0, 20.0 * math.pi / 180.0  # off-grid az+el (dome triplet)
    def drive_b(width):
        def d(sock, port):
            sock.sendto(osc("/obj/move", "ifff", 0, AZ_OFF, EL_OFF, 0.9), ("127.0.0.1", port))
            if width > 0.0:
                obj_dsp(sock, port, 0, 7, width)
        return d

    xb0 = run(a.bin, a.layout, a.channels, drive_b(0.0), "/tmp/objdsp_w0.wav")
    xb1 = run(a.bin, a.layout, a.channels, drive_b(1.2), "/tmp/objdsp_w1.wav")
    cw0 = [goertzel(c, F0) for c in read_channels("/tmp/objdsp_w0.wav", a.channels)]
    cw1 = [goertzel(c, F0) for c in read_channels("/tmp/objdsp_w1.wav", a.channels)]
    s0, s1 = sum(cw0), sum(cw1)
    act0 = sum(1 for e in cw0 if e > 0.02 * max(cw0)) if s0 else 0
    act1 = sum(1 for e in cw1 if e > 0.02 * max(cw1)) if s1 else 0
    n0 = [e / s0 for e in cw0] if s0 else cw0    # energy-normalised distribution
    n1 = [e / s1 for e in cw1] if s1 else cw1
    l1 = sum(abs(x - y) for x, y in zip(n0, n1)) # distribution shift (0 = no change)
    print(f"[objdsp-width] width=0  : active_ch={act0} "
          f"per_ch={['%.2e' % e for e in cw0]} xruns={xb0}")
    print(f"[objdsp-width] width=1.2: active_ch={act1} "
          f"per_ch={['%.2e' % e for e in cw1]} xruns={xb1}")
    print(f"[objdsp-width] distribution L1 shift={l1:.3f} (0=no effect)")
    # MDAP energy-normalises (Σg²=1) so the nominal direction stays dominant
    # (concentration is NOT a good signal); the real spread signature is more
    # speakers recruited AND the normalised distribution moving past the noise
    # floor. Margins so geometry that cannot show spread FAILS loudly.
    width_pass = (act1 > act0) and (l1 > 0.08) and xb0 == 0 and xb1 == 0

    ok = eq_pass and width_pass
    print(("PASS" if ok else "FAIL") +
          f": /obj/dsp per-obj EQ (110/495 {r0:.2f}->{r1:.2f}, obj7 held {held:.2f}) + "
          f"width (active_ch {act0}->{act1}, L1 {l1:.2f}), xruns=0")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
