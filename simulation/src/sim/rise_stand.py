"""spot_pico rising from REST to STAND, as the firmware moves it, with the ported stand compensation on or off.

REST and STAND keep the feet where they are and differ in the body's height: min_body_height (0.45 of the leg's reach)
and the middle of the height range (0.675 of it) in kinematics.h, scaled here to the model's legs. The height follows
the same critically damped smoothing as the firmware's body targets. Each servo turns toward its target no faster
than its speed, 720 deg/s by default as servo_controller.h limits it; a slower one stands in for a servo that lags
under load.
"""

from __future__ import annotations

import math

import mujoco
import numpy as np

from src.robot.firmware_gait import _CFG, BODY_HEIGHT, STAND_Z, BodyState
from src.robot.stand_compensation import (
    FEET_EASE_S,
    SMOOTHING_OMEGA,
    CriticalDamper,
    StandCompensation,
    apply_firmware_attitude,
    imu_roll_pitch,
)
from src.sim.mj_runtime import CONTROL_DT, SpotPicoSim
from src.sim.slope_stand import IMPRATIO

REACH = _CFG.legs["fr"]["L1"] + _CFG.legs["fr"]["L2"]
REST_DEPTH = 0.45 * REACH
STAND_DEPTH = 0.675 * REACH
SERVO_MAX_SPEED_DEG_S = 720.0
# The rear right leg's servos turning at 30 deg/s, as servos lagging under load might: the rise then tips the body by
# about 5 degrees, which lets the simulation show what the compensation does with a tilt the rise causes.
LAGGING_LEG = np.where(np.arange(12) // 3 == 2, 30.0, SERVO_MAX_SPEED_DEG_S)
REST_S = 1.0


def rise_to_stand(compensate: bool, seconds: float = 4.0, servo_speed_deg_s=SERVO_MAX_SPEED_DEG_S,
                  floor_quat=None, hold_s: float = FEET_EASE_S) -> np.ndarray:
    """Rests for REST_S on a floor tilted by `floor_quat` (w, x, y, z; flat when None), then stands for `seconds`.
    `servo_speed_deg_s` is one speed or one per joint, in actuator order; `hold_s` is the compensation's hold after the
    switch (StandCompensation). Returns, for every control tick from the
    switch to STAND, the time (s) and the IMU's REP-103 roll and pitch (degrees)."""
    sim = SpotPicoSim(floor_quat=floor_quat)
    sim.model.opt.cone = mujoco.mjtCone.mjCONE_ELLIPTIC
    sim.model.opt.impratio = IMPRATIO
    sim.reset_to_stand()
    body = BodyState(zm=REST_DEPTH - BODY_HEIGHT)
    joints = sim.body_targets_from_feet(body)
    floor = np.zeros(9)
    mujoco.mju_quat2Mat(floor, np.array([1.0, 0.0, 0.0, 0.0]) if floor_quat is None else np.asarray(floor_quat))
    base = sim.base_qposadr
    sim.data.qpos[base:base + 3] = floor.reshape(3, 3) @ np.array([0.0, 0.0, STAND_Z + body.zm])
    sim.data.qpos[base + 3:base + 7] = [1.0, 0.0, 0.0, 0.0] if floor_quat is None else floor_quat
    sim.data.qpos[sim.qpos_adr] = joints
    mujoco.mj_forward(sim.model, sim.data)

    max_step = np.radians(np.broadcast_to(servo_speed_deg_s, joints.shape)) * CONTROL_DT
    compensation, height = StandCompensation(hold_s), CriticalDamper()
    rest_ticks, readings = round(REST_S / CONTROL_DT), []
    for tick in range(rest_ticks + round(seconds / CONTROL_DT)):
        standing = tick >= rest_ticks
        if tick == rest_ticks:
            compensation.reset()
            height = CriticalDamper()
        target_zm = (STAND_DEPTH if standing else REST_DEPTH) - BODY_HEIGHT
        body.zm = height.step(body.zm, target_zm, CONTROL_DT, SMOOTHING_OMEGA)
        roll, pitch = imu_roll_pitch(sim.base_quat())
        if standing:
            readings.append(((tick - rest_ticks) * CONTROL_DT, math.degrees(roll), math.degrees(pitch)))
        omega, psi = compensation.step(roll, pitch, CONTROL_DT, standing and compensate)
        apply_firmware_attitude(body, omega, psi)
        joints = joints + np.clip(sim.body_targets_from_feet(body) - joints, -max_step, max_step)
        sim.set_joint_targets(joints)
        sim.step_physics()
    return np.array(readings)


def settle_time(readings: np.ndarray, band_deg: float = 0.5) -> float:
    """The time after which the body's tilt stays within `band_deg` of level."""
    outside = np.nonzero(np.hypot(readings[:, 1], readings[:, 2]) > band_deg)[0]
    if not len(outside):
        return 0.0
    return math.inf if outside[-1] == len(readings) - 1 else float(readings[outside[-1] + 1, 0])
