"""MuJoCo Gymnasium env for spot_pico locomotion, designed for sim-to-real.

Command: robot-frame velocity [vx_forward, vy_left] (m/s) + yaw rate (rad/s) -- what the reward tracks.

Control mode: "residual_pure" (the deploy target). The baseline gait is the ported ESP32
firmware gait driven by a deterministic command->gait map (analytic_gait_action); the policy
outputs ONLY per-leg foot XYZ residuals (4 legs x 3 = 12) added to the gait's foot targets
before IK. A zero action reproduces the firmware gait exactly, so training starts at a working
gait and only learns stabilizing corrections.

Observation (hardware-available ONLY -- open-loop micro-servos have no encoders):
  gravity vector in body frame (3) + gyro (3) + rpy (3) + previous commanded joint angles (12)
  + gait phase clock [sin, cos] (2) + command (3) + previous action (12) = 38.
Deliberately excludes base position, base linear velocity, measured joint pos/vel (unobservable
on the real robot). Those are used for REWARD only.
"""

from __future__ import annotations

from collections import deque

import numpy as np
import gymnasium as gym
import mujoco

from src.sim.mj_runtime import SpotPicoSim, CONTROL_DT, TERRAIN_MODEL_PATH
from src.sim.domain_rand import DomainRandomizer
from src.sim.terrain import randomize_hfield
from src.robot.firmware_gait import (
    GaitController,
    GaitState,
    BodyState,
    Kinematics,
    analytic_gait_action,
    set_mode,
    TROT,
    DEFAULT_FEET,
    STAND_Z,
)

ACT_DIM = 12  # 4 legs x XYZ foot residual
FOOT_RESIDUAL = 0.015  # m, per-leg residual authority added on top of the gait

# command sampling ranges (robot frame). Forward is primary; lateral/yaw phase in via curriculum.
CMD_VX = (-0.04, 0.07)
CMD_VY = (-0.03, 0.03)
CMD_YAW = (-1.2, 1.2)
ZERO_CMD_PROB = 0.05

# velocity-tracking kernel widths (ANYmal-style exp kernels), scaled to spot_pico's SMALL
# command magnitudes (~0.03-0.07 m/s). Too wide (e.g. 0.02) and missing the command is nearly
# free; too tight (e.g. 0.0006) and the kernel is a near-delta with no learning gradient. 0.0025
# gives a strong gradient across the command range: err 0.01 -> 0.96, err 0.05 -> 0.37 of peak.
VEL_SIGMA = 0.0025
YAW_SIGMA = 0.15


def _quat_to_rpy(q):
    w, x, y, z = q
    roll = np.arctan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
    pitch = np.arcsin(np.clip(2 * (w * y - z * x), -1, 1))
    yaw = np.arctan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
    return np.array([roll, pitch, yaw])


def _rot_vec_by_conj(q, v):
    """Express world vector v in the body frame: R(q)^T v."""
    out = np.zeros(3)
    conj = np.array([q[0], -q[1], -q[2], -q[3]])
    mujoco.mju_rotVecQuat(out, np.ascontiguousarray(v, dtype=float), conj)
    return out


