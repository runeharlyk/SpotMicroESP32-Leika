"""One Robot for the simulation and the real robot: Robot("simulation"), Robot("192.168.1.39") over WiFi, or
Robot("COM10") / Robot("/dev/ttyACM0") over the board's native USB port.

Velocities are REP-103 (forward, left, counter-clockwise) in m/s and rad/s; angles in degrees. Leaving the `with`
block, on an exception or Ctrl-C included, stops the robot and lets it rest; it never deactivates on its own.

A goal turn turns from the heading the goals so far meant to leave the robot at, not the one a walk drifted to, so a
square of goal moves closes on the robot's own heading.
"""
from contextlib import contextmanager

import numpy as np

from ..proto import message_pb2 as pb
from ..sim.recording import RecordingWriter
from . import motions
from .backends.base import ZERO, Backend, Calibration, Odometry, RobotError, RobotState, Velocity
from .constants import Gait, Mode
from .joint_scale import ServoSetup

# The modes a script may walk from. DEACTIVATED is the app's stop button, IDLE and CALIBRATION are the user's: walking
# would wake the servos the user turned off.
WALKS_FROM = (Mode.STAND, Mode.REST, Mode.WALK)


def _wrap(degrees: float) -> float:
    """Into (-180, 180]."""
    return 180.0 - (180.0 - degrees) % 360.0


