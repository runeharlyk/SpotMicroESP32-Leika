"""firmware_gait.py's gait engine against traces of the firmware's own walk_state.h.

esp32/test/host/export_firmware_traces.py replays a control script through the firmware headers once per variant and
writes app/tests/fixtures/firmware-trace-*.json. This replays the walking ticks of the same script through the port,
retargeted to the traced variant's stance feet and turn scale, and compares the feet. Joint angles are not compared:
the port's inverse kinematics is spot_pico's MJCF chain, which is none of the traced variants.

The port leaves out the firmware's command filter and its phase-clock rule, so the replay applies both itself and a
separate test, marked as a known divergence, shows the port without them.
"""
import json
import math
from pathlib import Path

import numpy as np
import pytest

from src.leika.speed_model import max_step_length
from src.robot import firmware_gait as fg

TRACES = Path(__file__).resolve().parents[2] / "app" / "tests" / "fixtures"
VARIANTS = ["SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE"]
WALK = 5
# The trace is float32 and the port float64; a full stride is tens of millimetres.
TOLERANCE = 5e-6
# Where a foot switches between stance and swing the curves meet only to within the swing's 1e-4 phase clamp, and the
# float32 and float64 phase clocks can round a tick to opposite sides of the switch.
SWITCH_MARGIN = 1e-5

# walk_state.h follows these with critical_damper.h, settling 95% of a step in 333 ms.
FILTER_OMEGA = 4.744 / 0.333
# walk_state.h runs the phase clock only while a filtered step exceeds this, and holds it at 0 otherwise.
MOVING = 0.001


def _to_port_frame(points):
    """The trace's body frame is x forward, y up, z right; the port's base frame is X left, Y rear, Z up."""
    points = np.asarray(points, dtype=float)[..., :3]
    return np.stack([-points[..., 2], -points[..., 0], points[..., 1]], axis=-1)


class _CriticalDamper:
    """critical_damper.h: the exact step of a critically damped spring towards a held target."""

    def __init__(self):
        self.velocity = 0.0

    def step(self, value, target, dt):
        offset = value - target
        drive = self.velocity + FILTER_OMEGA * offset
        decay = math.exp(-FILTER_OMEGA * dt)
        self.velocity = (self.velocity - FILTER_OMEGA * drive * dt) * decay
        return target + (offset + drive * dt) * decay


def _replay(variant, firmware_filter):
    """Yields (port feet, traced feet, legs in stance, stroke headings, legs near a contact switch) per walking tick.

    The stroke is walk_state.h's handleCommand mapping of the sticks. With firmware_filter the steps follow the
    firmware's filter and the phase clock its rule; without, the port is driven as its own callers drive it.
    """
    trace = json.loads((TRACES / f"firmware-trace-{variant}.json").read_text())
    kin, dt = trace["kin"], trace["dt"]
    step_length = max_step_length(variant)
    step_height = (kin["femur"] + kin["tibia"] - kin["coxa_offset"]) / 2

    controller, gait, body = fg.GaitController(), fg.GaitState(), fg.BodyState()
    controller.default_position = _to_port_frame(trace["ticks"][0]["body"]["feet"])
    steps = {"step_x": 0.0, "step_z": 0.0, "step_angle": 0.0}
    dampers, previous_mode = {}, None
    for tick in trace["ticks"]:
        fg.set_mode(gait, tick["gait"])
        mode = tick["mode"]
        entering, previous_mode = mode != previous_mode, mode
        if mode != WALK:
            continue
        if entering:
            # A state taking over resets the filter's velocities, not the steps it left behind.
            dampers = {name: _CriticalDamper() for name in steps}
        cmd = tick["cmd"]
        targets = {"step_x": cmd["ly"] * step_length, "step_z": -cmd["lx"] * step_length, "step_angle": cmd["rx"]}
        for name, target in targets.items():
            steps[name] = dampers[name].step(steps[name], target, dt) if firmware_filter else target
        # The port's step_z is to the left, the firmware's to the right.
        gait.step_x, gait.step_z, gait.step_angle = steps["step_x"], -steps["step_z"], steps["step_angle"]
        gait.step_velocity = cmd["s"]
        gait.step_height = cmd["s1"] * step_height
        gait.step_depth = fg.DEFAULT_STEP_DEPTH

        if not firmware_filter or any(abs(value) >= MOVING for value in steps.values()):
            controller.advance_phase(gait, dt)
        else:
            controller.set_phase(0.0)
        controller.generate_feet(gait, body)

        leg_phase = np.fmod(controller.phase + gait.offset, 1.0)
        stance = leg_phase <= gait.stand_frac
        near_switch = np.minimum(np.abs(leg_phase - gait.stand_frac), np.minimum(leg_phase, 1.0 - leg_phase))
        headings = np.array([fg._stroke(gait, foot)[1] for foot in controller.default_position])
        yield body.feet.copy(), _to_port_frame(tick["body"]["feet"]), stance, headings, near_switch < SWITCH_MARGIN


