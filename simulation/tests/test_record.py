import asyncio
import socket
import sys
from pathlib import Path

import pytest
from websockets.asyncio.server import serve

import record
from fake_robot import FakeRobot
from src.leika import link
from src.sim.recording import RecordingWriter, _read_records, load

FIXTURE = Path(__file__).parent / "fixtures" / "telemetry.leika"


# A robot that browns out or walks out of range sends no close: the recorder must notice the silence, reconnect and
# mark the gap, instead of waiting on a socket that will never speak again.
def test_a_link_that_falls_silent_is_marked_and_reconnected(tmp_path, monkeypatch):
    batches = [frame for _, frame in _read_records(FIXTURE.read_bytes()) if frame][1:3]
    connections = []

    async def robot(socket):
        connections.append(socket)
        await socket.send(batches[min(len(connections), len(batches)) - 1])
        await socket.wait_closed()  # silent from here on, as a robot that lost power

    async def run(path):
        async with serve(robot, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            writer = RecordingWriter(path)
            try:
                return await record.record(f"127.0.0.1:{port}", writer, seconds=3.0)
            finally:
                writer.close()

    monkeypatch.setattr(record, "SILENCE_S", 0.5, raising=False)
    path = tmp_path / "silent.leika"
    frames = asyncio.run(run(path))
    rec = load(path)
    assert len(connections) >= 2
    assert rec.link_gaps >= 1
    assert frames >= 2


# A robot that is still booting, or an httpd out of sockets, refuses the upgrade: the recorder must try again rather
# than end the recording.
def test_a_refused_handshake_is_retried(tmp_path):
    batch = [frame for _, frame in _read_records(FIXTURE.read_bytes()) if frame][1]
    attempts = []

    def refuse_first(connection, request):
        attempts.append(request)
        if len(attempts) == 1:
            return connection.respond(503, "booting\n")
        return None

    async def robot(socket):
        await socket.send(batch)
        await socket.wait_closed()

    async def run(path):
        async with serve(robot, "127.0.0.1", 0, process_request=refuse_first) as server:
            port = server.sockets[0].getsockname()[1]
            writer = RecordingWriter(path)
            try:
                return await record.record(f"127.0.0.1:{port}", writer, seconds=2.5)
            finally:
                writer.close()

    frames = asyncio.run(run(tmp_path / "refused.leika"))
    assert len(attempts) >= 2
    assert frames >= 1


# Over the native USB port the robot streams beside its log; leaving ends the subscriptions, as a closed port does not.
def test_records_over_the_usb_port_and_unsubscribes_at_the_end(tmp_path, monkeypatch):
    robot = FakeRobot(transport="serial")
    monkeypatch.setattr(link, "Serial", robot.serial_class)
    path = tmp_path / "usb.leika"
    writer = RecordingWriter(path)
    try:
        frames = asyncio.run(record.record(robot.target, writer, seconds=0.6))
    finally:
        writer.close()
        robot.stop()
    rec = load(path)
    assert rec.header.variant == robot.variant and len(rec.ticks["t_us"]) >= 3
    # The header, one tick per batch from the fake robot, the network samples, and the goal's status the subscribe to it
    # is answered with.
    assert len(rec.goals["host_rx_us"]) == 1
    assert 1 + len(rec.ticks["t_us"]) + len(rec.network["t_us"]) + 1 == frames
    assert sorted(message.unsub_notif.tag for _, message in robot.received if message.HasField("unsub_notif")) == sorted(
        record._tag(name) for name in record.TAGS
    )


# Nothing from the robot is no recording: the recorder says so and leaves no empty file, where it said it wrote one
# (Hexapod b796344).
def test_a_robot_that_never_answers_writes_nothing(tmp_path, monkeypatch):
    with socket.socket() as probe:  # a port nothing listens on
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    out = tmp_path / "nothing.leika"
    argv = ["record.py", "--robot", f"127.0.0.1:{port}", "--seconds", "0.5", "--out", str(out)]
    monkeypatch.setattr(sys, "argv", argv)
    with pytest.raises(SystemExit, match="nothing written"):
        record.main()
    assert not out.exists()
