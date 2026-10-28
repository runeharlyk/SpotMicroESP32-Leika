"""Goal moves in the MuJoCo simulation: the firmware's controller on the firmware's odometry, against the simulated base."""
import math

import numpy as np
import pytest

from src.leika import GoalCancelled, Mode, Robot, RobotError
from src.leika.backends.sim import MODEL_TO_REP
from test_odometry import CROSS_TRACK_M, SHORTFALL, TURN_ERROR

# The goal is judged once the walk has stood still, on the odometry's pose, so a distance lands on it: measured 0.0 mm
# off for 0.2 m, where the base walked 0.211 m, which the feet undercount. The heading at rest also takes up the trot's
# yaw rocking, which the gyro sees and the feet do not, so a turn lands within the goal's 3 degrees rather than on the
# point: 90 degrees ended 1.7 off at half speed and 1.7 at full speed, and 5 ended 2.6 off at full speed.
SETTLED_M = 0.003
SETTLED_DEG = 1.0
TURN_REACHED_DEG = 3.0


@pytest.fixture
def robot():
    client = Robot("simulation", realtime=False)
    client.connect()
    client.stand()
    client.sleep(1.0)
    yield client
    client.close()


def _pose(robot):
    sim = robot._backend.sim
    a = sim.base_qposadr
    return (MODEL_TO_REP @ sim.data.qpos[a : a + 3])[:2], robot.state().yaw


def _moved(robot, start):
    """MuJoCo's base displacement and turn since `start`, in the frame it faced then."""
    (p0, yaw0), (p1, yaw1) = start, _pose(robot)
    c, s = math.cos(math.radians(yaw0)), math.sin(math.radians(yaw0))
    d = p1 - p0
    return c * d[0] + s * d[1], -s * d[0] + c * d[1], (yaw1 - yaw0 + 180) % 360 - 180


def test_a_goal_of_0_2_m_forward_ends_on_the_odometry_and_short_of_the_base_by_its_error(robot):
    robot.reset_odometry()
    start = _pose(robot)
    robot.goal_move(forward=0.2)
    assert robot.state().mode is Mode.STAND
    robot.sleep(1.0)
    odometry = robot.odometry()
    assert odometry.x == pytest.approx(0.2, abs=SETTLED_M)
    assert abs(odometry.y) < SETTLED_M and abs(odometry.kinematic_yaw) < SETTLED_DEG
    x, y, yaw = _moved(robot, start)
    assert SHORTFALL[0] < odometry.x / x < SHORTFALL[1]
    assert odometry.yaw == pytest.approx(yaw, abs=SETTLED_DEG)
    assert abs(y) < CROSS_TRACK_M


def test_a_goal_of_90_degrees_ends_on_the_fused_heading_and_so_on_the_base(robot):
    robot.reset_odometry()
    start = _pose(robot)
    robot.goal_move(turn_deg=90)
    robot.sleep(1.0)
    odometry = robot.odometry()
    assert odometry.yaw == pytest.approx(90, abs=TURN_REACHED_DEG)
    assert np.hypot(odometry.x, odometry.y) < SETTLED_M
    x, y, yaw = _moved(robot, start)
    assert yaw == pytest.approx(90, rel=TURN_ERROR)
    assert SHORTFALL[0] < odometry.kinematic_yaw / yaw < SHORTFALL[1]
    assert np.hypot(x, y) < CROSS_TRACK_M


# At full speed the walk carries a turn on: judged where it comes to rest, a 90 degree goal ends within its tolerance,
# where it used to centre its sticks at the target and report it reached 4.4 degrees past.
def test_a_fast_turn_ends_within_its_tolerance_once_at_rest(robot):
    robot.reset_odometry()
    robot.goal_move(turn_deg=90, speed=1.0)
    robot.sleep(1.0)
    assert robot.odometry().yaw == pytest.approx(90, abs=TURN_REACHED_DEG)


# A metre at full speed: the trot drifts in heading, which the goal holds, and so its start line, where it used to curve
# 45 mm off it.
def test_a_fast_metre_holds_its_heading_and_its_line(robot):
    robot.reset_odometry()
    robot.goal_move(forward=1.0, speed=1.0)
    robot.sleep(1.0)
    odometry = robot.odometry()
    assert odometry.x == pytest.approx(1.0, abs=SETTLED_M)
    assert abs(odometry.y) < SETTLED_M
    assert abs(odometry.yaw) < TURN_REACHED_DEG


def test_a_mode_change_mid_goal_cancels_it_and_stops_the_walk(robot):
    backend = robot._backend
    # The goal is walked inside goal_move; a mode change from the "app" arrives on a tick in between.
    ticks = 0
    step = backend._step

    def step_then_stand():
        nonlocal ticks
        step()
        ticks += 1
        if ticks == 100:
            backend.set_mode(Mode.STAND)

    backend._step = step_then_stand
    with pytest.raises(GoalCancelled):
        robot.goal_move(forward=0.2)
    backend._step = step
    assert robot.state().mode is Mode.STAND
    assert 0.01 < robot.odometry().x < 0.1


def test_a_goal_is_refused_from_a_mode_that_cannot_walk(robot):
    robot.deactivate()
    with pytest.raises(RobotError):
        robot.goal_move(forward=0.1)
    with pytest.raises(ValueError):
        robot.goal_move(forward=0.1, turn_deg=10)
