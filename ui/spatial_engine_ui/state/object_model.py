"""Spatial object state model."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict


@dataclass
class SpatialObject:
    obj_id: int
    x: float = 0.0
    z: float = 0.0
    label: str = ""
    active: bool = True


class ObjectModel:
    """Holds state for up to N spatial objects."""

    def __init__(self) -> None:
        self._objects: Dict[int, SpatialObject] = {}

    def update(self, obj_id: int, x: float, z: float) -> None:
        if obj_id in self._objects:
            self._objects[obj_id].x = x
            self._objects[obj_id].z = z
        else:
            self._objects[obj_id] = SpatialObject(obj_id=obj_id, x=x, z=z)

    def get(self, obj_id: int) -> SpatialObject | None:
        return self._objects.get(obj_id)

    def all_objects(self) -> list[SpatialObject]:
        return list(self._objects.values())

    def spawn(self, obj_id: int, x: float = 0.0, z: float = 0.0, label: str = "") -> SpatialObject:
        obj = SpatialObject(obj_id=obj_id, x=x, z=z, label=label)
        self._objects[obj_id] = obj
        return obj

    def clear(self) -> None:
        self._objects.clear()
