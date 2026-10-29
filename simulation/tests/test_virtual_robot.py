"""The virtual robot (the firmware's motion, servo and socket code on the host) driven through the real robot's backend."""
import asyncio
import hashlib
import time

import numpy as np
import pytest
import websockets

import virtual_robot
from src.leika import Mode, Robot, RobotError
from src.leika.backends import real
from src.leika.backends.real import RealBackend
from src.proto import api_pb2
from src.proto import message_pb2 as pb
from src.sim import recording


@pytest.fixture
def served(binary):
    robot = virtual_robot.VirtualRobot(binary)
    yield robot
    robot.stop()


@pytest.fixture
def backend(served):
    client = RealBackend(f"127.0.0.1:{served.port}")
    client.connect()
    yield client
    client.close()


@pytest.fixture
def robot(backend):
    return Robot(backend=backend)


def _joint_spread(robot, seconds):
    """The largest swing of any joint, in degrees, over `seconds` of telemetry."""
    seen = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        seen.append(robot.state().joints)
        robot.sleep(0.05)
    return max(max(joint) - min(joint) for joint in zip(*seen))


def _eventually(done, timeout):
    deadline = time.monotonic() + timeout
    while not done():
        assert time.monotonic() < deadline
        time.sleep(0.02)


def test_it_reports_itself_as_a_level_deactivated_robot(binary, backend, robot):
    features = backend.features
    assert features.variant == "SPOTMICRO_ESP32"
    assert features.firmware_built_target == "virtual"
    assert features.firmware_version.count(".") == 2  # the APP_VERSION of esp32/factory_settings.ini
    with open(binary, "rb") as f:
        assert features.firmware_elf_sha256 == hashlib.sha256(f.read()).hexdigest()
    state = robot.state()
    assert state.mode is Mode.DEACTIVATED
    assert (state.roll, state.pitch) == (0, 0)
    assert state.accel == pytest.approx((0, 0, 9.81))


def test_it_stands_and_walks_on_the_firmware_gait(robot):
    resting = robot.state().joints
    robot.stand()
    robot.sleep(1.0)
    standing = robot.state().joints
    assert max(abs(a - b) for a, b in zip(resting, standing)) > 10
    assert _joint_spread(robot, 0.5) < 0.5

    robot.set_velocity(robot.max_velocity().vx / 2, 0, 0)
    assert robot.state().mode is Mode.WALK
    assert _joint_spread(robot, 1.0) > 5


def test_the_dead_man_stop_halts_a_walk_whose_input_falls_silent(robot, monkeypatch):
    robot.stand()
    robot.set_velocity(robot.max_velocity().vx / 2, 0, 0)
    robot.sleep(0.5)
    assert not robot.state().link_lost

    monkeypatch.setattr(real, "KEEPALIVE_S", 3600)  # the input still asks for a walk, but is no longer re-sent
    _eventually(lambda: robot.state().link_lost, 2.0)
    robot.sleep(1.5)  # the step length eases to zero, and the feet come down
    assert _joint_spread(robot, 0.5) < 0.5
    state = robot.state()
    assert (state.mode, state.link_drops) == (Mode.WALK, 1)

    robot.set_velocity(robot.max_velocity().vx / 2, 0, 0)
    _eventually(lambda: not robot.state().link_lost, 1.0)
    assert _joint_spread(robot, 1.0) > 5


def test_it_answers_the_servo_settings_and_refuses_what_it_does_not_have(backend):
    servo = backend.request(pb.CorrelationRequest(servo_settings_request=api_pb2.ServoSettingsRequest())).servo_settings
    assert len(servo.servos) == 12
    with pytest.raises(RobotError, match="Not available on the virtual robot"):
        backend.request(pb.CorrelationRequest(i2c_scan_data_request=pb.I2CScanDataRequest()))
    assert backend.state().mode is Mode.DEACTIVATED  # the link outlives a refusal


def test_only_the_robot_socket_is_served(served):
    async def connect(path):
        async with websockets.connect(f"ws://127.0.0.1:{served.port}{path}", open_timeout=2):
            pass

    with pytest.raises(websockets.InvalidStatus):
        asyncio.run(connect("/ws"))


def test_a_recording_holds_the_firmwares_header_and_every_tick_of_its_block(robot, tmp_path):
    path = tmp_path / "stand.leika"
    with robot.recording(path):
        robot.sleep(0.3)
        robot.stand()
        robot.sleep(1.0)
    rec = recording.load(path)
    header = rec.header
    assert (header.variant, header.firmware_version) == ("SPOTMICRO_ESP32", robot.features().firmware_version)
    assert header.batch_ticks == 10 and len(header.servo_settings.servos) == 12
    assert len(rec.ticks["seq"]) >= 50 and rec.missing_ticks == 0
    modes = rec.ticks["mode"]
    assert modes[0] == Mode.DEACTIVATED.value and modes[-1] == Mode.STAND.value
    # The firmware's own clock times the tick's parts, each inside the whole it belongs to.
    t = rec.ticks
    standing = modes == Mode.STAND.value
    assert t["motion_us"][standing].sum() > 0 and t["kinematics_us"][standing].sum() > 0
    assert np.all(t["motion_step_us"] + t["odometry_us"] + t["goal_us"] + t["kinematics_us"] <= t["motion_us"])
    assert np.all(t["motion_us"] <= t["compute_us"])
    assert np.all(t["servo_bus_wait_us"] + t["servo_transfer_us"] <= t["servo_write_us"])
