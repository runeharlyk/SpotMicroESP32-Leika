"""The sim-versus-real harness, with the virtual robot (the firmware's own motion code) as the robot."""
import math
import os
import re
from dataclasses import replace

import numpy as np
import pytest

import sim_vs_real
import virtual_robot
from src.leika import Gait, RobotError, speed_model
from src.robot.firmware_gait import TROT, GaitState, analytic_gait_action, set_mode

ROBOTS_TS = os.path.join(os.path.dirname(__file__), "..", "..", "app", "src", "lib", "simulation", "robots.ts")
WALKING = ("forward", "sideways", "turn")


@pytest.fixture(scope="module")
def virtual_mini(binary):
    served = virtual_robot.VirtualRobot(binary, sim_vs_real.VARIANT)
    try:
        yield sim_vs_real.drive(f"127.0.0.1:{served.port}")
    finally:
        served.stop()


@pytest.fixture(scope="module")
def simulation(virtual_mini):
    return sim_vs_real.record_simulation([trace.held for trace in virtual_mini])


def _segments(report):
    return {segment.name: segment for segment in report.segments}


def test_the_simulation_and_the_firmware_stay_within_the_thresholds(simulation, virtual_mini):
    report = sim_vs_real.compare(simulation, virtual_mini)
    assert sim_vs_real.breaches(report) == []
    segments = _segments(report)
    assert all(segments[name].stepped for name in WALKING)
    assert not segments["stand"].stepped and not segments["stop"].stepped
    assert max(segments["stand"].tilt_max_deg) < 0.5  # standing still in physics stays level, as the virtual robot reads


def test_it_measures_each_sides_cadence_as_its_gait_sets_it(simulation, virtual_mini):
    factor, _ = speed_model.GAITS[Gait.TROT]
    robot_hz = max(speed_model.SPEED, 0.5) * factor
    segments = _segments(sim_vs_real.compare(simulation, virtual_mini))
    for step in sim_vs_real.SCRIPT:
        if step.name not in WALKING:
            continue
        gait = GaitState()
        set_mode(gait, TROT)
        analytic_gait_action([step.velocity.vx, step.velocity.vy, step.velocity.yaw_rate], gait)
        sim_hz = max(gait.step_velocity, 0.5) * gait.speed_factor
        assert segments[step.name].sim_stepping.cadence_hz == pytest.approx(sim_hz, rel=0.02)
        assert segments[step.name].robot_stepping.cadence_hz == pytest.approx(robot_hz, rel=0.02)


def test_a_leg_stepping_with_the_wrong_pair_breaches(simulation, virtual_mini):
    fr, rr = [sim_vs_real.JOINT_NAMES.index(f"{leg}_hip") for leg in ("fr", "rr")]
    order = np.arange(12)
    order[fr:fr + 3], order[rr:rr + 3] = np.arange(rr, rr + 3), np.arange(fr, fr + 3)
    swapped = [replace(trace, joints=trace.joints[:, order]) for trace in virtual_mini]
    found = sim_vs_real.breaches(sim_vs_real.compare(simulation, swapped))
    for name in WALKING:
        assert any(re.match(rf"{name}: the \w\w foot lifts [+-]0\.[45]\d cycles apart", breach) for breach in found), found


# A servo turning the wrong way, calibrated at the stand: its joint moves mirrored about the stand pose.
@pytest.mark.parametrize("joint", sim_vs_real.JOINT_NAMES)
def test_a_single_reversed_joint_breaches(simulation, virtual_mini, joint):
    j = sim_vs_real.JOINT_NAMES.index(joint)
    stand = sim_vs_real.stand_pose(virtual_mini)[j]
    reversed_ = []
    for trace in virtual_mini:
        joints = trace.joints.copy()
        joints[:, j] = 2 * stand - joints[:, j]
        reversed_.append(replace(trace, joints=joints))
    found = sim_vs_real.breaches(sim_vs_real.compare(simulation, reversed_))
    assert any(f": {joint} " in breach for breach in found), found


# The trot starting on the other diagonal pair: the cycles and the foot phases agree, only the script time tells.
def test_a_gait_half_a_cycle_out_breaches(simulation, virtual_mini):
    half = 0.5 / (max(speed_model.SPEED, 0.5) * speed_model.GAITS[Gait.TROT][0])

    def ahead(trace):
        kept = trace.t >= half
        return replace(trace, t=trace.t[kept] - half, joints=trace.joints[kept], mode=trace.mode[kept],
                       roll=trace.roll[kept], pitch=trace.pitch[kept])

    shifted = [virtual_mini[0]] + [ahead(trace) for trace in virtual_mini[1:]]
    report = sim_vs_real.compare(simulation, shifted)
    segments = _segments(report)
    found = sim_vs_real.breaches(report)
    for name in WALKING:
        assert np.abs(segments[name].foot_phase_diff).max() < sim_vs_real.MAX_FOOT_PHASE_DIFF
        assert segments[name].cycle_rms_deg.max() < sim_vs_real.MAX_CYCLE_RMS_DEG
        assert any(re.match(rf"{name}: \w+ moved [\d.]+ degrees RMS apart", breach) for breach in found), found


def test_the_cycle_comparison_sees_through_a_delay_the_script_time_comparison_does_not(simulation):
    delay = 0.2
    delayed = [simulation[0]] + [
        replace(trace, t=np.concatenate([[0.0], trace.t + delay]), joints=np.vstack([trace.joints[:1], trace.joints]),
                mode=np.concatenate([trace.mode[:1], trace.mode]), roll=np.concatenate([trace.roll[:1], trace.roll]),
                pitch=np.concatenate([trace.pitch[:1], trace.pitch]))
        for trace in simulation[1:]
    ]
    segments = _segments(sim_vs_real.compare(simulation, delayed))
    for name in WALKING:
        segment = segments[name]
        assert segment.first_lift_lag_ms == pytest.approx(delay * 1000, abs=1)
        assert segment.cycle_rms_deg.max() < 0.5
        assert np.abs(segment.foot_phase_diff).max() < 0.01
        assert segment.sim_stepping.cadence_hz == pytest.approx(segment.robot_stepping.cadence_hz, rel=0.01)
        assert segment.motion_rms_deg.max() > 5


def test_it_refuses_a_robot_the_simulation_does_not_model(binary):
    served = virtual_robot.VirtualRobot(binary, "SPOTMICRO_ESP32")
    try:
        with pytest.raises(RobotError, match="models the Pico"):
            sim_vs_real.drive(f"127.0.0.1:{served.port}")
    finally:
        served.stop()


def test_the_joint_map_is_the_one_the_app_uses():
    with open(ROBOTS_TS) as f:
        block = re.search(r"const PICO_JOINTS: JointMap = \{(.*?)\n\}", f.read(), re.S).group(1)
    sign = [float(v) for v in re.search(r"sign: \[([^\]]*)\]", block).group(1).split(",")]
    offset = [math.radians(float(v)) for v in re.search(r"offset: degrees\(\[([^\]]*)\]\)", block).group(1).split(",")]
    assert list(sim_vs_real.MAP_SIGN) == sign
    assert list(sim_vs_real.MAP_OFFSET) == pytest.approx(offset)
