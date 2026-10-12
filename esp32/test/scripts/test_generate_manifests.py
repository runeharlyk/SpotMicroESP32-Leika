"""The release index the app's firmware page reads (flasher/generate_manifests.py, app/src/lib/firmware/release.ts):
run with uv run --with pytest pytest esp32/test/scripts
"""
import json
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[3] / "flasher" / "generate_manifests.py"


def _generate(site, tag):
    subprocess.run([sys.executable, str(SCRIPT), str(site), tag], check=True, capture_output=True)
    return json.loads((site / "firmware" / "ota.json").read_text())


# deploy.yml downloads every *.bin of the latest release into the site's firmware directory; release.yml publishes
# <env>.bin beside <env>.factory.bin. The app offers <env>.bin for <env> from images, and nothing without a tag.
def test_the_index_lists_each_app_image_under_its_env_and_strips_the_tag(tmp_path):
    firmware = tmp_path / "firmware"
    firmware.mkdir()
    for name in ("esp32-s3-n8r2.bin", "esp32-s3-n8r2.factory.bin", "esp32dev.factory.bin"):
        (firmware / name).write_bytes(b"\xe9")
    index = _generate(tmp_path, "v0.3.1")
    assert index == {"tag": "v0.3.1", "version": "0.3.1", "images": {"esp32-s3-n8r2": "esp32-s3-n8r2.bin"}}
    for file in index["images"].values():
        assert (firmware / file).is_file()


# A release from before the app images (v0.3.0 published only factory images) lists none, which the app reads as no
# update rather than as a broken site.
def test_a_release_of_factory_images_only_offers_no_update(tmp_path):
    (tmp_path / "firmware").mkdir()
    (tmp_path / "firmware" / "esp32-s3-n8r2.factory.bin").write_bytes(b"\xe9")
    assert _generate(tmp_path, "v0.3.0")["images"] == {}


def test_no_release_gives_no_tag(tmp_path):
    assert _generate(tmp_path, "none") == {"tag": None, "version": None, "images": {}}
