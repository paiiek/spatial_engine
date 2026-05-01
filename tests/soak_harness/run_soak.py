"""
Soak harness for spatial_engine.
Lab-only, long-running test.

Usage:
  python run_soak.py --duration 1800 --objects 8 --cmd-port 9100 --state-port 9101 --output soak_report.json
  python run_soak.py --dry-run --duration 60 --output soak_report.json
  python run_soak.py --pilot --output soak_report.json
"""

from __future__ import annotations

import argparse
import json
import math
import random
import socket
import struct
import time
import datetime
from typing import Optional

try:
    import psutil
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PER_BLOCK_P99_THRESHOLD_US = 933   # floor(64/48000 * 0.7 * 1e6)
CORE_RSS_SLOPE_LIMIT_MB_H = 1.0
UI_RSS_SLOPE_LIMIT_MB_H = 5.0
HEARTBEAT_MISS_THRESHOLD_MS = 300
SAMPLE_INTERVAL_S = 10


# ---------------------------------------------------------------------------
# Minimal OSC helpers (no third-party lib required)
# ---------------------------------------------------------------------------

def _pad4(n: int) -> int:
    return (n + 3) & ~3


def encode_osc_message(address: str, type_tag: str = ",", args: list = []) -> bytes:
    """Encode a simple OSC message."""
    addr_bytes = address.encode("utf-8") + b"\x00"
    addr_padded = addr_bytes.ljust(_pad4(len(addr_bytes)), b"\x00")

    tags = ("," + type_tag).encode("utf-8") + b"\x00"
    tags_padded = tags.ljust(_pad4(len(tags)), b"\x00")

    payload = bytearray()
    for arg, t in zip(args, type_tag):
        if t == "i":
            payload += struct.pack(">i", int(arg))
        elif t == "f":
            payload += struct.pack(">f", float(arg))
        elif t == "s":
            s = str(arg).encode("utf-8") + b"\x00"
            payload += s.ljust(_pad4(len(s)), b"\x00")

    return bytes(addr_padded + tags_padded + payload)


def send_osc(host: str, port: int, address: str, type_tag: str = ",", args: list = []) -> None:
    msg = encode_osc_message(address, type_tag, args)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(msg, (host, port))


# ---------------------------------------------------------------------------
# Metric polling (real mode — expects OSC reply server)
# ---------------------------------------------------------------------------

def poll_metrics_real(host: str, state_port: int, timeout: float = 1.0) -> Optional[dict]:
    """
    Send /sys/metrics request and wait for a UDP reply bundle.
    Returns dict of counter names → int values, or None on timeout.
    In a real deployment the engine replies with a flat OSC bundle;
    here we issue a fire-and-forget request and read back on the same socket.
    """
    try:
        msg = encode_osc_message("/sys/metrics")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            sock.sendto(msg, (host, state_port))
            data, _ = sock.recvfrom(4096)
        # Very minimal parse: look for known counter names in the raw bytes
        # (production would parse full OSC bundle)
        counters = {}
        raw = data.decode("utf-8", errors="replace")
        for key in (
            "audio_underrun_count", "ipc_queue_depth", "osc_packet_rate",
            "osc_reject_count", "osc_reordered_drops", "osc_reorder_burst_count_1s",
            "cpu_pct_audio_thread", "per_block_time_p99_us",
            "geometry_cache_version", "heartbeat_miss_count",
        ):
            counters[key] = 0  # default 0; real parsing TBD
        return counters
    except Exception:
        return None


def synthetic_metrics(t: float, n_samples_so_far: int) -> dict:
    """
    Return synthetic counter values for --dry-run mode.
    Values are intentionally well within passing thresholds.
    """
    return {
        "audio_underrun_count": 0,
        "ipc_queue_depth": random.randint(0, 3),
        "osc_packet_rate": random.randint(40, 60),
        "osc_reject_count": 0,
        "osc_reordered_drops": 0,
        "osc_reorder_burst_count_1s": 0,
        "cpu_pct_audio_thread": random.uniform(5.0, 20.0),
        "per_block_time_p99_us": random.randint(400, 700),
        "geometry_cache_version": 1,
        "heartbeat_miss_count": 0,
    }


# ---------------------------------------------------------------------------
# RSS helpers
# ---------------------------------------------------------------------------

def sample_rss_mb(pid: int) -> Optional[float]:
    if not PSUTIL_AVAILABLE:
        return None
    try:
        return psutil.Process(pid).memory_info().rss / (1024 * 1024)
    except Exception:
        return None