class Robot:
    def __init__(self, target: str = "simulation", *, realtime: bool = True, backend: Backend | None = None):
        if backend is None:
            if target == "simulation":
                from .backends.sim import SimBackend

                backend = SimBackend(realtime=realtime)
            else:
                from .backends.real import RealBackend

                backend = RealBackend(target)
        self._backend = backend
        # The odometry heading the goals so far meant to leave the robot at, in degrees; None until a goal takes the
        # heading it finds, and again once a velocity walk, a deactivation or a goal that did not end has moved it.
        self._intended: float | None = None

    def __enter__(self) -> "Robot":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        try:
            self.stop()
            if self.state().mode is not Mode.DEACTIVATED:  # a robot the user stopped stays stopped
                self._backend.set_mode(Mode.REST)
        except RobotError:
            pass  # the link is gone, and the robot's dead-man stop has halted it
        finally:
            self.close()

    def connect(self) -> None:
        self._backend.connect()

    def close(self) -> None:
        self._backend.close()

    def stand(self, height: float = 0.7) -> None:
        self.stop()
        self._backend.set_mode(Mode.STAND)
        self._backend.set_height(height)

    def rest(self) -> None:
        self.stop()
        self._backend.set_mode(Mode.REST)

    def deactivate(self) -> None:
        self.stop()
        self._backend.set_mode(Mode.DEACTIVATED)
        self._intended = None  # the odometry starts afresh

    def set_gait(self, gait: Gait) -> None:
        self._backend.set_gait(gait)

    def set_velocity(self, vx: float, vy: float, yaw_rate: float) -> Velocity:
        velocity = Velocity(vx, vy, yaw_rate)
        if velocity != ZERO:
            mode = self._backend.state().mode
            if mode not in WALKS_FROM:
                raise RobotError(f"the robot is {mode.name}: stand() it before walking")
            if mode is not Mode.WALK:
                self._backend.set_mode(Mode.WALK)  # STAND reads the same sticks as a shift and tilt of the body
            self._intended = None
        return self._backend.set_velocity(velocity)

    def stop(self) -> None:
        self._backend.set_velocity(ZERO)

    def state(self) -> RobotState:
        return self._backend.state()

    def calibrate(self) -> Calibration:
        return self._backend.calibrate()

    def max_velocity(self) -> Velocity:
        return self._backend.max_velocity()

    def features(self) -> pb.FeaturesDataResponse:
        """What the robot reported about itself on connecting: its variant and firmware version among them."""
        return self._backend.features

    def servo_settings(self) -> ServoSetup:
        """The robot's servo calibration and its variant's joint model."""
        return self._backend.servo_settings()

    def servo_output(self) -> tuple[np.ndarray, np.ndarray]:
        """The last write's joint angles (degrees, servo space) and PWMs, in joint order. A raw PWM stops the writes,
        so these stay what they were before it until a mode applies again."""
        return self._backend.servo_output()

    def set_servo_pwm(self, channel: int, pwm: int) -> None:
        """One PCA9685 channel, or every joint's for -1, to a raw PWM, which the robot holds within the servos' range
        (125-600) and nothing else: no joint's travel bounds it. The servos then leave the motion, every other one
        holding its last PWM, until a mode applies again (stand() or rest())."""
        self._backend.set_servo_pwm(channel, pwm)

    @contextmanager
    def recording(self, path):
        """Writes the robot's telemetry to the .leika file `path` while the block runs, as record.py does
        (src/sim/recording.py): the header, every tick, the network samples and the goal statuses. The file is complete
        up to the last frame also when the block raises."""
        writer = RecordingWriter(path)
        try:
            self._backend.stream_telemetry(writer.write)
            yield
        finally:
            try:
                self._backend.stream_telemetry(None)
            finally:
                writer.close()

    def odometry(self) -> Odometry:
        """Where the robot's own dead reckoning puts it since the last reset or deactivation."""
        return self._backend.odometry()

    def reset_odometry(self) -> Odometry:
        """Starts the dead reckoning afresh, in the frame the robot faces, which is then the heading the goals mean;
        returns where it had got to."""
        reached = self._backend.reset_odometry()
        self._intended = 0.0
        return reached

    def supports_goals(self) -> bool:
        """Whether the robot walks a distance or turns an angle itself, closed on its own odometry."""
        return self._backend.supports_goals()

    def goal_move(self, forward: float = 0.0, sideways: float = 0.0, turn_deg: float = 0.0,
                  speed: float = 0.5) -> float:
        """Walks `forward` or `sideways` metres, or turns `turn_deg` counter-clockwise (one of them, the others zero), at
        `speed` of the gait's top, on the robot's own odometry: a link that stalls for less than 2 s cannot cut it short.
        Returns once the robot reports it reached, standing; raises GoalCancelled if it ended early and RobotTimeout if
        it gave up.

        A walk holds the heading it starts on, and the next turn takes up what it drifts: after a walk that drifted 2
        degrees to the left, a 90 degree turn turns 88. Returns the turn it asked of the robot, in degrees.

        The odometry's distances count what the gait commanded, so until they are calibrated the robot lands off by
        their error; its heading fuses the gyro.
        """
        targets = (forward, sideways, turn_deg)
        if sum(t != 0 for t in targets) > 1:
            raise ValueError(f"a goal takes one target, not {targets}")
        if not any(targets):
            return 0.0
        mode = self._backend.state().mode
        if mode not in WALKS_FROM:
            raise RobotError(f"the robot is {mode.name}: stand() it before a goal")
        if mode is Mode.REST:
            self._backend.set_mode(Mode.STAND)  # a goal starts from STAND or WALK
        current = self._backend.odometry().yaw
        intended = current if self._intended is None else self._intended
        # Only the drift is wrapped, so a turn of 360 degrees stays one.
        asked = turn_deg + _wrap(intended - current) if turn_deg else 0.0
        self._intended = None
        if forward or sideways or asked:
            self._backend.goal_move(forward, sideways, asked, speed)
        self._intended = intended + turn_deg
        return asked

    def time(self) -> float:
        return self._backend.now()

    def sleep(self, seconds: float) -> None:
        self._backend.sleep(seconds)

    def move_forward(self, metres: float, speed: float | None = None) -> None:
        motions.move(self, metres, 0.0, speed)

    def move_sideways(self, metres: float, speed: float | None = None) -> None:
        motions.move(self, 0.0, metres, speed)

    def rotate(self, degrees: float, rate: float | None = None) -> None:
        motions.rotate(self, degrees, rate)
