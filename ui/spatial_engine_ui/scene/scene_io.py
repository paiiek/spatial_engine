"""Scene save/load (YAML format)."""
from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml

from ..state.object_model import ObjectModel, SpatialObject
from ..state.geometry_model import RoomGeometry, Speaker


def save_scene(path: str | Path, object_model: ObjectModel, geometry: RoomGeometry) -> None:
    data: dict[str, Any] = {
        "schema_version": 1,
        "objects": [
            {"id": o.obj_id, "x": o.x, "z": o.z, "label": o.label}
            for o in object_model.all_objects()
        ],
        "geometry": {
            "width": geometry.width,
            "depth": geometry.depth,
            "height": geometry.height,
            "speakers": [
                {"id": s.speaker_id, "x": s.x, "y": s.y, "z": s.z, "label": s.label}
                for s in geometry.speakers
            ],
        },
    }
    Path(path).write_text(yaml.safe_dump(data, default_flow_style=False), encoding="utf-8")


def load_scene(path: str | Path, object_model: ObjectModel, geometry: RoomGeometry) -> None:
    data = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    object_model.clear()
    for obj in data.get("objects", []):
        object_model.spawn(obj["id"], obj.get("x", 0.0), obj.get("z", 0.0), obj.get("label", ""))
    geo = data.get("geometry", {})
    geometry.width = geo.get("width", geometry.width)
    geometry.depth = geo.get("depth", geometry.depth)
    geometry.height = geo.get("height", geometry.height)
    geometry.speakers.clear()
    for sp in geo.get("speakers", []):
        geometry.add_speaker(sp["id"], sp.get("x", 0.0), sp.get("y", 0.0), sp.get("z", 0.0), sp.get("label", ""))
