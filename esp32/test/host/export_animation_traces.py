"""Builds animation_trace.cpp, runs it once per kinematics variant and writes the golden traces that pin the web app's
TypeScript port of the animation player (app/src/lib/animation/player.ts).

Run after compile_protos.py and pack_animations.py, from anywhere: python esp32/test/host/export_animation_traces.py
The host compiler is $CXX, or g++; it must support C++20 (on Windows, MSYS2's UCRT64 g++ works).
"""
import json
import os
import subprocess
import sys
import tempfile

from export_firmware_traces import FIRMWARE_INCLUDES, FIXTURES, HERE, INCLUDES, REPO, VARIANTS

SOURCES = [os.path.join(HERE, "animation_trace.cpp"),
           *[os.path.join(REPO, "submodules", "nanopb", name) for name in ("pb_common.c", "pb_encode.c", "pb_decode.c")],
           os.path.join(REPO, "esp32", "src", "platform_shared", "animation.pb.c")]


def build(workdir: str) -> str:
    binary = os.path.join(workdir, "animation_trace.exe")
    compiler = os.environ.get("CXX", "g++")
    command = [compiler, "-std=gnu++20", "-O0", *[f"-I{path}" for path in INCLUDES],
               *[f"-idirafter{path}" for path in FIRMWARE_INCLUDES], *SOURCES, "-o", binary]
    subprocess.run(command, check=True)
    return binary


def main() -> None:
    os.makedirs(FIXTURES, exist_ok=True)
    with tempfile.TemporaryDirectory() as workdir:
        binary = build(workdir)
        for variant in VARIANTS:
            result = subprocess.run([binary, variant], check=True, capture_output=True, text=True)
            trace = json.loads(result.stdout)
            path = os.path.join(FIXTURES, f"animation-trace-{variant}.json")
            with open(path, "w") as f:
                json.dump(trace, f)
            ticks = sum(len(c["ticks"]) for c in trace["cases"])
            print(f"{variant}: {len(trace['cases'])} cases, {ticks} ticks -> {os.path.relpath(path, REPO)}")


if __name__ == "__main__":
    sys.exit(main())
