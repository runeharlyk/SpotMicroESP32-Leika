"""Port of the firmware stand state's IMU compensation (esp32/include/motion_states/state.h, stand_state.h).

In STAND the firmware turns the body against the tilt the IMU measures: the roll and pitch offsets are the IMU's
REP-103 angles with their signs reversed, subtracted from the commanded attitude, plus the integral of that error
(anti-windup, cleared when a state takes over), clamped to the variant's roll and pitch limits and followed through
the same critically damped smoothing as every other body target. In every other
mode the attitude follows the commanded one (zero here) through the same smoothing.

The firmware's attitude lives in its body frame (x forward, y up, z lateral): omega turns about x, psi about z.
Which side z points to is the open "+z side" question; FIRMWARE_FROM_REP takes the repository's answer (z left, as
fit_pico_joint_map.py and the app's Pico joint map do) and MIRRORED_FIRMWARE_FROM_REP the other, so the levelling
check can measure both.
"""

from __future__ import annotations

import math

import numpy as np

from src.robot.firmware_gait import BodyState

# The model's axes are +X left, +Y rear, +Z up; REP-103 is forward, left, up.
MODEL_TO_REP = np.array([[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
FIRMWARE_FROM_REP = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]])
MIRRORED_FIRMWARE_FROM_REP = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]])

# KinConfig defaults (kinematics.h), degrees.
MAX_ROLL_DEG = 20.0
MAX_PITCH_DEG = 15.0
# MotionState::smoothing_omega: a step settles to 95% in a third of a second.
SMOOTHING_OMEGA = 4.744 / 0.333
# MotionState::IMU_INTEGRAL_GAIN (1/s) and MAX_INTEGRATION_DT (s).
IMU_INTEGRAL_GAIN = 1.0
MAX_INTEGRATION_DT = 0.05


class CriticalDamper:
    """Port of utils/critical_damper.h."""

    def __init__(self):
        self.velocity = 0.0

    def step(self, value: float, target: float, dt: float, omega: float) -> float:
        offset = value - target
        drive = self.velocity + omega * offset
        decay = math.exp(-omega * dt)
        self.velocity = (self.velocity - omega * drive * dt) * decay
        return target + (offset + drive * dt) * decay


def imu_roll_pitch(model_quat) -> tuple[float, float]:
    """REP-103 roll and pitch (rad, ZYX) of a body whose model-frame orientation is `model_quat` (w, x, y, z)."""
    w, x, y, z = (float(v) for v in model_quat)
    model = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
    ])
    r = MODEL_TO_REP @ model @ MODEL_TO_REP.T
    return math.atan2(r[2, 1], r[2, 2]), -math.asin(max(-1.0, min(1.0, r[2, 0])))


def firmware_rotation(omega_deg: float, phi_deg: float, psi_deg: float) -> np.ndarray:
    """Kinematics::euler2R: Rx(omega) Ry(phi) Rz(psi) in the firmware's body frame."""
    a, b, c = math.radians(omega_deg), math.radians(phi_deg), math.radians(psi_deg)
    rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    rz = np.array([[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return rx @ ry @ rz


def level_target(target: float, offset: float, integral: float, limit: float, dt: float) -> tuple[float, float]:
    """MotionState::levelTarget: (attitude target, new integral) for a commanded angle and its measured offset."""
    error = target - offset
    raw = error + integral
    if not (abs(raw) >= limit and raw * error > 0):
        integral = min(max(integral + IMU_INTEGRAL_GAIN * error * min(dt, MAX_INTEGRATION_DT), -limit), limit)
    return min(max(error + integral, -limit), limit), integral


def apply_firmware_attitude(body: BodyState, omega_deg: float, psi_deg: float,
                            firmware_from_rep: np.ndarray = FIRMWARE_FROM_REP) -> None:
    """Sets body's model-frame attitude to the firmware's (omega, psi), seen through the given +z side.

    The firmware's IK rotates the feet by its matrix in its own coordinates; the same coordinate rotation, carried
    into the model's axes, is decomposed into the model IK's Rz(psi) Ry(phi) Rx(omega).
    """
    to_firmware = firmware_from_rep @ MODEL_TO_REP
    r = to_firmware.T @ firmware_rotation(omega_deg, 0.0, psi_deg) @ to_firmware
    body.omega = math.atan2(r[2, 1], r[2, 2])
    body.phi = -math.asin(max(-1.0, min(1.0, r[2, 0])))
    body.psi = math.atan2(r[1, 0], r[0, 0])


class StandCompensation:
    """The firmware body attitude (omega, psi; degrees) as MotionState::smoothToBody steps it, commanded level."""

    def __init__(self):
        self.omega = 0.0
        self.psi = 0.0
        self.reset()

    def reset(self) -> None:
        """MotionState::resetSmoothing: a state taking over starts its dampers at rest and its integrals at zero.
        The attitude carries over."""
        self._omega_damper = CriticalDamper()
        self._psi_damper = CriticalDamper()
        self.omega_integral = 0.0
        self.psi_integral = 0.0

    def step(self, roll: float, pitch: float, dt: float, compensate: bool) -> tuple[float, float]:
        """One control tick from the IMU's REP-103 roll and pitch (rad); returns (omega, psi) in degrees."""
        if compensate:
            target_psi, self.psi_integral = level_target(0.0, -math.degrees(pitch), self.psi_integral,
                                                         MAX_PITCH_DEG, dt)
            target_omega, self.omega_integral = level_target(0.0, -math.degrees(roll), self.omega_integral,
                                                             MAX_ROLL_DEG, dt)
        else:
            target_psi = target_omega = 0.0
        self.psi = self._psi_damper.step(self.psi, target_psi, dt, SMOOTHING_OMEGA)
        self.omega = self._omega_damper.step(self.omega, target_omega, dt, SMOOTHING_OMEGA)
        return self.omega, self.psi
