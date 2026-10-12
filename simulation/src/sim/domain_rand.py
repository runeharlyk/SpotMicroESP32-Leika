"""Domain randomization for sim-to-real transfer.

Applied by `QuadrupedMjEnv` when `randomize=True`. Per-episode it perturbs the MODEL
(masses/inertia, CoM, friction, motor strength, servo supply voltage) and sets per-episode sensor biases,
action latency, and a push schedule. Per-step it adds IMU noise and applies pushes.

Ranges are conservative and scaled to spot_pico (~0.6 kg, open-loop micro-servos). The most
important term for this robot (WiFi/BLE link + no joint encoders) is action latency.
identify.py fits the IMU, latency and servo terms to a real recording and writes them as a JSON file;
`Ranges.identified` re-centres the ranges on it.
"""

import json
import math
from dataclasses import dataclass

import numpy as np
import mujoco

from src.sim.servo import RATED_VOLTS

# The ranges below are assumptions until real recordings replace them (identify.py, Ranges.identified).
GYRO_BIAS_STD = 0.05       # rad/s, per episode
GYRO_NOISE_STD = 0.05      # rad/s, per step
RPY_BIAS_DEG = 5.0         # degrees, per episode, uniform +-
RPY_NOISE_DEG = 1.5        # degrees, per step
GRAVITY_NOISE_STD = 0.02   # per step, on the unit gravity vector
SUPPLY_VOLTS = RATED_VOLTS  # undocumented on the Pico, so anywhere in the MG92B's rated range
STALL_SPREAD = (0.9, 1.1)  # per servo, unit-to-unit
ACTION_LATENCY_STEPS = (0, 3)  # rng.integers bounds: 0 to 2 control steps
CONTROL_DT = 0.01          # s, the 100 Hz control step the latency counts in
# An identified value narrows its range to 3 sigma around it, but no narrower than this share of today's range: one
# recording of one robot does not cover the next robot or the next battery.
MIN_RANGE_SHARE = 0.25


def _around(value: float, std: float, default: tuple[float, float]) -> tuple[float, float]:
    half = max(3.0 * std, MIN_RANGE_SHARE * (default[1] - default[0]) / 2)
    return value - half, value + half


@dataclass(frozen=True)
class Ranges:
    """What the randomizer draws from; the defaults are the assumptions above."""

    gyro_bias_mean: tuple[float, float, float] = (0.0, 0.0, 0.0)
    gyro_bias_std: float = GYRO_BIAS_STD
    gyro_noise_std: float = GYRO_NOISE_STD
    rpy_noise_deg: float = RPY_NOISE_DEG
    supply_volts: tuple[float, float] = SUPPLY_VOLTS
    stall_spread: tuple[float, float] = STALL_SPREAD
    action_latency_steps: tuple[int, int] = ACTION_LATENCY_STEPS

    @classmethod
    def identified(cls, path) -> "Ranges":
        """The default ranges, re-centred on the estimates identify.py wrote to `path`; a term it could not identify
        keeps its default. The gyro bias centres on the recorded robot's, but keeps the default spread, since one
        recording shows one boot's calibration; the unit-to-unit stall spread stays around the identified common
        strength, which the IMU cannot split between the servos."""
        found = json.loads(open(path).read())["estimates"]
        value = lambda name: found[name]["value"]
        std = lambda name: found[name]["std"] or 0.0
        changes = {}
        if all(f"gyro_bias_{axis}" in found for axis in "xyz"):
            changes["gyro_bias_mean"] = tuple(value(f"gyro_bias_{axis}") for axis in "xyz")
        if "gyro_noise_std" in found:
            changes["gyro_noise_std"] = value("gyro_noise_std")
        if "rpy_noise_deg" in found:
            changes["rpy_noise_deg"] = value("rpy_noise_deg")
        if "supply_volts" in found:
            low, high = _around(value("supply_volts"), std("supply_volts"), SUPPLY_VOLTS)
            changes["supply_volts"] = (max(low, RATED_VOLTS[0]), min(high, RATED_VOLTS[1]))
        if "stall_strength" in found:
            strength, sigma = value("stall_strength"), 3.0 * std("stall_strength")
            changes["stall_spread"] = (strength + STALL_SPREAD[0] - 1.0 - sigma, strength + STALL_SPREAD[1] - 1.0 + sigma)
        if {"actuator_lag_ms", "sensor_to_servo_ms_p50", "sensor_to_servo_ms_p99"} <= found.keys():
            lag, sigma = value("actuator_lag_ms"), 3.0 * std("actuator_lag_ms")
            step_ms = CONTROL_DT * 1000.0
            low_ms = value("sensor_to_servo_ms_p50") + max(lag - sigma, 0.0)
            high_ms = value("sensor_to_servo_ms_p99") + lag + sigma
            changes["action_latency_steps"] = (math.floor(low_ms / step_ms), math.ceil(high_ms / step_ms) + 1)
        return cls(**changes)


class DomainRandomizer:
    def __init__(self, model, ranges: Ranges = Ranges()):
        self.ranges = ranges
        self.base_body_mass = model.body_mass.copy()
        self.base_body_inertia = model.body_inertia.copy()
        self.base_body_ipos = model.body_ipos.copy()
        self.base_geom_friction = model.geom_friction.copy()
        self.base_gainprm = model.actuator_gainprm.copy()
        self.base_biasprm = model.actuator_biasprm.copy()
        self.base_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "base_link")

        # per-episode state
        self.gyro_bias = np.zeros(3)
        self.rpy_bias = np.zeros(3)
        self.action_latency_steps = 0
        self.next_push = 10**9
        self.push_steps_left = 0
        self.push_force = np.zeros(3)

    # -------------------------------------------------- per-episode (at reset)
    def reset_episode(self, model, servo, rng):
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
        ranges = self.ranges
        servo.set_supply(rng.uniform(*ranges.supply_volts), rng.uniform(*ranges.stall_spread, size=len(servo.act_ids)))

        # per-episode sensor biases + latency
        self.gyro_bias = rng.normal(ranges.gyro_bias_mean, ranges.gyro_bias_std, size=3)
        self.rpy_bias = np.deg2rad(rng.uniform(-RPY_BIAS_DEG, RPY_BIAS_DEG, size=3))
        self.action_latency_steps = int(rng.integers(*ranges.action_latency_steps))

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
        gyro = gyro + self.gyro_bias + rng.normal(0.0, self.ranges.gyro_noise_std, 3)
        rpy = rpy + self.rpy_bias + np.deg2rad(rng.normal(0.0, self.ranges.rpy_noise_deg, 3))
        grav = grav + rng.normal(0.0, GRAVITY_NOISE_STD, 3)
        return grav, gyro, rpy
