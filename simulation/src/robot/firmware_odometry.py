"""Port of the firmware's odometry (esp32/include/odometry.h), pinned to it by tests/test_odometry.py.

KinematicOdometry is the dead reckoning from the commanded feet. A foot in stance is fixed to the floor, so in the body
frame it moves by the inverse of the body's motion: the rigid motion that best carries the stance feet from one tick
to the next is the body's motion, inverted. The heading integrates the same fit's yaw.

FusedOdometry corrects that heading with the gyro: while the feet and the body's pose keep still, a scalar Kalman
filter learns the gyro's residual bias from the difference between its yaw rate and the feet's, and the heading
integrates the two rates weighted by their variances.

Input is in the firmware's frame (x forward, y up, z left; the body's xm, zm and its phi in degrees, a positive phi
turning the body clockwise); the estimate is REP-103 (x forward, y left, yaw counter-clockwise in radians) in the frame
the body faced at the reset.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

LIFTED_M = 0.003  # a foot this far above the lowest one is in the air, whatever its phase

# The fused odometry's noise model; esp32/include/odometry.h gives the sources.
GYRO_NOISE = 0.0012  # rad/s, a still Pico's gyro z, measured 2026-10-01
BIAS_PRIOR = 0.05  # rad/s, domain_rand.py's per-episode gyro bias
BIAS_WALK = 1e-4  # rad/s per square-root second, assumed
KIN_STILL = 0.0002  # rad/s, the feet's yaw rate error at a standstill
KIN_PER_SPEED = 2.0  # rad/s per m/s walked
KIN_PER_TURN = 0.4  # rad/s per rad/s turned
SETTLE_S = 0.2  # s the feet's motion takes to fade from their variance once they stop
STILL_RATE = 0.01  # rad/s: feet whose motion is worth less keep the robot still, and the gyro shows its bias
# rad/s: a body pose twisting faster (or shifting faster, at KIN_PER_SPEED), fading over SETTLE_S, leaves the bias alone,
# since the servos follow it late
POSE_STILL_RATE = GYRO_NOISE
MAX_DT_S = 0.1  # a longer tick counts as no gyro


@dataclass
class OdometryEstimate:
    x: float = 0.0
    y: float = 0.0
    yaw: float = 0.0
    distance: float = 0.0


@dataclass(frozen=True)
class BodyStep:
    """The body's motion over one tick, in the frame it faced at the start of the tick."""
    forward: float = 0.0
    left: float = 0.0
    turn: float = 0.0


class KinematicOdometry:
    def __init__(self):
        self.estimate = OdometryEstimate()
        self.turn_seen = False  # two feet or more were down in the last tick and the one before
        self._previous: np.ndarray | None = None
        self._previous_contact = np.zeros(4, dtype=bool)

    def reset(self) -> None:
        """Zeroes the estimate; the feet of the last tick still count."""
        self.estimate = OdometryEstimate()

    def update(self, feet, stance, xm: float = 0.0, zm: float = 0.0, phi_deg: float = 0.0) -> BodyStep:
        """One control tick; returns the step it took."""
        feet = np.asarray(feet, dtype=float)[:, :3]
        c, s = math.cos(math.radians(phi_deg)), math.sin(math.radians(phi_deg))
        forward, left = feet[:, 0] - xm, feet[:, 2] - zm
        planar = np.stack([c * forward - s * left, s * forward + c * left], axis=1)
        contact = np.asarray(stance, dtype=bool) & (feet[:, 1] - feet[:, 1].min() < LIFTED_M)
        self.turn_seen = False
        step = BodyStep()
        if self._previous is not None:
            step = self._integrate(self._previous, planar, contact & self._previous_contact)
        self._previous, self._previous_contact = planar, contact
        return step

    def _integrate(self, before: np.ndarray, after: np.ndarray, down: np.ndarray) -> BodyStep:
        if not down.any():
            return BodyStep()
        p, q = before[down], after[down]
        p_centre, q_centre = p.mean(axis=0), q.mean(axis=0)
        self.turn_seen = len(p) > 1
        if self.turn_seen:
            dp, dq = p - p_centre, q - q_centre
            turn = -math.atan2(float(np.sum(dp[:, 0] * dq[:, 1] - dp[:, 1] * dq[:, 0])), float(np.sum(dp * dq)))
        else:
            turn = 0.0
        c, s = math.cos(turn), math.sin(turn)
        step_x = float(p_centre[0] - (c * q_centre[0] - s * q_centre[1]))
        step_y = float(p_centre[1] - (s * q_centre[0] + c * q_centre[1]))
        e = self.estimate
        e.x += math.cos(e.yaw) * step_x - math.sin(e.yaw) * step_y
        e.y += math.sin(e.yaw) * step_x + math.cos(e.yaw) * step_y
        e.yaw = math.remainder(e.yaw + turn, 2.0 * math.pi)
        e.distance += math.hypot(step_x, step_y)
        return BodyStep(step_x, step_y, turn)


