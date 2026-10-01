"""Records the robot's telemetry to a .leika file: uv run python record.py --robot 192.168.1.39 --seconds 60

Subscribes to the telemetry batches and network samples, appends every frame as it arrives, and reconnects after a
dropped link, marking the gap. Ctrl-C ends the recording; the file is complete up to the last frame.
"""
import argparse
import asyncio
import time
from datetime import datetime

import websockets

from src.proto import message_pb2
from src.sim.recording import RecordingWriter

TAGS = ("telemetry_batch", "telemetry_network")
KINDS = {"telemetry_header", *TAGS}


def _host_us() -> int:
    return time.monotonic_ns() // 1000


def _subscription(name: str) -> bytes:
    tag = message_pb2.Message.DESCRIPTOR.fields_by_name[name].number
    return message_pb2.Message(sub_notif=message_pb2.SubscribeNotification(tag=tag)).SerializeToString()


async def record(url: str, writer: RecordingWriter, seconds: float | None) -> int:
    deadline = None if seconds is None else time.monotonic() + seconds
    frames = 0
    first = True
    while deadline is None or time.monotonic() < deadline:
        try:
            async with websockets.connect(url, open_timeout=10, ping_interval=None, max_size=None) as socket:
                if not first:
                    writer.gap(_host_us())
                first = False
                for name in TAGS:
                    await socket.send(_subscription(name))
                while deadline is None or time.monotonic() < deadline:
                    remaining = None if deadline is None else max(deadline - time.monotonic(), 0.01)
                    frame = await asyncio.wait_for(socket.recv(), timeout=remaining)
                    if message_pb2.Message.FromString(frame).WhichOneof("message") in KINDS:
                        writer.write(frame, _host_us())
                        frames += 1
        except TimeoutError:
            if deadline is not None and time.monotonic() >= deadline:
                break
            print("no answer from the robot; retrying")
        except (OSError, websockets.ConnectionClosed) as error:
            print(f"link lost ({error}); reconnecting")
            await asyncio.sleep(1)
    return frames


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--robot", required=True, help="host name or address, e.g. 192.168.1.39")
    parser.add_argument("--seconds", type=float, help="stop after this long; without it, Ctrl-C stops")
    parser.add_argument("--out", help="output file (default: recording-<time>.leika)")
    args = parser.parse_args()
    out = args.out or f"recording-{datetime.now():%Y%m%d-%H%M%S}.leika"
    writer = RecordingWriter(out)
    try:
        frames = asyncio.run(record(f"ws://{args.robot}/api/ws", writer, args.seconds))
    except KeyboardInterrupt:
        frames = None
    finally:
        writer.close()
    print(f"wrote {out}" + ("" if frames is None else f" ({frames} frames)"))


if __name__ == "__main__":
    main()
