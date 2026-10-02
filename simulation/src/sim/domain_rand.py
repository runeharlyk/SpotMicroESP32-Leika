"""Domain randomization for sim-to-real transfer.

Applied by `QuadrupedMjEnv` when `randomize=True`. Per-episode it perturbs the MODEL
(masses/inertia, CoM, friction, motor strength) and sets per-episode sensor biases,
action latency, and a push schedule. Per-step it adds IMU noise and applies pushes.

Ranges are conservative and scaled to spot_pico (~0.6 kg, open-loop micro-servos). The most
important term for this robot (WiFi/BLE link + no joint encoders) is action latency.
Calibrate the IMU noise ranges against real stationary logs later.
"""

import numpy as np
import mujoco

# The ranges below are assumptions until real recordings replace them (see report_recording.py).
GYRO_BIAS_STD = 0.05       # rad/s, per episode
GYRO_NOISE_STD = 0.05      # rad/s, per step
RPY_BIAS_DEG = 5.0         # degrees, per episode, uniform +-
RPY_NOISE_DEG = 1.5        # degrees, per step
GRAVITY_NOISE_STD = 0.02   # per step, on the unit gravity vector
ACTION_LATENCY_STEPS = (0, 3)  # rng.integers bounds: 0 to 2 control steps
CONTROL_DT = 0.01          # s, the 100 Hz control step the latency counts in


class DomainRandomizer:
    def __init__(self, model):
        self.base_body_mass = model.body_mass.copy()
        self.base_body_inertia = model.body_inertia.copy()
        self.base_body_ipos = model.body_ipos.copy()
        self.base_geom_friction = model.geom_friction.copy()
        self.base_gainprm = model.actuator_gainprm.copy()
        self.base_biasprm = model.actuator_biasprm.copy()
        self.base_forcerange = model.actuator_forcerange.copy()
        self.base_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "base_link")

        # per-episode state
        self.gyro_bias = np.zeros(3)
        self.rpy_bias = np.zeros(3)
        self.action_latency_steps = 0
        self.next_push = 10**9
        self.push_steps_left = 0
        self.push_force = np.zeros(3)

    # -------------------------------------------------- per-episode (at reset)
    def reset_episode(self, model, rng):
        scale = rng.uniform(0.85, 1.15, size=model.nbody)
        model.body_mass[:] = self.base_body_mass * scale
        model.body_inertia[:] = self.base_body_inertia * scale[:, None]
        # base CoM offset (battery placement uncertainty): +/-8mm xy, +/-5mm z
        model.body_ipos[self.base_id] = self.base_body_ipos[self.base_id] + rng.uniform(
            [-0.008, -0.008, -0.005], [0.008, 0.008, 0.005]
        )
        # ground/foot sliding friction
        model.geom_friction[:, 0] = self.base_geom_friction[:, 0] * rng.uniform(0.6, 1.4)
        # motor strength: position actuator kp lives in gainprm[:,0] and biasprm[:,1] = -kp
        kp = rng.uniform(0.85, 1.15)
        model.actuator_gainprm[:, 0] = self.base_gainprm[:, 0] * kp
        model.actuator_biasprm[:, 1] = self.base_biasprm[:, 1] * kp
        model.actuator_forcerange[:] = self.base_forcerange * rng.uniform(0.9, 1.1)

        # per-episode sensor biases + latency
        self.gyro_bias = rng.normal(0.0, GYRO_BIAS_STD, size=3)
        self.rpy_bias = np.deg2rad(rng.uniform(-RPY_BIAS_DEG, RPY_BIAS_DEG, size=3))
        self.action_latency_steps = int(rng.integers(*ACTION_LATENCY_STEPS))

        # push schedule (every ~1.5-2.5 s at 100 Hz)
        self.next_push = int(rng.integers(150, 250))
        self.push_steps_left = 0
        self.push_force[:] = 0.0

    # -------------------------------------------------- per-step
    def maybe_push(self, model, data, rng, step):
        if step >= self.next_push and self.push_steps_left == 0:
            self.push_force = rng.uniform(-1, 1, size=3) * np.array([0.6, 0.6, 0.2])  # N (0.6 kg robot)
            self.push_steps_left = 5
            self.next_push = step + int(rng.integers(150, 250))
        if self.push_steps_left > 0:
            data.xfrc_applied[self.base_id, :3] = self.push_force
            self.push_steps_left -= 1
        else:
            data.xfrc_applied[self.base_id, :3] = 0.0

    def noisy_imu(self, grav, gyro, rpy, rng):
        gyro = gyro + self.gyro_bias + rng.normal(0.0, GYRO_NOISE_STD, 3)
        rpy = rpy + self.rpy_bias + np.deg2rad(rng.normal(0.0, RPY_NOISE_DEG, 3))
        grav = grav + rng.normal(0.0, GRAVITY_NOISE_STD, 3)
        return grav, gyro, rpy
