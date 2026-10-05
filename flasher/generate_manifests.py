"""Write ESP Web Tools manifests for every board whose factory image is present.

Usage: python generate_manifests.py <site_dir> <tag>
Expects <site_dir>/firmware/<env>.factory.bin and writes <site_dir>/manifests/<env>.json
plus <site_dir>/available.json, which the flasher page reads to list the boards.
Also writes <site_dir>/firmware/ota.json, which lists the <env>.bin app images the app
offers for an update over the air. A tag of "none" means there is no release.
"""

import json
import sys
from pathlib import Path

site_dir = Path(sys.argv[1])
version = sys.argv[2]
boards = json.loads((Path(__file__).parent / "boards.json").read_text())

firmware_dir = site_dir / "firmware"
firmware_dir.mkdir(parents=True, exist_ok=True)
manifest_dir = site_dir / "manifests"
manifest_dir.mkdir(parents=True, exist_ok=True)

available = []
for board in boards:
    image = firmware_dir / f"{board['env']}.factory.bin"
    if not image.is_file():
        print(f"No firmware for {board['env']}, skipping")
        continue
    manifest = {
        "name": f"Spot Micro Leika - {board['name']}",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": board["chipFamily"],
            "parts": [{"path": f"../firmware/{image.name}", "offset": 0}],
        }],
    }
    (manifest_dir / f"{board['env']}.json").write_text(json.dumps(manifest, indent=2))
    available.append({**board, "manifest": f"manifests/{board['env']}.json"})

(site_dir / "available.json").write_text(json.dumps({"version": version, "boards": available}, indent=2))
print(f"Wrote {len(available)} manifests for version {version}")

tag = None if version == "none" else version
ota = {
    "tag": tag,
    "version": tag.removeprefix("v") if tag else None,
    "images": {
        board["env"]: f"{board['env']}.bin"
        for board in boards
        if (firmware_dir / f"{board['env']}.bin").is_file()
    },
}
(firmware_dir / "ota.json").write_text(json.dumps(ota, indent=2))
print(f"Wrote OTA index with {len(ota['images'])} app images")
