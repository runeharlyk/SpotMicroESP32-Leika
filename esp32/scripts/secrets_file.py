"""Keeps esp32/include/secrets.h in step with the tracked secrets.example.h.

secrets.h holds credentials and is never committed: the first build copies it from the example,
and every build checks that it defines everything the example does.
"""
import re
import shutil
from pathlib import Path

DEFINE = re.compile(r'^\s*#define\s+(\w+)\s+"((?:[^"\\]|\\.)*)"', re.M)
# WPA2 accepts passphrases of 8 to 63 characters, or a 64-digit hex key.
AP_PASSWORD_LENGTHS = range(8, 65)


class SecretsError(Exception):
    pass


def _defines(path: Path) -> dict[str, str]:
    return dict(DEFINE.findall(path.read_text(encoding="utf8")))


def ensure_secrets(example: Path, secrets: Path) -> str | None:
    """Creates secrets.h on the first build and returns a notice; raises SecretsError when it is unusable."""
    notice = None
    if not secrets.exists():
        shutil.copyfile(example, secrets)
        notice = (f"Created {secrets} from {example.name}; put your WiFi network in it and build again "
                  "to have the robot join it. It is ignored by git.")

    defined = _defines(secrets)
    missing = [name for name in _defines(example) if name not in defined]
    if missing:
        raise SecretsError(f"{secrets} lacks {', '.join(missing)}; copy them from {example.name}.")

    if len(defined["SECRET_AP_PASSWORD"]) not in AP_PASSWORD_LENGTHS:
        raise SecretsError(f"SECRET_AP_PASSWORD in {secrets} must be 8 to 64 characters, or the access point "
                           "will not start.")
    return notice
