#!/usr/bin/env python3
"""Functional soak for the v1.0 DoD §4: room reverb + decorrelation, practical
scene, xrun=0 over a sustained run.

The canonical tests/soak_harness/run_soak.py uses a request-reply metrics model
that predates the engine's 1 Hz unsolicited /sys/metrics emit (its real-mode
poller is a stub). This soak drives the engine the way it actually works:
launch -> handshake -> select room + enable decorr + activate moving objects ->
passively collect the 1 Hz /sys/metrics emit -> gate on xrun=0 and p99 below a
block-DERIVED budget (not the hardcoded 933 µs).

Practical scene (per the 1.4a finding that room reverb is O(spk^2) and real-time
only to ~16-24 speakers): a 12-speaker dome with moving objects, room reverb,
and decorrelation all active.

Usage: soak_room_decorr.py [--duration 1800] [--speakers 12] [--block 256]
Exit 0 = PASS (xrun=0, p99 within budget, ran the full duration).
"""
import argparse, math, os, re, socket, struct, subprocess, sys, time

def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        if t == "i": m += struct.pack(">i", a)
        elif t == "f": m += struct.pack(">f", a)
        elif t == "s": m += pad(a.encode() + b"\x00")
    return m

def write_dome(path, n_spk):
    """Two-ring dome with n_spk speakers (lower el=-20, upper el=+30)."""
    per = max(1, n_spk // 2)
    lines = ['version: "1.0"', 'name: "soak_dome"', 'regularity_hint: "IRREGULAR"', "speakers:"]
    ch = 1
    for ring_el in (-20.0, 30.0):
        for i in range(per):
            if ch > n_spk: break
            az = -180.0 + 360.0 * i / per
            lines += [f"  - channel: {ch}", f"    az_deg: {az:.2f}", f"    el_deg: {ring_el:.2f}"]
            ch += 1
    open(path, "w").write("\n".join(lines) + "\n")
    return ch - 1

def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--duration", type=int, default=1800, help="soak seconds (DoD: 1800)")
    ap.add_argument("--speakers", type=int, default=12, help="practical <=16 (room is O(spk^2))")
    ap.add_argument("--objects", type=int, default=8)
    ap.add_argument("--block", type=int, default=256)
    ap.add_argument("--rate", type=int, default=48000)
    a = ap.parse_args()

    layout = "/tmp/soak_dome.yaml"
    n_spk = write_dome(layout, a.speakers)
    port = free_port()
    budget_us = a.block / a.rate * 1e6
    p99_threshold = int(budget_us * 0.70)  # block-DERIVED (Phase 1.4c)

    cmd = [a.bin, "--backend", "null", "--input-backend", "null",
           "--channels", str(n_spk), "--rate", str(a.rate), "--block", str(a.block),
           "--layout", layout, "--seconds", str(a.duration + 2),
           "--osc-port", str(port), "--osc-bind", "127.0.0.1"]
    print(f"[soak] {a.duration}s  {n_spk}spk  {a.objects}obj  block={a.block} "
          f"budget={budget_us:.0f}us  p99_gate={p99_threshold}us (70% budget)")
    print("RUN:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0)); sock.settimeout(0.5)
    dst = ("127.0.0.1", port)
    time.sleep(0.7)
    sock.sendto(osc("/sys/handshake", "i", 1), dst)
    sock.sendto(osc("/reverb/select", "s", "room"), dst)
    sock.sendto(osc("/decorr/enable", "i", 1), dst)
    sock.sendto(osc("/decorr/mix", "f", 0.5), dst)
    for i in range(a.objects):
        sock.sendto(osc("/obj/dsp", "iiiif", 0, 0, i, 6, 0.5), dst)

    p99_max = 0; cpu_peak_max = 0; metric_xrun_max = 0; n_metrics = 0
    t0 = time.time(); t_end = t0 + a.duration; next_move = t0
    while time.time() < t_end:
        now = time.time()
        if now >= next_move:  # move objects ~2 Hz to exercise motion
            ph = now - t0
            for i in range(a.objects):
                az = math.sin(ph * 0.3 + i) * 1.4
                el = math.sin(ph * 0.17 + i) * 0.5
                sock.sendto(osc("/obj/move", "ifff", i, az, el, 1.0 + 0.5 * math.sin(ph + i)), dst)
            next_move = now + 0.5
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        if not data.startswith(b"/sys/metrics"): continue
        off = ((len(b"/sys/metrics") + 1) + 3) & ~3
        if data[off:off+2] != b",s": continue
        kv = data[off+4:].split(b"\x00", 1)[0].decode(errors="ignore")
        if "=" not in kv: continue
        k, v = kv.split("=", 1)
        try: vi = int(v)
        except ValueError: continue
        if k == "p99_us": p99_max = max(p99_max, vi); n_metrics += 1
        elif k == "cpu_peak_pct": cpu_peak_max = max(cpu_peak_max, vi)
        elif k == "xrun_count": metric_xrun_max = max(metric_xrun_max, vi)
    sock.close()

    try: out, _ = proc.communicate(timeout=10)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()
    blocks = xruns = None
    m = re.search(r"blocks=(\d+)\s+xruns=(\d+)", out)
    if m: blocks, xruns = int(m.group(1)), int(m.group(2))
    expected_blocks = a.duration * a.rate // a.block

    print(f"[soak] metrics_samples={n_metrics} p99_max={p99_max}us "
          f"cpu_peak_max={cpu_peak_max}% metric_xrun_max={metric_xrun_max}")
    print(f"[soak] engine: blocks={blocks} xruns={xruns} (expected ~{expected_blocks} blocks)")

    ok = True
    if xruns is None:
        print("FAIL: could not read engine xruns from stdout"); ok = False
    elif xruns != 0:
        print(f"FAIL: engine xruns={xruns} (expected 0)"); ok = False
    if metric_xrun_max != 0:
        print(f"FAIL: /sys/metrics xrun_count peaked at {metric_xrun_max}"); ok = False
    if n_metrics == 0:
        print("FAIL: no /sys/metrics emits collected (telemetry path broken)"); ok = False
    elif p99_max > p99_threshold:
        print(f"FAIL: p99_max={p99_max}us > {p99_threshold}us (70% budget)"); ok = False
    if blocks is not None and blocks < int(expected_blocks * 0.9):
        print(f"FAIL: engine ran only {blocks} blocks (< 90% of {expected_blocks})"); ok = False

    if ok:
        print(f"SOAK PASS: room+decorr {n_spk}spk/{a.objects}obj for {a.duration}s — "
              f"xruns=0, p99_max={p99_max}us < {p99_threshold}us, {blocks} blocks")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
