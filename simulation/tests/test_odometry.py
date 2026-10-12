"""The firmware's odometry, kinematic and fused: the port against the firmware's own traces, then against MuJoCo's
ground truth."""
import json
import math
from pathlib import Path

import numpy as np
import pytest

from src.leika import Robot
from src.leika.backends.sim import MODEL_TO_REP
from src.robot.firmware_odometry import GYRO_NOISE, FusedOdometry, KinematicOdometry

TRACES = Path(__file__).resolve().parents[2] / "app" / "tests" / "fixtures"
VARIANTS = ["SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE"]


# The traces walk forward, sideways, turning and in a crawl, and lean in stand (esp32/test/host/firmware_trace.cpp).
# The firmware sums in float, the port in double: they drift apart by well under a millimetre over the trace.
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_reproduces_the_firmwares_odometry_tick_by_tick(variant):
    trace = json.loads((TRACES / f"firmware-trace-{variant}.json").read_text())
    odometry = KinematicOdometry()
    worst_position = worst_yaw = 0.0
    for tick in trace["ticks"]:
        body = tick["body"]
        odometry.update(body["feet"], tick["stance"], body["xm"], body["zm"], body["phi"])
        expected, actual = tick["odometry"], odometry.estimate
        worst_position = max(worst_position, abs(actual.x - expected["x"]), abs(actual.y - expected["y"]),
                             abs(actual.distance - expected["distance"]))
        worst_yaw = max(worst_yaw, abs(math.remainder(actual.yaw - expected["yaw"], 2 * math.pi)))
    assert trace["ticks"][-1]["odometry"]["distance"] > 0.3
    assert worst_position < 2e-4 and worst_yaw < 2e-4


# The fused odometry on the same ticks, fed the trace's made-up gyro: more turn than the feet show, a bias and a
# wobble, and none in the crawl, where it falls back on the feet.
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_reproduces_the_firmwares_fused_odometry_tick_by_tick(variant):
    trace = json.loads((TRACES / f"firmware-trace-{variant}.json").read_text())
    odometry = FusedOdometry()
    worst_position = worst_yaw = worst_bias = 0.0
    for tick in trace["ticks"]:
        body = tick["body"]
        odometry.update(body["feet"], tick["stance"], body["xm"], body["zm"], body["phi"], tick["gyro"], trace["dt"])
        expected, actual = tick["fused"], odometry.estimate
        worst_position = max(worst_position, abs(actual.x - expected["x"]), abs(actual.y - expected["y"]),
                             abs(actual.distance - expected["distance"]))
        worst_yaw = max(worst_yaw, abs(math.remainder(actual.yaw - expected["yaw"], 2 * math.pi)),
                        abs(math.remainder(odometry.kinematic_yaw - expected["kinematic_yaw"], 2 * math.pi)))
        worst_bias = max(worst_bias, abs(odometry.bias - expected["bias"]))
    last = trace["ticks"][-1]
    assert any(tick["gyro"] is None for tick in trace["ticks"][1:])
    assert abs(last["fused"]["yaw"] - last["fused"]["kinematic_yaw"]) > 0.1
    assert worst_position < 2e-4 and worst_yaw < 2e-4 and worst_bias < 1e-6


# The traces' stand twists fast enough that the feet's motion alone keeps the bias still, so this pins the pose's own
# gate as odometry_test.cpp's aMovingBodyPoseTeachesTheBiasNothing does: a slow twist, then a slow shift, while the
# lagging gyro reads other than the feet, teaches nothing; once the pose keeps still the bias is learnt.
def test_a_moving_body_pose_teaches_the_bias_nothing():
    feet = np.array([[0.08, 0.0, 0.07], [0.08, 0.0, -0.07], [-0.08, 0.0, 0.07], [-0.08, 0.0, -0.07]])
    stance, dt = [True] * 4, 0.01
    odometry = FusedOdometry()
    phi = xm = 0.0
    for _ in range(200):
        phi += 1.0 * dt  # degrees: 0.017 rad/s, which the feet show and the gyro does not
        odometry.update(feet, stance, phi_deg=phi, gyro_rate=0.0, dt=dt)
    assert odometry.bias == 0.0
    for _ in range(200):
        xm += 0.002 * dt
        odometry.update(feet, stance, xm=xm, phi_deg=phi, gyro_rate=0.01, dt=dt)
    assert odometry.bias == 0.0
    for _ in range(200):
        odometry.update(feet, stance, xm=xm, phi_deg=phi, gyro_rate=0.01, dt=dt)
    assert odometry.bias == pytest.approx(0.01, abs=1e-3)


# The commanded feet overlook what the physics adds: on spot_pico the Bezier swing starts and ends with the foot
# still at floor height, 1.5 strokes out, so it pushes the body on beyond the stance stroke. Measured over 4 s after
# a second's settling, the odometry falls short of MuJoCo's base by about 7 % forward (0.160 m of 0.172 m), 10 %
# sideways (0.120 m of 0.133 m) and, by the feet alone, 9 % in a turn (136 of 149 degrees); a real robot's servos
# shorten its steps the other way, so either needs calibrating against a measurement. The bounds below hold those with
# some margin. The gyro corrects the heading: the fused yaw turns within 1 % of the base.
SHORTFALL = (0.78, 1.0)  # odometry / ground truth along the commanded direction
CROSS_TRACK_M = 0.02
STRAIGHT_YAW_DEG = 5.0
TURN_ERROR = 0.03  # of the base's turn, for the fused yaw


