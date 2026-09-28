"""Write ESP Web Tools manifests for every board whose factory image is present.

Usage: python generate_manifests.py <site_dir> <version>
Expects <site_dir>/firmware/<env>.factory.bin and writes <site_dir>/manifests/<env>.json
plus <site_dir>/available.json, which the flasher page reads to list the boards.
"""

import json
import sys
from pathlib import Path

site_dir = Path(sys.argv[1])
version = sys.argv[2]
boards = json.loads((Path(__file__).parent / "boards.json").read_text())

manifest_dir = site_dir / "manifests"
manifest_dir.mkdir(parents=True, exist_ok=True)

available = []
for board in boards:
    image = site_dir / "firmware" / f"{board['env']}.factory.bin"
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
