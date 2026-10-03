"""Builds firmware_trace.cpp, runs it once per kinematics variant and writes the golden traces that pin the
web app's TypeScript port of the firmware motion code (app/src/lib/simulation/firmware).

Run from esp32/test/host: python export_firmware_traces.py
The host compiler is $CXX, or g++; it must support C++20 (on Windows, MSYS2's UCRT64 g++ works).
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
FIXTURES = os.path.join(REPO, "app", "tests", "fixtures")
VARIANTS = ["SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE"]
INCLUDES = [os.path.join(HERE, "stubs"), os.path.join(REPO, "submodules", "nanopb")]
# After the system headers: the firmware's features.h would otherwise shadow the glibc <features.h> that <cmath> includes.
FIRMWARE_INCLUDES = [os.path.join(REPO, "esp32", "include"), os.path.join(REPO, "esp32", "src")]


def build(workdir: str) -> str:
    binary = os.path.join(workdir, "firmware_trace.exe")
    compiler = os.environ.get("CXX", "g++")
    # gnu++20 rather than c++20: the firmware uses M_PI, as the ESP32 toolchain allows.
    command = [compiler, "-std=gnu++20", "-O0", *[f"-I{path}" for path in INCLUDES],
               *[f"-idirafter{path}" for path in FIRMWARE_INCLUDES], os.path.join(HERE, "firmware_trace.cpp"), "-o", binary]
    subprocess.run(command, check=True)
    return binary


def main() -> None:
    os.makedirs(FIXTURES, exist_ok=True)
    with tempfile.TemporaryDirectory() as workdir:
        binary = build(workdir)
        for variant in VARIANTS:
            result = subprocess.run([binary, variant], check=True, capture_output=True, text=True)
            trace = json.loads(result.stdout)
            path = os.path.join(FIXTURES, f"firmware-trace-{variant}.json")
            with open(path, "w") as f:
                json.dump(trace, f)
            print(f"{variant}: {len(trace['ticks'])} ticks -> {os.path.relpath(path, REPO)}")


if __name__ == "__main__":
    sys.exit(main())
