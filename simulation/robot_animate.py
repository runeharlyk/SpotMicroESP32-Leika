"""Drives the robot's animation clips from the command line, for bench tests without the app:

    uv run python robot_animate.py --robot COM10 list
    uv run python robot_animate.py --robot 192.168.1.39 upload my_clip.json   # WiFi only, then validated
    uv run python robot_animate.py --robot COM10 validate wave
    uv run python robot_animate.py --robot COM10 play bow SPEED=1.5 REPEAT=2  # prints status and mode until it settles
    uv run python robot_animate.py --robot COM10 stop                         # prints status and mode until it settles
    uv run python robot_animate.py --robot COM10 mode STAND                   # a clip needs any mode but DEACTIVATED
    uv run python robot_animate.py --robot COM10 watch                        # prints AnimationStatus until Ctrl-C
    uv run python robot_animate.py --robot COM10 shell                        # the commands above on one open link

--robot is the board's native USB port (COM10, /dev/ttyACM0) or its address on WiFi, as for Robot(). The robot takes
file uploads over its WebSocket only, so `upload` needs the address. An uploaded clip is the JSON clip of
docs/animation.md, encoded and written to /animations/<name>.pb; a built-in of the same name would win over it, so its
name is refused. A COM port is exclusive on Windows, so play and stop follow the run themselves instead of relying on
watch, and shell keeps the link open so a held or looping clip can be stopped and plays can be chained.
Run `uv run python gen_protos.py` once first: it generates the protocol classes in src/proto/.
"""
import argparse
import queue
import shlex
import sys
import threading
import time
from pathlib import Path

from google.protobuf import json_format

from src.leika import Mode, RobotError
from src.leika.backends.real import RealBackend
from src.leika.link import is_serial_port
from src.proto import animation_pb2, filesystem_pb2 as fs, message_pb2 as pb

# The app's chunk (app/src/lib/filesystem/chunkedTransfer.ts); a clip is a few KB at most.
CHUNK = 16 * 1024
UPLOAD_TIMEOUT_S = 10.0
# A running clip reports every 200 ms (main.cpp publishAnimationStatus): this long without a status, nothing runs.
QUIET_S = 2.0
POLL_S = 0.1


def clip_path(name: str) -> str:
    """Where the firmware's AnimationStore looks for an uploaded clip."""
    return f"/animations/{name}.pb"


def load_clip(path: Path) -> animation_pb2.Animation:
    clip = json_format.Parse(path.read_text(encoding="utf-8"), animation_pb2.Animation())
    if not clip.name:
        raise SystemExit(f"{path}: the clip has no name")
    return clip


def list_clips(robot: RealBackend) -> list[pb.AnimationEntry]:
    return list(robot.request(pb.CorrelationRequest(animation_list_request=pb.AnimationListRequest())).animation_list.animations)


def show_list(robot: RealBackend) -> None:
    for entry in list_clips(robot):
        print(f"{entry.name:32s} {'built-in' if entry.builtin else f'{entry.size} bytes'}")


def upload(robot: RealBackend, path: Path) -> None:
    clip = load_clip(path)
    if any(entry.builtin and entry.name == clip.name for entry in list_clips(robot)):
        raise SystemExit(f"{clip.name} is built into the firmware, which plays its own; rename the clip")
    data = clip.SerializeToString()
    chunks = [data[offset:offset + CHUNK] for offset in range(0, len(data), CHUNK)] or [b""]
    start = fs.FSUploadStart(path=clip_path(clip.name), file_size=len(data), total_chunks=len(chunks))
    started = robot.request(pb.CorrelationRequest(fs_upload_start=start)).fs_upload_start_response
    if not started.success:
        raise SystemExit(f"the robot refused the upload: {started.error}")
    completed = queue.Queue()
    robot.listen("fs_upload_complete", completed.put, subscribe=False)
    try:
        for index, chunk in enumerate(chunks):
            robot.send(pb.Message(fs_upload_data=fs.FSUploadData(transfer_id=started.transfer_id, chunk_index=index,
                                                                 data=chunk)))
        deadline = time.monotonic() + UPLOAD_TIMEOUT_S
        while True:
            try:
                done = completed.get(timeout=max(0.0, deadline - time.monotonic()))
            except queue.Empty:
                raise SystemExit("the robot never confirmed the upload") from None
            if done.transfer_id == started.transfer_id:
                break
    finally:
        robot.listen("fs_upload_complete", None)
    if not done.success:
        raise SystemExit(f"the upload failed: {done.error}")
    print(f"uploaded {len(data)} bytes to {clip_path(clip.name)}")
    if not validate(robot, clip.name):
        robot.request(pb.CorrelationRequest(fs_delete_request=fs.FSDeleteRequest(path=clip_path(clip.name))))
        raise SystemExit(f"the robot refused {clip.name}, so it was deleted again")


def validate(robot: RealBackend, name: str) -> bool:
    """Prints the robot's report on the clip; whether it loads."""
    # A clip that does not load comes back as 422 with the report, so the report decides, not the status.
    report = robot.ask(pb.CorrelationRequest(animation_validate=pb.AnimationValidate(name=name))).animation_report
    if not report.ok:
        print(f"{name}: INVALID {report.error}")
        return False
    params = " ".join(f"{animation_pb2.ParamId.Name(p.id)}={p.min:g}..{p.max:g}" for p in report.params)
    shape = "loops" if report.loop else "holds its end" if report.hold_end else "plays once"
    print(f"{name}: ok, {report.duration:.2f} s, {shape}, clamped {report.clamped_mask:012b}  {params}".rstrip())
    return True


