"""The firmware walk gait's velocity (esp32/include/motion_states/walk_state.h), for driving the real robot.

The sticks set the stroke: step_x = ly * max_step_length, step_z = -lx * max_step_length, step_angle = rx. A stance sweeps
each foot through the stroke in the stance part of the cycle (duty), and the body moves at the stance feet's speed, so
vx = step_x * rate / duty. The cycle rate is max(s, 0.5) * speed_factor; the client holds s at 0.5, the slowest the gait
allows.

That holds while the joints keep under walk_state.h's MAX_JOINT_SPEED_DEG_S. Where the stroke or the lift would turn one
faster, the firmware runs its clock only the 64ths of each tick that keep the fastest joint at the cap, so the cycle
takes longer and the body walks slower than the stroke implies. The model works the slowing out the same way: it walks
the cycle's phase through the firmware's own kinematics at the client's body height and step height, and stretches
each stretch of it that is too fast.
"""
import re
from dataclasses import dataclass

import numpy as np

from .backends.base import UnknownVariant, Velocity
from .constants import Gait

SPEED = 0.5
STEP_HEIGHT = 0.5
MAX_JOINT_SPEED_DEG_S = 0.75 * 720
# coxa, coxa offset, femur, tibia, length, width (metres) per variant, from esp32/include/kinematics.h; pinned by
# tests/test_speed_model.py.
GEOMETRY = {
    "SPOTMICRO_ESP32": (0.0605, 0.01, 0.1112, 0.1185, 0.2075, 0.078),
    "SPOTMICRO_ESP32_MINI": (0.035, 0.0, 0.06, 0.06, 0.16, 0.08),
    "SPOTMICRO_YERTLE": (0.035, 0.0, 0.13, 0.13, 0.24, 0.078),
}
# Yertle's knee servo turns with its femur (KinConfig::kneeFollowsFemur).
KNEE_FOLLOWS_FEMUR = {"SPOTMICRO_YERTLE"}
GAITS = {Gait.TROT: (2.0, 0.75), Gait.CRAWL: (0.5, 0.85)}  # speed factor, duty
STEP_DEPTH = 0.002
TROT_OFFSETS = (0.0, 0.5, 0.5, 0.0)
CRAWL_OFFSETS = (0.25, 0.75, 0.5, 0.0)  # WalkState::set_mode_crawl's default order
SLOWDOWN_PARTS = 64
# Points sampled along a cycle, and the halvings that find the sticks for a velocity.
SAMPLES = 400
HALVINGS = 24
BEZIER_STEPS = np.array([-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0])
BEZIER_HEIGHTS = np.array([0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0])
BINOMIALS = np.array([1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1], dtype=float)


@dataclass(frozen=True)
class Sticks:
    lx: float
    ly: float
    rx: float

    def scaled(self, share: float) -> "Sticks":
        return Sticks(self.lx * share, self.ly * share, self.rx * share)


def known_variant(reported: str) -> str:
    if not reported:
        raise UnknownVariant("the robot has no variant chosen yet; choose it in the app first")
    variant = re.sub(r"_V\d+$", "", reported)
    if variant not in GEOMETRY:
        raise UnknownVariant(f"no leg lengths for the robot's variant {reported!r}")
    return variant


def _reach(variant: str) -> float:
    _, coxa_offset, femur, tibia, _, _ = GEOMETRY[variant]
    return femur + tibia - coxa_offset


def max_step_length(variant: str) -> float:
    return 0.8 * _reach(variant)


def _rate(gait: Gait) -> float:
    return max(SPEED, 0.5) * GAITS[gait][0]


def _per_stick(variant: str, gait: Gait) -> tuple[float, float]:
    """Metres per second of body speed, and radians per second of turn, for a full stick."""
    duty = GAITS[gait][1]
    return max_step_length(variant) * _rate(gait) / duty, _rate(gait) / duty


def _leg_angles(variant: str, x, y, z) -> np.ndarray:
    """Kinematics::legIK in degrees, for feet at (x, y, z) in their legs' frames."""
    coxa, coxa_offset, femur, tibia, _, _ = GEOMETRY[variant]
    f = np.sqrt(np.maximum(0.0, x * x + y * y - coxa * coxa))
    g = f - coxa_offset
    h = np.sqrt(g * g + z * z)
    hip = -np.arctan2(y, x) - np.arctan2(f, -coxa)
    knee = np.arccos(np.clip((h * h - femur * femur - tibia * tibia) / (2 * femur * tibia), -1.0, 1.0))
    thigh = np.arctan2(z, g) - np.arctan2(tibia * np.sin(knee), femur + tibia * np.cos(knee))
    if variant in KNEE_FOLLOWS_FEMUR:
        knee = knee + thigh
    return np.degrees(np.stack([hip, thigh, knee]))


