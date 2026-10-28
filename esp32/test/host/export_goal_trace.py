"""Builds goal_trace.cpp, runs it and writes the golden trace that pins the simulation's port of the goal controller
(simulation/src/robot/firmware_goal.py). test_host_programs.py fails while the written trace is stale.

Run from the repository root: python esp32/test/host/export_goal_trace.py
The host compiler is $CXX, or g++; it must support C++20 (on Windows, MSYS2's UCRT64 g++ works).
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
FIXTURE = os.path.join(REPO, "simulation", "tests", "fixtures", "firmware-goal-trace.json")
INCLUDES = [os.path.join(HERE, "stubs")]
FIRMWARE_INCLUDES = [os.path.join(REPO, "esp32", "include"), os.path.join(REPO, "esp32", "src")]


def trace(workdir: str) -> dict:
    """The trace as the firmware's controller prints it, built as the firmware builds it (-Ofast)."""
    binary = os.path.join(workdir, "goal_trace.exe")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=gnu++20", "-Ofast", *[f"-I{path}" for path in INCLUDES],
                    *[f"-idirafter{path}" for path in FIRMWARE_INCLUDES], os.path.join(HERE, "goal_trace.cpp"),
                    "-o", binary], check=True)
    return json.loads(subprocess.run([binary], check=True, capture_output=True, text=True).stdout)


def main() -> None:
    with tempfile.TemporaryDirectory() as workdir:
        result = trace(workdir)
    with open(FIXTURE, "w") as f:
        json.dump(result, f)
    ticks = sum(len(scenario["ticks"]) for scenario in result["scenarios"])
    print(f"{len(result['scenarios'])} goals, {ticks} ticks -> {os.path.relpath(FIXTURE, REPO)}")


if __name__ == "__main__":
    sys.exit(main())
