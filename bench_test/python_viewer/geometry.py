"""
Coordinate geometry for the AFI920 3D viewer.

World frame: ISO 8855 automotive convention
    X = forward, Y = left, Z = up   (right-handed)

A radar reports each detection in its OWN sensor frame as spherical
(radial_distance, azimuth, elevation):
    azimuth   : + from X (forward) toward Y (left)
    elevation : + upward from the XY plane toward Z

Sensor-frame Cartesian:
    x = r * cos(el) * cos(az)
    y = r * cos(el) * sin(az)
    z = r * sin(el)

Each sensor is mounted with an offset (offset_x/y/z, metres) and an
orientation (yaw/pitch/roll, degrees) read from the Config API. The world
point is:
    p_world = R(yaw, pitch, roll) @ p_sensor + offset
with the standard intrinsic Z-Y-X rotation (yaw about Z, then pitch about Y,
then roll about X).

numpy only — no GUI imports, so this module is unit-testable headless.

Copyright (c) 2026 bitsensing Inc.
"""

import math
from typing import Tuple

import numpy as np


def spherical_to_cartesian(r: np.ndarray, az: np.ndarray,
                           el: np.ndarray) -> np.ndarray:
    """Vectorised spherical -> sensor-frame Cartesian (ISO 8855).

    Args:
        r, az, el: equal-length 1-D arrays (metres, radians, radians).
    Returns:
        (N, 3) float64 array of [x, y, z] in the sensor frame.
    """
    r = np.asarray(r, dtype=np.float64)
    az = np.asarray(az, dtype=np.float64)
    el = np.asarray(el, dtype=np.float64)
    ce = np.cos(el)
    x = r * ce * np.cos(az)
    y = r * ce * np.sin(az)
    z = r * np.sin(el)
    return np.stack((x, y, z), axis=-1)


def rotation_matrix(yaw_deg: float, pitch_deg: float,
                    roll_deg: float) -> np.ndarray:
    """Intrinsic Z-Y-X rotation matrix (world_from_sensor), ISO 8855.

    R = Rz(yaw) @ Ry(pitch) @ Rx(roll)
    """
    y = math.radians(yaw_deg)
    p = math.radians(pitch_deg)
    r = math.radians(roll_deg)
    cy, sy = math.cos(y), math.sin(y)
    cp, sp = math.cos(p), math.sin(p)
    cr, sr = math.cos(r), math.sin(r)
    rz = np.array([[cy, -sy, 0.0], [sy, cy, 0.0], [0.0, 0.0, 1.0]])
    ry = np.array([[cp, 0.0, sp], [0.0, 1.0, 0.0], [-sp, 0.0, cp]])
    rx = np.array([[1.0, 0.0, 0.0], [0.0, cr, -sr], [0.0, sr, cr]])
    return rz @ ry @ rx


class SensorPose:
    """Rigid mounting transform for one sensor (world_from_sensor)."""

    def __init__(self, offset: Tuple[float, float, float] = (0.0, 0.0, 0.0),
                 yaw_deg: float = 0.0, pitch_deg: float = 0.0,
                 roll_deg: float = 0.0):
        self.offset = np.asarray(offset, dtype=np.float64).reshape(3)
        self.yaw_deg = float(yaw_deg)
        self.pitch_deg = float(pitch_deg)
        self.roll_deg = float(roll_deg)
        self.R = rotation_matrix(yaw_deg, pitch_deg, roll_deg)

    def transform(self, pts_sensor: np.ndarray) -> np.ndarray:
        """Map (N, 3) sensor-frame points into the world frame."""
        pts = np.asarray(pts_sensor, dtype=np.float64)
        if pts.size == 0:
            return pts.reshape(0, 3)
        return pts @ self.R.T + self.offset

    @property
    def position(self) -> np.ndarray:
        """Sensor origin in world coordinates (the mounting offset)."""
        return self.offset

    def forward(self) -> np.ndarray:
        """Unit boresight (sensor +X) expressed in the world frame."""
        return self.R @ np.array([1.0, 0.0, 0.0])

    def to_dict(self) -> dict:
        return {
            "offset_x_m": float(self.offset[0]),
            "offset_y_m": float(self.offset[1]),
            "offset_z_m": float(self.offset[2]),
            "yaw_deg": self.yaw_deg,
            "pitch_deg": self.pitch_deg,
            "roll_deg": self.roll_deg,
        }

    @classmethod
    def from_dict(cls, d: dict) -> "SensorPose":
        d = d or {}
        return cls(
            offset=(d.get("offset_x_m", 0.0), d.get("offset_y_m", 0.0),
                    d.get("offset_z_m", 0.0)),
            yaw_deg=d.get("yaw_deg", 0.0),
            pitch_deg=d.get("pitch_deg", 0.0),
            roll_deg=d.get("roll_deg", 0.0),
        )

    def __repr__(self) -> str:
        o = self.offset
        return (f"SensorPose(offset=({o[0]:.3f},{o[1]:.3f},{o[2]:.3f}), "
                f"yaw={self.yaw_deg:.1f}, pitch={self.pitch_deg:.1f}, "
                f"roll={self.roll_deg:.1f})")
