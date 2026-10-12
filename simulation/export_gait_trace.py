"""Exports a golden trace of firmware_gait.py for the web app's TypeScript port.

The app's app/src/lib/simulation/pico-gait.ts must reproduce this trace, so the browser
simulation and this Python simulation run the same controller.
Run from simulation/: uv run python export_gait_trace.py
"""
import json
import os

import numpy as np

from src.leika.speed_model import Sticks
from src.robot.firmware_gait import (
    DEFAULT_FEET,
    STAND_Z,
    BodyState,
    GaitController,
    GaitState,
    Kinematics,
    SpotPicoKinConfig,
    gait_action,
    leg_ik,
)

CONTROL_DT = 0.01
# (name, sticks [lx, ly, rx], ticks): stand, then each stick (a full turn, which the joint speed cap
# slows), then a mixed one past the legs' reach, then a stop the smoothing eases out of, landing the
# raised feet before the phase holds.
SEGMENTS = [
    ("stand", [0.0, 0.0, 0.0], 50),
    ("forward", [0.0, 0.5, 0.0], 150),
    ("strafe", [-0.25, 0.0, 0.0], 100),
    ("turn", [0.0, 0.0, 1.0], 100),
    ("mixed", [0.6, 1.0, -1.0], 100),
    ("stop", [0.0, 0.0, 0.0], 100),
]
POSE = dict(omega=0.1, phi=-0.05, psi=0.08, xm=0.005, ym=-0.004, zm=0.01)
UNREACHABLE = ("fr", [-0.2, -0.1, -0.3])
OUTPUT = os.path.join(os.path.dirname(__file__), "..", "app", "tests", "fixtures", "pico-gait-trace.json")


def main() -> None:
    gait, controller, body, kinematics = GaitState(), GaitController(), BodyState(), Kinematics()
    ticks = []
    for name, sticks, count in SEGMENTS:
        for _ in range(count):
            gait_action(Sticks(*sticks), gait)
            controller.step(gait, CONTROL_DT)
            controller.generate_feet(gait, body)
            ticks.append({
                "segment": name,
                "sticks": sticks,
                "phase": controller.phase,
                "feet": body.feet.tolist(),
                "angles": kinematics.inverse_kinematics(body).tolist(),
            })

    posed = BodyState(**POSE)
    leg, target = UNREACHABLE
    trace = {
        "control_dt": CONTROL_DT,
        "default_feet": DEFAULT_FEET.tolist(),
        "stand_z": STAND_Z,
        "ticks": ticks,
        "pose": {"body": POSE, "angles": kinematics.inverse_kinematics(posed).tolist()},
        "unreachable": {
            "leg": leg,
            "target": target,
            "angles": leg_ik(SpotPicoKinConfig(), leg, np.array(target)).tolist(),
        },
    }
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w") as f:
        json.dump(trace, f)
    print(f"Wrote {len(ticks)} ticks to {os.path.normpath(OUTPUT)}")


if __name__ == "__main__":
    main()
