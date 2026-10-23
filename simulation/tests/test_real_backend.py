"""The real robot's backend against a fake robot on a local WebSocket and on a fake USB serial port."""
import asyncio
import base64
import hashlib
import math
import re
import time

import pytest

from fake_robot import FakeRobot
from src.leika import (
    Gait, GoalCancelled, Mode, Odometry, Robot, RobotDisconnected, RobotError, RobotTimeout, UnknownVariant,
)
from src.leika import link
from src.leika.backends.base import Velocity
from src.leika import speed_model
from src.leika.backends import real
from src.leika.backends.real import RealBackend
from src.proto import message_pb2 as pb


# A send returns once the client sent it; the fake robot records it a moment later on its own thread.
def _eventually(done, timeout=1.0):
    deadline = time.monotonic() + timeout
    while not done():
        assert time.monotonic() < deadline, "the fake robot never received it"
        time.sleep(0.01)


# Every robot a test makes, on the transport the test runs over; a serial port opens onto the newest.
@pytest.fixture(params=["websocket", "serial"])
def make_fake(request, monkeypatch):
    made = []

    def make(**options):
        robot = FakeRobot(transport=request.param, **options)
        monkeypatch.setattr(link, "Serial", robot.serial_class)
        made.append(robot)
        return robot

    yield make
    for robot in made:
        robot.stop()


@pytest.fixture
def fake(make_fake):
    return make_fake()


@pytest.fixture
def robot(fake, monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 1.0)
    monkeypatch.setattr(real, "SILENCE_S", 0.6)
    client = Robot(backend=RealBackend(fake.target))
    client.connect()
    yield client
    client.close()


def test_connecting_reads_the_variant_and_the_state(robot):
    state = robot.state()
    assert state.mode is Mode.DEACTIVATED
    assert (state.roll, state.yaw) == pytest.approx((math.degrees(0.1), math.degrees(1.0)))
    assert state.joints[0] == pytest.approx(math.degrees(0.5))
    assert robot.max_velocity() == speed_model.max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, 0.7)


# A robot told its servos run at 4.8 V walks slower, and the client asks it for faster sticks to make up for it.
def test_the_speed_model_runs_at_the_robots_servo_supply(make_fake):
    fake = make_fake(supply_voltage=4.8)
    client = Robot(backend=RealBackend(fake.target))
    client.connect()
    try:
        low = speed_model.max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, 0.7, supply_volts=4.8)
        assert client.max_velocity() == low
        assert low.vx < 0.9 * speed_model.max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, 0.7).vx
        wanted = Velocity(0.8 * low.vx, 0.0, 0.0)
        client.stand()
        client.set_velocity(wanted.vx, wanted.vy, wanted.yaw_rate)
        sticks = speed_model.sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, wanted, 0.7, supply_volts=4.8)
        assert sticks.ly > speed_model.sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, wanted, 0.7).ly
        _eventually(lambda: fake.inputs() and fake.inputs()[-1][1].left.y == pytest.approx(sticks.ly))
    finally:
        client.close()


# A firmware from before the setting reports no supply: the client takes the 6 V the firmware then runs at.
def test_a_robot_without_a_supply_runs_at_the_default(make_fake):
    fake = make_fake(supply_voltage=0.0)
    client = Robot(backend=RealBackend(fake.target))
    client.connect()
    try:
        assert client.max_velocity() == speed_model.max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, 0.7)
    finally:
        client.close()


def test_an_unknown_variant_is_refused(make_fake):
    fake = make_fake(variant="SPOTMICRO_GIANT")
    with pytest.raises(UnknownVariant):
        RealBackend(fake.target).connect()
    _eventually(lambda: fake.connections == 0)  # a failed connect leaves no subscriber on the robot


