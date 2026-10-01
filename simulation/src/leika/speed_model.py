"""The firmware walk gait's velocity (esp32/include/motion_states/walk_state.h), for driving the real robot.

The sticks set the stroke: step_x = ly * max_step_length, step_z = -lx * max_step_length, step_angle = rx. A stance sweeps
each foot through the stroke in the stance part of the cycle (duty), and the body moves at the stance feet's speed, so
vx = step_x * rate / duty. The cycle rate is max(s, 0.5) * speed_factor; the client holds s at 0.5, the slowest the gait
allows, since faster cycles make the swings outrun the servos.
"""
import re
from dataclasses import dataclass

from .backends.base import UnknownVariant, Velocity
from .constants import Gait

SPEED = 0.5
# femur, tibia, coxa offset (metres) per variant, from esp32/include/kinematics.h; pinned by tests/test_speed_model.py.
LEGS = {
    "SPOTMICRO_ESP32": (0.1112, 0.1185, 0.01),
    "SPOTMICRO_ESP32_MINI": (0.06, 0.06, 0.0),
    "SPOTMICRO_YERTLE": (0.13, 0.13, 0.0),
}
GAITS = {Gait.TROT: (2.0, 0.75), Gait.CRAWL: (0.5, 0.85)}  # speed factor, duty


@dataclass(frozen=True)
class Sticks:
    lx: float
    ly: float
    rx: float


def known_variant(reported: str) -> str:
    variant = re.sub(r"_V\d+$", "", reported)
    if variant not in LEGS:
        raise UnknownVariant(f"no leg lengths for the robot's variant {reported!r}")
    return variant


def max_step_length(variant: str) -> float:
    femur, tibia, coxa_offset = LEGS[variant]
    return 0.8 * (femur + tibia - coxa_offset)


def _per_stick(variant: str, gait: Gait) -> tuple[float, float]:
    """Metres per second of body speed, and radians per second of turn, for a full stick."""
    factor, duty = GAITS[gait]
    rate = max(SPEED, 0.5) * factor
    return max_step_length(variant) * rate / duty, rate / duty


def velocity_of(variant: str, gait: Gait, sticks: Sticks) -> Velocity:
    linear, angular = _per_stick(variant, gait)
    return Velocity(sticks.ly * linear, sticks.lx * linear, -sticks.rx * angular)


def sticks_for(variant: str, gait: Gait, velocity: Velocity) -> Sticks:
    linear, angular = _per_stick(variant, gait)
    clamp = lambda value: max(-1.0, min(1.0, value))
    return Sticks(clamp(velocity.vy / linear), clamp(velocity.vx / linear), clamp(-velocity.yaw_rate / angular))


def max_velocity(variant: str, gait: Gait) -> Velocity:
    linear, angular = _per_stick(variant, gait)
    return Velocity(linear, linear, angular)
