"""robot_animate.py against a fake robot on a local WebSocket and on a fake USB serial port."""
import io
import json
from pathlib import Path

import pytest
from google.protobuf import json_format

import robot_animate
from fake_robot import FakeRobot
from src.leika import link
from src.leika.backends import real
from src.proto import animation_pb2, message_pb2 as pb

WAVE = Path(__file__).resolve().parents[2] / "animations" / "wave.json"


@pytest.fixture
def make_fake(monkeypatch):
    made = []

    def make(transport):
        robot = FakeRobot(transport=transport)
        monkeypatch.setattr(link, "Serial", robot.serial_class)
        made.append(robot)
        return robot

    monkeypatch.setattr(real, "MODE_TIMEOUT_S", 1.0)
    monkeypatch.setattr(robot_animate, "QUIET_S", 0.5)
    yield make
    for robot in made:
        robot.stop()


@pytest.fixture(params=["websocket", "serial"])
def fake(request, make_fake):
    return make_fake(request.param)


# The robot takes file uploads over its WebSocket only.
@pytest.fixture
def websocket_fake(make_fake):
    return make_fake("websocket")


def animate(fake, *command):
    return robot_animate.main(["--robot", fake.target, *command])


def clip_file(tmp_path, **changes):
    """The built-in wave as a new clip, renamed, with `changes` to its fields."""
    clip = {**json.loads(WAVE.read_text(encoding="utf-8")), "name": "wave_two", **changes}
    path = tmp_path / f"{clip['name']}.json"
    path.write_text(json.dumps(clip), encoding="utf-8")
    return path


def requested(fake, kind):
    return [m.correlation_request for _, m in fake.received
            if m.WhichOneof("message") == "correlation_request" and m.correlation_request.WhichOneof("request") == kind]


def test_lists_the_built_in_and_the_uploaded_clips(fake, capsys):
    fake.files["/animations/hop.pb"] = b"\x08\x01"
    animate(fake, "list")
    lines = capsys.readouterr().out.splitlines()
    assert any(line.split() == ["wave", "built-in"] for line in lines)
    assert any(line.split() == ["hop", "2", "bytes"] for line in lines)


def test_uploads_the_encoded_clip_where_the_robot_looks_and_validates_it(websocket_fake, tmp_path, capsys):
    path = clip_file(tmp_path)
    animate(websocket_fake, "upload", str(path))
    expected = json_format.Parse(path.read_text(encoding="utf-8"), animation_pb2.Animation()).SerializeToString()
    assert websocket_fake.files["/animations/wave_two.pb"] == expected
    assert [r.animation_validate.name for r in requested(websocket_fake, "animation_validate")] == ["wave_two"]
    assert "wave_two: ok" in capsys.readouterr().out


def test_a_clip_the_robot_refuses_is_deleted_again(websocket_fake, tmp_path, capsys):
    with pytest.raises(SystemExit, match="refused wave_two"):
        animate(websocket_fake, "upload", str(clip_file(tmp_path, schema=2)))
    assert websocket_fake.files == {}
    assert "INVALID schema must be 1" in capsys.readouterr().out


def test_a_built_in_name_is_refused_before_anything_is_written(websocket_fake, tmp_path):
    with pytest.raises(SystemExit, match="built into the firmware"):
        animate(websocket_fake, "upload", str(clip_file(tmp_path, name="wave")))
    assert requested(websocket_fake, "fs_upload_start") == []


def test_an_upload_over_usb_is_refused_without_a_word_to_the_robot(make_fake, tmp_path):
    fake = make_fake("serial")
    with pytest.raises(SystemExit, match="over WiFi only"):
        animate(fake, "upload", str(clip_file(tmp_path)))
    assert requested(fake, "fs_upload_start") == []


def test_play_follows_the_clip_through_its_states_back_to_the_mode(fake, capsys):
    fake.mode = pb.STAND
    animate(fake, "play", "bow", "SPEED=1.5", "REPEAT=2")
    out = capsys.readouterr().out
    states = [line.split()[1] for line in out.splitlines() if line.startswith("bow")]
    assert states[0] == "ANIM_ENTRY" and "ANIM_PLAYING" in states and states[-2:] == ["ANIM_EXIT", "ANIM_IDLE"]
    assert out.splitlines()[-1] == "mode STAND"
    assert [(animation_pb2.ParamId.Name(p.id), p.value) for p in fake.played[-1].params] == [("SPEED", 1.5),
                                                                                             ("REPEAT", 2.0)]


def test_a_refused_play_says_why(fake):
    with pytest.raises(SystemExit, match="Stand up first"):
        animate(fake, "play", "bow")


def test_play_leaves_a_looping_clip_running_until_stop_ends_it(fake, capsys):
    fake.mode, fake.clip_s = pb.STAND, 0.2
    animate(fake, "play", "wave")
    assert "looping (or repeating) until stopped" in capsys.readouterr().out
    assert fake.mode == pb.ANIMATE
    animate(fake, "stop")
    out = capsys.readouterr().out
    assert "ANIM_IDLE" in out and out.splitlines()[-1] == "mode STAND"
    assert fake.mode == pb.STAND


# The shell keeps the link, so a clip that holds its end can be stopped from the next line.
def test_the_shell_stops_a_held_clip(fake, capsys, monkeypatch):
    fake.mode = pb.REST
    monkeypatch.setattr("sys.stdin", io.StringIO("play sit\nstop\nquit\n"))
    animate(fake, "shell")
    out = capsys.readouterr().out
    held = out.index("holding the last keyframe")
    assert "ANIM_EXIT" in out[held:] and "ANIM_IDLE" in out[held:]
    assert "mode REST" in out[held:]
    assert fake.mode == pb.REST
    assert len(fake.ports) <= 1 and fake.connections <= 1