def test_stand_waits_for_the_mode_and_sends_the_height(robot, fake):
    robot.stand(height=0.6)
    assert robot.state().mode is Mode.STAND
    _eventually(lambda: fake.inputs()[-1][1].height == pytest.approx(0.6))
    _, last = fake.inputs()[-1]
    assert (last.height, last.speed, last.s1) == pytest.approx((0.6, 0.5, 0.5))
    assert (last.left.x, last.left.y, last.right.x) == (0, 0, 0)


def test_a_mode_the_robot_never_reports_times_out(make_fake, monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 0.5)
    fake = make_fake(obey=False)
    client = Robot(backend=RealBackend(fake.target))
    client.connect()
    started = time.monotonic()
    with pytest.raises(RobotTimeout):
        client.stand()
    assert time.monotonic() - started < 1.5
    client.close()


def test_a_velocity_walks_with_a_keep_alive_and_a_stop_is_sent_once(robot, fake):
    robot.stand()
    commanded = robot.set_velocity(0.04, 0.02, -0.3)
    assert fake.mode == pb.WALK
    robot.sleep(0.6)
    moving = [(t, data) for t, data in fake.inputs() if data.left.y]
    assert len(moving) >= 5
    assert max(b - a for (a, _), (b, _) in zip(moving, moving[1:])) < 0.15
    _, data = moving[-1]
    sticks = speed_model.sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, Velocity(0.04, 0.02, -0.3), 0.7)
    # The firmware strafes right on a positive left.x and turns left on a positive right.x.
    assert sticks.lx < 0 and sticks.rx < 0
    assert (data.left.x, data.left.y, data.right.x) == pytest.approx((sticks.lx, sticks.ly, sticks.rx))
    assert (commanded.vx, commanded.vy, commanded.yaw_rate) == pytest.approx((0.04, 0.02, -0.3))
    robot.stop()
    _eventually(lambda: fake.inputs()[-1][1].left.y == 0)
    before = len(fake.inputs())
    robot.sleep(0.4)
    after = fake.inputs()
    assert len(after) == before and after[-1][1].left.y == 0


def test_leaving_after_an_error_stops_then_rests(fake, monkeypatch):
    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 1.0)
    with pytest.raises(ValueError):
        with Robot(backend=RealBackend(fake.target)) as client:
            client.stand()  # the robot boots deactivated, and a script does not wake it by walking
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


def test_a_request_the_robot_refuses_raises(robot, fake):
    fake.status = 503
    with pytest.raises(RobotError):
        robot.calibrate()


# The sticks for a velocity depend on the gait: a gait change re-sends them, so the robot keeps the velocity asked for.
def test_a_gait_change_keeps_the_velocity(robot, fake):
    robot.stand()
    robot.set_velocity(0.02, 0, 0)
    robot.set_gait(Gait.CRAWL)
    crawl = speed_model.sticks_for("SPOTMICRO_ESP32_MINI", Gait.CRAWL, Velocity(0.02, 0.0, 0.0), 0.7)
    trot = speed_model.sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, Velocity(0.02, 0.0, 0.0), 0.7)
    assert crawl.ly != pytest.approx(trot.ly, rel=1e-3)
    _eventually(lambda: fake.inputs()[-1][1].left.y == pytest.approx(crawl.ly, rel=1e-3))


# AUTO swaps the gait by the sticks, which a velocity's sticks depend on: the client refuses it rather than walk
# another velocity than asked, and the robot keeps its gait.
def test_auto_is_refused_and_the_gait_kept(robot, fake):
    robot.stand()
    with pytest.raises(RobotError, match="AUTO"):
        robot.set_gait(Gait.AUTO)
    assert "walk_gait" not in fake.kinds()


# A stall shorter than a batch trips and clears the dead-man between two newest ticks: it must still be counted.
def test_a_dead_man_stop_between_two_reads_is_counted(robot, fake):
    assert robot.state().link_drops == 0
    fake.blip()
    _eventually(lambda: robot.state().link_drops == 1)


