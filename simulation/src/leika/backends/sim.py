"""The MuJoCo spot_pico simulation behind the backend interface: the ported firmware gait, odometry and goal controller,
stepped at the control rate.

A goal move runs the firmware's controller on the firmware's odometry, as the robot does, rather than the host-side
motions: so it lands where the robot would, by its odometry, whose distances fall 7 to 10 % short of MuJoCo's base
and whose heading, fused with MuJoCo's gyro, turns within 1 % of it.
"""
import math
import threading
import time

import mujoco
import numpy as np

from ...robot.firmware_gait import (DEFAULT_FEET, FIRMWARE_FEET, STAND_HEIGHT, TROT, VARIANT, BodyState, GaitController,
                                    GaitState, gait_action, set_mode)
from ...robot.stand_compensation import MODEL_TO_REP, StandCompensation, apply_firmware_attitude, imu_roll_pitch
from ...robot.firmware_goal import Goal, GoalController, GoalState
from ...robot.firmware_odometry import BodyStep, FusedOdometry
from ...sim.mj_runtime import CONTROL_DT, SpotPicoSim
from .. import speed_model
from ..constants import Gait, Mode
from .base import ZERO, Calibration, GoalCancelled, Odometry, RobotError, RobotState, RobotTimeout, Velocity

NO_STICKS = speed_model.Sticks(0.0, 0.0, 0.0)
# The frame odometry.h takes the feet in: forward, up, right.
MODEL_TO_FIRMWARE = np.array([[0.0, -1.0, 0.0], [0.0, 0.0, 1.0], [-1.0, 0.0, 0.0]])
# The firmware's odometry sees its own feet: each foot's offset from its home, about the firmware's home.
TO_FIRMWARE_FEET = np.column_stack([FIRMWARE_FEET, DEFAULT_FEET[:, 2]]) - DEFAULT_FEET


