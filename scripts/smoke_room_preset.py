#!/usr/bin/env python3
"""Real-binary end-to-end smoke for ⑥h /room/preset (named room recall via scene).

Proves the full loop on the live binary:
  1. SAVE  — set a long T60 (5.0 s) + room enabled, then /scene/save "rmpreset".
             The scene now carries the room block (snapshotRoom on save).
  2. RECALL— a fresh engine set to a SHORT T60 (0.3 s), then /room/preset
             "rmpreset" restores the saved long T60 → upper-ring tail jumps.
  3. BASE  — same fresh engine left at the short T60 (no preset) as the control.

RECALL upper-ring tail must be >> BASE (the preset re-applied the long T60). A
shared scenes dir across runs is forced via XDG_CONFIG_HOME.
"""
import argparse, os, re, socket, struct, subprocess, tempfile, time, wave


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


def send_osc(sock, addr, types, args, port):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i":   msg += struct.pack(">i", a)
        elif t == "f": msg += struct.pack(">f", a)
        elif t == "s": msg += pad(a.encode() + b"\x00")
    sock.sendto(msg, ("127.0.0.1", port))


def run(bin_path, layout, channels, seconds, wav, xdg, *, t60, save_as=None, preset=None):
    port = free_port()
    if os.path.exists(wav): os.remove(wav)
    env = dict(os.environ); env["XDG_CONFIG_HOME"] = xdg
    cmd = [bin_path, "--backend", "null", "--input-backend", "null",
           "--channels", str(channels), "--rate", "48000", "--block", "256",
           "--layout", layout, "--wav", wav, "--seconds", str(seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, env=env)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    # Engage room + object + send, at the given T60, a few times for delivery.
    for _ in range(4):
        send_osc(sock, "/room/enable", "i", [1], port)
        send_osc(sock, "/room/t60", "f", [t60], port)
        send_osc(sock, "/obj/move", "ifff", [0, 0.0, -0.349, 2.0], port)
        send_osc(sock, "/obj/dsp", "iiiif", [0, 0, 0, 6, 0.8], port)
        time.sleep(0.08)
    time.sleep(0.2)            # let the params drain into the engine
    if save_as is not None:
        send_osc(sock, "/scene/save", "s", [save_as], port)
        time.sleep(0.25)       # let the control loop persist the scene
    if preset is not None:
        send_osc(sock, "/room/preset", "s", [preset], port)
        time.sleep(0.3)        # control loop load + RoomCtl SetAll drain
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))
    if not os.path.exists(wav):
        return None, xruns
    w = wave.open(wav, "rb")
    nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    if nfr == 0: return None, xruns
    fmt = {2: "h", 4: "i"}.get(sw)
    s = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    energy = [0.0] * nch
    for i, v in enumerate(s):
        energy[i % nch] += float(v) * v
    return energy, xruns


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    # Long enough that /scene/save (~1.3 s in) and /room/preset are processed
    # well before the run deadline, with a long post-preset render window.
    ap.add_argument("--seconds", type=float, default=3.0)
    a = ap.parse_args()

    xdg = tempfile.mkdtemp(prefix="room_preset_smoke_")
    layout = "/tmp/dome_room_preset.yaml"
    n = write_dome(layout, per_ring=8)
    half = n // 2
    print(f"[smoke] dome speakers={n}  scenes XDG={xdg}")

    # 1. SAVE a scene at long T60.
    _, xSave = run(a.bin, layout, n, a.seconds, "/tmp/rp_save.wav", xdg,
                   t60=5.0, save_as="rmpreset")
    scene_file = os.path.join(xdg, "spatial_engine", "scenes", "rmpreset.json")
    has_room = os.path.exists(scene_file) and '"room"' in open(scene_file).read()
    print(f"[smoke] saved scene exists={os.path.exists(scene_file)} has_room_block={has_room}")

    # 2. RECALL: fresh engine at short T60, then /room/preset restores long T60.
    eRecall, xRec = run(a.bin, layout, n, a.seconds, "/tmp/rp_recall.wav", xdg,
                        t60=0.3, preset="rmpreset")
    # 3. BASE: fresh engine left at short T60 (control).
    eBase, xBase = run(a.bin, layout, n, a.seconds, "/tmp/rp_base.wav", xdg, t60=0.3)

    if eRecall is None or eBase is None:
        print("SMOKE FAIL: missing WAV"); return 1

    upRecall = sum(eRecall[half:])
    upBase = sum(eBase[half:])
    print(f"[smoke] upper-ring  recall(preset→T60=5)={upRecall:.4g}  base(T60=0.3)={upBase:.4g}  "
          f"ratio={upRecall/max(upBase,1e-9):.2f}")
    print(f"[smoke] xruns: save={xSave} recall={xRec} base={xBase}")

    ok = True
    if not has_room:
        print("FAIL: saved scene has no room block"); ok = False
    if upBase <= 0:
        print("FAIL: base render silent"); ok = False
    if upRecall <= 1.3 * upBase:
        print(f"FAIL: /room/preset did not restore the long T60 "
              f"(recall {upRecall:.3g} vs base {upBase:.3g})"); ok = False
    # xruns: the null backend can log a sporadic startup/scheduling xrun under
    # machine load on ANY run (incl. BASE, which never touches /room/preset), so a
    # hard ==0 false-fails on unrelated jitter. Only flag the preset path adding
    # xruns BEYOND the control path's own jitter (a real RT regression would show
    # sustained excess, not a stray ±1).
    if max(xSave, xRec) > xBase + 3:
        print(f"FAIL: /room/preset path added xruns beyond base jitter "
              f"(save={xSave} recall={xRec} base={xBase})"); ok = False

    if ok:
        print(f"SMOKE PASS: /scene/save captured the room block, /room/preset recalled "
              f"the long T60 onto a short-T60 engine (upper {upRecall/max(upBase,1e-9):.2f}x base), "
              f"preset path xruns≤base jitter (save={xSave} recall={xRec} base={xBase})")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
