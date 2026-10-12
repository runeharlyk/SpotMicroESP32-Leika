"""spot_pico standing on a uniformly tilted floor, levelled (or not) by the ported firmware stand compensation."""

from __future__ import annotations

import math

import mujoco
import numpy as np

from src.robot.firmware_gait import STAND_Z, BodyState
from src.robot.stand_compensation import FIRMWARE_FROM_REP, StandCompensation, apply_firmware_attitude, imu_roll_pitch
from src.sim.mj_runtime import CONTROL_DT, SpotPicoSim

# Floor tilt axes (model frame: +X left, +Y rear) for which a positive slope reads as positive REP-103 pitch (nose
# down) and positive REP-103 roll (left side up).
NOSE_DOWN = (1.0, 0.0, 0.0)
LEFT_UP = (0.0, -1.0, 0.0)
# MuJoCo's soft pyramidal friction lets a still robot creep down the slope (11 mm/s at 8 degrees with the scene's
# defaults); an elliptic cone with a stiff tangential impedance holds the feet where they stand, as rubber feet on a
# ramp do. (The no-slip pass holds them too, but rocks the body by 3 degrees.)
IMPRATIO = 100.0


def stand_on_slope(tilt_axis, slope_deg: float, levelling: bool, seconds: float = 4.0,
                   firmware_from_rep: np.ndarray = FIRMWARE_FROM_REP) -> np.ndarray:
    """Stands the robot normal to a floor tilted by `slope_deg` about `tilt_axis` (model frame) and returns the
    IMU's REP-103 (roll, pitch) in degrees at every control tick."""
    half = math.radians(slope_deg) / 2
    floor = np.array([math.cos(half), *(math.sin(half) * np.asarray(tilt_axis, dtype=float))])
    sim = SpotPicoSim(floor_quat=floor)
    sim.model.opt.cone = mujoco.mjtCone.mjCONE_ELLIPTIC
    sim.model.opt.impratio = IMPRATIO
    sim.reset_to_stand()
    tilt = np.zeros(9)
    mujoco.mju_quat2Mat(tilt, floor)
    base = sim.base_qposadr
    sim.data.qpos[base:base + 3] = tilt.reshape(3, 3) @ np.array([0.0, 0.0, STAND_Z])
    sim.data.qpos[base + 3:base + 7] = floor
    mujoco.mj_forward(sim.model, sim.data)

    compensation, body, readings = StandCompensation(), BodyState(), []
    for _ in range(round(seconds / CONTROL_DT)):
        roll, pitch = imu_roll_pitch(sim.base_quat())
        readings.append((roll, pitch))
        omega, psi = compensation.step(roll, pitch, CONTROL_DT, levelling)
        apply_firmware_attitude(body, omega, psi, firmware_from_rep)
        sim.set_joint_targets(sim.body_targets_from_feet(body))
        sim.step_physics()
    return np.degrees(np.array(readings))