def joint_speeds(variant: str, gait: Gait, sticks: Sticks, height: float) -> np.ndarray:
    """The fastest joint's speed (deg/s) through each of SAMPLES stretches of a cycle, at the commanded clock.

    Every foot where walk_state.h puts it, through the firmware's kinematics, with the body level at `height`; the
    crawl's body shift is left out.
    """
    coxa, _, _, _, length, width = GEOMETRY[variant]
    duty = GAITS[gait][1]
    reach = _reach(variant)
    # Per leg: its mount and its home (KinConfig's mountOffsets and default_feet_positions), and its stroke.
    sides = np.array([[1, 1], [1, -1], [-1, 1], [-1, -1]], dtype=float)
    mounts = sides * [length / 2, width / 2]
    homes = mounts + sides * [0, coxa]
    stroke_x = sticks.ly * max_step_length(variant) - sticks.rx * homes[:, 1]
    stroke_z = -sticks.lx * max_step_length(variant) + sticks.rx * homes[:, 0]
    half = (np.hypot(stroke_x, stroke_z) / 2)[:, None]
    heading = np.arctan2(stroke_z, stroke_x)[:, None]
    walking = half > 0

    offsets = np.array(TROT_OFFSETS if gait is Gait.TROT else CRAWL_OFFSETS)[:, None]
    leg_phase = np.mod(np.linspace(0.0, 1.0, SAMPLES + 1) + offsets, 1.0)
    stance = leg_phase <= duty
    phase = np.where(stance, leg_phase / duty, (leg_phase - duty) / (1 - duty))
    powers = np.arange(12)
    weights = BINOMIALS * phase[..., None] ** powers * (1 - phase[..., None]) ** powers[::-1]
    along = np.where(stance, half * (1 - 2 * phase), half * (weights @ BEZIER_STEPS))
    dip = -STEP_DEPTH * np.cos(np.pi * along / (2 * np.where(walking, half, 1)))
    lift = np.where(walking, np.where(stance, dip, STEP_HEIGHT * reach / 2 * (weights @ BEZIER_HEIGHTS)), 0.0)

    # The foot from its mount, in the leg's frame as calculate_inverse_kinematics turns it.
    forward = homes[:, :1] - mounts[:, :1] + along * np.cos(heading)
    outward = homes[:, 1:] - mounts[:, 1:] + along * np.sin(heading)
    up = lift - reach * (0.45 + 0.45 * height)
    x = np.where(np.arange(4)[:, None] % 2 == 1, outward, -outward)
    angles = _leg_angles(variant, x, up, forward)
    return np.abs(np.diff(angles, axis=-1)).max(axis=(0, 1)) * SAMPLES * _rate(gait)


def slowdown(variant: str, gait: Gait, sticks: Sticks, height: float) -> float:
    """The share of the commanded cadence the firmware walks these sticks at: 1 when the servos follow all of it.

    Each stretch runs at the most 64ths of the clock that keep its fastest joint at the cap, as WalkState::step finds.
    """
    allowed = MAX_JOINT_SPEED_DEG_S / np.maximum(joint_speeds(variant, gait, sticks, height), 1e-9)
    parts = np.where(allowed >= 1, SLOWDOWN_PARTS, np.maximum(np.floor(allowed * SLOWDOWN_PARTS), 1))
    return float(1 / np.mean(SLOWDOWN_PARTS / parts))


def velocity_of(variant: str, gait: Gait, sticks: Sticks, height: float) -> Velocity:
    linear, angular = _per_stick(variant, gait)
    share = slowdown(variant, gait, sticks, height)
    return Velocity(sticks.ly * linear * share, sticks.lx * linear * share, -sticks.rx * angular * share)


def sticks_for(variant: str, gait: Gait, velocity: Velocity, height: float) -> Sticks:
    """The sticks that walk this velocity, or as near as the gait gets in its direction.

    A longer stick in the same direction walks faster even where the firmware slows it, so the stick is found by
    halving between none and the longest in that direction.
    """
    linear, angular = _per_stick(variant, gait)
    wanted = Sticks(velocity.vy / linear, velocity.vx / linear, -velocity.yaw_rate / angular)
    longest = max(abs(wanted.lx), abs(wanted.ly), abs(wanted.rx))
    if longest == 0:
        return wanted
    low, high = 0.0, 1.0 / longest
    if high * slowdown(variant, gait, wanted.scaled(high), height) <= 1:
        return wanted.scaled(high)
    for _ in range(HALVINGS):
        share = (low + high) / 2
        if share * slowdown(variant, gait, wanted.scaled(share), height) < 1:
            low = share
        else:
            high = share
    return wanted.scaled(low)


def max_velocity(variant: str, gait: Gait, height: float) -> Velocity:
    """The fastest the gait walks along each axis on its own, at this body height."""
    return Velocity(velocity_of(variant, gait, Sticks(0.0, 1.0, 0.0), height).vx,
                    velocity_of(variant, gait, Sticks(1.0, 0.0, 0.0), height).vy,
                    -velocity_of(variant, gait, Sticks(0.0, 0.0, 1.0), height).yaw_rate)
