from pathlib import Path
import subprocess
import sys

Import("env")

project_dir = Path(env["PROJECT_DIR"])
filesystem_dir = project_dir / "esp32" / "data"

Path(filesystem_dir).mkdir(exist_ok=True)

sys.path.insert(0, str(project_dir / "esp32" / "scripts"))
from secrets_file import SecretsError, ensure_secrets  # noqa: E402

include_dir = project_dir / "esp32" / "include"
try:
    notice = ensure_secrets(include_dir / "secrets.example.h", include_dir / "secrets.h")
except SecretsError as error:
    print(f"Error: {error}", file=sys.stderr)
    env.Exit(1)
if notice:
    print(notice)

proto_script = project_dir / "esp32" / "scripts" / "compile_protos.py"
print("Running proto compilation...")
result = subprocess.run([sys.executable, str(proto_script)], cwd=str(project_dir))
if result.returncode != 0:
    print("Error: Proto compilation failed. Is the nanopb submodule initialised? "
          "Run 'git submodule update --init --recursive'.", file=sys.stderr)
    env.Exit(1)
