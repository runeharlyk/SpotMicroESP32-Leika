import math
import numpy as np
from typing import TypedDict
from enum import Enum

try:
    from src.robot.kinematics import BodyStateT
except ImportError:
    from robot.kinematics import BodyStateT


class GaitType(Enum):
    TROT_GATE = 0
    CRAWL_GATE = 1


default_offset = {
    GaitType.TROT_GATE: [0, 0.5, 0.5, 0],
    GaitType.CRAWL_GATE: [0, 1 / 4, 2 / 4, 3 / 4],
}

default_stand_frac = {
    GaitType.TROT_GATE: 3 / 4,
    GaitType.CRAWL_GATE: 3 / 4,
}


class GaitStateT(TypedDict):
    step_height: float
    step_x: float
    step_z: float
    step_angle: float
    step_velocity: float
    step_depth: float
    stand_frac: float
    offset: list[float]
    gait_type: GaitType


length_multipliers = np.array([-1.4, -1.0, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0])
height_profile = np.array([0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0])


def sine_curve(length, angle, height, phase):
    x, z = length * (1 - 2 * phase) * np.cos(angle), length * (1 - 2 * phase) * np.sin(angle)
    y = height * np.cos(np.pi * (x + z) / (2 * length)) if length else 0
    return np.array([x, z, y])


def yaw_arc(feet, current):
    return (
        np.pi / 2
        + np.arctan2(feet[1], feet[0])
        + np.arctan2(np.linalg.norm(current[:2] - feet[:2]), np.linalg.norm(feet[:2]))
    )


def get_control_points(length, angle, height):
    x_polar, z_polar = np.cos(angle), np.sin(angle)

    x = length * length_multipliers * x_polar
    z = length * length_multipliers * z_polar
    y = height * height_profile
    return np.stack([x, z, y], axis=1)


def bezier_curve(length, angle, height, phase):
    ctrl = get_control_points(length, angle, height)
    n = len(ctrl) - 1
    coeffs = np.array([math.comb(n, i) * (phase**i) * ((1 - phase) ** (n - i)) for i in range(n + 1)])
    return np.sum(ctrl * coeffs[:, None], axis=0)


class GaitController:
    def __init__(self, default_position: np.ndarray):
        self.default_position = default_position
        self.phase = 0.0

    def step(self, gait: GaitStateT, body: BodyStateT, dt: float):
        step_x, step_z, angle = gait["step_x"], gait["step_z"], gait["step_angle"]
        if not any((step_x, step_z, angle)):
            body["feet"] = body["feet"] + (self.default_position - body["feet"]) * dt * 10
            self.phase = 0.0
            return

        self._advance_phase(dt, gait["step_velocity"])

        stand_fraction = gait["stand_frac"]
        depth = gait["step_depth"]
        height = gait["step_height"]
        offsets = gait["offset"]

        length = np.hypot(step_x, step_z)
        if step_x < 0:
            length = -length
        turn_amplitude = np.arctan2(step_z, length) * 2

        new_feet = np.zeros_like(self.default_position)

        for i, (default_foot, current_foot) in enumerate(zip(self.default_position, body["feet"])):
            phase = (self.phase + offsets[i]) % 1
            ph_norm, curve_fn, amp = self._phase_params(phase, stand_fraction, depth, height)
            delta_pos = curve_fn(length / 2, turn_amplitude, amp, ph_norm)
            delta_rot = curve_fn(np.rad2deg(angle), yaw_arc(default_foot, current_foot), amp, ph_norm)
            new_feet[i][:2] = default_foot[:2] + delta_pos[:2] + delta_rot[:2]
            # new_feet[i][3] = 1

        body["feet"] = new_feet

    def _advance_phase(self, dt: float, velocity: float):
        self.phase = (self.phase + dt * velocity) % 1

    def _phase_params(self, phase: float, stand_frac: float, depth: float, height: float):
        if phase < stand_frac:
            return phase / stand_frac, sine_curve, -depth
        return (phase - stand_frac) / (1 - stand_frac), bezier_curve, height