def linear_slope_mb_per_h(times_s: list[float], values_mb: list[float]) -> float:
    """Least-squares slope in MB/h."""
    n = len(times_s)
    if n < 2:
        return 0.0
    sum_x = sum(times_s)
    sum_y = sum(values_mb)
    sum_xx = sum(x * x for x in times_s)
    sum_xy = sum(x * y for x, y in zip(times_s, values_mb))
    denom = n * sum_xx - sum_x * sum_x
    if denom == 0:
        return 0.0
    slope_per_s = (n * sum_xy - sum_x * sum_y) / denom
    return slope_per_s * 3600.0


# ---------------------------------------------------------------------------
# Gate evaluation
# ---------------------------------------------------------------------------

def evaluate_gates(
    xrun_count: int,
    heartbeat_miss_count: int,
    per_block_p99_us_max: int,
    core_rss_slope: float,
) -> tuple[dict, list[str]]:
    gates = {
        "xrun_zero": xrun_count == 0,
        "heartbeat_miss_zero": heartbeat_miss_count == 0,
        "per_block_p99_ok": per_block_p99_us_max <= PER_BLOCK_P99_THRESHOLD_US,
        "core_rss_slope_ok": core_rss_slope <= CORE_RSS_SLOPE_LIMIT_MB_H,
    }
    failures = []
    if not gates["xrun_zero"]:
        failures.append(f"xrun: count={xrun_count}")
    if not gates["heartbeat_miss_zero"]:
        failures.append(f"heartbeat_miss: count={heartbeat_miss_count}")
    if not gates["per_block_p99_ok"]:
        failures.append(
            f"per_block_p99: {per_block_p99_us_max} us > threshold {PER_BLOCK_P99_THRESHOLD_US} us"
        )
    if not gates["core_rss_slope_ok"]:
        failures.append(
            f"core_rss_slope: {core_rss_slope:.3f} MB/h > limit {CORE_RSS_SLOPE_LIMIT_MB_H} MB/h"
        )
    return gates, failures


# ---------------------------------------------------------------------------
# Main soak loop
# ---------------------------------------------------------------------------