class SimBackend:
    def __init__(self, realtime: bool = True):
        self.realtime = realtime
        self.sim = SpotPicoSim()
        self._gait = GaitState()
        set_mode(self._gait, TROT)
        self._controller = GaitController()
        self._body = BodyState()
        self._levelling = StandCompensation()
        self._odometry = FusedOdometry()
        self._goal = GoalController()
        self._mode = Mode.REST
        self._top = speed_model.max_velocity(VARIANT, Gait.TROT, STAND_HEIGHT)
        self._sticks = NO_STICKS
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

    # One control tick: the gait follows the velocity while walking, as the firmware's does, and holds still otherwise;
    # in STAND the body levels itself against the IMU's tilt, as the firmware's stand state does.
    def _step(self) -> None:
        with self._lock:
            walking = self._mode is Mode.WALK
            gait_action(self._sticks if walking else NO_STICKS, self._gait)
            if not walking:
                # The firmware's walk state is not running, so no stroke eases out; a raised foot lands along its swing.
                self._gait.step_x = self._gait.step_z = self._gait.step_angle = 0.0
                self._gait.smoothing = np.zeros(3)
            self._controller.step(self._gait, CONTROL_DT)
            self._controller.generate_feet(self._gait, self._body)
            roll, pitch = imu_roll_pitch(self.sim.base_quat())
            omega, psi = self._levelling.step(roll, pitch, CONTROL_DT, self._mode is Mode.STAND)
            apply_firmware_attitude(self._body, omega, psi)
            # The gyro's yaw rate as the firmware reads it, from the IMU sample the physics left; MuJoCo's has no bias.
            gyro_rate = float((MODEL_TO_REP @ self.sim.gyro())[2])
            step = self._odometry.update((self._body.feet + TO_FIRMWARE_FEET) @ MODEL_TO_FIRMWARE.T,
                                         self._controller.stance(self._gait), gyro_rate=gyro_rate, dt=CONTROL_DT)
            if self._goal.running:
                self._drive_goal(step, CONTROL_DT)
            self.sim.set_joint_targets(self.sim.body_targets_from_feet(self._body))
            self.sim.step_physics()
            self._t += CONTROL_DT

    # As the firmware's MotionService: the goal's sticks for the next tick, and once reached, STAND with them centred.
    def _drive_goal(self, step: BodyStep, dt: float) -> None:
        sticks = self._goal.update(step, dt)
        self._sticks = speed_model.Sticks(-sticks.left, sticks.forward, sticks.turn)
        if self._goal.state is GoalState.REACHED:
            self._mode = Mode.STAND

    def _cancel_goal(self) -> None:
        if self._goal.running:
            self._goal.cancel()
            self._sticks = NO_STICKS

    def supports_goals(self) -> bool:
        return True

    def goal_move(self, forward: float, sideways: float, turn_deg: float, speed: float) -> None:
        goal = Goal(forward, sideways, math.radians(turn_deg), speed)
        if not goal.valid():
            raise ValueError(f"a goal takes one target at a speed up to 1, not {goal}")
        with self._lock:
            if self._mode not in (Mode.STAND, Mode.WALK):
                raise RobotError(f"the robot is {self._mode.name}: a goal starts from STAND or WALK")
            # As MotionService::goalMove, at the speeds a full stick commands (WalkState::stanceRate).
            linear, angular = speed_model.per_stick(VARIANT, Gait.TROT)
            self._goal = GoalController()
            self._goal.start(goal, linear, angular)
            self._mode = Mode.WALK
            self._drive_goal(BodyStep(), 0.0)
        try:
            while self._goal.running:
                self.sleep(CONTROL_DT)
        finally:
            with self._lock:
                self._cancel_goal()  # an exception, Ctrl-C included, leaves no goal walking
        if self._goal.state is GoalState.TIMED_OUT:
            raise RobotTimeout(f"the goal came {self._goal.progress:.3f} of {goal} in {self._goal.timeout:.1f} s")
        if self._goal.state is GoalState.CANCELLED:
            raise GoalCancelled("the goal was cancelled")

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
            self._cancel_goal()
            if mode is not self._mode:
                self._levelling.reset()
            self._mode = mode
            if mode is Mode.DEACTIVATED:
                self._odometry = FusedOdometry()

    # The port has the trot only: it lacks the crawl's body shift over the stance feet (WalkState::updateBodyPosition).
    def set_gait(self, gait: Gait) -> None:
        if gait is not Gait.TROT:
            raise RobotError(f"the simulation models the trot only, not {gait.name}")
        with self._lock:
            self._cancel_goal()
            set_mode(self._gait, TROT)

    def set_height(self, height: float) -> None:
        """The simulated gait stands at the one height the client defaults to (firmware_gait.STAND_HEIGHT)."""

    def max_velocity(self) -> Velocity:
        return self._top

    def set_velocity(self, velocity: Velocity) -> Velocity:
        top = self.max_velocity()
        clamp = lambda value, limit: max(-limit, min(limit, value))
        commanded = Velocity(clamp(velocity.vx, top.vx), clamp(velocity.vy, top.vy), clamp(velocity.yaw_rate, top.yaw_rate))
        sticks = speed_model.sticks_for(VARIANT, Gait.TROT, commanded, STAND_HEIGHT)
        with self._lock:
            # As the app's sticks: centred ones leave a goal its own, any other takes over.
            if self._goal.running:
                if commanded == ZERO:
                    return commanded
                self._goal.cancel()
            self._sticks = sticks
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

    def odometry(self) -> Odometry:
        with self._lock:
            return self._reported()

    def reset_odometry(self) -> Odometry:
        with self._lock:
            reached = self._reported()
            self._odometry.reset()
            return reached

    def _reported(self) -> Odometry:
        e = self._odometry.estimate
        return Odometry(x=e.x, y=e.y, yaw=math.degrees(e.yaw), distance=e.distance,
                        kinematic_yaw=math.degrees(self._odometry.kinematic_yaw))