class QuadrupedMjEnv(gym.Env):
    metadata = {"render_modes": []}

    def __init__(self, randomize: bool = False, episode_seconds: float = 20.0,
                 seed: int | None = None, command: tuple | None = None,
                 resample_steps: int = 0, terrain: float = 0.0):
        super().__init__()
        self.randomize = randomize
        self.terrain = terrain  # >0: max bump height (m) of per-episode random heightfield
        self.fixed_command = None if command is None else np.asarray(command, dtype=np.float32)
        self.curriculum = 1.0  # 0 = forward only, 1 = full command range
        self.resample_steps = resample_steps
        self.max_steps = int(episode_seconds / CONTROL_DT)

        self.sim = SpotPicoSim(TERRAIN_MODEL_PATH) if terrain > 0 else SpotPicoSim()
        self.gc = GaitController()
        self.gait = GaitState()
        set_mode(self.gait, TROT)
        self.body = BodyState()
        self.np_random_, _ = gym.utils.seeding.np_random(seed)
        self.dr = DomainRandomizer(self.sim.model) if randomize else None
        self._cmd_buffer: deque = deque(maxlen=8)

        self.act_dim = ACT_DIM
        self.action_space = gym.spaces.Box(-1.0, 1.0, shape=(self.act_dim,), dtype=np.float32)

        self.prev_action = np.zeros(self.act_dim, dtype=np.float32)
        self.prev_joint_cmd = self.sim.stand_pose.astype(np.float32)
        self.cmd = np.zeros(3, dtype=np.float32)
        self.gait_phase = 0.0

        obs_dim = 3 + 3 + 3 + 12 + 2 + 3 + self.act_dim  # = 38
        self.observation_space = gym.spaces.Box(-np.inf, np.inf, shape=(obs_dim,), dtype=np.float32)
        self.current_step = 0

    # ------------------------------------------------------------------ reset
    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        if seed is not None:
            self.np_random_, _ = gym.utils.seeding.np_random(seed)
        if self.randomize:
            self.dr.reset_episode(self.sim.model, self.sim.servo, self.np_random_)
        if self.terrain > 0:
            randomize_hfield(self.sim.model, self.np_random_, self.terrain)
        self.sim.reset_to_stand()
        self.gc = GaitController()
        self.gait = GaitState()
        set_mode(self.gait, TROT)
        self.body = BodyState()
        self.gait_phase = 0.0
        self.prev_action[:] = 0.0
        self.prev_joint_cmd = self.sim.stand_pose.astype(np.float32)
        self._cmd_buffer.clear()
        self._sample_command()
        self.current_step = 0
        return self._get_obs(), {}

    def set_curriculum(self, level):
        self.curriculum = float(np.clip(level, 0.0, 1.0))

    def _sample_command(self):
        if self.fixed_command is not None:
            self.cmd[:] = self.fixed_command
            return
        r = self.np_random_
        L = self.curriculum
        if r.random() < ZERO_CMD_PROB:
            self.cmd[:] = 0.0
        else:
            # forward is always available; backward, lateral and yaw phase in with curriculum L
            self.cmd[0] = r.uniform(CMD_VX[0] * L, CMD_VX[1])
            self.cmd[1] = r.uniform(CMD_VY[0] * L, CMD_VY[1] * L)
            self.cmd[2] = r.uniform(CMD_YAW[0] * L, CMD_YAW[1] * L)

    # ------------------------------------------------------------------ step
    def step(self, action):
        action = np.clip(np.asarray(action, dtype=np.float32), -1.0, 1.0)
        self._cur_action = action
        joint_cmd = self._action_to_joints(action)

        # action latency (DR): apply a delayed command to the servos
        self._cmd_buffer.append(joint_cmd)
        if self.randomize and self.dr.action_latency_steps > 0:
            idx = max(0, len(self._cmd_buffer) - 1 - self.dr.action_latency_steps)
            effective = self._cmd_buffer[idx]
        else:
            effective = joint_cmd
        self.sim.set_joint_targets(effective)

        if self.randomize:
            self.dr.maybe_push(self.sim.model, self.sim.data, self.np_random_, self.current_step)
        self.sim.step_physics()

        obs = self._get_obs()
        reward, terminated, terms = self._reward_and_done()
        self.current_step += 1
        if self.resample_steps and self.fixed_command is None and self.current_step % self.resample_steps == 0:
            self._sample_command()
        truncated = self.current_step >= self.max_steps

        self.prev_action = action
        self.prev_joint_cmd = joint_cmd.astype(np.float32)
        return obs, float(reward), bool(terminated), bool(truncated), terms

    def _action_to_joints(self, action):
        # analytic baseline gait from the command (mirrors the firmware), then advance phase,
        # generate the foot targets, and add the policy's per-leg foot residuals.
        analytic_gait_action(self.cmd, self.gait)
        self.gc.advance_phase(self.gait, CONTROL_DT)
        self.gait_phase = self.gc.phase
        self.gc.generate_feet(self.gait, self.body)
        res = action.reshape(4, 3) * FOOT_RESIDUAL
        self.body.feet = self.body.feet + res
        return self.sim.body_targets_from_feet(self.body)

    # ------------------------------------------------------------------ obs
    def _get_obs(self):
        q = self.sim.base_quat()
        grav = _rot_vec_by_conj(q, np.array([0.0, 0.0, -1.0]))
        gyro = self.sim.gyro()
        rpy = _quat_to_rpy(q)
        if self.randomize:
            grav, gyro, rpy = self.dr.noisy_imu(grav, gyro, rpy, self.np_random_)
        phase_clock = np.array([np.sin(2 * np.pi * self.gait_phase), np.cos(2 * np.pi * self.gait_phase)])
        obs = np.concatenate([grav, gyro, rpy, self.prev_joint_cmd, phase_clock, self.cmd, self.prev_action])
        return obs.astype(np.float32)

    # ------------------------------------------------------------------ reward
    def _reward_and_done(self):
        d = self.sim.data
        q = self.sim.base_quat()
        v_world = d.qvel[0:3].copy()
        v_body = _rot_vec_by_conj(q, v_world)
        fwd = -v_body[1]       # robot forward = -Y in base frame
        left = v_body[0]       # robot left    = +X in base frame
        gyro = self.sim.gyro()
        yaw_rate = gyro[2]
        grav = _rot_vec_by_conj(q, np.array([0.0, 0.0, -1.0]))

        r_vel = np.exp(-((fwd - self.cmd[0]) ** 2 + (left - self.cmd[1]) ** 2) / VEL_SIGMA)
        r_yaw = np.exp(-((yaw_rate - self.cmd[2]) ** 2) / YAW_SIGMA)
        pen_upright = grav[0] ** 2 + grav[1] ** 2
        pen_height = (self.sim.base_height() - STAND_Z) ** 2
        pen_vz = d.qvel[2] ** 2
        pen_energy = np.sum(d.actuator_force ** 2)
        pen_arate = np.sum((self._cur_action - self.prev_action) ** 2)
        pen_slip = self.sim.foot_slip_sq()
        pen_power = self.sim.actuator_power()
        pen_angvel = gyro[0] ** 2 + gyro[1] ** 2
        pen_res = np.sum(self._cur_action ** 2)

        # velocity tracking must DOMINATE: earlier runs slowed to a crawl because the motion
        # penalties (power/energy/slip/angvel) outweighed a weak/gradient-less velocity term.
        terms = {
            "r_vel": 3.0 * r_vel,
            "r_yaw": 1.0 * r_yaw,
            "p_upright": -1.0 * pen_upright,
            "p_height": -20.0 * pen_height,   # small robot: mm-scale height errors need weight
            "p_vz": -0.5 * pen_vz,
            "p_energy": -5e-3 * pen_energy,
            "p_power": -2e-3 * pen_power,
            "p_arate": -0.01 * pen_arate,
            "p_slip": -0.02 * pen_slip,
            "p_angvel": -0.01 * pen_angvel,
            "p_res": -0.02 * pen_res,
            "alive": 0.1,
            "fwd": fwd,
            "left": left,
            "yaw_rate": yaw_rate,
        }
        reward = sum(v for k, v in terms.items() if k.startswith(("r_", "p_", "alive")))

        terminated = bool((grav[2] > -0.5) or (self.sim.base_height() < 0.03))
        if terminated:
            reward -= 1.0
        return reward, terminated, terms


def make_env(randomize=False, seed=0, resample_steps=0, terrain=0.0):
    """Factory for SubprocVecEnv (must be picklable / module-level)."""
    def _thunk():
        return QuadrupedMjEnv(randomize=randomize, seed=seed, resample_steps=resample_steps,
                              terrain=terrain)
    return _thunk
