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
    """Reserve a random free TCP port on loopback."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
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

    port = _free_port()
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
