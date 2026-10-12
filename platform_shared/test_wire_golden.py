import shutil
from pathlib import Path

import pytest

import wire_golden

PROTOS = Path(__file__).resolve().parent


@pytest.fixture
def protos(tmp_path):
    """A copy of the schemas and of the committed golden, to edit without touching the tree."""
    proto_dir = tmp_path / "protos"
    proto_dir.mkdir()
    for proto in PROTOS.glob("*.proto"):
        shutil.copy(proto, proto_dir)
    shutil.copy(wire_golden.GOLDEN, tmp_path / "golden.json")
    return proto_dir


def edit(proto_dir: Path, file: str, old: str, new: str) -> None:
    path = proto_dir / file
    text = path.read_text()
    assert text.count(old) == 1, f"{old!r} must occur once in {file}"
    path.write_text(text.replace(old, new))


def run(proto_dir: Path, *flags: str) -> int:
    return wire_golden.main([*flags, "--proto-dir", str(proto_dir), "--golden", str(proto_dir.parent / "golden.json")])


def test_the_committed_golden_matches_the_schemas(protos):
    assert run(protos, "--check") == 0


def test_a_renumbered_enum_value_fails_the_check_and_the_update(protos, capsys):
    edit(protos, "message.proto", "WALK = 5;", "WALK = 8;")

    assert run(protos, "--check") == 1
    assert run(protos) == 1
    assert "socket_message.ModesEnum.WALK was renumbered from 5 to 8" in capsys.readouterr().err


def test_a_renumbered_oneof_member_fails_the_check(protos, capsys):
    edit(protos, "message.proto", "ModeData mode = 130;", "ModeData mode = 900;")

    assert run(protos, "--check") == 1
    assert "socket_message.Message.mode was renumbered from 130 to 900" in capsys.readouterr().err


def test_a_removed_field_fails_unless_its_number_is_reserved(protos, capsys):
    edit(protos, "message.proto", "float tilt_deg = 3;", "")
    assert run(protos) == 1
    assert "IMUCalibrateData.tilt_deg = 3 was removed without reserving its number" in capsys.readouterr().err

    edit(protos, "message.proto", "bool levelled = 2;", "bool levelled = 2; reserved 3;")
    assert run(protos, "--check") == 1, "the golden still lists the retired field"
    assert run(protos) == 0
    assert run(protos, "--check") == 0


def test_an_added_field_passes_only_once_the_golden_lists_it(protos, capsys):
    edit(protos, "message.proto", "float tilt_deg = 3;", "float tilt_deg = 3; float yaw_deg = 4;")

    assert run(protos, "--check") == 1
    assert "socket_message.IMUCalibrateData.yaw_deg = 4" in capsys.readouterr().err
    assert run(protos) == 0
    assert run(protos, "--check") == 0
