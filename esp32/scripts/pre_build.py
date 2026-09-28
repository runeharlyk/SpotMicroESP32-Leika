from pathlib import Path
import subprocess
import sys

Import("env")

project_dir = Path(env["PROJECT_DIR"])
filesystem_dir = project_dir / "esp32" / "data"

Path(filesystem_dir).mkdir(exist_ok=True)

proto_script = project_dir / "esp32" / "scripts" / "compile_protos.py"
print("Running proto compilation...")
result = subprocess.run([sys.executable, str(proto_script)], cwd=str(project_dir))
if result.returncode != 0:
    print("Error: Proto compilation failed. Is the nanopb submodule initialised? "
          "Run 'git submodule update --init --recursive'.", file=sys.stderr)
    env.Exit(1)
