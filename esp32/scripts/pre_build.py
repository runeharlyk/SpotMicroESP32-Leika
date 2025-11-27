from pathlib import Path

Import("env")

filesystem_dir = env["PROJECT_DIR"] + "/esp32/data"

Path(filesystem_dir).mkdir(exist_ok=True)