def test_calibration_returns_the_robots_result(robot):
    result = robot.calibrate()
    assert (result.still, result.levelled, result.tilt_deg) == (True, True, pytest.approx(4.2))


# Only the offset changes: a write built from defaults would reset the pins, the bus speed and the mounting.
def test_storing_a_compass_offset_keeps_every_other_peripheral_setting(fake):
    backend = RealBackend(fake.target)
    backend.connect()
    try:
        backend.set_mag_offset([12.5, -40.0, 7.25])
    finally:
        backend.close()
    stored = fake.peripheral_settings
    assert list(stored.imu.mag_offset) == [12.5, -40.0, 7.25]
    assert (stored.sda, stored.scl, stored.frequency) == (21, 22, 400000)
    assert list(stored.imu.mounting) == [0, 1, 0, -1, 0, 0, 0, 0, 1]


# A robot keeps streaming to a serial port the client closed until its buffer stays full for seconds.
def test_closing_ends_the_telemetry_subscription(robot, fake):
    robot.close()
    _eventually(lambda: "unsub_notif" in fake.kinds())
    unsubscribed = [message.unsub_notif.tag for _, message in fake.received if message.HasField("unsub_notif")]
    assert unsubscribed == [pb.Message.DESCRIPTOR.fields_by_name["telemetry_batch"].number]


# Opening a port asserts DTR and RTS unless they were cleared first, and an ESP32 resets on that.
@pytest.mark.parametrize("make_fake", ["serial"], indirect=True)
def test_the_usb_port_opens_without_resetting_the_board(robot, fake):
    assert [port.opened_with for port in fake.ports] == [(False, False)]


def test_odometry_is_read_in_degrees_and_a_reset_returns_where_it_had_got_to(robot):
    reached = Odometry(x=0.5, y=-0.25, yaw=pytest.approx(90.0), distance=0.75, kinematic_yaw=pytest.approx(60.0))
    assert robot.odometry() == reached
    assert robot.reset_odometry() == reached
    assert robot.odometry() == Odometry(0.0, 0.0, 0.0, 0.0, 0.0)


def _goal_moves(fake):
    return [m.correlation_request.goal_move for _, m in fake.received
            if m.WhichOneof("message") == "correlation_request" and m.correlation_request.HasField("goal_move")]


def test_a_goal_move_is_sent_once_and_returns_when_the_robot_reports_it_reached(robot, fake):
    assert robot.supports_goals()
    robot.stand()
    started = time.monotonic()
    robot.goal_move(turn_deg=-45, speed=0.7)
    assert time.monotonic() - started >= fake.goal_s
    [move] = _goal_moves(fake)
    assert (move.forward_m, move.sideways_m, move.turn_deg, move.speed) == pytest.approx((0, 0, -45, 0.7))
    assert fake.inputs()[-1][1].left.y == 0  # the robot walks it: no sticks, no keep-alive
    assert "goal_status" in [_sub_tag_name(m) for _, m in fake.received if m.WhichOneof("message") == "sub_notif"]


def _sub_tag_name(message):
    return pb.Message.DESCRIPTOR.fields_by_number[message.sub_notif.tag].name


# A broadcast that never arrives still ends the wait: the client asks for the status itself.
def test_a_goal_whose_end_was_not_heard_is_asked_after(robot, fake, monkeypatch):
    monkeypatch.setattr(real, "GOAL_POLL_S", 0.2)
    fake.goal_quiet = True
    robot.stand()
    robot.goal_move(forward=0.1)
    assert fake.goal.state == pb.GOAL_REACHED


# The robot cancels a goal whose client has been silent for 2 s, which is how it finds a closed USB port: while a goal
# runs, the client asks for its status every GOAL_POLL_S, broadcasts heard or not.
def test_a_running_goals_client_keeps_asking_for_its_status(robot, fake):
    fake.goal_s = 2.0
    robot.stand()
    robot.goal_move(forward=0.1)
    asked = [t for t, m in fake.received if m.WhichOneof("message") == "correlation_request"
             and m.correlation_request.HasField("goal_status_request")]
    assert len(asked) >= 3
    assert max(b - a for a, b in zip(asked, asked[1:])) < 2 * real.GOAL_POLL_S


