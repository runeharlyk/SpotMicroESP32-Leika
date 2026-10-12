"""The port of the firmware stand state's IMU compensation, and what it does to a robot on a slope."""
import math

import numpy as np
import pytest

from src.robot import stand_compensation
from src.robot.firmware_gait import BodyState
from src.robot.stand_compensation import (
    FEET_EASE_S,
    MAX_PITCH_DEG,
    MAX_ROLL_DEG,
    CriticalDamper,
    StandCompensation,
    SMOOTHING_OMEGA,
    apply_firmware_attitude,
    imu_roll_pitch,
)
from src.sim.mj_runtime import CONTROL_DT, SpotPicoSim
from src.sim.slope_stand import LEFT_UP, NOSE_DOWN, stand_on_slope

DEG = math.radians(1)


def settle(roll, pitch, ticks=300):
    compensation = StandCompensation()
    for _ in range(ticks):
        attitude = compensation.step(roll, pitch, CONTROL_DT, True)
    return attitude


def test_the_damper_settles_a_step_to_95_percent_in_a_third_of_a_second():
    damper, value, path = CriticalDamper(), 0.0, []
    for _ in range(round(0.333 / CONTROL_DT)):
        value = damper.step(value, 1.0, CONTROL_DT, SMOOTHING_OMEGA)
        path.append(value)
    assert path[-1] == pytest.approx(0.95, abs=0.01)
    assert max(path) <= 1.0


def test_a_tilt_is_answered_with_the_reversed_offsets_like_the_firmware_host_test():
    """stand_compensation_test.cpp: left side up rolls the body back (omega > 0), nose down lifts it (psi > 0)."""
    omega, psi = settle(5 * DEG, 0, ticks=70)
    assert omega > 1 and abs(psi) < 1e-6
    omega, psi = settle(0, 5 * DEG, ticks=70)
    assert psi > 1 and abs(omega) < 1e-6


def test_a_tilt_that_does_not_go_away_winds_the_answer_up_to_the_limits_and_no_further():
    assert settle(-5 * DEG, 5 * DEG, ticks=3000) == pytest.approx((-MAX_ROLL_DEG, MAX_PITCH_DEG), abs=1e-3)


def test_the_compensation_holds_until_the_takeover_has_settled_like_the_firmware_host_test():
    compensation = StandCompensation()
    for _ in range(round(FEET_EASE_S / CONTROL_DT)):
        attitude = compensation.step(6 * DEG, 8 * DEG, CONTROL_DT, True)
    assert attitude == (0, 0)
    assert compensation.omega_integral == compensation.psi_integral == 0
    for _ in range(50):
        attitude = compensation.step(6 * DEG, 8 * DEG, CONTROL_DT, True)
    assert min(attitude) > 1


def test_a_state_taking_over_starts_without_the_last_ones_integral():
    compensation = StandCompensation()
    for _ in range(300):
        compensation.step(5 * DEG, 5 * DEG, CONTROL_DT, True)
    assert compensation.psi_integral > 1
    compensation.reset()
    for _ in range(100):
        attitude = compensation.step(0, 0, CONTROL_DT, True)
    assert attitude == pytest.approx((0, 0), abs=1e-3)


def test_outside_stand_the_attitude_eases_back_to_level():
    compensation = StandCompensation()
    for _ in range(300):
        compensation.step(5 * DEG, 5 * DEG, CONTROL_DT, True)
    compensation.reset()
    for _ in range(100):
        attitude = compensation.step(5 * DEG, 5 * DEG, CONTROL_DT, False)
    assert attitude == pytest.approx((0, 0), abs=1e-3)


def hold_attitude(omega_deg, psi_deg, seconds=1.5):
    sim = SpotPicoSim()
    sim.reset_to_stand()
    body = BodyState()
    apply_firmware_attitude(body, omega_deg, psi_deg)
    for _ in range(round(seconds / CONTROL_DT)):
        sim.set_joint_targets(sim.body_targets_from_feet(body))
        sim.step_physics()
    return np.degrees(imu_roll_pitch(sim.base_quat()))


def test_the_attitude_moves_the_simulated_body_as_measured_on_the_pico():
    """2026-10-01, Pico in STAND: a positive omega lowered the REP-103 roll and a positive psi lowered the pitch."""
    roll, pitch = hold_attitude(6, 0)
    assert roll < -4 and abs(pitch) < 0.5
    roll, pitch = hold_attitude(0, 6)
    assert pitch < -4 and abs(roll) < 0.5


@pytest.mark.parametrize("axis, column", [(NOSE_DOWN, 1), (LEFT_UP, 0)])
def test_on_a_slope_the_proportional_term_alone_levels_the_body_halfway(axis, column, monkeypatch):
    """At gain 1 it leaves body = -slope / 2, so the IMU reads half the slope: what the integral action is for."""
    monkeypatch.setattr(stand_compensation, "IMU_INTEGRAL_GAIN", 0.0)
    off = stand_on_slope(axis, 8, False)[-100:, column]
    on = stand_on_slope(axis, 8, True)[-100:, column]
    assert off.mean() == pytest.approx(8, abs=0.5)
    assert on.mean() == pytest.approx(off.mean() / 2, abs=0.3)
    assert np.ptp(on) < 0.1


@pytest.mark.parametrize("axis, column", [(NOSE_DOWN, 1), (LEFT_UP, 0)])
def test_on_a_slope_the_integral_action_levels_the_body_fully_without_overshoot(axis, column):
    tilt = stand_on_slope(axis, 8, True, seconds=10)[:, column]
    assert abs(tilt[400]) < 1.0
    assert abs(tilt[-100:]).max() < 0.1
    assert tilt.min() > -0.1


def test_on_a_flat_floor_the_levelled_stand_holds_still():
    tilt = stand_on_slope(NOSE_DOWN, 0, True, seconds=8)
    assert np.abs(tilt[-500:]).max() < 0.05
    assert np.ptp(tilt[-500:], axis=0).max() < 0.01

