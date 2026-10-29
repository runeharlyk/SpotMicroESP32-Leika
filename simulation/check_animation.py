"""Plays the firmware's animation traces on the Pico in MuJoCo and reports what the reachability sweep cannot:
clamped joints, commanded joint speed against the servos' speed limit, body tilt, how much of the time something
other than a foot touches the ground, how long fewer than three feet held the body up, how close the centre of mass
came to the edge of the feet it stood on, and whether it fell. A clip played to its end must keep the centre of mass
MIN_SUPPORT_MARGIN_MM inside its feet; a stopped one is exempt, as Exit carries the body back to the middle while a
raised foot is still coming down, whatever the clip does.

    uv run python check_animation.py           # the built-in clips in the MINI trace
    uv run python check_animation.py --all     # every case in it, the synthetic ones that pin the app too

The trace is esp32/test/host/export_animation_traces.py's (app/tests/fixtures): the firmware's own player, so the
angles here are the ones the robot would be sent. They go through MotionService's joint directions, the servo
controller's speed limit and the app's Pico joint map (app/src/lib/simulation/robots.ts), which puts leg 0 on the
model's fl joints. Exits 1 when any checked case fails, so it can gate CI.
"""
import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from src.sim.mj_runtime import CONTROL_DT, JOINT_NAMES, SpotPicoSim

REPO = Path(__file__).resolve().parents[1]
TRACE = REPO / "app" / "tests" / "fixtures" / "animation-trace-SPOTMICRO_ESP32_MINI.json"
LIBRARY = REPO / "animations"
FALL_TILT_DEG = 45.0
# A quadruped holding still needs three feet down; on two it is tipping over a diagonal, unless that lasts only
# as long as a foot takes to land.
TIP_SECONDS = 0.1
SETTLE_SECONDS = 1.0
# The wave kept 9 mm and tipped the Pico while its IMU, 6 degrees off level, tilted the body enough to carry the centre
# of mass about as far (8 mm at the 81 mm stand); with the IMU levelled it stayed up, but rocked.
MIN_SUPPORT_MARGIN_MM = 20.0


def cpp_constant(path: Path, name: str) -> np.ndarray:
    """A numeric constant or array initialiser from a firmware header."""
    match = re.search(rf"\b{name}\b(?:\[\d*\])?\s*=\s*\{{?([^;}}]*)", path.read_text())
    if not match:
        raise ValueError(f"{name} not found in {path}")
    return np.array([float(v) for v in match.group(1).split(",")])


def pico_joint_map(path: Path = REPO / "app" / "src" / "lib" / "simulation" / "robots.ts"):
    """The model's joint names in firmware order, and per joint the sign and offset (rad) of
    model = sign * servo angle + offset."""
    block = re.search(r"const PICO_JOINTS: JointMap = \{(.*?)\n\}", path.read_text(), re.S)
    if not block:
        raise ValueError(f"PICO_JOINTS not found in {path}")
    lists = [re.findall(r"'([^']+)'|(-?\d+(?:\.\d+)?)", item) for item in re.findall(r"\[([^\]]*)\]", block.group(1))]
    legs, joints, sign, offset = ([text or number for text, number in items] for items in lists)
    names = [f"{leg}{joint}" for leg in legs for joint in joints]
    return names, np.array(sign, float), np.radians(np.array(offset, float))


SERVO_MAX_SPEED_DEG_S = float(cpp_constant(REPO / "esp32" / "include" / "peripherals" / "servo_output.h",
                                           "SERVO_MAX_SPEED_DEG_S")[0])
JOINT_DIRECTION = cpp_constant(REPO / "esp32" / "include" / "joint_model.h", "JOINT_DIRECTION")
MODEL_JOINTS, MODEL_SIGN, MODEL_OFFSET = pico_joint_map()
# The simulation's joint order, as indices into the firmware's.
TO_SIM = np.array([MODEL_JOINTS.index(name) for name in JOINT_NAMES])


def model_angles(servo_deg: np.ndarray) -> np.ndarray:
    """The simulation's joint targets (rad, its order) for the firmware's servo angles (deg, MotionService's order)."""
    return (MODEL_SIGN * np.radians(servo_deg) + MODEL_OFFSET)[TO_SIM]