class FusedOdometry:
    def __init__(self):
        self.kinematic = KinematicOdometry()
        self.estimate = OdometryEstimate()
        self.bias = 0.0
        self._bias_variance = BIAS_PRIOR**2
        self._motion_variance = 0.0  # of the feet's rate, from their motion
        self._last_pose = (0.0, 0.0, 0.0)  # the body's phi, xm and zm
        self._pose_variance = 0.0  # of the yaw rate the pose's motion leaves the gyro, as _motion_variance

    @property
    def kinematic_yaw(self) -> float:
        return self.kinematic.estimate.yaw

    def reset(self) -> None:
        """Zeroes the estimate; the bias, a property of the gyro, stays."""
        self.kinematic.reset()
        self.estimate = OdometryEstimate()

    def update(self, feet, stance, xm: float = 0.0, zm: float = 0.0, phi_deg: float = 0.0,
               gyro_rate: float | None = None, dt: float = 0.0) -> BodyStep:
        """One control tick of `dt` seconds with the gyro's yaw rate, or None; returns the step it took."""
        step = self.kinematic.update(feet, stance, xm, zm, phi_deg)
        last_phi, last_xm, last_zm = self._last_pose
        if dt > 0:
            pose_rate = max(abs(math.radians(phi_deg - last_phi)),
                            KIN_PER_SPEED * math.hypot(xm - last_xm, zm - last_zm)) / dt
            self._pose_variance = max(pose_rate**2, self._pose_variance * math.exp(-dt / SETTLE_S))
        self._last_pose = (phi_deg, xm, zm)
        turn = step.turn
        if gyro_rate is not None and 0 < dt <= MAX_DT_S:
            self._bias_variance += BIAS_WALK**2 * dt
            # Feet that did not see the turn leave it to the gyro.
            turn = self._fuse(gyro_rate, step, dt) if self.kinematic.turn_seen else (gyro_rate - self.bias) * dt
        e = self.estimate
        e.x += math.cos(e.yaw) * step.forward - math.sin(e.yaw) * step.left
        e.y += math.sin(e.yaw) * step.forward + math.cos(e.yaw) * step.left
        e.yaw = math.remainder(e.yaw + turn, 2.0 * math.pi)
        e.distance += math.hypot(step.forward, step.left)
        return BodyStep(step.forward, step.left, turn)

    def _fuse(self, gyro_rate: float, step: BodyStep, dt: float) -> float:
        feet_rate, speed = step.turn / dt, math.hypot(step.forward, step.left) / dt
        self._motion_variance = max((KIN_PER_SPEED * speed) ** 2 + (KIN_PER_TURN * feet_rate) ** 2,
                                    self._motion_variance * math.exp(-dt / SETTLE_S))
        feet_variance = KIN_STILL**2 + self._motion_variance
        # Moving feet would teach the bias their undercount, a moving pose the servos' lag.
        if self._motion_variance < STILL_RATE**2 and self._pose_variance < POSE_STILL_RATE**2:
            gain = self._bias_variance / (self._bias_variance + GYRO_NOISE**2 + feet_variance)
            self.bias += gain * (gyro_rate - feet_rate - self.bias)
            self._bias_variance *= 1 - gain
        gyro_variance = GYRO_NOISE**2 + self._bias_variance
        gyro_weight = feet_variance / (feet_variance + gyro_variance)
        return (gyro_weight * (gyro_rate - self.bias) + (1 - gyro_weight) * feet_rate) * dt
