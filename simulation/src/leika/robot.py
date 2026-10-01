"""One Robot for the simulation and the real robot: Robot("simulation") or Robot("192.168.1.39").

Velocities are REP-103 (forward, left, counter-clockwise) in m/s and rad/s; angles in degrees. Leaving the `with`
block, on an exception or Ctrl-C included, stops the robot and lets it rest; it never deactivates on its own.
"""
from . import motions
from .backends.base import ZERO, Backend, Calibration, RobotError, RobotState, Velocity
from .constants import Gait, Mode


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

    def __enter__(self) -> "Robot":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        try:
            self.rest()
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

    def set_gait(self, gait: Gait) -> None:
        self._backend.set_gait(gait)

    def set_velocity(self, vx: float, vy: float, yaw_rate: float) -> Velocity:
        velocity = Velocity(vx, vy, yaw_rate)
        if velocity != ZERO and self._backend.state().mode is not Mode.WALK:
            self._backend.set_mode(Mode.WALK)  # STAND reads the same sticks as a shift and tilt of the body
        return self._backend.set_velocity(velocity)

    def stop(self) -> None:
        self._backend.set_velocity(ZERO)

    def state(self) -> RobotState:
        return self._backend.state()

    def calibrate(self) -> Calibration:
        return self._backend.calibrate()

    def max_velocity(self) -> Velocity:
        return self._backend.max_velocity()

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
