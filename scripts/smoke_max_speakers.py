#!/usr/bin/env python3
"""Real-binary smoke for the Phase 0.5 speaker-dimension lift (64 -> 128).

Drives the live spatial_engine_core binary with a 100-speaker layout (> the old
64 cap) and verifies through the engine that:
  - the engine starts and renders a 100-channel bus (a 64-capped build would
    assert/refuse the layout at prepareToPlay — RT_ASSERTS=ON),
  - output is non-silent, and
  - a VBAP point source aimed at a HIGH-INDEX speaker (channel index > 64)
    actually lights that channel up — i.e. speakers past 64 render end-to-end.

REQUIRES a binary built with -DSPATIAL_ENGINE_MAX_SPEAKERS=128 (default --bin
points at build-128). Running the default 64 binary with --channels 100 would
trip the renderers' MAX_SPEAKERS assert, which is the point.

/obj/algo wire format is [seq, id, obj, algo] (4 ints): CommandDecoder eats the
first two ints of any ",ii…" message as a seq/id header. algo 0 = VBAP.
"""
import argparse, os, re, socket, struct, subprocess, time, wave


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def write_ring(path, n):
    """Dense horizontal ring, n speakers, channel i at az = -180 + 360 i/n."""
    lines = ['version: "1.0"', f'name: "ring{n}"', 'regularity_hint: "CIRCULAR"',
             "speakers:"]
    for i in range(n):
        az = -180.0 + 360.0 * i / n
        lines += [f"  - id: {i+1}", f"    channel: {i+1}",
                  f"    az_deg: {az}", "    el_deg: 0.0", "    dist_m: 1.0"]
    open(path, "w").write("\n".join(lines) + "\n")
    return path


def send_osc(sock, addr, args, ip, port):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00")
    types = ","
    for a in args: types += "i" if isinstance(a, int) else "f"
    msg += pad(types.encode() + b"\x00")
    for a in args:
        msg += struct.pack(">i", a) if isinstance(a, int) else struct.pack(">f", a)
    sock.sendto(msg, (ip, port))


def capture(bin_path, layout, channels, seconds, wav, az_rad, el_rad, dist_m, algo):
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
        send_osc(sock, "/obj/algo", [0, 0, 0, algo], "127.0.0.1", port)
        send_osc(sock, "/obj/move", [0, az_rad, el_rad, dist_m], "127.0.0.1", port)
        time.sleep(0.25)
    sock.close()
    try:
        out, _ = proc.communicate(timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        proc.kill(); out, _ = proc.communicate()
    print("--- engine log tail ---"); print("\n".join(out.splitlines()[-6:]))
    xruns = 0
    for ln in out.splitlines():
        m = re.search(r"xrun[s]?[^\d]*(\d+)", ln, re.IGNORECASE)
        if m: xruns = max(xruns, int(m.group(1)))
    if not os.path.exists(wav):
        print("FAIL: no WAV produced (engine refused the >64 layout?)"); return None, xruns
    w = wave.open(wav, "rb")
    nch, sw, nfr = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(nfr); w.close()
    if nfr == 0: print("FAIL: empty WAV"); return None, xruns
    fmt = {2: "h", 4: "i"}.get(sw)
    if fmt is None: print(f"FAIL: unsupported sample width {sw}"); return None, xruns
    samples = struct.unpack("<" + fmt * (len(raw) // sw), raw)
    energy = [0.0] * nch
    for i, v in enumerate(samples):
        energy[i % nch] += float(v) * v
    return energy, xruns


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-128/core/spatial_engine_core"))
    ap.add_argument("--speakers", type=int, default=100)   # > 64 cap
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()

    n = a.speakers
    layout = write_ring("/tmp/ring_max_speakers.yaml", n)

    # Aim a VBAP point source at a high-index speaker: channel index kt > 64.
    # kt at ~70% of the ring -> az well away from the +/-180 wrap so the engine's
    # azimuth sign cannot make it ambiguous with a low-index mirror.
    kt = int(n * 0.7)                       # 70 of 100 -> index 70 (> 64)
    az_deg = -180.0 + 360.0 * kt / n        # the layout azimuth of channel kt
    az_rad = az_deg * 3.14159265358979 / 180.0

    print(f"[smoke] speakers={n}  target_channel_index={kt}  az={az_deg:.1f}deg")
    energy, xruns = capture(a.bin, layout, n, a.seconds,
                            "/tmp/ring_max.wav", az_rad, 0.0, 1.0, algo=0)
    if energy is None:
        print("SMOKE FAIL"); return 1

    nch = len(energy)
    tot = sum(energy)
    amax = max(range(nch), key=lambda i: energy[i])
    # Active = channels carrying >1% of total energy.
    active = [i for i in range(nch) if tot > 0 and energy[i] > 0.01 * tot]
    high_active = [i for i in active if i > 64]
    print(f"[smoke] nch={nch}  total_energy={tot:.4g}  argmax_ch={amax}  "
          f"active={active}  xruns={xruns}")

    ok = True
    if nch != n:
        print(f"FAIL: engine rendered {nch} channels, expected {n}"); ok = False
    if tot <= 0:
        print("FAIL: silent output"); ok = False
    if amax <= 64:
        print(f"FAIL: loudest channel {amax} is not a >64 index "
              f"(speaker dimension may still be capped at 64)"); ok = False
    if not high_active:
        print("FAIL: no channel with index > 64 is active"); ok = False

    if ok:
        print(f"SMOKE PASS: 100-speaker engine renders; high-index channel "
              f"{amax} (>64) is loudest, {len(active)} active, xruns={xruns}")
        return 0
    print("SMOKE FAIL"); return 1


if __name__ == "__main__":
    raise SystemExit(main())