def _errors(variant, firmware_filter, compare):
    """The largest error per walking tick over the feet away from a contact switch, in the coordinates compare names:
    "horizontal", "vertical" where the two stance depths agree, or "stance vertical"."""
    errors = []
    for port, traced, stance, headings, near_switch in _replay(variant, firmware_filter):
        error = np.abs(port - traced)
        compared = ~near_switch
        if compare == "vertical":
            # The firmware's stance depth reads the stroke's forward plus right component; the port's stride frame is
            # mirrored, so the two agree only on strokes along an axis (see the stance depth test below).
            compared &= ~stance | (np.abs(np.sin(2 * headings)) < 1e-9)
        if compare == "stance vertical":
            compared &= stance
        axes = [0, 1] if compare == "horizontal" else [2]
        if compared.any():
            errors.append(error[compared][:, axes].max())
    return np.array(errors)


@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_strides_like_the_firmware(variant, monkeypatch):
    # The firmware turns each foot by step_angle times its radius; the port scales that for spot_pico.
    monkeypatch.setattr(fg, "TURN_RATE", 1.0)
    horizontal = _errors(variant, firmware_filter=True, compare="horizontal")
    vertical = _errors(variant, firmware_filter=True, compare="vertical")
    assert len(horizontal) > 800 and len(vertical) > 400
    assert horizontal.max() < TOLERANCE
    assert vertical.max() < TOLERANCE


@pytest.mark.xfail(strict=True, reason=(
    "firmware_gait.py applies the command to its steps at once, while walk_state.h follows step_x, step_z and "
    "step_angle with a critically damped spring settling in 333 ms; and the port's phase clock runs on every tick, "
    "while walk_state.h runs it only while a filtered step exceeds 0.001 and resets it to 0 otherwise"))
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_without_the_firmware_command_filter(variant, monkeypatch):
    monkeypatch.setattr(fg, "TURN_RATE", 1.0)
    assert _errors(variant, firmware_filter=False, compare="horizontal").max() < TOLERANCE


@pytest.mark.xfail(strict=True, reason=(
    "walk_state.h's stanceCurve sinks a stance foot by depth * cos(pi * (forward + right) / (2 * length)), and the "
    "port's stride frame has its cross axis to the left, so it evaluates forward + left instead, which differs on any "
    "diagonal stroke, as every turning foot has; on a diagonal stroke the firmware's formula itself raises a stance "
    "foot above its stance height at the stroke ends or sinks it for the whole stance, depending on the quadrant, "
    "so the fix belongs in walk_state.h (the along-stroke distance) before the port copies it"))
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_sinks_stance_feet_like_the_firmware(variant, monkeypatch):
    monkeypatch.setattr(fg, "TURN_RATE", 1.0)
    assert _errors(variant, firmware_filter=True, compare="stance vertical").max() < TOLERANCE
