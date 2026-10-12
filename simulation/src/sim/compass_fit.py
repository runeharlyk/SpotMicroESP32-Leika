"""Hard-iron calibration of the compass from a telemetry recording made while the robot is turned through all orientations.

The firmware turns a raw compass reading into the body frame as mounting @ alignment @ soft_iron @ (raw - offset)
(esp32/include/peripherals/imu/imu.h). The recording holds the body-frame field and, in its header, the settings in
force, so the raw readings are recovered exactly and a sphere is fitted to them: its centre is the hard-iron offset.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from src.sim.recording import Recording

MAG_VALID = 4  # ImuValid::MAG
# The field turned through less than this share of the sphere does not pin the centre down: the fit trusts every
# direction only when the smallest spread of the unit directions is this large (an even sphere gives 1/3).
MIN_DIRECTION_SPREAD = 0.05


@dataclass
class CompassFit:
    offset: np.ndarray  # microtesla, chip frame, to store as ImuSettings.mag_offset
    radius: float  # microtesla, the field's strength after the offset
    residual: float  # microtesla, RMS distance of the samples from the sphere
    samples: int
    direction_spread: float  # smallest eigenvalue of the unit directions' covariance; 1/3 for an even sphere

    @property
    def covered(self) -> bool:
        return self.direction_spread >= MIN_DIRECTION_SPREAD


def _matrix(values, default: np.ndarray) -> np.ndarray:
    return np.asarray(values, dtype=float).reshape(3, 3) if len(values) == 9 else default


def raw_readings(rec: Recording) -> np.ndarray:
    """The compass readings as the chip gave them, one per compass sample, not one per control tick."""
    t = rec.ticks
    fresh = (t["imu_valid"].astype(np.int64) & MAG_VALID) != 0
    mag, mag_t = t["mag"][fresh], t["imu_mag_t_us"][fresh]
    _, first = np.unique(mag_t, return_index=True)
    body = mag[np.sort(first)]
    imu = rec.header.imu_settings if rec.header is not None else None
    eye = np.eye(3)
    mounting = _matrix(imu.mounting, eye) if imu else eye
    alignment = _matrix(imu.mag_alignment, eye) if imu else eye
    soft_iron = _matrix(imu.mag_soft_iron, eye) if imu else eye
    offset = np.asarray(imu.mag_offset, dtype=float) if imu and len(imu.mag_offset) == 3 else np.zeros(3)
    to_body = mounting @ alignment @ soft_iron
    return body @ np.linalg.inv(to_body).T + offset


def fit_sphere(points: np.ndarray) -> CompassFit:
    """Least-squares sphere |p - c|^2 = r^2, linear in c and r^2 - |c|^2."""
    if len(points) < 10:
        raise ValueError(f"{len(points)} compass samples; turn the robot through all orientations while recording")
    a = np.hstack([2 * points, np.ones((len(points), 1))])
    b = np.sum(points**2, axis=1)
    solution, *_ = np.linalg.lstsq(a, b, rcond=None)
    centre = solution[:3]
    radius = float(np.sqrt(solution[3] + centre @ centre))
    distances = np.linalg.norm(points - centre, axis=1)
    directions = (points - centre) / distances[:, None]
    spread = float(np.min(np.linalg.eigvalsh(np.cov(directions.T))))
    return CompassFit(centre, radius, float(np.sqrt(np.mean((distances - radius) ** 2))), len(points), spread)


def fit_recording(rec: Recording) -> CompassFit:
    return fit_sphere(raw_readings(rec))
