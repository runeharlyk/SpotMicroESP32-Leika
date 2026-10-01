"""The MuJoCo backend, headless and stepped only inside sleep()."""
import numpy as np
import pytest

from src.leika.backends.base import Velocity, ZERO
from src.leika.backends.sim import MODEL_TO_REP, SimBackend
from src.leika.constants import Mode


@pytest.fixture
def sim():
    backend = SimBackend(realtime=False)
    backend.connect()
    yield backend
    backend.close()


def _position(backend):
    a = backend.sim.base_qposadr
    return MODEL_TO_REP @ backend.sim.data.qpos[a : a + 3]


def test_a_standing_robot_reads_level(sim):
    sim.set_mode(Mode.STAND)
    sim.sleep(1.0)
    state = sim.state()
    assert state.mode is Mode.STAND
    assert abs(state.roll) < 2 and abs(state.pitch) < 2 and abs(state.yaw) < 2
    assert state.t == pytest.approx(1.0, abs=0.011)
    assert state.accel[2] == pytest.approx(9.81, abs=0.5)


def test_walking_forward_left_and_counter_clockwise_follow_rep103(sim):
    sim.set_mode(Mode.WALK)
    start = _position(sim)
    sim.set_velocity(Velocity(0.04, 0, 0))
    sim.sleep(3.0)
    forward = _position(sim) - start
    assert forward[0] > 0.08 and abs(forward[1]) < 0.03
    sim.set_velocity(Velocity(0, 0.03, 0))
    middle = _position(sim)
    sim.sleep(3.0)
    assert (_position(sim) - middle)[1] > 0.05
    yaw = sim.state().yaw
    sim.set_velocity(Velocity(0, 0, 1.0))
    sim.sleep(1.0)
    assert sim.state().gyro[2] > 0.2
    assert sim.state().yaw > yaw + 10


def test_a_velocity_moves_the_robot_only_when_it_walks(sim):
    sim.set_mode(Mode.STAND)
    start = _position(sim)
    sim.set_velocity(Velocity(0.04, 0, 0))
    sim.sleep(2.0)
    assert np.linalg.norm(_position(sim) - start) < 0.01


def test_a_velocity_beyond_the_gait_is_clamped(sim):
    top = sim.max_velocity()
    assert sim.set_velocity(Velocity(10, -10, 100)) == Velocity(top.vx, -top.vy, top.yaw_rate)
    assert sim.set_velocity(ZERO) == ZERO
