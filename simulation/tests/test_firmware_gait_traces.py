"""firmware_gait.py's gait engine against traces of the firmware's own walk_state.h.

esp32/test/host/export_firmware_traces.py replays a control script through the firmware headers once per variant and
writes app/tests/fixtures/firmware-trace-*.json. This replays the walking ticks of the same script through the port,
retargeted to the traced variant's stance feet, and compares the feet. Joint angles are not compared:
the port's inverse kinematics is spot_pico's MJCF chain, which is none of the traced variants.

The port runs the firmware's command filter, its phase-clock rules (the slowdown for the servos' speed and the stop)
and its reach bound through the MINI's legs at one body height rather than each traced variant's own, so the replay
takes the filtered steps, the phase and the bound from the trace, and a separate test, marked as a known divergence,
shows the port driven as its callers drive it.
"""
import json
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


def _to_port_frame(feet):
    """The trace's body frame is x forward, y up, z left, its legs fl, fr, rl, rr; the port's base frame is X left,
    Y rear, Z up, its legs fr, fl, rr, rl."""
    feet = np.asarray(feet, dtype=float)[[1, 0, 3, 2], :3]
    return np.stack([feet[:, 2], -feet[:, 0], feet[:, 1]], axis=-1)


def _replay(variant, firmware_filter):
    """Yields (port feet, traced feet, legs in stance, stroke headings, legs near a contact switch) per walking tick.

    With firmware_filter the steps (after the command smoothing), the phase and the reach bound are the firmware's own
    from the trace: its clock and its smoothing slow where a joint would outrun the servos and its bound shrinks a
    stroke past the legs' reach, both through each variant's own kinematics, which this port has for the MINI only;
    what is pinned is the feet's paths. Without, the port follows walk_state.h's handleCommand mapping of the sticks
    and is driven as its own callers drive it.
    """
    trace = json.loads((TRACES / f"firmware-trace-{variant}.json").read_text())
    kin = trace["kin"]
    step_length = max_step_length(variant)
    step_height = (kin["femur"] + kin["tibia"] - kin["coxa_offset"]) / 2

    controller, gait, body = fg.GaitController(), fg.GaitState(), fg.BodyState()
    controller.default_position = _to_port_frame(trace["ticks"][0]["body"]["feet"])
    controller.firmware_feet = controller.default_position[:, :2]
    port_bound = fg._reach_bound
    try:
        yield from _replay_ticks(trace, controller, gait, body, step_length, step_height, firmware_filter)
    finally:
        fg._reach_bound = port_bound


def _replay_ticks(trace, controller, gait, body, step_length, step_height, firmware_filter):
    for tick in trace["ticks"]:
        fg.set_mode(gait, tick["walked"])
        if tick["mode"] != WALK:
            continue
        cmd = tick["cmd"]
        if firmware_filter:
            walk = tick["walk"]
            step_x, step_z, step_angle = walk["step_x"], walk["step_z"], walk["step_angle"]
        else:
            step_x, step_z, step_angle = cmd["ly"] * step_length, -cmd["lx"] * step_length, cmd["rx"]
        gait.step_velocity = cmd["s"]
        gait.step_height = cmd["s1"] * step_height
        gait.step_depth = fg.STEP_DEPTH

        if firmware_filter:
            gait.step_x, gait.step_z, gait.step_angle = step_x, step_z, step_angle
            controller.set_phase(walk["phase"])
            fg._reach_bound = lambda _gait, bound=(walk["share"], walk["swing_height"]): bound
        else:
            gait.target = np.array([step_x, step_z, step_angle])
            controller.step(gait, trace["dt"])
        controller.generate_feet(gait, body)

        leg_phase = np.fmod(controller.phase + gait.offset, 1.0)
        stance = leg_phase <= gait.stand_frac
        near_switch = np.minimum(np.abs(leg_phase - gait.stand_frac), np.minimum(leg_phase, 1.0 - leg_phase))
        headings = np.array([fg._stroke(gait, foot)[1] for foot in controller.firmware_feet])
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
def test_the_port_strides_like_the_firmware(variant):
    horizontal = _errors(variant, firmware_filter=True, compare="horizontal")
    vertical = _errors(variant, firmware_filter=True, compare="vertical")
    assert len(horizontal) > 800 and len(vertical) > 400
    assert horizontal.max() < TOLERANCE
    assert vertical.max() < TOLERANCE


@pytest.mark.xfail(strict=True, reason=(
    "walk_state.h slows its command smoothing and its phase clock where a joint of the traced variant would outrun "
    "the servos, bounds the stroke to that variant's reach at the trace's body height and shifts the crawl's body, "
    "while the port measures both through the MINI's legs at the client's stand height and has no crawl shift"))
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_without_the_firmware_command_filter(variant):
    assert _errors(variant, firmware_filter=False, compare="horizontal").max() < TOLERANCE


@pytest.mark.parametrize("variant", VARIANTS)
def test_the_port_sinks_stance_feet_like_the_firmware(variant):
    assert _errors(variant, firmware_filter=True, compare="stance vertical").max() < TOLERANCE
