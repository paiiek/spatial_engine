"""WGUI-S6 — SHA256 wire-bytes comparison helper for RTT e2e tests.

Shared utility for ``test_rtt_*.py`` to:
  1. Build the canonical ADM-OSC datagram for ``/adm/obj/{n}/aed`` via
     ``pythonosc.OscMessageBuilder`` (the same encoder ``SimpleUDPClient``
     uses internally) and SHA256-hash it.
  2. Compare an observed datagram's SHA256 against the expected hash.

Why a separate helper?
  ``test_rtt_e2e_wire.py`` runs 200 samples × per-sample hash check; a
  helper keeps the per-sample inner loop short and lets ``test_rtt_mock.py``
  / ``test_rtt_udp_live.py`` reuse the same expected-bytes builder for
  consistency (so a future pythonosc encoding drift breaks one place, not
  three).
"""
from __future__ import annotations

import hashlib

from pythonosc.osc_message_builder import OscMessageBuilder


def expected_aed_bytes(n: int, azim: float, elev: float, dist: float) -> bytes:
    """Canonical ADM-OSC ``/adm/obj/{n}/aed`` wire datagram.

    Mirrors the encoding used by ``server._dispatch_to_osc`` →
    ``osc_bridge.send_osc`` → ``SimpleUDPClient.send_message`` for the
    ``obj_pos`` WebSocket message type.

    Address pattern length / padding:
      ``/adm/obj/{n}/aed`` is at least 16 chars (n=1..64 produces 16..17 chars
      + NUL terminator → padded to 20 bytes), typetag ``,fff`` (4 bytes,
      already 4-aligned), 3 × float32 (12 bytes) = 32–36 bytes total. No
      alignment concerns for this address/arg shape.
    """
    b = OscMessageBuilder(address=f"/adm/obj/{int(n)}/aed")
    b.add_arg(float(azim))
    b.add_arg(float(elev))
    b.add_arg(float(dist))
    return b.build().dgram


def sha256_hex(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def compare(expected_bytes: bytes, actual_bytes: bytes, *, debug: bool = False) -> tuple[bool, str, str]:
    """Return ``(match, expected_hash, actual_hash)``.

    If ``debug`` is True, prints both hashes on mismatch (used by the
    e2e wire test on first failure to surface the wire diff).
    """
    eh = sha256_hex(expected_bytes)
    ah = sha256_hex(actual_bytes)
    match = eh == ah
    if debug and not match:  # pragma: no cover — diagnostic only
        print(
            f"[wire_hash_compare] MISMATCH\n"
            f"  expected sha256: {eh}\n"
            f"  actual   sha256: {ah}\n"
            f"  expected bytes : {expected_bytes!r}\n"
            f"  actual   bytes : {actual_bytes!r}"
        )
    return match, eh, ah
