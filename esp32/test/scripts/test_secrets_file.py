"""The build's secrets check (esp32/scripts/secrets_file.py): run with
uv run --with pytest pytest esp32/test/scripts
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from secrets_file import SecretsError, ensure_secrets  # noqa: E402

EXAMPLE = '''#pragma once
#define SECRET_WIFI_SSID ""
#define SECRET_WIFI_PASSWORD ""
#define SECRET_AP_PASSWORD "spot-leika"
'''


@pytest.fixture
def paths(tmp_path):
    example = tmp_path / "secrets.example.h"
    example.write_text(EXAMPLE)
    return example, tmp_path / "secrets.h"


def test_creates_the_secrets_from_the_example_on_the_first_build(paths):
    example, secrets = paths
    notice = ensure_secrets(example, secrets)
    assert secrets.read_text() == EXAMPLE
    assert "secrets.h" in notice


def test_leaves_existing_secrets_alone(paths):
    example, secrets = paths
    mine = EXAMPLE.replace('SSID ""', 'SSID "HomeNet"')
    secrets.write_text(mine)
    assert ensure_secrets(example, secrets) is None
    assert secrets.read_text() == mine


def test_names_every_define_the_secrets_lack(paths):
    example, secrets = paths
    secrets.write_text('#define SECRET_WIFI_SSID "HomeNet"\n')
    with pytest.raises(SecretsError) as error:
        ensure_secrets(example, secrets)
    assert "SECRET_WIFI_PASSWORD" in str(error.value)
    assert "SECRET_AP_PASSWORD" in str(error.value)
    assert "SECRET_WIFI_SSID" not in str(error.value)


@pytest.mark.parametrize("password", ["short12", "x" * 65])
def test_rejects_an_access_point_password_wpa2_would_refuse(paths, password):
    example, secrets = paths
    secrets.write_text(EXAMPLE.replace('"spot-leika"', f'"{password}"'))
    with pytest.raises(SecretsError, match="8 to 64"):
        ensure_secrets(example, secrets)
