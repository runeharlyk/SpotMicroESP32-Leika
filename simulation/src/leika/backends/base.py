"""What every backend offers the Robot facade, and the values that cross it."""
from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

import numpy as np

from ...proto import message_pb2 as pb
from ..constants import Gait, Mode

if TYPE_CHECKING:  # joint_scale reaches this module through the speed model
    from ..joint_scale import ServoSetup


@dataclass(frozen=True)
class Velocity:
    vx: float  # m/s forward
    vy: float  # m/s to the left
    yaw_rate: float  # rad/s counter-clockwise


ZERO = Velocity(0.0, 0.0, 0.0)


@dataclass(frozen=True)
class RobotState:
    t: float  # seconds on the backend's clock
    mode: Mode
    roll: float  # degrees, REP-103
    pitch: float
    yaw: float
    gyro: tuple[float, float, float]  # rad/s
    accel: tuple[float, float, float]  # m/s^2
    joints: tuple[float, ...]  # degrees
    link_lost: bool  # the robot's dead-man stop is in force
    link_drops: int  # dead-man stops since connecting, each counted once


# Dead reckoning from the commanded feet since the last reset or deactivation, in the frame the robot faced then. It
# counts the steps the gait commanded, so on the real robot it runs ahead of a tape measure.
@dataclass(frozen=True)
class Odometry:
    x: float  # m forward
    y: float  # m to the left
    yaw: float  # degrees counter-clockwise, -180 to 180, the gyro fused in
    distance: float  # m along the path
    kinematic_yaw: float  # degrees, the feet's alone: against yaw over a turn, the gait's turn scale


@dataclass(frozen=True)
class Calibration:
    still: bool  # the gyro bias was taken
    levelled: bool  # the tilt was folded into the IMU mounting
    tilt_deg: float


class RobotError(Exception):
    pass


class RobotTimeout(RobotError):
    pass


class RobotDisconnected(RobotError):
    pass


class GoalCancelled(RobotError):
    """A goal the robot ended early: a cancel, the app's sticks, a mode or gait change, or a newer goal."""


class UnknownVariant(RobotError):
    pass


class Backend(Protocol):
    def connect(self) -> None: ...
    def close(self) -> None: ...
    def set_mode(self, mode: Mode) -> None: ...  # returns once the robot reports the mode
    def set_gait(self, gait: Gait) -> None: ...
    def set_height(self, height: float) -> None: ...  # 0 (lowest) to 1
    def set_velocity(self, velocity: Velocity) -> Velocity: ...  # returns what was commanded, after clamping
    def max_velocity(self) -> Velocity: ...
    def state(self) -> RobotState: ...
    def calibrate(self) -> Calibration: ...
    def odometry(self) -> Odometry: ...
    def reset_odometry(self) -> Odometry: ...  # starts afresh; returns where it had got to
    def supports_goals(self) -> bool: ...  # whether the robot drives goal moves itself
    # One target, the others zero; speed 0 to 1 of the gait's top. Returns once reached, else raises GoalCancelled or
    # RobotTimeout.
    def goal_move(self, forward: float, sideways: float, turn_deg: float, speed: float) -> None: ...
    def now(self) -> float: ...
    def sleep(self, seconds: float) -> None: ...
    @property
    def features(self) -> pb.FeaturesDataResponse: ...  # what the robot reported about itself on connecting
    def servo_settings(self) -> ServoSetup: ...
    def servo_output(self) -> tuple[np.ndarray, np.ndarray]: ...  # the last write's angles (degrees) and PWMs
    def set_servo_pwm(self, channel: int, pwm: int) -> None: ...  # ServoPWMData: one channel, or every joint's for -1
    # Hands sink(frame, host_us) every frame a recording keeps (record.py's: the header, the telemetry batches, the
    # network samples and the goal statuses), each an encoded Message with its arrival in microseconds, until called
    # with None, after which the sink is not called again.
    def stream_telemetry(self, sink: Callable[[bytes, int], None] | None) -> None: ...
