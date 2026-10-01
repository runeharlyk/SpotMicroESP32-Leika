"""Shared MuJoCo runtime for spot_pico, reused by replay + the RL env.

Verified facts:
  - Actuator/qpos joint order is [fr, fl, rr, rl] x [hip, femur, tibia], which is exactly the
    flatten order of Kinematics.inverse_kinematics(...).reshape(4, 3).
  - IK matches MuJoCo forward kinematics to 0.0 mm (see the module self-test), so
    ctrl[joint] = ik_angle_in_radians directly.
  - Physics timestep 0.002 s; gait/policy control at 0.01 s (100 Hz, firmware rate) -> frame_skip = 5.
"""

import os

import numpy as np
import mujoco

from src.robot.firmware_gait import Kinematics, BodyState, STAND_Z

MODEL_PATH = os.path.join(os.path.dirname(__file__), "..", "resources", "spot_pico", "scene.xml")
TERRAIN_MODEL_PATH = os.path.join(os.path.dirname(__file__), "..", "resources", "spot_pico", "scene_terrain.xml")
CONTROL_DT = 0.01  # 100 Hz, matches the firmware control loop
LEG_NAMES = ("fr", "fl", "rr", "rl")
JOINT_NAMES = [f"{lg}_{j}_joint" for lg in LEG_NAMES for j in ("hip", "femur", "tibia")]


class SpotPicoSim:
    """Thin wrapper: loads the model, maps IK angles (rad) to actuators, steps physics."""

    def __init__(self, model_path: str = MODEL_PATH):
        self.model = mujoco.MjModel.from_xml_path(os.path.abspath(model_path))
        self.data = mujoco.MjData(self.model)
        self.kin = Kinematics()
        self.frame_skip = int(round(CONTROL_DT / self.model.opt.timestep))

        nid = lambda kind, n: mujoco.mj_name2id(self.model, kind, n)
        self.act_ids = np.array([nid(mujoco.mjtObj.mjOBJ_ACTUATOR, n) for n in JOINT_NAMES])
        self.qpos_adr = np.array(
            [self.model.jnt_qposadr[nid(mujoco.mjtObj.mjOBJ_JOINT, n)] for n in JOINT_NAMES]
        )
        self.base_id = nid(mujoco.mjtObj.mjOBJ_BODY, "base_link")
        self.base_qposadr = self.model.jnt_qposadr[nid(mujoco.mjtObj.mjOBJ_JOINT, "root")]
        self.imu_site = nid(mujoco.mjtObj.mjOBJ_SITE, "imu")
        self.foot_site_ids = np.array([nid(mujoco.mjtObj.mjOBJ_SITE, f"foot_{lg}") for lg in LEG_NAMES])
        self.foot_geom_ids = np.array([nid(mujoco.mjtObj.mjOBJ_GEOM, f"foot_{lg}") for lg in LEG_NAMES])
        self.ground_geom_id = nid(mujoco.mjtObj.mjOBJ_GEOM, "floor")
        self._gyro_slice = self._sensor_slice("imu_gyro")
        self._accel_slice = self._sensor_slice("imu_acc")

    @property
    def stand_pose(self) -> np.ndarray:
        """12 joint angles (rad) for the nominal standing pose."""
        return self.kin.inverse_kinematics(BodyState())

    def reset_to_stand(self):
        mujoco.mj_resetData(self.model, self.data)
        stand = self.stand_pose
        a = self.base_qposadr
        self.data.qpos[a : a + 7] = [0.0, 0.0, STAND_Z, 1.0, 0.0, 0.0, 0.0]
        self.data.qpos[self.qpos_adr] = stand
        mujoco.mj_forward(self.model, self.data)
        self.data.ctrl[self.act_ids] = stand

    def set_joint_targets(self, angles_rad: np.ndarray):
        self.data.ctrl[self.act_ids] = angles_rad

    def step_physics(self, n: int | None = None):
        for _ in range(n if n is not None else self.frame_skip):
            mujoco.mj_step(self.model, self.data)

    def body_targets_from_feet(self, body: BodyState) -> np.ndarray:
        return self.kin.inverse_kinematics(body)

    # --- hardware-available state (for the RL observation) ---
    def base_quat(self) -> np.ndarray:
        a = self.base_qposadr
        return self.data.qpos[a + 3 : a + 7].copy()  # w, x, y, z

    def base_height(self) -> float:
        return float(self.data.qpos[self.base_qposadr + 2])

    def gyro(self) -> np.ndarray:
        return self.data.sensordata[self._gyro_slice].copy()

    def accel(self) -> np.ndarray:
        return self.data.sensordata[self._accel_slice].copy()

    def _sensor_slice(self, name):
        sid = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_SENSOR, name)
        adr = self.model.sensor_adr[sid]
        dim = self.model.sensor_dim[sid]
        return slice(adr, adr + dim)

    def actuator_power(self) -> float:
        """Mechanical power sum |torque * joint_velocity| (W): cost-of-transport signal."""
        return float(np.sum(np.abs(self.data.actuator_force * self.data.actuator_velocity)))

    def foot_slip_sq(self) -> float:
        """Sum of squared horizontal foot speed over feet currently touching the ground."""
        d = self.data
        contact_feet = set()
        for c in range(d.ncon):
            g1, g2 = d.contact[c].geom1, d.contact[c].geom2
            for i, fg in enumerate(self.foot_geom_ids):
                if (g1 == fg and g2 == self.ground_geom_id) or (g2 == fg and g1 == self.ground_geom_id):
                    contact_feet.add(i)
        total = 0.0
        res = np.zeros(6)
        for i in contact_feet:
            mujoco.mj_objectVelocity(
                self.model, d, mujoco.mjtObj.mjOBJ_SITE, int(self.foot_site_ids[i]), res, 0
            )
            total += res[3] ** 2 + res[4] ** 2  # world-frame x, y linear velocity
        return float(total)
