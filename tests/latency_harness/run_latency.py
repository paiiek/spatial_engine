#!/usr/bin/env python3
"""
Latency harness for spatial_engine P10.

Measures stages 2-6: T0 = OSC packet receive at juce::OSCReceiver callback,
T1 = sample written to Dante PCIe buffer just before ALSA period hand-off.

Lab-only (requires real Dante hardware). Use --dry-run for CI/development.
"""

import argparse
import json
import math
import os
import platform
import random
import socket
import struct
import time
from datetime import datetime, timezone
from pathlib import Path


# ---------------------------------------------------------------------------
# Minimal OSC encode/decode (no external deps)
# ---------------------------------------------------------------------------

def _pad4(n: int) -> int:
    return (n + 3) & ~3


def osc_encode_string(s: str) -> bytes:
    b = s.encode("utf-8") + b"\x00"
    return b.ljust(_pad4(len(b)), b"\x00")


def osc_encode_float(f: float) -> bytes:
    return struct.pack(">f", f)


def osc_encode_int(i: int) -> bytes:
    return struct.pack(">i", i)


def osc_build_message(address: str, type_tag: str, *args) -> bytes:
    """Build a minimal OSC 1.0 message."""
    msg = osc_encode_string(address)
    msg += osc_encode_string("," + type_tag)
    for tag, val in zip(type_tag, args):
        if tag == "f":
            msg += osc_encode_float(val)
        elif tag == "i":
            msg += osc_encode_int(val)
        elif tag == "s":
            msg += osc_encode_string(val)
    return msg


def osc_parse_message(data: bytes) -> dict:
    """Parse a minimal OSC message into address + kwargs dict."""
    offset = 0

    def read_string():
        nonlocal offset
        end = data.index(b"\x00", offset)
        s = data[offset:end].decode("utf-8")
        offset = _pad4(end + 1)
        return s

    address = read_string()
    type_tags = read_string()  # starts with ","

    result = {"address": address, "args": []}
    for tag in type_tags[1:]:  # skip leading ","
        if tag == "f":
            val = struct.unpack_from(">f", data, offset)[0]
            offset += 4
        elif tag == "i":
            val = struct.unpack_from(">i", data, offset)[0]
            offset += 4
        elif tag == "s":
            val = read_string()
        else:
            val = None
        result["args"].append((tag, val))
    return result


# ---------------------------------------------------------------------------
# System info helpers
# ---------------------------------------------------------------------------

def _read_file(path: str, default: str = "unknown") -> str:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return default


def gather_system_info() -> dict:
    kernel = _read_file("/proc/version", platform.version())
    governor = _read_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "unknown"
    )
    preempt_rt = "PREEMPT_RT" in kernel
    return {
        "kernel": kernel,
        "governor": governor,
        "preempt_rt": preempt_rt,
        "machine_model": platform.node(),
    }


# ---------------------------------------------------------------------------
# Percentile helper
# ---------------------------------------------------------------------------

def percentile(sorted_data: list, p: float) -> float:
    """Return p-th percentile (0-100) from a sorted list using nearest-rank."""
    if not sorted_data:
        return 0.0
    idx = max(0, math.ceil(p / 100.0 * len(sorted_data)) - 1)
    return sorted_data[idx]


# ---------------------------------------------------------------------------
# Dry-run synthetic sample generator
# ---------------------------------------------------------------------------

def generate_synthetic_samples(n: int, mean_us: float = 2000.0, sigma_log: float = 0.4) -> list:
    """Lognormal samples with ~mean_us mean in microseconds."""
    mu_log = math.log(mean_us) - 0.5 * sigma_log ** 2
    samples = []
    for _ in range(n):
        u1 = random.random() or 1e-12
        u2 = random.random() or 1e-12
        z = math.sqrt(-2 * math.log(u1)) * math.cos(2 * math.pi * u2)
        samples.append(math.exp(mu_log + sigma_log * z))
    return samples


# ---------------------------------------------------------------------------
# Live measurement (real hardware)
# ---------------------------------------------------------------------------