@dataclass
class Report:
    label: str
    clamped_mask: int = 0           # bit per firmware joint: the IK clamped it, or the model's range would
    peak_joint_speed: float = 0.0   # deg/s, commanded
    tilt_max_deg: float = 0.0
    knock_fraction: float = 0.0     # fraction of steps with a non-foot ground contact
    unsupported_seconds: float = 0.0  # the longest stretch with fewer than three feet on the ground
    # The centre of mass's least distance inside the feet on the ground, taken once three or more of them have stood
    # for TIP_SECONDS: a static reading, which a foot landing or a brisk move carries below zero without a tip.
    support_margin_mm: float = np.inf
    required_margin_mm: float = MIN_SUPPORT_MARGIN_MM
    fell: bool = False
    steps: int = 0

    def failures(self) -> list[str]:
        checks = [(self.clamped_mask != 0, "clamped"), (self.peak_joint_speed > SERVO_MAX_SPEED_DEG_S, "too fast"),
                  (self.knock_fraction > 0.0, "knocks"), (self.unsupported_seconds > TIP_SECONDS, "tips"),
                  (self.support_margin_mm < self.required_margin_mm, "narrow"), (self.fell, "fell")]
        return [name for failed, name in checks if failed]

    def ok(self) -> bool:
        return not self.failures()


def check(case: dict, sim: SpotPicoSim) -> Report:
    report = Report(case["label"], required_margin_mm=MIN_SUPPORT_MARGIN_MM if case.get("stopAt", -1) < 0 else -np.inf)
    ticks = case["ticks"]
    low, high = sim.model.actuator_ctrlrange[sim.act_ids].T
    servo = np.array(ticks[0]["angles"]) * JOINT_DIRECTION
    sim.reset_to_joints(model_angles(servo))
    sim.step_physics(round(SETTLE_SECONDS / sim.model.opt.timestep))
    max_step = SERVO_MAX_SPEED_DEG_S * CONTROL_DT
    previous = servo
    knock_steps = 0
    unsupported = 0
    stance, stood = (), 0
    targets = [np.array(t["angles"]) * JOINT_DIRECTION for t in ticks]
    targets += [targets[-1]] * round(SETTLE_SECONDS / CONTROL_DT)
    for i, target in enumerate(targets):
        if i < len(ticks):
            report.clamped_mask |= ticks[i]["mask"]
        report.peak_joint_speed = max(report.peak_joint_speed, float(np.max(np.abs(target - previous))) / CONTROL_DT)
        previous = target
        servo = servo + np.clip(target - servo, -max_step, max_step)
        q = model_angles(servo)
        outside = (q < low) | (q > high)
        report.clamped_mask |= sum(1 << int(TO_SIM[j]) for j in np.flatnonzero(outside))
        sim.set_joint_targets(q)
        sim.step_physics()
        report.steps += 1
        report.tilt_max_deg = max(report.tilt_max_deg, sim.tilt_deg())
        feet, knock = sim.ground_contacts()
        knock_steps += knock
        unsupported = unsupported + 1 if len(feet) < 3 else 0
        stance, stood = feet, stood + 1 if feet == stance else 1
        if len(feet) >= 3 and stood * CONTROL_DT >= TIP_SECONDS:
            report.support_margin_mm = min(report.support_margin_mm, 1000.0 * sim.support_margin(feet))
        report.unsupported_seconds = max(report.unsupported_seconds, unsupported * CONTROL_DT)
    report.knock_fraction = knock_steps / report.steps
    report.fell = report.tilt_max_deg > FALL_TILT_DEG
    return report


def builtin_cases(trace: dict) -> list[dict]:
    names = {path.stem for path in LIBRARY.glob("*.json")}
    return [case for case in trace["cases"] if case["clip"]["name"] in names]


def format_row(r: Report) -> str:
    verdict = ", ".join(r.failures()) or "ok"
    clamps = "none" if r.clamped_mask == 0 else f"{r.clamped_mask:012b}"
    return (f"{r.label:40s} {verdict:10s} tilt {r.tilt_max_deg:4.1f} deg  peak joint {r.peak_joint_speed:5.0f} deg/s "
            f"(limit {SERVO_MAX_SPEED_DEG_S:.0f})  knock {r.knock_fraction:4.2f}  under 3 feet "
            f"{r.unsupported_seconds:4.2f} s  margin {r.support_margin_mm:4.1f} mm  clamped {clamps}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", nargs="?", type=Path, default=TRACE, help="an animation trace of the MINI variant")
    ap.add_argument("--all", action="store_true", help="check every case, not only the built-in clips")
    args = ap.parse_args(argv)
    trace = json.loads(args.trace.read_text())
    if trace["variant"] != "SPOTMICRO_ESP32_MINI":
        ap.error(f"the Pico runs SPOTMICRO_ESP32_MINI, not {trace['variant']}")
    if abs(trace["dt"] - CONTROL_DT) > 1e-6:
        ap.error(f"the trace's tick is {trace['dt']} s, the control loop's {CONTROL_DT} s")
    cases = trace["cases"] if args.all else builtin_cases(trace)
    sim = SpotPicoSim()
    failed = 0
    for case in cases:
        r = check(case, sim)
        print(format_row(r))
        failed += 0 if r.ok() else 1
    print(f"{len(cases) - failed}/{len(cases)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