def test_a_goal_the_robot_gives_up_or_cancels_raises(robot, fake):
    robot.stand()
    fake.goal_end = pb.GOAL_TIMED_OUT
    with pytest.raises(RobotTimeout):
        robot.goal_move(forward=0.1)
    fake.goal_end = pb.GOAL_CANCELLED
    with pytest.raises(GoalCancelled):
        robot.goal_move(sideways=0.1)


def test_a_goal_the_robot_refuses_raises(robot, fake):
    robot.stand()
    fake.status = 409
    with pytest.raises(RobotError):
        robot.goal_move(forward=0.1)


# The robot may have taken a goal whose reply was lost, or whose wait Ctrl-C ended: the client cancels it.
@pytest.mark.parametrize("ending", [RobotTimeout, KeyboardInterrupt])
def test_a_goal_whose_reply_never_came_is_cancelled(robot, fake, monkeypatch, ending):
    robot.stand()
    fake.goal_s = 5.0
    request = robot._backend.request

    def reply_lost(sent):
        reply = request(sent)
        if sent.HasField("goal_move"):
            raise ending("no reply")
        return reply

    monkeypatch.setattr(robot._backend, "request", reply_lost)
    with pytest.raises(ending):
        robot.goal_move(forward=0.1)
    _eventually(lambda: fake.goal.state == pb.GOAL_CANCELLED)


def test_an_interrupted_goal_is_cancelled_on_the_robot(robot, fake, monkeypatch):
    robot.stand()
    fake.goal_s = 5.0

    def interrupt(goal):
        raise KeyboardInterrupt

    monkeypatch.setattr(robot._backend, "_await_goal", interrupt)
    with pytest.raises(KeyboardInterrupt):
        robot.goal_move(forward=0.1)
    _eventually(lambda: fake.goal.state == pb.GOAL_CANCELLED)


def test_a_firmware_without_goals_moves_host_side():
    fake = FakeRobot(goals=False)
    try:
        client = Robot(backend=RealBackend(fake.target))
        client.connect()
        try:
            assert not client.supports_goals()
            client.stand()
            client.move_forward(0.01, speed=0.05)
            assert _goal_moves(fake) == []
            assert any(data.left.y > 0 for _, data in fake.inputs())
        finally:
            client.close()
    finally:
        fake.stop()


# A robot that lost power sends no close frame and answers nothing: the link's keep-alive finds it gone, where a
# request-only tool, which subscribes to no stream, would otherwise wait on it until the TCP connection timed out.
def test_a_link_to_a_robot_that_stopped_answering_closes(monkeypatch):
    monkeypatch.setattr(link, "KEEPALIVE_S", 0.2)
    monkeypatch.setattr(link, "KEEPALIVE_TIMEOUT_S", 0.3)

    async def handshake_then_silence(reader, writer):
        request = (await reader.readuntil(b"\r\n\r\n")).decode()
        key = re.search(r"Sec-WebSocket-Key: (\S+)", request, re.IGNORECASE).group(1)
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        await writer.drain()
        await asyncio.sleep(10)

    async def recv_until_closed():
        server = await asyncio.start_server(handshake_then_silence, "127.0.0.1", 0)
        port = server.sockets[0].getsockname()[1]
        opened = await link.WebSocketLink.open(f"ws://127.0.0.1:{port}/api/ws", timeout=1)
        started = time.monotonic()
        try:
            with pytest.raises(link.LinkClosed):
                await asyncio.wait_for(opened.recv(), timeout=3)
            return time.monotonic() - started
        finally:
            server.close()

    assert asyncio.run(recv_until_closed()) < 2
