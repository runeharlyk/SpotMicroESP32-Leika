"""The MuJoCo spot_pico simulation behind the backend interface: the ported firmware gait, stepped at the control rate."""
import math
import threading
import time

import mujoco
import numpy as np

from ...robot.firmware_gait import GAIT_COEF, TROT, BodyState, GaitController, GaitState, analytic_gait_action, set_mode
from ...sim.mj_runtime import CONTROL_DT, SpotPicoSim
from ..constants import Gait, Mode
from .base import ZERO, Calibration, RobotError, RobotState, Velocity

# The model's axes are +X left, +Y rear, +Z up; REP-103 is forward, left, up.
MODEL_TO_REP = np.array([[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])


class SimBackend:
    def __init__(self, realtime: bool = True):
        self.realtime = realtime
        self.sim = SpotPicoSim()
        self._gait = GaitState()
        set_mode(self._gait, TROT)
        self._controller = GaitController()
        self._body = BodyState()
        self._mode = Mode.REST
        self._velocity = ZERO
        self._t = 0.0
        self._lock = threading.Lock()
        self._running = False
        self._thread = None

    def connect(self) -> None:
        self.sim.reset_to_stand()
        if self.realtime:
            self._running = True
            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()

    def close(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join()

    def _run(self) -> None:
        while self._running:
            start = time.monotonic()
            self._step()
            time.sleep(max(0.0, CONTROL_DT - (time.monotonic() - start)))

    # One control tick: the gait follows the velocity while walking, as the firmware's does, and holds still otherwise.
    def _step(self) -> None:
        with self._lock:
            v = self._velocity if self._mode is Mode.WALK else ZERO
            analytic_gait_action(np.array([v.vx, v.vy, v.yaw_rate], dtype=np.float32), self._gait)
            self._controller.advance_phase(self._gait, CONTROL_DT)
            self._controller.generate_feet(self._gait, self._body)
            self.sim.set_joint_targets(self.sim.body_targets_from_feet(self._body))
            self.sim.step_physics()
            self._t += CONTROL_DT

    def now(self) -> float:
        return self._t

    def sleep(self, seconds: float) -> None:
        if self.realtime:
            time.sleep(seconds)
            return
        for _ in range(round(seconds / CONTROL_DT)):
            self._step()

    def set_mode(self, mode: Mode) -> None:
        with self._lock:
            self._mode = mode

    # The gait's command gains (GAIT_COEF) are fitted for trot: in crawl it walks about a fifth of the velocity it
    # reports, so a distance move would land far short.
    def set_gait(self, gait: Gait) -> None:
        if gait is not Gait.TROT:
            raise RobotError(f"the simulation models the trot only, not {gait.name}")
        with self._lock:
            set_mode(self._gait, TROT)

    def set_height(self, height: float) -> None:
        """The simulated gait stands at one height."""

    def max_velocity(self) -> Velocity:
        return Velocity(GAIT_COEF["gain_x"], GAIT_COEF["gain_y"], GAIT_COEF["gain_yaw"])

    def set_velocity(self, velocity: Velocity) -> Velocity:
        top = self.max_velocity()
        clamp = lambda value, limit: max(-limit, min(limit, value))
        commanded = Velocity(clamp(velocity.vx, top.vx), clamp(velocity.vy, top.vy), clamp(velocity.yaw_rate, top.yaw_rate))
        with self._lock:
            self._velocity = commanded
        return commanded

    def state(self) -> RobotState:
        with self._lock:
            matrix = np.zeros(9)
            mujoco.mju_quat2Mat(matrix, self.sim.base_quat())
            r = MODEL_TO_REP @ matrix.reshape(3, 3) @ MODEL_TO_REP.T
            gyro = MODEL_TO_REP @ self.sim.gyro()
            accel = MODEL_TO_REP @ self.sim.accel()
            joints = np.degrees(self.sim.data.qpos[self.sim.qpos_adr])
            return RobotState(
                t=self._t,
                mode=self._mode,
                roll=math.degrees(math.atan2(r[2, 1], r[2, 2])),
                pitch=math.degrees(-math.asin(max(-1.0, min(1.0, r[2, 0])))),
                yaw=math.degrees(math.atan2(r[1, 0], r[0, 0])),
                gyro=tuple(float(g) for g in gyro),
                accel=tuple(float(a) for a in accel),
                joints=tuple(float(j) for j in joints),
                link_lost=False,
                link_drops=0,
            )

    def calibrate(self) -> Calibration:
        return Calibration(still=True, levelled=True, tilt_deg=0.0)
