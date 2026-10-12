"""What the firmware's motion asks of the MG92B servos, read from the traces of the firmware's own code.

esp32/test/host/export_firmware_traces.py and export_animation_traces.py run the firmware's walk, stand, rest and
animation player on the host for every variant and write the joint angles they command (app/tests/fixtures). This
measures those angles against the envelope the hardware has, every constant read from the firmware's headers:

- speed: the joint rate against the walk's cap (walk_state.h's MAX_JOINT_SPEED_DEG_S, three quarters of the servo
  limit, since the MG92B slows under load and on a lower supply) and against the servo controller's slew limit
  (servo_output.h's SERVO_MAX_SPEED_DEG_S), which nothing but the walk keeps under on its own;
- lag: how far the servo controller's output trails the command when it clips a faster one, the readout of "the
  servo could not keep up" an open-loop robot inherits as a pose error, at any fraction of the servo's speed;
- travel: the joint angle against the variant's travel in joint_model.h.

Torque is not in a trace; check_animation.py plays the animations through MuJoCo's MG92B model for that.
"""
import json
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[3]
FIXTURES = REPO / "app" / "tests" / "fixtures"
FIRMWARE = REPO / "esp32" / "include"
VARIANTS = ("SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE")
MODES = {3: "REST", 4: "STAND", 5: "WALK"}
GAITS = {0: "TROT", 1: "CRAWL", 2: "AUTO"}
# A rate within this share of a limit counts as at it, as the Hexapod's tool counts 95% of its envelope.
NEAR_LIMIT = 0.95


def _servo_max_speed() -> float:
    text = (FIRMWARE / "peripherals" / "servo_output.h").read_text()
    return float(re.search(r"SERVO_MAX_SPEED_DEG_S\s*=\s*([\d.]+)", text).group(1))


def _walk_cap_share() -> float:
    text = (FIRMWARE / "motion_states" / "walk_state.h").read_text()
    return float(re.search(r"MAX_JOINT_SPEED_DEG_S\s*=\s*([\d.]+)f?\s*\*\s*SERVO_MAX_SPEED_DEG_S", text).group(1))


def travel_limits() -> dict[str, np.ndarray]:
    """Per variant, the travel of each of the 12 joints (deg, IK convention, leg * 3 + coxa, femur, knee): [min, max]."""
    text = (FIRMWARE / "joint_model.h").read_text()
    limits = {}
    for variant in VARIANTS:
        block = re.search(rf"JOINT_MODEL_{variant}\s*=\s*\{{(.*?)\}};", text, re.S)
        if not block:
            raise ValueError(f"JOINT_MODEL_{variant} not found in joint_model.h")
        travel = re.findall(r"\{\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\}", block.group(1))[-3:]
        limits[variant] = np.tile(np.array(travel, dtype=float), (4, 1))
    return limits


SERVO_MAX_SPEED_DEG_S = _servo_max_speed()
WALK_CAP_DEG_S = _walk_cap_share() * SERVO_MAX_SPEED_DEG_S
TRAVEL = travel_limits()


@dataclass
class Segment:
    """A stretch of commanded joint angles (deg, IK convention), one row per tick, `start` the angles before it."""
    variant: str
    label: str
    angles: np.ndarray
    start: np.ndarray
    dt: float
    walking: bool


def firmware_segments(variant: str) -> list[Segment]:
    """The firmware trace's runs of one mode, gait and command, each starting from the angles the last one left."""
    trace = json.loads((FIXTURES / f"firmware-trace-{variant}.json").read_text())
    segments, rows, key = [], [], None
    previous = start = np.array(trace["ticks"][0]["ik"], dtype=float)
    for tick in trace["ticks"]:
        cmd = tick["cmd"]
        this = (tick["mode"], tick["gait"], tuple(round(cmd[k], 3) for k in ("lx", "ly", "rx", "ry", "h")))
        if this != key and rows:
            segments.append(_firmware_segment(variant, key, rows, start, trace["dt"]))
            start, rows = previous, []
        key = this
        rows.append(tick["ik"])
        previous = np.array(tick["ik"], dtype=float)
    segments.append(_firmware_segment(variant, key, rows, start, trace["dt"]))
    return segments


def _firmware_segment(variant, key, rows, start, dt) -> Segment:
    mode, gait, (lx, ly, rx, ry, h) = key
    sticks = " ".join(f"{name}={value:g}" for name, value in zip(("lx", "ly", "rx", "ry"), (lx, ly, rx, ry)) if value)
    label = MODES.get(mode, str(mode)) + (f" {GAITS.get(gait, gait)}" if mode == 5 else "") + f" h={h:g}"
    return Segment(variant, f"{label} {sticks}".rstrip(), np.array(rows, dtype=float), start, dt, mode == 5)


def animation_segments(variant: str) -> list[Segment]:
    """Each case of the animation trace, from the pose it starts in."""
    trace = json.loads((FIXTURES / f"animation-trace-{variant}.json").read_text())
    segments = []
    for case in trace["cases"]:
        angles = np.array([tick["angles"] for tick in case["ticks"]], dtype=float)
        segments.append(Segment(variant, f"clip: {case['label']}", angles, angles[0], trace["dt"], False))
    return segments


def rates(segment: Segment) -> np.ndarray:
    """Each joint's commanded rate on each tick (deg/s)."""
    path = np.vstack([segment.start, segment.angles])
    return np.abs(np.diff(path, axis=0)) / segment.dt


def servo_lag(segment: Segment, speed_scale: float = 1.0) -> np.ndarray:
    """How far the servo controller's output trails each joint's command on each tick (deg), with the servos at
    `speed_scale` of SERVO_MAX_SPEED_DEG_S: servo_controller.h moves an angle at most that far per tick."""
    max_step = speed_scale * SERVO_MAX_SPEED_DEG_S * segment.dt
    servo = segment.start.copy()
    lag = np.empty_like(segment.angles)
    for tick, target in enumerate(segment.angles):
        servo = servo + np.clip(target - servo, -max_step, max_step)
        lag[tick] = np.abs(target - servo)
    return lag


def travel_excess(segment: Segment) -> np.ndarray:
    """How far each joint's command lies outside its travel on each tick (deg); 0 inside it."""
    low, high = TRAVEL[segment.variant].T
    return np.maximum(low - segment.angles, 0) + np.maximum(segment.angles - high, 0)


def travel_margin(segment: Segment) -> float:
    """The least distance any command keeps inside its travel (deg); negative when one leaves it."""
    low, high = TRAVEL[segment.variant].T
    return float(np.min(np.minimum(segment.angles - low, high - segment.angles)))


def speed_margin(segment: Segment) -> float:
    """The share of the servo's speed the segment's fastest commanded joint rate takes the servos to: below 1, a servo
    slowed to that share of SERVO_MAX_SPEED_DEG_S still follows it exactly; above 1, even nominal servos clip it."""
    return float(rates(segment).max() / SERVO_MAX_SPEED_DEG_S)


def all_segments() -> list[Segment]:
    return [s for variant in VARIANTS for s in firmware_segments(variant) + animation_segments(variant)]
