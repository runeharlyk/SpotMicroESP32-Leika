from pathlib import Path
import subprocess
import sys

Import("env")

project_dir = Path(env["PROJECT_DIR"])
filesystem_dir = project_dir / "esp32" / "data"

Path(filesystem_dir).mkdir(exist_ok=True)

proto_script = project_dir / "esp32" / "scripts" / "compile_protos.py"
if proto_script.exists():
    print("Running proto compilation...")
    result = subprocess.run([sys.executable, str(proto_script)], cwd=str(project_dir))
    if result.returncode != 0:
        print("Warning: Proto compilation failed")