def show_status(status: pb.AnimationStatus) -> None:
    print(f"{status.name:16s} {pb.AnimationState.Name(status.state):12s} t={status.t:6.2f}  "
          f"clamped {status.clamped_mask:012b}")


def follow(robot: RealBackend, start) -> None:
    """Sends a request and prints the clip's status and the robot's mode until the run settles: back to idle and out
    of ANIMATE, or QUIET_S without a status (a refused play raises; a stop with nothing playing sends nothing). A clip
    holding its last keyframe, or one that has looped (or repeated) for QUIET_S, never settles on its own, so following
    ends there with the reason; Ctrl-C ends it too."""
    statuses = queue.Queue()
    robot.listen("animation_status", statuses.put)
    # The robot answered the subscribe with the clip's status as it was, before the request: history, such as an idle
    # that would end the following at once.
    while not statuses.empty():
        statuses.get_nowait()
    mode = robot.state().mode
    try:
        start()
        heard = time.monotonic()
        idle = False
        state, since, last_t, wrapped = None, heard, 0.0, False
        while True:
            now_mode = robot.state().mode
            if now_mode is not mode:
                mode = now_mode
                print(f"mode {mode.name}")
            if idle and mode is not Mode.ANIMATE:
                return
            try:
                status = statuses.get(timeout=POLL_S)
            except queue.Empty:
                if time.monotonic() - heard >= QUIET_S:
                    return
                continue
            heard = time.monotonic()
            show_status(status)
            idle = status.state == pb.ANIM_IDLE
            if status.state != state:
                state, since, wrapped = status.state, heard, False
            elif status.state == pb.ANIM_PLAYING and status.t < last_t:
                wrapped = True
            last_t = status.t
            if state == pb.ANIM_HOLD:
                print("holding the last keyframe until stopped; not following further")
                return
            if wrapped and heard - since >= QUIET_S:
                print("looping (or repeating) until stopped; not following further")
                return
    except KeyboardInterrupt:
        print("stopped following")
    finally:
        robot.listen("animation_status", None)


def play(robot: RealBackend, name: str, params: list[str]) -> None:
    request = pb.AnimationPlay(name=name)
    for item in params:
        key, _, value = item.partition("=")
        request.params.add(id=animation_pb2.ParamId.Value(key), value=float(value))
    follow(robot, lambda: robot.request(pb.CorrelationRequest(animation_play=request)))


def stop(robot: RealBackend) -> None:
    follow(robot, lambda: robot.request(pb.CorrelationRequest(animation_stop=pb.AnimationStop())))


def watch(robot: RealBackend) -> None:
    robot.listen("animation_status", show_status)
    print("watching AnimationStatus, Ctrl-C to stop")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        robot.listen("animation_status", None)


# ANIMATE is entered by playing a clip, never picked.
PICKABLE = [mode.name for mode in Mode if mode is not Mode.ANIMATE]


def add_commands(sub) -> None:
    sub.add_parser("list")
    sub.add_parser("upload").add_argument("file", type=Path, help="a JSON clip")
    sub.add_parser("validate").add_argument("name")
    play_parser = sub.add_parser("play")
    play_parser.add_argument("name")
    play_parser.add_argument("params", nargs="*", help="PARAM=value, e.g. SPEED=1.5")
    sub.add_parser("stop")
    sub.add_parser("mode").add_argument("name", choices=PICKABLE)
    sub.add_parser("watch")


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--robot", required=True,
                    help="the native USB port, e.g. COM10, or the host name or address, e.g. 192.168.1.39")
    sub = ap.add_subparsers(dest="command", required=True)
    add_commands(sub)
    sub.add_parser("shell")
    return ap


def run(robot: RealBackend, target: str, args) -> None:
    if args.command == "list":
        show_list(robot)
    elif args.command == "upload":
        if is_serial_port(target):
            raise SystemExit("the robot takes file uploads over WiFi only: give its address as --robot")
        upload(robot, args.file)
    elif args.command == "validate":
        validate(robot, args.name)
    elif args.command == "play":
        play(robot, args.name, args.params)
    elif args.command == "stop":
        stop(robot)
    elif args.command == "mode":
        robot.set_mode(Mode[args.name])
    else:
        watch(robot)


def shell(robot: RealBackend, target: str) -> None:
    """Reads the commands, without --robot, from stdin until quit or the end of input."""
    ap = argparse.ArgumentParser(prog="", add_help=False, exit_on_error=False)
    add_commands(ap.add_subparsers(dest="command", required=True))
    print("commands: list, upload FILE, validate NAME, play NAME [PARAM=VALUE ...], stop, mode NAME, watch, quit")
    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if line == "quit":
            return
        if not line:
            continue
        try:
            run(robot, target, ap.parse_args(shlex.split(line)))
        except KeyboardInterrupt:
            print("interrupted")
        except (argparse.ArgumentError, SystemExit, KeyError, ValueError, OSError, RobotError) as error:
            # SystemExit is argparse rejecting the line or a refused upload; the link stays open.
            print(f"error: {error}")


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    robot = RealBackend(args.robot)
    try:
        robot.connect()
        if args.command == "shell":
            shell(robot, args.robot)
        else:
            run(robot, args.robot, args)
    except RobotError as error:
        raise SystemExit(f"error: {error}") from error
    finally:
        robot.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
