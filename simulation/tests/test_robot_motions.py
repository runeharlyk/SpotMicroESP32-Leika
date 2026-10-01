"""The facade and its motions, on the scripted backend and in the simulation."""
import numpy as np
import pytest

from scripted_backend import ScriptedBackend
from src.leika import Mode, Robot
from src.leika.backends.base import ZERO, RobotTimeout, Velocity
from src.leika.backends.sim import MODEL_TO_REP, SimBackend


def _robot(**options):
    backend = ScriptedBackend(**options)
    return Robot(backend=backend), backend


def test_a_velocity_starts_the_walk():
    robot, backend = _robot()
    robot.stand()
    robot.set_velocity(0.05, 0, 0)
    assert backend.modes == [Mode.STAND, Mode.WALK]
    robot.stop()
    assert backend.velocity == ZERO and backend.mode is Mode.WALK


def test_a_move_runs_half_the_top_speed_for_its_distance():
    robot, backend = _robot()
    robot.stand()
    robot.move_forward(0.5)
    (start, v), (end, stop) = backend.commands[-2:]  # stand() sent a stop first
    assert v == Velocity(0.05, 0, 0) and stop == ZERO
    assert end - start == pytest.approx(10.0)
    assert (backend.x, backend.y) == pytest.approx((0.5, 0.0))


def test_moves_go_back_and_to_the_left():
    robot, backend = _robot()
    robot.move_forward(-0.2)
    robot.move_sideways(0.1)
    assert (backend.x, backend.y) == pytest.approx((-0.2, 0.1))


def test_a_move_beyond_the_top_speed_still_covers_its_distance():
    robot, backend = _robot()
    robot.move_forward(0.3, speed=5.0)
    assert backend.x == pytest.approx(0.3)


# The robot's yaw arrives late and its gait turns slower than commanded: the turn must still land on target.
def test_a_lagging_turn_slows_down_and_lands_on_target():
    robot, backend = _robot(yaw_lag_s=0.2, turn_scale=0.9)
    robot.rotate(90)
    assert backend.yaw == pytest.approx(90, abs=3)
    last_turn = [v.yaw_rate for _, v in backend.commands if v.yaw_rate][-1]
    assert abs(last_turn) < 0.35 * backend.top.yaw_rate
    start = backend.yaw
    robot.rotate(-90)
    assert backend.yaw - start == pytest.approx(-90, abs=3)


def test_a_turn_across_the_wrap_and_a_full_circle():
    robot, backend = _robot(yaw=170.0)
    robot.rotate(40)
    assert (backend.yaw + 180) % 360 - 180 == pytest.approx(-150, abs=3)
    start = backend.yaw
    robot.rotate(360)
    assert backend.yaw - start == pytest.approx(360, abs=3)


def test_a_move_or_turn_without_speed_is_refused():
    robot, _ = _robot()
    with pytest.raises(ValueError):
        robot.move_forward(0.1, speed=0)
    with pytest.raises(ValueError):
        robot.rotate(10, rate=0)


def test_a_turn_that_never_comes_times_out_and_stops():
    robot, backend = _robot(turn_scale=0.0)
    with pytest.raises(RobotTimeout):
        robot.rotate(45)
    assert backend.velocity == ZERO


def test_an_exception_stops_and_rests():
    robot, backend = _robot()
    with pytest.raises(KeyboardInterrupt):
        with robot:
            robot.set_velocity(0.05, 0, 0.2)
            raise KeyboardInterrupt
    assert backend.velocity == ZERO
    assert backend.modes[-1] is Mode.REST and Mode.DEACTIVATED not in backend.modes
    assert backend.closed


def _sim_position(backend):
    a = backend.sim.base_qposadr
    return MODEL_TO_REP @ backend.sim.data.qpos[a : a + 3]


# The simulated gait tracks its velocity loosely (measured while planning: +2 to +24%), so the distance tolerance is
# wide; the turn closes on the yaw and lands tightly.
def test_the_simulation_walks_a_distance_and_turns_an_angle():
    backend = SimBackend(realtime=False)
    with Robot(backend=backend) as robot:
        robot.stand()
        start = _sim_position(backend)
        robot.move_forward(0.3)
        assert np.linalg.norm(_sim_position(backend) - start) == pytest.approx(0.3, abs=0.06)
        yaw = robot.state().yaw
        robot.rotate(90)
        assert (robot.state().yaw - yaw + 180) % 360 - 180 == pytest.approx(90, abs=4)
