#!/usr/bin/env python3
"""Real-binary smoke for the ⑥b spatial room engine (late FDN fanned out to
cube-corner VBAP directions).

Drives the live spatial_engine_core binary on a 3D dome layout and shows that
selecting the room reverb (/reverb/select ,s "room") and feeding an object a
reverb send spreads energy across MANY speakers — strictly more than the dry
object alone — and adds tail energy. A mono/uniform reverb could not produce a
direction-dependent multi-speaker spread driven by the 8 FDN line taps.

OSC wire-format notes (CommandDecoder eats the first two ints of any ,ii… msg
as a seq/id header — see payload_int_offset):
  /obj/move ,ifff -> [obj, az, el, dist]            (1 leading int, no header eaten)
  /obj/dsp  ,iiiif -> [seq, id, obj, param, value]  (param 6 = reverb_send)
  /reverb/select ,s -> "fdn" | "ir" | "room"
"""
import argparse, math, os, re, socket, struct, subprocess, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_dome(path, per_ring=8):
    """Two rings: lower el=-20, upper el=+30, per_ring speakers each."""
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


def capture(bin_path, layout, channels, seconds, wav, reverb_send):
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
        send_osc(sock, "/reverb/select", "s", ["room"], "127.0.0.1", port)
        # Object 0 active, aimed at the LOWER ring (az=0, el=-20°) so the dry
        # direct sound stays in the lower hemisphere. The room reverb's +y cube
        # corners fan energy onto the UPPER ring, which the dry never reaches —
        # so upper-ring energy isolates the reverb free of dry run-to-run jitter.
        send_osc(sock, "/obj/move", "ifff", [0, 0.0, -0.349, 2.0], "127.0.0.1", port)
        send_osc(sock, "/obj/dsp", "iiiif", [0, 0, 0, 6, reverb_send], "127.0.0.1", port)
        time.sleep(0.25)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    print("--- engine log tail ---"); print("\n".join(out.splitlines()[-4:]))
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


def active_count(energy, frac=0.01):
    tot = sum(energy)
    return sum(1 for e in energy if tot > 0 and e > frac * tot)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()

    n = write_dome("/tmp/dome_room.yaml", per_ring=8)   # 16 speakers, 3D
    print(f"[smoke] dome speakers={n}")

    eBase, xB = capture(a.bin, "/tmp/dome_room.yaml", n, a.seconds,
                        "/tmp/room_base.wav", reverb_send=0.0)   # dry object only
    eRoom, xR = capture(a.bin, "/tmp/dome_room.yaml", n, a.seconds,
                        "/tmp/room_on.wav",  reverb_send=0.8)    # dry + spatial room reverb
    if eBase is None or eRoom is None:
        print("SMOKE FAIL"); return 1

    half = len(eRoom) // 2          # speakers [0,half) = lower ring, [half,n) = upper ring
    # Upper-ring energy isolates the reverb: the dry object is in the lower
    # hemisphere, so any upper-ring energy is the room reverb's +y cube corners.
    upBase = sum(eBase[half:])
    upRoom = sum(eRoom[half:])
    upActiveRoom = sum(1 for s in range(half, len(eRoom))
                       if max(eRoom[half:]) > 0 and eRoom[s] > 0.05 * max(eRoom[half:]))
    print(f"[smoke] dry-only:  total={sum(eBase):.4g}  upperRing={upBase:.4g}  xruns={xB}")
    print(f"[smoke] room-on:   total={sum(eRoom):.4g}  upperRing={upRoom:.4g}  xruns={xR}")
    print(f"[smoke] upper-ring active in room mode = {upActiveRoom}/{len(eRoom)-half}")
    print("[smoke] upper-ring per-spk (room): " +
          " ".join(f"{e:.2g}" for e in eRoom[half:]))

    ok = True
    if sum(eBase) <= 0: print("FAIL: dry baseline silent"); ok = False
    if upRoom <= 5.0 * max(upBase, 1.0):
        print(f"FAIL: room reverb added little upper-ring energy "
              f"(room {upRoom:.3g} vs dry {upBase:.3g}) — not engaging?"); ok = False
    if upActiveRoom < 3:
        print(f"FAIL: upper-ring footprint too narrow ({upActiveRoom}); "
              f"expected ≥3 from the +y cube-corner FDN lines"); ok = False

    if ok:
        print(f"SMOKE PASS: spatial room reverb fans onto {upActiveRoom} upper-ring "
              f"speakers (dry stays lower; upper room/dry = {upRoom/max(upBase,1.0):.1f}x), "
              f"xruns={xR}")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
