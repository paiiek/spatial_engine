#!/usr/bin/env python3
"""
scripts/verify_byte_baseline.py — verify OFF-build byte baseline.

Reads ``.ci/off_baseline.bytes.sha256`` (sha256sum-c format: ``<hex>  <path>``)
and validates each listed artefact. Exit codes propagate to CI:

* ``0`` — every entry matched.
* ``1`` — at least one mismatch / missing file / parse error (in --strict).
* ``2`` — argparse / IO error (manifest absent etc.).

In *non-strict* mode (default), missing artefacts are reported but do not
trigger a non-zero exit — useful for local working trees without a fresh
NO_JUCE OFF build. In ``--strict`` mode, ANY missing entry is a failure.

Sentinel reference (plan §7.2):
    bash scripts/verify_byte_baseline.py --strict
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys
from pathlib import Path
from typing import List, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = REPO_ROOT / ".ci" / "off_baseline.bytes.sha256"

# Result symbols
OK = "OK"
FAIL = "FAIL"
MISSING = "MISSING"


def _parse_manifest(path: Path) -> List[Tuple[str, str]]:
    """Parse sha256sum manifest into ``[(expected_hex, relpath), ...]``."""
    entries: List[Tuple[str, str]] = []
    with path.open("r", encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, start=1):
            line = raw.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            # sha256sum format: "<64 hex>  <path>" (two spaces, binary mode "*")
            # Be lenient: accept one-or-more whitespace.
            parts = line.split(None, 1)
            if len(parts) != 2 or len(parts[0]) != 64:
                raise ValueError(
                    f"Malformed manifest line {lineno} in {path}: {line!r}"
                )
            hexsum, relpath = parts[0].lower(), parts[1].lstrip("*").strip()
            entries.append((hexsum, relpath))
    return entries


def _hash_file(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            buf = fh.read(chunk)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest()


def verify(manifest: Path, strict: bool) -> int:
    if not manifest.is_file():
        print(f"[verify_byte_baseline] manifest not found: {manifest}",
              file=sys.stderr)
        return 2

    try:
        entries = _parse_manifest(manifest)
    except ValueError as exc:
        print(f"[verify_byte_baseline] parse error: {exc}", file=sys.stderr)
        return 2

    if not entries:
        print(f"[verify_byte_baseline] manifest is empty: {manifest}",
              file=sys.stderr)
        return 2

    n_ok = n_fail = n_missing = 0
    rows: List[Tuple[str, str, str]] = []   # (status, relpath, detail)

    for expected, relpath in entries:
        target = (REPO_ROOT / relpath).resolve()
        if not target.is_file():
            n_missing += 1
            rows.append((MISSING, relpath, f"file does not exist: {target}"))
            continue
        actual = _hash_file(target)
        if actual == expected:
            n_ok += 1
            rows.append((OK, relpath, expected))
        else:
            n_fail += 1
            rows.append((FAIL, relpath,
                         f"expected={expected} actual={actual}"))

    width = max((len(r[1]) for r in rows), default=10)
    for status, relpath, detail in rows:
        print(f"  [{status:7}] {relpath.ljust(width)}  {detail}")

    total = len(entries)
    print(
        f"[verify_byte_baseline] manifest={manifest.name}  "
        f"total={total}  ok={n_ok}  fail={n_fail}  missing={n_missing}  "
        f"strict={strict}"
    )

    if n_fail > 0:
        return 1
    if strict and n_missing > 0:
        return 1
    return 0


def main(argv: List[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="verify_byte_baseline.py",
        description=(
            "Verify NO_JUCE OFF byte baseline against "
            ".ci/off_baseline.bytes.sha256 (sha256sum -c equivalent)."
        ),
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help=f"manifest path (default: {DEFAULT_MANIFEST.relative_to(REPO_ROOT)})",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="treat MISSING files as failure (CI mode)",
    )
    args = parser.parse_args(argv)
    return verify(args.manifest, args.strict)


if __name__ == "__main__":
    sys.exit(main())
