"""The real robot's backend against a fake robot on a local WebSocket."""
import math
import time

import pytest

from fake_robot import FakeRobot
from src.leika import Mode, Robot, RobotDisconnected, RobotTimeout, UnknownVariant
from src.leika.backends import real
from src.leika.backends.real import RealBackend
from src.proto import message_pb2 as pb


# A send returns once the client sent it; the fake robot records it a moment later on its own thread.
def _eventually(done, timeout=1.0):
    deadline = time.monotonic() + timeout
    while not done():
        assert time.monotonic() < deadline, "the fake robot never received it"
        time.sleep(0.01)


@pytest.fixture
def fake():
    robot = FakeRobot()
    yield robot
    robot.stop()


@pytest.fixture
def robot(fake, monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 1.0)
    monkeypatch.setattr(real, "SILENCE_S", 0.6)
    client = Robot(backend=RealBackend(f"127.0.0.1:{fake.port}"))
    client.connect()
    yield client
    client.close()


def test_connecting_reads_the_variant_and_the_state(robot):
    state = robot.state()
    assert state.mode is Mode.DEACTIVATED
    assert (state.roll, state.yaw) == pytest.approx((math.degrees(0.1), math.degrees(1.0)))
    assert state.joints[0] == pytest.approx(math.degrees(0.5))
    assert robot.max_velocity().vx == pytest.approx(0.8 * 0.12 * 1.0 / 0.75)


def test_an_unknown_variant_is_refused():
    fake = FakeRobot(variant="SPOTMICRO_GIANT")
    try:
        with pytest.raises(UnknownVariant):
            RealBackend(f"127.0.0.1:{fake.port}").connect()
    finally:
        fake.stop()


def test_stand_waits_for_the_mode_and_sends_the_height(robot, fake):
    robot.stand(height=0.6)
    assert robot.state().mode is Mode.STAND
    _eventually(lambda: fake.inputs()[-1][1].height == pytest.approx(0.6))
    _, last = fake.inputs()[-1]
    assert (last.height, last.speed, last.s1) == pytest.approx((0.6, 0.5, 0.5))
    assert (last.left.x, last.left.y, last.right.x) == (0, 0, 0)


def test_a_mode_the_robot_never_reports_times_out(monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 0.5)
    fake = FakeRobot(obey=False)
    try:
        client = Robot(backend=RealBackend(f"127.0.0.1:{fake.port}"))
        client.connect()
        started = time.monotonic()
        with pytest.raises(RobotTimeout):
            client.stand()
        assert time.monotonic() - started < 1.5
        client.close()
    finally:
        fake.stop()


def test_a_velocity_walks_with_a_keep_alive_and_a_stop_is_sent_once(robot, fake):
    robot.stand()
    commanded = robot.set_velocity(0.064, 0.0, -0.5)
    assert fake.mode == pb.WALK
    robot.sleep(0.6)
    moving = [(t, data) for t, data in fake.inputs() if data.left.y]
    assert len(moving) >= 5
    assert max(b - a for (a, _), (b, _) in zip(moving, moving[1:])) < 0.15
    _, data = moving[-1]
    assert (data.left.y, data.right.x) == pytest.approx((0.5, 0.5 * 0.75))
    assert (commanded.vx, commanded.yaw_rate) == pytest.approx((0.064, -0.5))
    robot.stop()
    _eventually(lambda: fake.inputs()[-1][1].left.y == 0)
    before = len(fake.inputs())
    robot.sleep(0.4)
    after = fake.inputs()
    assert len(after) == before and after[-1][1].left.y == 0


def test_leaving_after_an_error_stops_then_rests(fake, monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 1.0)
    with pytest.raises(ValueError):
        with Robot(backend=RealBackend(f"127.0.0.1:{fake.port}")) as client:
            client.set_velocity(0.03, 0, 0)
            raise ValueError("a bug in the script")
    kinds = fake.kinds()
    last_input = max(i for i, kind in enumerate(kinds) if kind == "controller_data")
    assert fake.received[last_input][1].controller_data.left.y == 0
    assert kinds[last_input + 1 :].count("mode") == 1 and fake.mode == pb.REST


def test_a_dropped_link_raises(robot, fake):
    fake.drop()
    with pytest.raises(RobotDisconnected):
        robot.sleep(1.0)
    with pytest.raises(RobotDisconnected):
        robot.state()


def test_a_silent_link_counts_as_disconnected(robot, fake):
    fake.silent = True
    started = time.monotonic()
    with pytest.raises(RobotDisconnected):
        robot.sleep(3.0)
    assert time.monotonic() - started < 1.5


def test_calibration_returns_the_robots_result(robot):
    result = robot.calibrate()
    assert (result.still, result.levelled, result.tilt_deg) == (True, True, pytest.approx(4.2))