def run_live(args) -> list:
    """Send OSC pos jumps and collect /sys/metrics replies."""
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state_sock.bind(("0.0.0.0", args.state_port))
    state_sock.settimeout(0.05)

    samples = []
    deadline = time.perf_counter() + args.duration
    obj_ids = list(range(args.objects))
    obj_idx = 0

    try:
        while time.perf_counter() < deadline:
            obj_id = obj_ids[obj_idx % len(obj_ids)]
            obj_idx += 1

            # Record T0 before sending (ns precision)
            t0_ns = time.perf_counter_ns()
            msg = osc_build_message(
                f"/obj/{obj_id}/pos", "fff",
                random.uniform(-1.0, 1.0),
                random.uniform(-1.0, 1.0),
                random.uniform(0.0, 1.0),
            )
            cmd_sock.sendto(msg, ("127.0.0.1", args.cmd_port))

            # Wait for /sys/metrics reply
            try:
                data, _ = state_sock.recvfrom(4096)
                parsed = osc_parse_message(data)
                if parsed["address"] == "/sys/metrics":
                    arg_dict = {k: v for k, v in parsed["args"]}
                    t0_us = arg_dict.get("t0_us")
                    t1_us = arg_dict.get("t1_us")
                    if t0_us is not None and t1_us is not None:
                        delta_us = float(t1_us) - float(t0_us)
                        if delta_us > 0:
                            samples.append(delta_us)
            except socket.timeout:
                pass

            time.sleep(0.1)  # ~10 Hz injection rate
    finally:
        cmd_sock.close()
        state_sock.close()

    return samples


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_result(samples: list, args, sysinfo: dict) -> dict:
    sorted_s = sorted(samples)
    n = len(sorted_s)
    p50 = percentile(sorted_s, 50) if n else 0.0
    p95 = percentile(sorted_s, 95) if n else 0.0
    p99 = percentile(sorted_s, 99) if n else 0.0

    return {
        "kernel": sysinfo["kernel"],
        "governor": sysinfo["governor"],
        "jack_period_frames": 64,
        "sample_rate_hz": 48000,
        "dante_driver_version": "unknown",
        "audio_interface": "unknown",
        "machine_model": sysinfo["machine_model"],
        "preempt_rt": sysinfo["preempt_rt"],
        "num_objects": args.objects,
        "n_samples": n,
        "p50_us": round(p50, 2),
        "p95_us": round(p95, 2),
        "p99_us": round(p99, 2),
        "pass": bool(p99 < args.threshold_us),
        "threshold_us": args.threshold_us,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "stages": "2-6 (T0=OSC_recv, T1=Dante_PCIe_write)",
    }


def main():
    parser = argparse.ArgumentParser(description="Spatial Engine latency harness (stages 2-6)")
    parser.add_argument("--duration", type=float, default=3600.0, help="Measurement duration in seconds")
    parser.add_argument("--objects", type=int, default=8, help="Number of active spatial objects")
    parser.add_argument("--cmd-port", type=int, default=9100, help="OSC command port")
    parser.add_argument("--state-port", type=int, default=9101, help="OSC state/metrics reply port")
    parser.add_argument("--output", type=str, default="baseline.json", help="Output JSON path")
    parser.add_argument("--threshold-us", type=int, default=5000, help="p99 pass threshold in microseconds")
    parser.add_argument("--dry-run", action="store_true", help="Simulate without hardware (synthetic samples)")
    args = parser.parse_args()

    sysinfo = gather_system_info()

    if args.dry_run:
        # ~10 samples/sec * duration, capped at 5000 for speed
        n_samples = min(int(args.duration * 10), 5000)
        print(f"[dry-run] Generating {n_samples} synthetic latency samples (lognormal ~2ms mean)")
        samples = generate_synthetic_samples(n_samples)
    else:
        print(f"[live] Starting {args.duration}s measurement with {args.objects} objects")
        print(f"       cmd={args.cmd_port}  state={args.state_port}")
        samples = run_live(args)

    result = build_result(samples, args, sysinfo)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2))

    print(f"[result] n={result['n_samples']}  p50={result['p50_us']}us  "
          f"p95={result['p95_us']}us  p99={result['p99_us']}us  "
          f"pass={result['pass']}")
    print(f"[output] {out_path}")


if __name__ == "__main__":
    main()