@pytest.fixture
def robot():
    client = Robot("simulation", realtime=False)
    client.connect()
    client.stand()
    yield client
    client.close()


def _ground_truth(robot):
    sim = robot._backend.sim
    a = sim.base_qposadr
    return (MODEL_TO_REP @ sim.data.qpos[a : a + 3])[:2], math.radians(robot.state().yaw)


def _walk(robot, vx, vy, yaw_rate, seconds=4.0):
    """The odometry and MuJoCo's base displacement, in the frame the robot faced at the start, over `seconds`."""
    robot.set_velocity(vx, vy, yaw_rate)
    robot.sleep(1.0)
    robot.reset_odometry()
    start, heading = _ground_truth(robot)
    robot.sleep(seconds)
    odometry = robot.odometry()
    end, end_heading = _ground_truth(robot)
    c, s = math.cos(heading), math.sin(heading)
    d = end - start
    truth = (c * d[0] + s * d[1], -s * d[0] + c * d[1], math.degrees(math.remainder(end_heading - heading, 2 * math.pi)))
    return odometry, truth


def test_walking_forward_tracks_the_simulated_base(robot):
    odometry, (x, y, yaw) = _walk(robot, 0.04, 0.0, 0.0)
    assert x > 0.1
    assert SHORTFALL[0] < odometry.x / x < SHORTFALL[1]
    assert abs(odometry.y - y) < CROSS_TRACK_M
    assert abs(odometry.yaw - yaw) < STRAIGHT_YAW_DEG
    assert odometry.distance == pytest.approx(odometry.x, rel=1e-3)


def test_walking_sideways_tracks_the_simulated_base(robot):
    odometry, (x, y, yaw) = _walk(robot, 0.0, 0.03, 0.0)
    assert y > 0.1
    assert SHORTFALL[0] < odometry.y / y < SHORTFALL[1]
    assert abs(odometry.x - x) < CROSS_TRACK_M
    assert abs(odometry.yaw - yaw) < STRAIGHT_YAW_DEG


def test_turning_in_place_tracks_the_simulated_heading(robot):
    odometry, (x, y, yaw) = _walk(robot, 0.0, 0.0, 0.6)
    assert 90 < yaw < 180
    assert SHORTFALL[0] < odometry.kinematic_yaw / yaw < SHORTFALL[1]
    assert odometry.yaw == pytest.approx(yaw, rel=TURN_ERROR)
    assert np.hypot(odometry.x - x, odometry.y - y) < CROSS_TRACK_M


# MuJoCo's gyro has neither bias nor noise. The robot's has the measured noise and what bias the boot calibration
# left: here domain_rand.py's one sigma, 0.05 rad/s, which the filter must learn standing before the walk. Measured on
# an 8 s arc at 0.04 m/s and 0.25 rad/s: the base turns 124 degrees, the feet show 11 fewer and the fused heading 0.3
# more. The position ends 22 mm off by the fused heading and 16 mm by the feet's, whose short steps and short turn
# partly cancel: the fused position is held to the steps' shortfall instead, over the path walked.
GYRO_BIAS = 0.05


def test_walking_an_arc_on_a_biased_gyro_tracks_the_heading_better_than_the_feet(robot):
    sim = robot._backend.sim
    raw, rng = sim.gyro, np.random.default_rng(0)
    sim.gyro = lambda: raw() + MODEL_TO_REP.T @ np.array([0.0, 0.0, GYRO_BIAS + rng.normal(0.0, GYRO_NOISE)])
    robot.sleep(2.0)
    robot.reset_odometry()
    start, heading = _ground_truth(robot)
    robot.set_velocity(0.04, 0.0, 0.25)
    robot.sleep(8.0)
    robot.set_velocity(0.0, 0.0, 0.0)
    robot.sleep(1.0)
    odometry = robot.odometry()
    end, end_heading = _ground_truth(robot)
    c, s = math.cos(heading), math.sin(heading)
    d = end - start
    x, y = c * d[0] + s * d[1], -s * d[0] + c * d[1]
    yaw = math.degrees(math.remainder(end_heading - heading, 2 * math.pi))
    assert 90 < yaw < 150
    assert odometry.kinematic_yaw - yaw < -5
    assert abs(odometry.yaw - yaw) < 2
    assert robot._backend._odometry.bias == pytest.approx(GYRO_BIAS, abs=0.005)
    assert np.hypot(odometry.x - x, odometry.y - y) < (1 - SHORTFALL[0]) * odometry.distance


def test_standing_still_and_deactivating(robot):
    robot.sleep(1.0)
    still = robot.odometry()
    assert (still.x, still.y, still.distance) == (0.0, 0.0, 0.0)
    assert abs(still.yaw) < 0.05  # degrees: the gyro sees the body settle
    _walk(robot, 0.04, 0.0, 0.0, seconds=1.0)
    assert robot.odometry().x > 0.02
    robot.deactivate()
    assert robot.odometry().distance == 0.0
