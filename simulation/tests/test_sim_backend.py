"""The MuJoCo backend, headless and stepped only inside sleep()."""
import re
from pathlib import Path

import numpy as np
import pytest

from src.leika import Robot, speed_model
from src.leika.backends.base import RobotError, Velocity, ZERO
from src.leika.backends.sim import MODEL_TO_REP, SimBackend
from src.leika.constants import Gait, Mode
from src.sim import recording

FACTORY_SETTINGS = Path(__file__).resolve().parents[2] / "esp32" / "factory_settings.ini"


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


# The simulated gait's gains are fitted for trot; in crawl it walks about a fifth of what it reports.
def test_the_simulation_refuses_a_gait_it_does_not_model(sim):
    with pytest.raises(RobotError):
        sim.set_gait(Gait.CRAWL)
    sim.set_gait(Gait.TROT)


def test_a_velocity_beyond_the_gait_is_clamped(sim):
    top = sim.max_velocity()
    assert sim.set_velocity(Velocity(10, -10, 100)) == Velocity(top.vx, -top.vy, top.yaw_rate)
    assert sim.set_velocity(ZERO) == ZERO


def test_it_reports_itself_as_the_mini_on_the_firmwares_version(sim):
    robot = Robot(backend=sim)
    assert speed_model.known_variant(robot.features().variant) == "SPOTMICRO_ESP32_MINI"
    version = re.search(r'APP_VERSION=\\"([^"\\]*)\\"', FACTORY_SETTINGS.read_text())[1]
    assert robot.features().firmware_version == version


# What a recording of the robot holds, so a script's recording reads alike from either.
def test_a_recording_holds_every_tick_of_its_block_as_the_robot_reported_it(sim, tmp_path):
    robot = Robot(backend=sim)
    path = tmp_path / "stand.leika"
    robot.sleep(0.5)
    with robot.recording(path):
        robot.stand()
        robot.sleep(1.0)
        state = robot.state()
        angles, pwm = robot.servo_output()
        odometry = robot.odometry()
    size = path.stat().st_size
    robot.sleep(0.5)
    assert path.stat().st_size == size

    rec = recording.load(path)
    header = rec.header
    assert (header.variant, header.firmware_version) == ("SPOTMICRO_ESP32_MINI", robot.features().firmware_version)
    assert (header.control_rate_hz, header.batch_ticks) == (100, 10)
    assert [servo.center_pwm for servo in header.servo_settings.servos] == list(robot.servo_settings().center_pwm)
    assert header.servo_settings.model.pwm_per_degree == robot.servo_settings().pwm_per_degree
    ticks = rec.ticks
    assert len(ticks["seq"]) == 100 and rec.missing_ticks == 0
    assert np.all(np.diff(ticks["t_us"]) == 10_000) and np.all(ticks["period_us"] == 10_000)
    assert ticks["t_us"][0] == 510_000 and ticks["t_us"][-1] == round(state.t * 1e6)
    assert ticks["mode"][-1] == Mode.STAND.value and ticks["gait"][-1] == Gait.TROT.value
    assert np.degrees(ticks["rpy"][-1]) == pytest.approx((state.roll, state.pitch, state.yaw), abs=1e-4)
    assert ticks["gyro"][-1] == pytest.approx(state.gyro, abs=1e-5)
    assert ticks["accel"][-1] == pytest.approx(state.accel, abs=1e-4)
    assert np.degrees(ticks["angles"][-1]) == pytest.approx(angles, abs=1e-4)
    assert ticks["pwm"][-1] == pytest.approx(pwm)
    assert (ticks["odometry_x"][-1], np.degrees(ticks["odometry_yaw"][-1])) == pytest.approx((odometry.x, odometry.yaw),
                                                                                             abs=1e-5)
