#!/usr/bin/env python3
"""Real-binary smoke for the ⑥e-4 /room/* OSC control scheme.

Drives the live spatial_engine_core binary on a 3D dome and proves the new
single-leading-tag room control path reaches the DSP:

  * /room/enable ,i 1          engages the spatial room (upper-ring energy
                               appears that a below-horizon dry object cannot
                               produce — same isolation as smoke_room_reverb).
  * /room/t60 ,f <s>           a long T60 accumulates strictly more upper-ring
                               tail energy than a short one (the late FDN loop
                               gain is driven by the OSC value).
  * /room/set ,f x13           the atomic bundle applies without xruns / crash.

Wire format (NO ,ii seq/id header — single leading tag):
  /room/enable ,i 1
  /room/t60    ,f 1.8
  /room/set    ,f x13  t60 sx sy sz earlyW earlyBal clSend clDiff clVol
                       eqHP eqLP hfCorner hfRatio
  /obj/move ,ifff [obj az el dist]      (1 leading int, no header eaten)
  /obj/dsp  ,iiiif [seq id obj param v] (param 6 = reverb_send)
"""
import argparse, os, re, socket, struct, subprocess, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_dome(path, per_ring=8):
    lines = ['version: "1.0"', 'name: "dome"', 'regularity_hint: "IRREGULAR"',
             "speakers:"]
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
    msg = pad(addr.encode() + b"\x00")
    msg += pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i":   msg += struct.pack(">i", a)
        elif t == "f": msg += struct.pack(">f", a)
        elif t == "s": msg += pad(a.encode() + b"\x00")
    sock.sendto(msg, (ip, port))


def capture(bin_path, layout, channels, seconds, wav, reverb_send, t60, use_set=False):
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
        # Engage the room via the NEW /room/enable path (not /reverb/select).
        send_osc(sock, "/room/enable", "i", [1], "127.0.0.1", port)
        if use_set:
            # Atomic bundle: t60 sx sy sz earlyW earlyBal clSend clDiff clVol
            #                eqHP eqLP hfCorner hfRatio
            send_osc(sock, "/room/set", "fffffffffffff",
                     [t60, 6.0, 5.0, 3.0, 45.0, 0.45, 0.4, 0.48, 630.0,
                      120.0, 10000.0, 6200.0, 0.62], "127.0.0.1", port)
        else:
            send_osc(sock, "/room/t60", "f", [t60], "127.0.0.1", port)
        # Object below the horizon so the dry sound stays in the lower ring.
        send_osc(sock, "/obj/move", "ifff", [0, 0.0, -0.349, 2.0], "127.0.0.1", port)
        send_osc(sock, "/obj/dsp", "iiiif", [0, 0, 0, 6, reverb_send], "127.0.0.1", port)
        time.sleep(0.25)
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
    samples = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    energy = [0.0] * nch
    for i, v in enumerate(samples):
        energy[i % nch] += float(v) * v
    return energy, xruns


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()

    n = write_dome("/tmp/dome_room_ctl.yaml", per_ring=8)   # 16 speakers, 3D
    half = n // 2
    print(f"[smoke] dome speakers={n}")

    # Dry baseline (room enabled but no reverb send) isolates the upper ring.
    eDry, xD = capture(a.bin, "/tmp/dome_room_ctl.yaml", n, a.seconds,
                       "/tmp/room_ctl_dry.wav", reverb_send=0.0, t60=1.2)
    eShort, xS = capture(a.bin, "/tmp/dome_room_ctl.yaml", n, a.seconds,
                         "/tmp/room_ctl_short.wav", reverb_send=0.8, t60=0.3)
    eLong, xL = capture(a.bin, "/tmp/dome_room_ctl.yaml", n, a.seconds,
                        "/tmp/room_ctl_long.wav", reverb_send=0.8, t60=5.0)
    # /room/set atomic bundle must not crash / xrun.
    eSet, xSet = capture(a.bin, "/tmp/dome_room_ctl.yaml", n, a.seconds,
                         "/tmp/room_ctl_set.wav", reverb_send=0.8, t60=4.0, use_set=True)
    if any(e is None for e in (eDry, eShort, eLong, eSet)):
        print("SMOKE FAIL"); return 1

    upDry   = sum(eDry[half:])
    upShort = sum(eShort[half:])
    upLong  = sum(eLong[half:])
    upSet   = sum(eSet[half:])
    print(f"[smoke] upper-ring  dry={upDry:.4g}  t60=0.3={upShort:.4g}  "
          f"t60=5.0={upLong:.4g}  set(t60=4)={upSet:.4g}")
    print(f"[smoke] xruns: dry={xD} short={xS} long={xL} set={xSet}")

    ok = True
    if sum(eShort) <= 0:
        print("FAIL: engine silent"); ok = False
    if upShort <= 5.0 * max(upDry, 1.0):
        print(f"FAIL: /room/enable did not engage the spatial room "
              f"(upper {upShort:.3g} vs dry {upDry:.3g})"); ok = False
    if upLong <= 1.3 * upShort:
        print(f"FAIL: /room/t60 5.0 did not extend the tail vs 0.3 "
              f"(long {upLong:.3g} vs short {upShort:.3g})"); ok = False
    if upSet <= 5.0 * max(upDry, 1.0):
        print(f"FAIL: /room/set bundle did not engage the room (upper {upSet:.3g})"); ok = False
    if max(xD, xS, xL, xSet) != 0:
        print("FAIL: xruns occurred under /room/* control"); ok = False

    if ok:
        print(f"SMOKE PASS: /room/enable engages the spatial room "
              f"(upper/dry={upShort/max(upDry,1.0):.1f}x), /room/t60 5.0 extends the "
              f"tail {upLong/max(upShort,1e-9):.2f}x vs 0.3, /room/set bundle clean, "
              f"xruns=0")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
