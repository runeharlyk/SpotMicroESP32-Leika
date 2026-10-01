"""What every backend offers the Robot facade, and the values that cross it."""
from dataclasses import dataclass
from typing import Protocol

from ..constants import Gait, Mode


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
    def now(self) -> float: ...
    def sleep(self, seconds: float) -> None: ...
