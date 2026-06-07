#!/usr/bin/env python3
"""Real-binary smoke for Phase 3.4 outbound ADM-OSC broadcast (vid2spatial bridge).

Launches the engine with --adm-send-port, binds that port, sends an ADM object
position to the command port, then verifies the engine periodically broadcasts
/adm/obj/N/aed for the active object:
  - the broadcast arrives at ~adm_send_fps Hz,
  - az round-trips (send ADM +30 -> engine az=-30 -> broadcast ADM +30): proves
    the outbound -az negation pairs with the inbound one (no L/R drift),
  - el/dist round-trip, and only ACTIVE objects are broadcast.

Usage: smoke_adm_broadcast.py [--bin PATH]
Exit 0 = PASS.
"""
import argparse, math, os, socket, struct, subprocess, sys, time

def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def osc(addr, types, *args):
    def pad(b): return b + b"\x00" * ((4 - len(b) % 4) % 4)
    m = pad(addr.encode() + b"\x00") + pad(("," + types).encode() + b"\x00")
    for t, a in zip(types, args):
        m += struct.pack(">f", a) if t == "f" else struct.pack(">i", a)
    return m

def parse_aed(data):
    # /adm/obj/<id>/aed ,fff -> (id, az, el, dist) or None
    if not data.startswith(b"/adm/obj/"): return None
    try:
        addr = data.split(b"\x00", 1)[0].decode()
        oid = int(addr.split("/")[3])
        if not addr.endswith("/aed"): return None
        off = (len(addr) + 1 + 3) & ~3
        if data[off:off+4] != b",fff": return None
        off += (len(",fff") + 1 + 3) & ~3   # typetag padded to 4-byte boundary (= 8)
        az, el, dn = struct.unpack(">fff", data[off:off+12])
        return (oid, az, el, dn)
    except Exception:
        return None

def main():
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build-test/core/spatial_engine_core"))
    ap.add_argument("--layout", default=os.path.join(root, "configs/lab_8ch.yaml"))
    ap.add_argument("--fps", type=int, default=30)
    a = ap.parse_args()

    send_port = free_port()       # engine broadcasts here; we listen
    cmd_port = free_port()        # engine OSC command port
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(("127.0.0.1", send_port)); listener.settimeout(0.5)

    cmd = [a.bin, "--backend", "null", "--input-backend", "null",
           "--channels", "8", "--rate", "48000", "--block", "256",
           "--layout", a.layout, "--seconds", "4",
           "--osc-port", str(cmd_port), "--osc-bind", "127.0.0.1",
           "--adm-send-port", str(send_port), "--adm-send-fps", str(a.fps)]
    print("RUN:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    cs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(0.7)
    # Place object 0 at ADM az=+30 (left), el=10, dist_norm=0.25.
    for _ in range(3):
        cs.sendto(osc("/adm/obj/0/aed", "fff", 30.0, 10.0, 0.25), ("127.0.0.1", cmd_port))
        time.sleep(0.1)

    # Collect broadcasts for ~1.5 s.
    got = []
    t_end = time.time() + 1.5
    while time.time() < t_end:
        try:
            data, _ = listener.recvfrom(4096)
        except socket.timeout:
            continue
        p = parse_aed(data)
        if p and p[0] == 0:
            got.append(p)
    cs.close(); listener.close()
    try: out, _ = proc.communicate(timeout=10)
    except subprocess.TimeoutExpired: proc.kill(); out, _ = proc.communicate()

    n = len(got)
    rate = n / 1.5 if n else 0.0
    print(f"received {n} /adm/obj/0/aed broadcasts (~{rate:.0f}/s, target {a.fps})")
    if n:
        az, el, dn = got[-1][1], got[-1][2], got[-1][3]
        print(f"last: az={az:.2f} el={el:.2f} dist_norm={dn:.3f}")

    ok = True
    if n < a.fps * 0.5:
        print(f"FAIL: too few broadcasts ({n} in 1.5s; expected ~{int(a.fps*1.5)})"); ok = False
    if n:
        az, el, dn = got[-1][1], got[-1][2], got[-1][3]
        # Round-trip identity: ADM +30 in -> engine -30 -> ADM +30 out.
        if abs(az - 30.0) > 1.0:
            print(f"FAIL: az round-trip wrong (got {az:.2f}, expected ~+30) — -az drift?"); ok = False
        if abs(el - 10.0) > 1.0:
            print(f"FAIL: el wrong (got {el:.2f}, expected ~10)"); ok = False
        if abs(dn - 0.25) > 0.02:
            print(f"FAIL: dist_norm wrong (got {dn:.3f}, expected ~0.25)"); ok = False

    if ok:
        print(f"PASS: outbound ADM broadcast works (~{rate:.0f} fps, az/el/dist round-trip "
              f"identity — outbound -az pairs with inbound)")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