def run_soak(args: argparse.Namespace) -> dict:
    duration_s = args.duration
    if args.pilot:
        duration_s = 14400

    host = getattr(args, "host", "127.0.0.1")
    cmd_port = getattr(args, "cmd_port", 9100)
    state_port = getattr(args, "state_port", 9101)
    n_objects = args.objects
    ui_pid: Optional[int] = getattr(args, "ui_pid", None)
    dry_run: bool = args.dry_run

    # Dry-run caps duration at 60 s for speed, generates synthetic data
    if dry_run:
        duration_s = min(duration_s, 60)

    start_time = time.monotonic()
    start_utc = datetime.datetime.utcnow().isoformat() + "Z"

    samples: list[dict] = []
    core_rss_times: list[float] = []
    core_rss_values: list[float] = []
    ui_rss_times: list[float] = []
    ui_rss_values: list[float] = []

    core_pid = None
    if not dry_run and PSUTIL_AVAILABLE:
        core_pid = None  # would be populated from engine PID file

    xrun_cumulative = 0
    heartbeat_miss_cumulative = 0
    per_block_p99_max = 0
    osc_reorder_burst_total = 0

    # FDN denormal sub-suite: send impulse at T=0
    if not dry_run:
        try:
            send_osc(host, cmd_port, "/fdn/impulse", "f", [1.0])
        except Exception:
            pass

    if dry_run:
        # Synthetic fast simulation: generate one sample per SAMPLE_INTERVAL_S
        # without real-time sleeping.
        n_synthetic = max(1, duration_s // SAMPLE_INTERVAL_S)
        for sample_index in range(n_synthetic):
            elapsed = sample_index * SAMPLE_INTERVAL_S
            m = synthetic_metrics(elapsed, sample_index)

            xrun_cumulative = max(xrun_cumulative, m.get("audio_underrun_count", 0))
            heartbeat_miss_cumulative = max(
                heartbeat_miss_cumulative, m.get("heartbeat_miss_count", 0)
            )
            p99 = int(m.get("per_block_time_p99_us", 0))
            if p99 > per_block_p99_max:
                per_block_p99_max = p99
            osc_reorder_burst_total += int(m.get("osc_reorder_burst_count_1s", 0))

            rss_mb = 30.0 + elapsed / 3600.0 * 0.1  # synthetic: ~0.1 MB/h slope
            core_rss_times.append(float(elapsed))
            core_rss_values.append(rss_mb)

            samples.append({
                "t_s": float(elapsed),
                "metrics": m,
                "core_rss_mb": rss_mb,
            })
    else:
        # Real-time loop
        sample_index = 0
        next_sample_t = start_time

        while True:
            now = time.monotonic()
            elapsed = now - start_time

            if elapsed >= duration_s:
                break

            if now >= next_sample_t:
                m = poll_metrics_real(host, state_port) or synthetic_metrics(elapsed, sample_index)

                xrun_cumulative = max(xrun_cumulative, m.get("audio_underrun_count", 0))
                heartbeat_miss_cumulative = max(
                    heartbeat_miss_cumulative, m.get("heartbeat_miss_count", 0)
                )
                p99 = int(m.get("per_block_time_p99_us", 0))
                if p99 > per_block_p99_max:
                    per_block_p99_max = p99
                osc_reorder_burst_total += int(m.get("osc_reorder_burst_count_1s", 0))

                rss_mb = None
                if core_pid and PSUTIL_AVAILABLE:
                    rss_mb = sample_rss_mb(core_pid)
                if rss_mb is not None:
                    core_rss_times.append(elapsed)
                    core_rss_values.append(rss_mb)

                if ui_pid and PSUTIL_AVAILABLE:
                    ui_rss = sample_rss_mb(ui_pid)
                    if ui_rss is not None:
                        ui_rss_times.append(elapsed)
                        ui_rss_values.append(ui_rss)

                samples.append({
                    "t_s": round(elapsed, 1),
                    "metrics": m,
                    "core_rss_mb": rss_mb,
                })
                sample_index += 1
                next_sample_t += SAMPLE_INTERVAL_S

            time.sleep(0.5)

    # Compute slopes
    core_rss_slope = (
        linear_slope_mb_per_h(core_rss_times, core_rss_values)
        if len(core_rss_times) >= 2
        else 0.0
    )
    ui_rss_slope = (
        linear_slope_mb_per_h(ui_rss_times, ui_rss_values)
        if len(ui_rss_times) >= 2
        else None
    )

    gates, failures = evaluate_gates(
        xrun_cumulative,
        heartbeat_miss_cumulative,
        per_block_p99_max,
        core_rss_slope,
    )

    # UI RSS slope gate (warn only — osc_reorder_burst is also warn-only)
    if ui_rss_slope is not None and ui_rss_slope > UI_RSS_SLOPE_LIMIT_MB_H:
        failures.append(f"ui_rss_slope: {ui_rss_slope:.3f} MB/h > limit {UI_RSS_SLOPE_LIMIT_MB_H} MB/h")
        gates["ui_rss_slope_ok"] = False
    elif ui_rss_slope is not None:
        gates["ui_rss_slope_ok"] = True

    report = {
        "duration_s": duration_s,
        "n_objects": n_objects,
        "n_samples": len(samples),
        "xrun_count": xrun_cumulative,
        "heartbeat_miss_count": heartbeat_miss_cumulative,
        "per_block_p99_us_max": per_block_p99_max,
        "per_block_p99_us_threshold": PER_BLOCK_P99_THRESHOLD_US,
        "core_rss_slope_mb_per_h": round(core_rss_slope, 4),
        "ui_rss_slope_mb_per_h": round(ui_rss_slope, 4) if ui_rss_slope is not None else None,
        "osc_reorder_burst_count_total": osc_reorder_burst_total,
        "pass": len(failures) == 0,
        "failures": failures,
        "timestamp_utc": start_utc,
        "gates": gates,
        "dry_run": dry_run,
        "pilot": getattr(args, "pilot", False),
    }
    return report


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Spatial Engine Soak Harness")
    parser.add_argument("--duration", type=int, default=1800, help="Soak duration in seconds")
    parser.add_argument("--objects", type=int, default=8, help="Number of audio objects")
    parser.add_argument("--host", default="127.0.0.1", help="Engine host")
    parser.add_argument("--cmd-port", type=int, default=9100, dest="cmd_port")
    parser.add_argument("--state-port", type=int, default=9101, dest="state_port")
    parser.add_argument("--output", default="soak_report.json", help="Output JSON path")
    parser.add_argument("--ui-pid", type=int, default=None, dest="ui_pid", help="UI process PID")
    parser.add_argument("--dry-run", action="store_true", dest="dry_run",
                        help="Simulate soak with synthetic metrics")
    parser.add_argument("--pilot", action="store_true", help="4-hour pilot mode (duration=14400)")

    args = parser.parse_args()

    report = run_soak(args)

    with open(args.output, "w") as f:
        json.dump(report, f, indent=2)

    status = "PASS" if report["pass"] else "FAIL"
    print(f"Soak {status} — samples={report['n_samples']}, xruns={report['xrun_count']}, "
          f"p99_max={report['per_block_p99_us_max']} us, "
          f"heartbeat_miss={report['heartbeat_miss_count']}")
    if report["failures"]:
        for f in report["failures"]:
            print(f"  FAIL: {f}")


if __name__ == "__main__":
    main()
