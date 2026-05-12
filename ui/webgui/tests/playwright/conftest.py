"""Pytest fixtures for the playwright harness.

Provides:

* ``uvicorn_server`` — subprocess running ``ui.webgui.server:app`` on a
  random loopback port; ``/health`` polled until ready; SIGTERM on
  teardown. Module-scoped so a single server services every test in a
  module (warmup cost paid once).
* ``browser`` / ``context`` / ``page`` — async-playwright browser /
  context / page, headless chromium. Skips automatically if chromium
  binaries are not installed (graceful degradation; see
  ``README.md``).
"""
from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterator

import pytest

# ---------------------------------------------------------------------------
# Marker registration — keep `pytest --strict-markers` happy.
# ---------------------------------------------------------------------------

def pytest_configure(config: pytest.Config) -> None:  # noqa: D401
    config.addinivalue_line(
        "markers",
        "playwright: browser-based fps/coord/concurrency harness (slow). "
        "Requires `playwright` + `chromium`. Skipped if either is missing.",
    )


# ---------------------------------------------------------------------------
# Repo root — anchor for subprocess cwd.
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[4]


def _free_port() -> int:
    """Reserve a random free TCP port on loopback.

    Sets ``SO_REUSEADDR`` so the brief TIME_WAIT window between this
    probe-bind and the subsequent uvicorn bind does not race against the
    ephemeral-port allocator (TOCTOU narrowing — does not eliminate the
    race, but combined with the health-poll retry below it is sufficient).
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


@pytest.fixture(scope="module")
def uvicorn_server() -> Iterator[str]:
    """Spawn ``uvicorn ui.webgui.server:app`` on a random port.

    Yields the base URL (``http://127.0.0.1:PORT``).
    """
    import requests  # local import — only needed when fixture is used.

    env = os.environ.copy()
    # Force unbuffered output so failures surface in CI logs promptly.
    env["PYTHONUNBUFFERED"] = "1"
    # Mirror pytest.ini `pythonpath = . ui ui/webgui bridge` so the
    # uvicorn subprocess can import `spatial_engine_ui`, `ui.webgui.*`,
    # and `bridge.*` exactly like the pytest harness does.
    extra_paths = [
        str(REPO_ROOT),
        str(REPO_ROOT / "ui"),
        str(REPO_ROOT / "ui" / "webgui"),
        str(REPO_ROOT / "bridge"),
    ]
    existing_pp = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = os.pathsep.join(
        [p for p in extra_paths if p] + ([existing_pp] if existing_pp else [])
    )

    # Retry the port-allocation + uvicorn-spawn up to 3 times to cover the
    # _free_port TOCTOU window (another process can grab the port between
    # `s.close()` and the uvicorn subprocess `bind()`).
    proc = None
    port = -1
    last_output = b""
    for attempt in range(3):
        port = _free_port()
        proc = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "uvicorn",
                "ui.webgui.server:app",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--log-level",
                "warning",
            ],
            cwd=str(REPO_ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        # Quick sanity probe — give uvicorn 0.3s to fail fast on bind error.
        time.sleep(0.3)
        if proc.poll() is None:
            break
        last_output, _ = proc.communicate(timeout=1)
        if b"Address already in use" not in last_output:
            raise RuntimeError(
                f"uvicorn exited early on attempt {attempt + 1} "
                f"(rc={proc.returncode}). Output:\n"
                f"{last_output.decode(errors='replace')}"
            )
    else:
        raise RuntimeError(
            "uvicorn could not bind a free port after 3 attempts. "
            f"Last output:\n{last_output.decode(errors='replace')}"
        )
    assert proc is not None
    base_url = f"http://127.0.0.1:{port}"

    # Health-poll: 30 × 0.2s = 6s max.
    ready = False
    for _ in range(30):
        if proc.poll() is not None:
            # Process exited before /health became reachable.
            out, _ = proc.communicate(timeout=1)
            raise RuntimeError(
                f"uvicorn exited early (rc={proc.returncode}). "
                f"Output:\n{out.decode(errors='replace')}"
            )
        try:
            r = requests.get(f"{base_url}/health", timeout=0.5)
            if r.status_code == 200:
                ready = True
                break
        except Exception:
            time.sleep(0.2)
    if not ready:
        proc.terminate()
        try:
            out, _ = proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            out = b"(killed)"
        raise RuntimeError(
            f"uvicorn did not become ready on {base_url}. "
            f"Output:\n{out.decode(errors='replace')}"
        )

    try:
        yield base_url
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)
