"""System identification on synthetic recordings made at known parameters (tests/synthetic_recording.py)."""
import json
import sys
from pathlib import Path

import mujoco
import numpy as np
import pytest
from scipy.spatial.transform import Rotation

import identify
from src.envs.quadruped_mj_env import QuadrupedMjEnv
from src.sim import domain_rand, sysid
from src.sim.domain_rand import Ranges
from src.sim.mj_runtime import SpotPicoSim
from src.sim.recording import load, to_model_joints
from synthetic_recording import write

TRUTH = dict(volts=5.2, strength=0.95, lag_ms=12.0, gyro_bias=(0.01, -0.02, 0.005), gyro_noise=0.05,
             tilt_noise_deg=1.5)
FIRMWARE_TRACE = Path(__file__).parents[2] / "app" / "tests" / "fixtures" / "firmware-trace-SPOTMICRO_ESP32_MINI.json"


@pytest.fixture(scope="module")
def walk(tmp_path_factory):
    path = tmp_path_factory.mktemp("sysid") / "walk.leika"
    write(path, **TRUTH, stand_s=2.0, walk_s=3.0)
    return path, sysid.identify(load(path))


def _within(estimate, truth, tolerance):
    assert estimate.identified, estimate.note
    assert abs(estimate.value - truth) <= tolerance
    assert abs(estimate.value - truth) <= 4 * estimate.std  # the stated sigma is honest


def _firmware_feet(tick):
    """The firmware's foot targets in its body frame (x forward, y up, z left), legs fl, fr, rl, rr."""
    body = tick["body"]
    rotation = Rotation.from_euler("XYZ", [body["omega"], body["phi"], body["psi"]], degrees=True).as_matrix()
    return (np.array(body["feet"]) - [body["xm"], body["ym"], body["zm"]]) @ rotation


def _model_feet(tick):
    """The model's feet posed by the firmware's angles, in the firmware's body frame and leg order."""
    sim = SpotPicoSim()
    sim.data.qpos[sim.qpos_adr] = to_model_joints(np.radians(tick["angles"]))
    mujoco.mj_kinematics(sim.model, sim.data)
    feet = sim.data.site_xpos[sim.foot_site_ids] - sim.data.xpos[sim.base_id]
    return np.column_stack([-feet[:, 1], feet[:, 2], feet[:, 0]])[[1, 0, 3, 2]]


# The replay drives the model with the robot's written angles: a sign or leg-order slip in the joint map would move the
# model's feet elsewhere than the firmware's. The two bodies' origins differ, so the feet are compared by how they move
# from the stand to the firmware trace's most rotated body.
def test_the_joint_map_moves_the_model_feet_as_the_firmware_moves_its_own():
    ticks = json.loads(FIRMWARE_TRACE.read_text())["ticks"]
    stand = next(t for t in ticks if t["mode"] == 4)
    rotated = max(ticks, key=lambda t: abs(t["body"]["phi"]) + abs(t["body"]["psi"]))
    moved = _model_feet(rotated) - _model_feet(stand)
    expected = _firmware_feet(rotated) - _firmware_feet(stand)
    assert np.linalg.norm(expected, axis=1).min() > 0.01
    assert np.linalg.norm(moved - expected, axis=1).max() < 0.01


def test_a_walk_recovers_the_servo_parameters_and_the_lag(walk):
    _, found = walk
    _within(found["supply_volts"], TRUTH["volts"], 0.05)
    _within(found["stall_strength"], TRUTH["strength"], 0.02)
    _within(found["actuator_lag_ms"], TRUTH["lag_ms"], 1.0)  # the right physics step
    assert found["fit_residual_rms"].value < 1.3


def test_the_still_start_gives_the_gyro_bias_and_the_imu_noise(walk):
    _, found = walk
    for axis, bias in zip("xyz", TRUTH["gyro_bias"]):
        _within(found[f"gyro_bias_{axis}"], bias, 0.015)
    _within(found["gyro_noise_std"], TRUTH["gyro_noise"], 0.01)
    _within(found["rpy_noise_deg"], TRUTH["tilt_noise_deg"], 0.3)
    assert not found["rpy_bias"].identified
    assert found["sensor_to_servo_ms_p50"].value == pytest.approx(6.0)  # IMU age 4 ms + compute 1 ms + write 1 ms


def test_the_walk_narrows_the_randomisation_around_what_it_found(walk, tmp_path):
    path, found = walk
    centre = tmp_path / "centre.json"
    centre.write_text(json.dumps(sysid.centre(found, str(path))))
    ranges = Ranges.identified(centre)
    assert ranges.supply_volts[0] < TRUTH["volts"] < ranges.supply_volts[1]
    assert np.ptp(ranges.supply_volts) < np.ptp(domain_rand.SUPPLY_VOLTS) / 2
    assert np.mean(ranges.stall_spread) == pytest.approx(TRUTH["strength"], abs=0.02)
    # 6 ms from sensor to servo and 12 ms of lag: one to two control steps
    assert ranges.action_latency_steps == (1, 3)
    np.testing.assert_allclose(ranges.gyro_bias_mean, TRUTH["gyro_bias"], atol=0.015)
    assert ranges.gyro_bias_std == domain_rand.GYRO_BIAS_STD

    env = QuadrupedMjEnv(randomize=True, seed=0, dr_ranges=ranges)
    for _ in range(5):
        env.reset()
        assert ranges.supply_volts[0] <= env.sim.servo.volts <= ranges.supply_volts[1]
        assert ranges.action_latency_steps[0] <= env.dr.action_latency_steps < ranges.action_latency_steps[1]


# A robot that never moves says nothing about its servos: the tool must say so instead of reporting the search's
# starting point, and must still give the IMU noise.
def test_a_still_recording_identifies_the_imu_but_not_the_servos(tmp_path, monkeypatch, capsys):
    path = tmp_path / "still.leika"
    write(path, **TRUTH, stand_s=3.0, walk_s=0.0)
    monkeypatch.setattr(sys, "argv", ["identify.py", str(path)])
    identify.main()
    written = json.loads(path.with_suffix(".sysid.json").read_text())
    assert {"supply_volts", "stall_strength", "actuator_lag_ms"} <= written["not_identifiable"].keys()
    assert written["estimates"]["gyro_noise_std"]["value"] == pytest.approx(TRUTH["gyro_noise"], rel=0.1)
    assert "supply_volts             not identifiable" in capsys.readouterr().out
    assert Ranges.identified(path.with_suffix(".sysid.json")).supply_volts == domain_rand.SUPPLY_VOLTS


@pytest.mark.parametrize("change, reason", [
    (dict(stand_s=0.0), "does not begin with a robot standing still"),
    (dict(variant="SPOTMICRO_YERTLE"), "the replay model is the Pico"),
])
def test_a_recording_the_replay_cannot_start_from_is_refused(tmp_path, change, reason):
    path = tmp_path / "refused.leika"
    write(path, **TRUTH, **{"stand_s": 2.0, "walk_s": 1.5, **change})
    found = sysid.identify(load(path))
    for name in ("supply_volts", "stall_strength", "actuator_lag_ms"):
        assert not found[name].identified and reason in found[name].note
