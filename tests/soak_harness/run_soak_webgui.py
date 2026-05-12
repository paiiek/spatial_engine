"""S5 — 48h soak harness for the WebGUI domain (G7 governance).

Brings up:
  * uvicorn ``ui.webgui.server:app`` on a free port (subprocess, env-gated
    ``SPE_DEBUG_ENDPOINTS=1``).
  * Mock OSC sink on UDP 9100 (counts recv drops while uvicorn is alive).
  * 60 Hz WebSocket drag synthesiser client (asyncio task) — exercises
    ``/adm/obj/<n>/aed`` send path.

Samples 1 Hz:
  * Process RSS (psutil ``memory_info().rss``).
  * Process fd count (psutil ``num_fds`` — Linux/macOS).
  * asyncio task count via ``GET /api/_debug/asyncio_tasks`` (server-side
    introspection; harness cannot probe child process loops directly).

Fault injection: ``--reconnect-interval`` seconds, default 300 (= 5 min).
  Sends SIGKILL to uvicorn, restarts on the same port, WS client reconnects
  automatically. Counters reported as ``fault_injections`` and ``reconnects``.

Output: JSON report (default ``/tmp/soak_webgui_<UTC>.json`` or
``--report-path``). Schema is asserted by
``tests/soak_harness/test_soak_webgui_schema.py``.

Dry-run: ``--duration 600`` (10 min) — sentinel mechanism check before the
48 h wall-clock run.

48 h wall-clock invocation lives in
``tests/soak_harness/SOAK_WEBGUI_README.md`` (user queue — this phase only
implements + dry-runs).
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import datetime
import json
import logging
import math
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Optional

logger = logging.getLogger("soak_webgui")


# ---------------------------------------------------------------------------
# Optional deps — psutil for RSS/fd, websockets for drag client
# ---------------------------------------------------------------------------

try:
    import psutil
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False

try:
    import websockets
    WEBSOCKETS_AVAILABLE = True
except ImportError:
    WEBSOCKETS_AVAILABLE = False

try:
    import requests
    REQUESTS_AVAILABLE = True
except ImportError:
    REQUESTS_AVAILABLE = False


# ---------------------------------------------------------------------------
# uvicorn subprocess helpers
# ---------------------------------------------------------------------------

def _free_port() -> int:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def _start_uvicorn(
    repo_root: Path,
    port: int,
    env_overrides: Optional[dict] = None,
    boot_timeout_s: float = 15.0,
) -> subprocess.Popen:
    """Spawn ``uvicorn ui.webgui.server:app`` and block until ``/health`` is up.

    Caller is responsible for terminating the returned ``Popen``.
    """
    env = os.environ.copy()
    env["SPE_DEBUG_ENDPOINTS"] = "1"  # enable /api/_debug/asyncio_tasks
    env["PYTHONUNBUFFERED"] = "1"
    # Mirror pytest.ini ``pythonpath = . ui ui/webgui bridge`` so the
    # uvicorn subprocess can import ``spatial_engine_ui``, ``ui.webgui.*``,
    # and ``bridge.*`` from the source tree (cf.
    # ui/webgui/tests/playwright/conftest.py).
    extra_paths = [
        str(repo_root),
        str(repo_root / "ui"),
        str(repo_root / "ui" / "webgui"),
        str(repo_root / "bridge"),
    ]
    existing_pp = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = os.pathsep.join(
        [p for p in extra_paths if p]
        + ([existing_pp] if existing_pp else [])
    )
    if env_overrides:
        env.update(env_overrides)

    cmd = [
        sys.executable, "-m", "uvicorn",
        "ui.webgui.server:app",
        "--host", "127.0.0.1",
        "--port", str(port),
        "--log-level", "warning",
    ]
    proc = subprocess.Popen(
        cmd,
        cwd=str(repo_root),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Health-check loop
    deadline = time.time() + boot_timeout_s
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(
                f"uvicorn exited during boot (rc={proc.returncode})"
            )
        if REQUESTS_AVAILABLE:
            try:
                r = requests.get(
                    f"http://127.0.0.1:{port}/health", timeout=0.5
                )
                if r.status_code == 200:
                    return proc
            except Exception:
                pass
        time.sleep(0.2)

    # Boot failed — clean up
    proc.kill()
    try:
        proc.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        pass
    raise RuntimeError("uvicorn did not become healthy in time")


def _stop_uvicorn(proc: subprocess.Popen, sig: int = signal.SIGTERM) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(sig)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            pass


# ---------------------------------------------------------------------------
# Mock OSC 9100 sink — counts inbound UDP datagrams from osc_bridge
# ---------------------------------------------------------------------------

class OscSink:
    """Bind UDP 9100, drain datagrams in a background thread, count them."""

    def __init__(self, port: int = 9100) -> None:
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.recv_count = 0
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", self.port))
        self.sock.settimeout(0.5)
        self._thread = threading.Thread(target=self._loop, daemon=True,
                                        name="osc-sink-9100")
        self._thread.start()

    def _loop(self) -> None:
        assert self.sock is not None
        while not self._stop.is_set():
            try:
                self.sock.recvfrom(8192)
                self.recv_count += 1
            except socket.timeout:
                continue
            except OSError:
                break

    def close(self) -> None:
        self._stop.set()
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        if self._thread is not None:
            self._thread.join(timeout=1.5)


# ---------------------------------------------------------------------------
# 60 Hz WebSocket drag synthesiser client (async, with reconnect loop)
# ---------------------------------------------------------------------------

async def _drag_client(
    port_ref: dict,
    duration_s: float,
    counters: dict,
    stop_flag: dict,
) -> None:
    """Persistent WS drag client.

    ``port_ref`` is a single-key dict ``{"port": int}`` so the fault-injector
    can mutate the target port mid-flight (the uvicorn restart picks a new
    port). ``stop_flag`` is ``{"stop": bool}``.
    """
    if not WEBSOCKETS_AVAILABLE:
        counters["sent"] = 0
        counters["received"] = 0
        counters["reconnects"] = 0
        counters["last_error"] = "websockets module unavailable"
        # Idle wait so the duration still matches.
        end = time.monotonic() + duration_s
        while time.monotonic() < end and not stop_flag["stop"]:
            await asyncio.sleep(0.5)
        return

    counters.setdefault("sent", 0)
    counters.setdefault("received", 0)
    counters.setdefault("reconnects", 0)
    counters.setdefault("last_error", None)
    start = time.monotonic()
    period = 1.0 / 60.0

    while time.monotonic() - start < duration_s and not stop_flag["stop"]:
        port = port_ref["port"]
        try:
            async with websockets.connect(
                f"ws://127.0.0.1:{port}/ws",
                open_timeout=5,
                ping_interval=20,
                ping_timeout=10,
                close_timeout=2,
            ) as ws:
                while (
                    time.monotonic() - start < duration_s
                    and not stop_flag["stop"]
                ):
                    if port_ref["port"] != port:
                        # uvicorn restarted on new port → drop & reconnect.
                        break
                    angle = (time.monotonic() - start) * 2 * math.pi * 0.5
                    msg = json.dumps({
                        "type": "obj_pos",
                        "n": 1,
                        "azim": math.sin(angle) * 180.0,
                        "elev": 0.0,
                        "dist": 0.5,
                    })
                    try:
                        await ws.send(msg)
                        counters["sent"] += 1
                    except Exception as exc:
                        counters["last_error"] = repr(exc)
                        break
                    # Best-effort drain — avoid backpressure on long soaks.
                    try:
                        await asyncio.wait_for(ws.recv(), timeout=0.001)
                        counters["received"] += 1
                    except asyncio.TimeoutError:
                        pass
                    except Exception as exc:
                        counters["last_error"] = repr(exc)
                        break
                    await asyncio.sleep(period)
        except Exception as exc:
            counters["last_error"] = repr(exc)
        if time.monotonic() - start < duration_s and not stop_flag["stop"]:
            counters["reconnects"] += 1
            await asyncio.sleep(0.5)


# ---------------------------------------------------------------------------
# Async sampler — RSS / fd / asyncio task count
# ---------------------------------------------------------------------------

async def _sampler(
    proc_ref: dict,
    port_ref: dict,
    duration_s: float,
    report: dict,
    interval_s: float,
    stop_flag: dict,
) -> None:
    rss_series: list[dict] = []
    fd_series: list[dict] = []
    asyncio_series: list[dict] = []

    start = time.monotonic()
    while time.monotonic() - start < duration_s and not stop_flag["stop"]:
        t_rel = time.monotonic() - start
        proc = proc_ref["proc"]
        port = port_ref["port"]

        rss_mb: Optional[float] = None
        fd_count: Optional[int] = None
        if PSUTIL_AVAILABLE and proc is not None and proc.poll() is None:
            try:
                p = psutil.Process(proc.pid)
                rss_mb = p.memory_info().rss / 1024.0 / 1024.0
                try:
                    fd_count = p.num_fds()
                except (AttributeError, psutil.AccessDenied):
                    fd_count = None
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

        if rss_mb is not None:
            rss_series.append({"t": round(t_rel, 3),
                               "rss_mb": round(rss_mb, 3)})
        if fd_count is not None:
            fd_series.append({"t": round(t_rel, 3), "fd": int(fd_count)})

        # asyncio task count via env-gated debug endpoint
        n_tasks: Optional[int] = None
        if REQUESTS_AVAILABLE and proc is not None and proc.poll() is None:
            try:
                r = requests.get(
                    f"http://127.0.0.1:{port}/api/_debug/asyncio_tasks",
                    timeout=0.5,
                )
                if r.status_code == 200:
                    j = r.json()
                    if j.get("ok") is True:
                        n_tasks = int(j["n_tasks"])
            except Exception:
                pass
        if n_tasks is not None:
            asyncio_series.append({"t": round(t_rel, 3),
                                   "n_tasks": n_tasks})

        await asyncio.sleep(interval_s)

    report["rss_series"] = rss_series
    report["fd_series"] = fd_series
    report["asyncio_series"] = asyncio_series


# ---------------------------------------------------------------------------
# Fault injector — SIGKILL uvicorn at fixed interval, restart
# ---------------------------------------------------------------------------

async def _fault_injector(
    repo_root: Path,
    proc_ref: dict,
    port_ref: dict,
    duration_s: float,
    interval_s: float,
    report: dict,
    stop_flag: dict,
) -> None:
    n = 0
    start = time.monotonic()
    while (
        time.monotonic() - start < duration_s
        and interval_s > 0
        and not stop_flag["stop"]
    ):
        # Sleep until next injection or end
        slept = 0.0
        while (
            slept < interval_s
            and time.monotonic() - start < duration_s
            and not stop_flag["stop"]
        ):
            await asyncio.sleep(min(1.0, interval_s - slept))
            slept += 1.0
        if (
            time.monotonic() - start >= duration_s
            or stop_flag["stop"]
        ):
            break

        proc = proc_ref["proc"]
        if proc is None or proc.poll() is not None:
            continue

        # SIGKILL + wait, then restart on a NEW free port so the WS client
        # reconnect path is exercised even when the previous port is in
        # TIME_WAIT.
        try:
            proc.send_signal(signal.SIGKILL)
            n += 1
            try:
                proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                pass
        except ProcessLookupError:
            pass

        try:
            new_port = _free_port()
            new_proc = await asyncio.get_event_loop().run_in_executor(
                None, _start_uvicorn, repo_root, new_port
            )
            proc_ref["proc"] = new_proc
            port_ref["port"] = new_port
            # Restore low_latency mode (default is ``ai`` per ADR 0013).
            if REQUESTS_AVAILABLE:
                try:
                    requests.post(
                        f"http://127.0.0.1:{new_port}/api/mode?mode=low_latency",
                        timeout=2.0,
                    )
                except Exception:
                    pass
        except Exception as exc:
            report["fault_restart_errors"] = report.get(
                "fault_restart_errors", []
            ) + [repr(exc)]
            # If the restart failed, stop further injections.
            break

    report["fault_injections"] = n


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------

def _resolve_repo_root() -> Path:
    """Locate the repo root (containing ``ui/webgui/server.py``)."""
    here = Path(__file__).resolve()
    for p in [here.parent.parent.parent, here.parent.parent, here.parent]:
        if (p / "ui" / "webgui" / "server.py").exists():
            return p
    raise RuntimeError("repo root not found")


async def _run(args: argparse.Namespace) -> dict:
    repo_root = _resolve_repo_root()
    start_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    start_mono = time.monotonic()

    counters: dict = {
        "sent": 0,
        "received": 0,
        "reconnects": 0,
        "last_error": None,
    }
    report: dict = {
        "schema_version": 1,
        "domain": "webgui",
        "duration_s": args.duration,
        "reconnect_interval_s": args.reconnect_interval,
        "sample_interval_hz": args.sample_interval_hz,
        "timestamp_utc_start": start_utc,
        "rss_series": [],
        "fd_series": [],
        "asyncio_series": [],
        "fault_injections": 0,
        "ws_counters": counters,
        "osc_sink_recv_count": 0,
        "dry_run": bool(args.dry_run),
    }

    # 1. OSC sink (UDP 9100). Bind FIRST so uvicorn / osc_bridge's
    #    SimpleUDPClient send never hits ICMP port-unreachable.
    sink = OscSink(port=args.osc_sink_port)
    try:
        sink.start()
    except OSError as exc:
        report["fatal_error"] = f"osc sink bind failed: {exc!r}"
        return report

    proc: Optional[subprocess.Popen] = None
    try:
        # 2. uvicorn boot
        try:
            port = _free_port()
            proc = _start_uvicorn(repo_root, port)
        except Exception as exc:
            report["fatal_error"] = f"uvicorn boot failed: {exc!r}"
            return report

        proc_ref = {"proc": proc}
        port_ref = {"port": port}
        stop_flag = {"stop": False}

        # Switch into ``low_latency`` mode so the soak actually exercises the
        # 9100 wire. In ``ai`` mode (default) WebGUI suppresses obj_pos per
        # ADR 0013 — useful in production where vid2spatial owns 9100, but
        # in the soak we are simulating WebGUI-as-sole-producer.
        if REQUESTS_AVAILABLE:
            try:
                requests.post(
                    f"http://127.0.0.1:{port}/api/mode?mode=low_latency",
                    timeout=2.0,
                )
            except Exception as exc:
                report.setdefault("warnings", []).append(
                    f"mode=low_latency switch failed: {exc!r}"
                )

        # 3. Tasks
        sample_period = 1.0 / max(args.sample_interval_hz, 1)
        tasks = [
            asyncio.create_task(
                _drag_client(port_ref, args.duration, counters, stop_flag),
                name="drag_client",
            ),
            asyncio.create_task(
                _sampler(proc_ref, port_ref, args.duration, report,
                         sample_period, stop_flag),
                name="sampler",
            ),
            asyncio.create_task(
                _fault_injector(repo_root, proc_ref, port_ref,
                                args.duration, args.reconnect_interval,
                                report, stop_flag),
                name="fault_injector",
            ),
        ]

        try:
            await asyncio.gather(*tasks)
        except Exception as exc:
            report["task_error"] = repr(exc)
        finally:
            stop_flag["stop"] = True
            for t in tasks:
                if not t.done():
                    t.cancel()
            for t in tasks:
                with contextlib.suppress(Exception):
                    await t

        proc = proc_ref["proc"]
    finally:
        if proc is not None:
            _stop_uvicorn(proc)
        sink.close()
        report["osc_sink_recv_count"] = sink.recv_count
        report["duration_actual_s"] = round(
            time.monotonic() - start_mono, 3
        )
        report["timestamp_utc_end"] = datetime.datetime.now(
            datetime.timezone.utc
        ).isoformat()

    return report


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="WebGUI 48h soak harness (G7).")
    p.add_argument("--duration", type=int, default=172800,
                   help="Soak duration in seconds (default 172800 = 48h). "
                        "Use 600 for dry-run.")
    p.add_argument("--report-path", default=None,
                   help="Output JSON path (default /tmp/soak_webgui_<ts>.json)")
    p.add_argument("--reconnect-interval", type=int, default=300,
                   help="Seconds between SIGKILL fault injections (default 300).")
    p.add_argument("--sample-interval-hz", type=int, default=1,
                   help="psutil/asyncio sampling rate (default 1 Hz).")
    p.add_argument("--osc-sink-port", type=int, default=9100,
                   help="Mock OSC sink port (default 9100, matches osc_bridge).")
    p.add_argument("--dry-run", action="store_true",
                   help="Tag report as a sentinel-validation run.")
    p.add_argument(
        "--threshold-rss-mb-h", type=float, default=None,
        help="Optional fixed day-2 RSS slope threshold (MB/h). When unset "
             "the harness records only and external sentinels evaluate.",
    )
    return p


def main(argv: Optional[list[str]] = None) -> int:
    logging.basicConfig(
        level=os.environ.get("SOAK_LOG_LEVEL", "INFO"),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    args = build_parser().parse_args(argv)

    if args.report_path is None:
        ts = datetime.datetime.now(datetime.timezone.utc).strftime(
            "%Y%m%dT%H%M%SZ"
        )
        args.report_path = f"/tmp/soak_webgui_{ts}.json"

    report = asyncio.run(_run(args))

    if args.threshold_rss_mb_h is not None:
        report["threshold_rss_mb_h_input"] = args.threshold_rss_mb_h

    out = Path(args.report_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2))

    n_rss = len(report.get("rss_series", []))
    n_fd = len(report.get("fd_series", []))
    n_async = len(report.get("asyncio_series", []))
    sent = report["ws_counters"].get("sent", 0)
    reconnects = report["ws_counters"].get("reconnects", 0)
    print(
        f"soak_webgui done: report={out} "
        f"duration_actual={report.get('duration_actual_s')}s "
        f"rss_samples={n_rss} fd_samples={n_fd} "
        f"asyncio_samples={n_async} "
        f"ws_sent={sent} reconnects={reconnects} "
        f"fault_injections={report.get('fault_injections', 0)} "
        f"osc_recv={report.get('osc_sink_recv_count', 0)}"
    )
    if "fatal_error" in report:
        print(f"FATAL: {report['fatal_error']}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
