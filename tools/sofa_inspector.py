#!/usr/bin/env python3
"""tools/sofa_inspector.py — print KEMAR SOFA metadata (P0 deliverable).

Resolves Risk #14 / Critic-MA #6.e: validates the actual KEMAR file at
``/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa`` so that
BinauralMonitor (A2) can pick its partition strategy at P9.

The SOFA spec (AES69) ships data over an HDF5/NetCDF4 container, so we
read it with ``h5py`` directly to avoid the ``spaudiopy`` / ``scipy.special``
dependency thicket (see MEMORY.md scipy 1.17 note).

Output is human-readable AND machine-parseable JSON on a final line tagged
``SOFA_INSPECT_JSON:`` so just-recipes can grep it.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any

DEFAULT_KEMAR = "/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa"


def _decode(value: Any) -> Any:
    """h5py returns bytes for string attrs; decode to str for printing/JSON."""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    if hasattr(value, "tolist"):
        return value.tolist()
    return value


def inspect_sofa(path: Path) -> dict[str, Any]:
    try:
        import h5py
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "h5py not available. Install via: /home/seung/miniforge3/bin/pip install h5py"
        ) from exc

    if not path.exists():
        raise SystemExit(f"SOFA file not found: {path}")

    info: dict[str, Any] = {
        "path": str(path),
        "size_bytes": path.stat().st_size,
    }
    with h5py.File(path, "r") as f:
        # Top-level attributes — SOFA convention metadata.
        attrs = {k: _decode(v) for k, v in f.attrs.items()}
        info["attributes"] = attrs

        # Core data fields per SOFA AES69.
        for ds_name in ("Data.IR", "Data.Delay", "Data.SamplingRate",
                        "SourcePosition", "ReceiverPosition", "ListenerPosition"):
            if ds_name in f:
                ds = f[ds_name]
                info[ds_name] = {
                    "shape": list(ds.shape),
                    "dtype": str(ds.dtype),
                }
                if ds_name == "Data.SamplingRate":
                    info[ds_name]["value"] = ds[...].tolist()

        # Convenience: derived BinauralMonitor sub-budget inputs (A2).
        if "Data.IR" in f:
            ir = f["Data.IR"]
            shape = list(ir.shape)
            # SOFA SimpleFreeFieldHRIR shape: (M, R, N)  M=measurements R=receivers N=samples
            ir_len = shape[-1] if len(shape) >= 1 else None
            n_meas = shape[0] if len(shape) >= 1 else None
            n_recv = shape[1] if len(shape) >= 2 else None
            info["derived"] = {
                "measurement_count": n_meas,
                "receiver_count": n_recv,
                "ir_length_samples": ir_len,
            }
            sr_field = info.get("Data.SamplingRate", {}).get("value")
            sr = sr_field[0] if isinstance(sr_field, list) and sr_field else sr_field
            if sr and ir_len:
                info["derived"]["ir_length_ms"] = round(ir_len * 1000.0 / float(sr), 4)
                info["derived"]["sample_rate_hz"] = float(sr)

    return info


def format_human(info: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append(f"SOFA file: {info['path']}")
    lines.append(f"  size: {info['size_bytes'] / (1024**2):.1f} MB")
    attrs = info.get("attributes", {})
    for key in ("Conventions", "Version", "SOFAConventions", "SOFAConventionsVersion",
                "DataType", "RoomType", "Title", "Author", "DateCreated"):
        if key in attrs:
            lines.append(f"  {key}: {attrs[key]}")
    derived = info.get("derived", {})
    if derived:
        lines.append("Derived:")
        for k, v in derived.items():
            lines.append(f"  {k}: {v}")
    if "Data.SamplingRate" in info:
        lines.append(f"  Data.SamplingRate: {info['Data.SamplingRate']}")
    if "Data.IR" in info:
        lines.append(f"  Data.IR: shape={info['Data.IR']['shape']} dtype={info['Data.IR']['dtype']}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Print KEMAR SOFA metadata for BinauralMonitor sub-budget (A2).")
    parser.add_argument("path", nargs="?", default=DEFAULT_KEMAR, type=Path)
    parser.add_argument("--json", action="store_true", help="Emit JSON only.")
    args = parser.parse_args(argv)

    info = inspect_sofa(args.path)
    if args.json:
        print(json.dumps(info, indent=2, default=str))
    else:
        print(format_human(info))
        # Trailing machine-parseable line so just-recipes can grep.
        print("SOFA_INSPECT_JSON:" + json.dumps(info, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
