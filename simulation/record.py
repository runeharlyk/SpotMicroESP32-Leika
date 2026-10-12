"""Records the robot's telemetry to a .leika file: uv run python record.py --robot 192.168.1.39 --seconds 60

Subscribes to the telemetry batches and network samples, appends every frame as it arrives, and reconnects after a
dropped link, marking the gap. Ctrl-C ends the recording; the file is complete up to the last frame. --robot COM10
records over the board's native USB port instead of WiFi.
Run `uv run python gen_protos.py` once first: it generates the protocol classes in src/proto/.
"""
import argparse
import asyncio
import time
from datetime import datetime

from src.leika.link import LinkClosed, open_link
from src.proto import message_pb2
from src.sim.recording import RecordingWriter

TAGS = ("telemetry_batch", "telemetry_network")
KINDS = {"telemetry_header", *TAGS}
OPEN_TIMEOUT_S = 10.0
# The robot sends a network sample every second while recording: this long without a frame means the link is gone,
# even when no close ever arrives (a brown-out, or walking out of WiFi range).
SILENCE_S = 3.0


def _host_us() -> int:
    return time.monotonic_ns() // 1000


def _tag(name: str) -> int:
    return message_pb2.Message.DESCRIPTOR.fields_by_name[name].number


def _subscription(name: str) -> bytes:
    return message_pb2.Message(sub_notif=message_pb2.SubscribeNotification(tag=_tag(name))).SerializeToString()


# A robot keeps streaming to a USB port that was closed until its buffer stays full for seconds, so the subscriptions
# are ended first.
async def _hang_up(link) -> None:
    try:
        for name in TAGS:
            await link.send(message_pb2.Message(unsub_notif=message_pb2.UnsubscribeNotification(tag=_tag(name))).SerializeToString())
    except LinkClosed:
        pass
    await link.close()


async def record(target: str, writer: RecordingWriter, seconds: float | None) -> int:
    deadline = None if seconds is None else time.monotonic() + seconds
    frames = 0
    first = True
    while deadline is None or time.monotonic() < deadline:
        try:
            link = await open_link(target, OPEN_TIMEOUT_S)
            try:
                if not first:
                    writer.gap(_host_us())
                first = False
                for name in TAGS:
                    await link.send(_subscription(name))
                while deadline is None or time.monotonic() < deadline:
                    remaining = None if deadline is None else max(deadline - time.monotonic(), 0.01)
                    wait = SILENCE_S if remaining is None else min(remaining, SILENCE_S)
                    try:
                        frame = await asyncio.wait_for(link.recv(), timeout=wait)
                    except TimeoutError:
                        if deadline is not None and time.monotonic() >= deadline:
                            return frames
                        raise ConnectionError(f"no frame for {SILENCE_S:g} s") from None
                    if message_pb2.Message.FromString(frame).WhichOneof("message") in KINDS:
                        writer.write(frame, _host_us())
                        frames += 1
            finally:
                await _hang_up(link)
        except TimeoutError:
            if deadline is not None and time.monotonic() >= deadline:
                break
            print("no answer from the robot; retrying")
        except (OSError, LinkClosed) as error:
            print(f"link lost ({error}); reconnecting")
            await asyncio.sleep(1)
    return frames


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--robot", required=True,
                        help="host name or address, e.g. 192.168.1.39, or the native USB port, e.g. COM10")
    parser.add_argument("--seconds", type=float, help="stop after this long; without it, Ctrl-C stops")
    parser.add_argument("--out", help="output file (default: recording-<time>.leika)")
    args = parser.parse_args()
    out = args.out or f"recording-{datetime.now():%Y%m%d-%H%M%S}.leika"
    writer = RecordingWriter(out)
    try:
        frames = asyncio.run(record(args.robot, writer, args.seconds))
    except KeyboardInterrupt:
        frames = None
    finally:
        writer.close()
    print(f"wrote {out}" + ("" if frames is None else f" ({frames} frames)"))


if __name__ == "__main__":
    main()
