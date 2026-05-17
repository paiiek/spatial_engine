"""
v0.5.1 Q1 — pytest fixture exercising the OSC outbound warning channel.

Spawns the standalone spatial_engine_core with the test-only
`--force-probe` and `--emit-no-sofa-after-ms` flags, opens a loopback
listener, exchanges a handshake (so the engine captures the peer
endpoint), then asserts BOTH /sys/binaural_warning codes arrive within
500 ms of the emit deadline:

  * /sys/binaural_warning ,sf "ambivs_disabled_cpu" <throughput>
    — emitted by SpatialEngine::triggerBinauralProbe() after a B2 probe
      clamps the mode to Direct. We force this on the standalone by
      passing `--binaural-sofa <fixture>` + `--force-probe`.

  * /sys/binaural_warning ,s "no_sofa_loaded"
    — emitted via OSCBackend::sendReply() from the standalone's
      `--emit-no-sofa-after-ms` path (matches what the VST3
      first-render latch does).

These two emissions are FORCED by the standalone CLI so the wire surface
is observable from pytest. The same code paths are exercised in the
VST3 layer at runtime; the C++ unit tests
(test_osc_outbound_reply_smoke + test_binaural_probe_warning_emission)
cover the structural plumbing independently.
"""

from __future__ import annotations

import os
import socket
import struct
import subprocess
import time
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
ENGINE_BIN_CANDIDATES = [
    REPO_ROOT / "core" / "build" / "spatial_engine_core",
    REPO_ROOT / "build" / "core" / "spatial_engine_core",
    REPO_ROOT / "build_off" / "core" / "spatial_engine_core",
]
SOFA_FIXTURE = REPO_ROOT / "core" / "tests" / "fixtures" / "synthetic_min.speh"


def find_engine() -> Path | None:
    for p in ENGINE_BIN_CANDIDATES:
        if p.exists() and os.access(p, os.X_OK):
            return p
    return None


def free_udp_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _osc_pad(s: bytes) -> bytes:
    pad = 4 - (len(s) % 4)
    return s + b"\x00" * pad


def build_osc_packet(addr: str, type_tag: str, *args) -> bytes:
    out = bytearray()
    out += _osc_pad(addr.encode("ascii") + b"\x00")
    out += _osc_pad(type_tag.encode("ascii") + b"\x00")
    arg_i = 0
    for t in type_tag[1:]:
        v = args[arg_i]
        arg_i += 1
        if t == "i":
            out += struct.pack(">i", int(v))
        elif t == "f":
            out += struct.pack(">f", float(v))
        elif t == "s":
            out += _osc_pad(v.encode("ascii") + b"\x00")
        else:
            raise ValueError(f"unsupported OSC type tag '{t}'")
    return bytes(out)


def parse_osc_address(pkt: bytes) -> str:
    nul = pkt.find(b"\x00")
    if nul < 0:
        return ""
    return pkt[:nul].decode("ascii", errors="replace")


def parse_osc_type_tag(pkt: bytes) -> str:
    """Returns the type tag string ("" if malformed)."""
    nul = pkt.find(b"\x00")
    if nul < 0:
        return ""
    # advance to type-tag string (4-byte aligned past address+null)
    addr_end = nul + (4 - (nul % 4))
    rest = pkt[addr_end:]
    nul2 = rest.find(b"\x00")
    if nul2 < 0:
        return ""
    return rest[:nul2].decode("ascii", errors="replace")


@pytest.fixture(scope="module")
def engine_binary():
    bin_path = find_engine()
    if not bin_path:
        pytest.skip("spatial_engine_core binary not built (run cmake + make first)")
    return bin_path


@pytest.fixture
def warning_capture(engine_binary):
    """
    Spawn engine with --force-probe + --emit-no-sofa-after-ms, open a
    listener, send handshake, return the listener socket and the engine
    process for the test to read replies from.
    """
    osc_port = free_udp_port()
    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.bind(("127.0.0.1", 0))
    client.settimeout(1.0)

    # Test-only flags: emit both warnings 500 ms after start. The pytest
    # body sleeps 300 ms before draining so the handshake lands first.
    cmd = [
        str(engine_binary),
        "--osc-port", str(osc_port),
        "--seconds", "5",
        "--block", "64",
        "--channels", "8",
        "--rate", "48000",
        "--binaural-sofa", str(SOFA_FIXTURE),
        "--binaural-enable",
        # Force a synthetic low throughput so the probe path emits
        # `/sys/binaural_warning ,sf "ambivs_disabled_cpu" 0.5`
        # deterministically (production HW would pass the real probe).
        "--inject-probe-throughput", "0.5",
        "--emit-no-sofa-after-ms", "500",
    ]
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(REPO_ROOT / "core" / "build"),
    )

    # Wait for OSC listener bind, then send handshake (captures peer).
    time.sleep(0.2)
    client.sendto(
        build_osc_packet("/sys/handshake", ",i", 1),
        ("127.0.0.1", osc_port),
    )
    # Brief settle so the engine has time to capture the peer endpoint
    # via recvfrom() before it emits.
    time.sleep(0.05)

    yield (proc, client, osc_port)

    try:
        proc.terminate()
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)
    finally:
        client.close()


class TestBothWarningCodesObserved:
    def test_both_codes_within_500ms(self, warning_capture):
        proc, client, _port = warning_capture
        # Drain replies for up to 2 s (well within the 500 ms emit
        # deadline + handshake settle). We stop early when both codes
        # have been seen.
        seen_ambivs = False
        seen_no_sofa = False
        deadline = time.monotonic() + 2.0
        emit_observed_at = None
        while time.monotonic() < deadline and not (seen_ambivs and seen_no_sofa):
            try:
                pkt, _src = client.recvfrom(1024)
            except socket.timeout:
                continue
            addr = parse_osc_address(pkt)
            if addr != "/sys/binaural_warning":
                continue
            tag = parse_osc_type_tag(pkt)
            if emit_observed_at is None:
                emit_observed_at = time.monotonic()
            if tag == ",sf" and b"ambivs_disabled_cpu" in pkt:
                seen_ambivs = True
            elif tag == ",s" and b"no_sofa_loaded" in pkt:
                seen_no_sofa = True

        # Surface stderr only on failure to keep healthy runs quiet.
        if not (seen_ambivs and seen_no_sofa):
            try:
                stderr = proc.stderr.read1(8192).decode("utf-8", errors="replace")
            except Exception:
                stderr = "<unreadable>"
            pytest.fail(
                f"seen ambivs_disabled_cpu={seen_ambivs} "
                f"no_sofa_loaded={seen_no_sofa}; engine stderr:\n{stderr}"
            )
        # All emissions must arrive within 500 ms once the first one
        # appears (cheap defence against a runaway IO drain).
        assert emit_observed_at is not None
        elapsed_ms = (time.monotonic() - emit_observed_at) * 1000
        assert elapsed_ms < 500, (
            f"emissions spread over {elapsed_ms:.1f}ms — should be < 500ms"
        )
