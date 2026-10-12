"""The firmware's kinematic odometry: the port against the firmware's own traces, then against MuJoCo's ground truth."""
import json
import math
from pathlib import Path

import numpy as np
import pytest

from src.leika import Robot
from src.leika.backends.sim import MODEL_TO_REP
from src.robot.firmware_odometry import KinematicOdometry

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


# The commanded feet overlook what the physics adds: on spot_pico the Bezier swing starts and ends with the foot
# still at floor height, 1.5 strokes out, so it pushes the body on beyond the stance stroke. Measured over 4 s after
# a second's settling, the odometry falls short of MuJoCo's base by about 11 % forward (0.148 m of 0.166 m), 15 %
# sideways (0.127 m of 0.149 m) and 13 % in a turn (105 of 121 degrees); a real robot's servos shorten its steps the
# other way, so either needs calibrating against a measurement. The bounds below hold those with some margin.
SHORTFALL = (0.78, 1.0)  # odometry / ground truth along the commanded direction
CROSS_TRACK_M = 0.02
STRAIGHT_YAW_DEG = 5.0


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
    odometry, (x, y, yaw) = _walk(robot, 0.0, 0.0, 1.0)
    assert 90 < yaw < 180
    assert SHORTFALL[0] < odometry.yaw / yaw < SHORTFALL[1]
    assert np.hypot(odometry.x - x, odometry.y - y) < CROSS_TRACK_M


def test_standing_still_and_deactivating(robot):
    robot.sleep(1.0)
    still = robot.odometry()
    assert (still.x, still.y, still.yaw, still.distance) == (0.0, 0.0, 0.0, 0.0)
    _walk(robot, 0.04, 0.0, 0.0, seconds=1.0)
    assert robot.odometry().x > 0.02
    robot.deactivate()
    assert robot.odometry().distance == 0.0
