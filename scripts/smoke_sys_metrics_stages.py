#!/usr/bin/env python3
"""Real-binary smoke for the Phase 1.4b per-stage /sys/metrics timings.

Launches spatial_engine_core, handshakes so the engine captures the reply peer,
activates several objects with room reverb + decorrelation, then collects the
1 Hz /sys/metrics emit and asserts the NEW per-stage fields are present and that
the engine actually populated them from real audio:
  - stage_render_us  > 0  (active objects -> renderer dispatch runs)
  - stage_room_us    > 0  (room reverb selected -> early/late/cluster fan-out)
  - stage_decorr_us  present (>= 0; > 0 once decorr is enabled)
  - stage_binaural_us present (>= 0)
and that xruns == 0.

Usage: smoke_sys_metrics_stages.py [--bin PATH] [--layout PATH]
Exit 0 = PASS.
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time

def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    msg = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i": msg += struct.pack(">i", a)
        elif t == "f": msg += struct.pack(">f", a)
        elif t == "s": msg += pad(a.encode() + b"\x00")
    return msg

def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--seconds", type=int, default=4)
    ap.add_argument("--channels", type=int, default=8)
    a = ap.parse_args()
    port = free_port()

    cmd = [a.bin, "--backend", "null", "--input-backend", "null",
           "--channels", str(a.channels), "--rate", "48000", "--block", "256",
           "--layout", a.layout, "--seconds", str(a.seconds),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    print("RUN:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0)); sock.settimeout(1.0)
    dst = ("127.0.0.1", port)
    time.sleep(0.7)
    # Handshake (engine captures this socket as the /sys/metrics reply peer).
    sock.sendto(osc("/sys/handshake", "i", 1), dst)
    # Select room reverb + enable decorrelation.
    sock.sendto(osc("/reverb/select", "s", "room"), dst)
    sock.sendto(osc("/decorr/enable", "i", 1), dst)
    sock.sendto(osc("/decorr/mix", "f", 0.5), dst)
    # Activate objects (/obj/move ,ifff) + reverb send (/obj/dsp [seq,id,obj,param,value]).
    for i in range(6):
        sock.sendto(osc("/obj/move", "ifff", i, (i - 3) * 0.3, 0.1, 1.5), dst)
        sock.sendto(osc("/obj/dsp", "iiiif", 0, 0, i, 6, 0.6), dst)

    # Collect /sys/metrics key=values for a few seconds (1 Hz emit).
    fields = {}
    t_end = time.time() + a.seconds
    while time.time() < t_end:
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        if not data.startswith(b"/sys/metrics"): continue
        # ",s" then padded kv string
        off = ((len(b"/sys/metrics") + 1) + 3) & ~3
        if data[off:off+2] != b",s": continue
        off += 4
        kv = data[off:].split(b"\x00", 1)[0].decode(errors="ignore")
        if "=" in kv:
            k, v = kv.split("=", 1)
            try: fields[k] = int(v)
            except ValueError: fields[k] = v
    sock.close()
    try: out, _ = proc.communicate(timeout=a.seconds + 8)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    xruns = None
    m = re.search(r"xruns=(\d+)", out)
    if m: xruns = int(m.group(1))

    print("collected /sys/metrics fields:", {k: fields[k] for k in sorted(fields)})
    print("engine xruns:", xruns)

    required = ["stage_render_us", "stage_room_us", "stage_decorr_us", "stage_binaural_us"]
    ok = True
    for k in required:
        if k not in fields:
            print(f"FAIL: missing /sys/metrics field '{k}'"); ok = False
    if ok:
        if fields.get("stage_render_us", 0) <= 0:
            print(f"FAIL: stage_render_us={fields.get('stage_render_us')} (expected > 0 "
                  f"with active objects)"); ok = False
        if fields.get("stage_room_us", 0) <= 0:
            print(f"FAIL: stage_room_us={fields.get('stage_room_us')} (expected > 0 "
                  f"with room reverb selected)"); ok = False
    if xruns not in (0, None):
        print(f"FAIL: xruns={xruns} (expected 0)"); ok = False

    if ok:
        print(f"PASS: per-stage /sys/metrics populated from real audio "
              f"(render={fields['stage_render_us']}us room={fields['stage_room_us']}us "
              f"decorr={fields['stage_decorr_us']}us binaural={fields['stage_binaural_us']}us), "
              f"xruns={xruns}")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
