"""Port of the firmware's kinematic dead reckoning (esp32/include/odometry.h), pinned to it by tests/test_odometry.py.

A foot in stance is fixed to the floor, so in the body frame it moves by the inverse of the body's motion: the rigid
motion that best carries the stance feet from one tick to the next is the body's motion, inverted. The heading
integrates the same fit's yaw.

Input is in the firmware's frame (x forward, y up, z right; the body's xm, zm and its phi in degrees); the estimate is
REP-103 (x forward, y left, yaw counter-clockwise in radians) in the frame the body faced at the reset.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

LIFTED_M = 0.003  # a foot this far above the lowest one is in the air, whatever its phase


@dataclass
class OdometryEstimate:
    x: float = 0.0
    y: float = 0.0
    yaw: float = 0.0
    distance: float = 0.0


class KinematicOdometry:
    def __init__(self):
        self.estimate = OdometryEstimate()
        self._previous: np.ndarray | None = None
        self._previous_contact = np.zeros(4, dtype=bool)

    def reset(self) -> None:
        """Zeroes the estimate; the feet of the last tick still count."""
        self.estimate = OdometryEstimate()

    def update(self, feet, stance, xm: float = 0.0, zm: float = 0.0, phi_deg: float = 0.0) -> None:
        feet = np.asarray(feet, dtype=float)[:, :3]
        c, s = math.cos(math.radians(phi_deg)), math.sin(math.radians(phi_deg))
        forward, left = feet[:, 0] - xm, zm - feet[:, 2]
        planar = np.stack([c * forward + s * left, -s * forward + c * left], axis=1)
        contact = np.asarray(stance, dtype=bool) & (feet[:, 1] - feet[:, 1].min() < LIFTED_M)
        if self._previous is not None:
            self._integrate(self._previous, planar, contact & self._previous_contact)
        self._previous, self._previous_contact = planar, contact

    def _integrate(self, before: np.ndarray, after: np.ndarray, down: np.ndarray) -> None:
        if not down.any():
            return
        p, q = before[down], after[down]
        p_centre, q_centre = p.mean(axis=0), q.mean(axis=0)
        if len(p) > 1:
            dp, dq = p - p_centre, q - q_centre
            turn = -math.atan2(float(np.sum(dp[:, 0] * dq[:, 1] - dp[:, 1] * dq[:, 0])), float(np.sum(dp * dq)))
        else:
            turn = 0.0
        c, s = math.cos(turn), math.sin(turn)
        step_x = p_centre[0] - (c * q_centre[0] - s * q_centre[1])
        step_y = p_centre[1] - (s * q_centre[0] + c * q_centre[1])
        e = self.estimate
        e.x += math.cos(e.yaw) * step_x - math.sin(e.yaw) * step_y
        e.y += math.sin(e.yaw) * step_x + math.cos(e.yaw) * step_y
        e.yaw = math.remainder(e.yaw + turn, 2.0 * math.pi)
        e.distance += math.hypot(step_x, step_y)
