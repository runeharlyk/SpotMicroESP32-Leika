"""Exports a golden trace of firmware_gait.py for the web app's TypeScript port.

The app's app/src/lib/simulation/pico-gait.ts must reproduce this trace, so the browser
simulation and this Python simulation run the same controller.
Run from simulation/: uv run python export_gait_trace.py
"""
import json
import os

import numpy as np

from src.robot.firmware_gait import (
    DEFAULT_FEET,
    GAIT_COEF,
    STAND_Z,
    BodyState,
    GaitController,
    GaitState,
    Kinematics,
    SpotPicoKinConfig,
    analytic_gait_action,
    leg_ik,
)

CONTROL_DT = 0.01
# (name, [vx, vy, yaw], ticks): stand, then each command direction, then a mixed one.
SEGMENTS = [
    ("stand", [0.0, 0.0, 0.0], 50),
    ("forward", [0.06, 0.0, 0.0], 150),
    ("strafe", [0.0, 0.03, 0.0], 100),
    ("turn", [0.0, 0.0, 2.0], 100),
    ("mixed", [0.04, -0.02, 1.0], 100),
]
POSE = dict(omega=0.1, phi=-0.05, psi=0.08, xm=0.005, ym=-0.004, zm=0.01)
UNREACHABLE = ("fr", [-0.2, -0.1, -0.3])
OUTPUT = os.path.join(os.path.dirname(__file__), "..", "app", "tests", "fixtures", "pico-gait-trace.json")


def main() -> None:
    gait, controller, body, kinematics = GaitState(), GaitController(), BodyState(), Kinematics()
    ticks = []
    for name, cmd, count in SEGMENTS:
        for _ in range(count):
            analytic_gait_action(cmd, gait)
            controller.advance_phase(gait, CONTROL_DT)
            controller.generate_feet(gait, body)
            ticks.append({
                "segment": name,
                "cmd": cmd,
                "phase": controller.phase,
                "feet": body.feet.tolist(),
                "angles": kinematics.inverse_kinematics(body).tolist(),
            })

    posed = BodyState(**POSE)
    leg, target = UNREACHABLE
    trace = {
        "control_dt": CONTROL_DT,
        "gait_coef": GAIT_COEF,
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
