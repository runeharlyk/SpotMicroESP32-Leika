"""measure_pwm_per_degree.py on the MuJoCo model with a known error in the servos' counts per degree."""
import dataclasses
import re
from pathlib import Path

import numpy as np
import pytest
from scipy.spatial.transform import Rotation

import measure_pwm_per_degree as measurement
from src.leika import joint_scale, speed_model
from src.leika.backends.sim import PICO_SERVOS, SimBackend
from src.leika.constants import Mode
from src.sim.mj_runtime import SpotPicoSim

VARIANT = "SPOTMICRO_ESP32_MINI"
JOINT_MODEL_H = Path(__file__).resolve().parents[2] / "esp32" / "include" / "joint_model.h"
PICO_ROLL_NOISE_DEG = 0.22


class NoisyImu:
    """The backend with the Pico's roll and pitch noise on every state, as its IMU reports them."""

    def __init__(self, backend, sigma: float, seed: int):
        self._backend, self._sigma, self._rng = backend, sigma, np.random.default_rng(seed)

    def __getattr__(self, name):
        return getattr(self._backend, name)

    def state(self):
        state = self._backend.state()
        roll, pitch = self._rng.normal(0.0, self._sigma, 2)
        return dataclasses.replace(state, roll=state.roll + roll, pitch=state.pitch + pitch)


def standing(servo_scale: float | None, floor_quat=None) -> SimBackend:
    backend = SimBackend(realtime=False, servo_scale=servo_scale)
    if floor_quat is not None:
        backend.sim = SpotPicoSim(floor_quat=floor_quat)
    backend.connect()
    backend.set_mode(Mode.STAND)
    backend.sleep(2.0)
    return backend


def test_the_simulated_servos_take_the_minis_joint_model():
    line = re.search(r"JOINT_MODEL_SPOTMICRO_ESP32_MINI = \{\{([^}]*)\},\s*\{([^}]*)\},\s*([\d.]+)f",
                     JOINT_MODEL_H.read_text())
    assert PICO_SERVOS.direction == tuple(int(v) for v in line[1].split(","))
    assert PICO_SERVOS.center_angle == tuple(int(v) for v in line[2].split(","))
    assert PICO_SERVOS.pwm_per_degree == float(line[3])


def test_leg_point_inverts_the_firmwares_leg_ik():
    for hip, femur, knee in [(0.0, -40.0, 80.0), (12.0, -25.0, 60.0), (-20.0, -70.0, 120.0)]:
        x, y, z = joint_scale.leg_point(VARIANT, hip, femur, knee)
        assert speed_model.leg_angles(VARIANT, x, y, z) == pytest.approx([hip, femur, knee], abs=1e-9)


@pytest.mark.parametrize("true_scale", [2.28, 1.8])
def test_the_measurement_recovers_the_servos_true_scale(true_scale):
    backend = standing(true_scale)
    result = measurement.measure(backend, VARIANT)
    assert result.fit.scale == pytest.approx(true_scale, rel=0.02)
    assert result.fit.residual_rms < measurement.MAX_RESIDUAL_DEG
    # A raised hip IK angle lifts the left feet: the body rolls right, by more the larger the step.
    assert np.all(np.sign(result.tilts[:, 0]) == -np.sign(result.offsets))
    assert np.abs(result.tilts[:, 1]).max() < 0.1


def test_the_measurement_holds_within_its_stated_tolerance_under_the_picos_imu_noise():
    backend = standing(2.28)
    result = measurement.measure(NoisyImu(backend, PICO_ROLL_NOISE_DEG, seed=7), VARIANT)
    assert result.fit.scale == pytest.approx(2.28, rel=0.04)
    # The docstring's noise budget: 0.06 degrees per step over 3.2 degrees of roll, about 2 %.
    assert 0.01 < result.fit.sigma / result.fit.scale < 0.04


def test_the_measurement_hands_the_robot_back_standing_under_the_motion():
    backend = standing(2.28)
    before, _ = backend.servo_output()
    measurement.measure(backend, VARIANT)
    backend.sleep(2.0)
    after, _ = backend.servo_output()
    state = backend.state()
    assert state.mode is Mode.STAND
    assert backend._held_pwm is None
    assert abs(state.roll) < 0.5 and abs(state.pitch) < 0.5
    assert np.abs(after - before).max() < 1.0


def test_the_measurement_refuses_a_robot_that_does_not_stand():
    backend = SimBackend(realtime=False, servo_scale=2.28)
    backend.connect()
    backend.sleep(1.0)
    with pytest.raises(measurement.Refused, match="REST"):
        measurement.measure(backend, VARIANT)


def test_the_measurement_refuses_a_sloping_floor_the_stand_has_levelled_the_body_on():
    slope = Rotation.from_euler("y", 4.0, degrees=True).as_quat(scalar_first=True)  # the model's +y is rearward
    backend = standing(2.28, floor_quat=slope)
    backend.sleep(4.0)
    assert abs(backend.state().roll) < 1.0  # the stand levelled it, so the IMU alone cannot tell
    with pytest.raises(measurement.Refused, match="slopes"):
        measurement.measure(backend, VARIANT)
    assert backend._held_pwm is None
