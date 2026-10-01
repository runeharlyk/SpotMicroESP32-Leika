import asyncio
from pathlib import Path

from websockets.asyncio.server import serve

import record
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
                return await record.record(f"ws://127.0.0.1:{port}", writer, seconds=3.0)
            finally:
                writer.close()

    monkeypatch.setattr(record, "SILENCE_S", 0.5, raising=False)
    path = tmp_path / "silent.leika"
    frames = asyncio.run(run(path))
    rec = load(path)
    assert len(connections) >= 2
    assert rec.link_gaps >= 1
    assert frames >= 2
