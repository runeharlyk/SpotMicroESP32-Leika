"""The firmware walk gait's velocity model, against the firmware's own motion traces."""
import json
from dataclasses import astuple
from pathlib import Path

import numpy as np
import pytest

from src.leika.backends.base import UnknownVariant, Velocity
from src.leika.constants import Gait
from src.leika.speed_model import (
    GEOMETRY, SPEED, STEP_HEIGHT, Sticks, known_variant, max_velocity, slowdown, sticks_for, velocity_of,
)

TRACES = Path(__file__).resolve().parents[2] / "app" / "tests" / "fixtures"
VARIANTS = ["SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE"]
WALK, SETTLE_TICKS = 5, 60  # the 333 ms command filter has settled after 60 ticks
HEIGHT = 0.7


def _trace(variant):
    return json.loads((TRACES / f"firmware-trace-{variant}.json").read_text())


def _walk_runs(trace):
    """Runs of ticks walking with one constant, non-zero command."""
    runs, current, key = [], [], None
    for tick in trace["ticks"]:
        this = (tick["mode"], tick["gait"], tuple(sorted(tick["cmd"].items())))
        if this != key and current:
            runs.append((key, current))
            current = []
        key = this
        current.append(tick)
    runs.append((key, current))
    for (mode, gait, cmd), ticks in runs:
        cmd = dict(cmd)
        if mode == WALK and (cmd["lx"] or cmd["ly"] or cmd["rx"]):
            yield Gait(gait), cmd, ticks[SETTLE_TICKS:]


@pytest.mark.parametrize("variant", VARIANTS)
def test_the_geometry_is_the_firmwares(variant):
    kin = _trace(variant)["kin"]
    names = ("coxa", "coxa_offset", "femur", "tibia", "L", "W")
    assert GEOMETRY[variant] == pytest.approx(tuple(kin[name] for name in names), abs=1e-6)


def _body_velocity(trace, ticks) -> Velocity:
    """The body velocity the stance feet imply, averaged over the run's whole cycles.

    A stance foot is fixed on the ground, so in the body frame it moves as -(v + w x r), r its home: each tick's stance
    feet give v and w by least squares. The firmware slows its clock in the fast part of a step, so the velocity varies
    through a cycle and only whole cycles average to the model's. The trace's body frame is x forward, y up, z right;
    REP-103 is x, -z. A foot is in stance while it is not lifted, and a cycle runs from one foot's lift to its next.
    """
    rest = np.array(trace["ticks"][0]["body"]["feet"])
    feet = np.array([t["body"]["feet"] for t in ticks])
    lifts = max(([i for i in range(1, len(feet)) if feet[i - 1, leg, 1] <= 1e-6 < feet[i, leg, 1]] for leg in range(4)),
                key=len)
    assert len(lifts) >= 2, "the run walks no whole cycle"
    velocities = []
    for i in range(lifts[0] + 1, lifts[-1] + 1):
        stance = [leg for leg in range(4) if max(feet[i - 1, leg, 1], feet[i, leg, 1]) <= 1e-6]
        moved = (feet[i] - feet[i - 1]) / trace["dt"]
        rows = [row for leg in stance for row in ([-1, 0, -rest[leg, 2]], [0, -1, -rest[leg, 0]])]
        observed = [value for leg in stance for value in (moved[leg, 0], -moved[leg, 2])]
        velocities.append(np.linalg.lstsq(np.array(rows), np.array(observed), rcond=None)[0])
    return Velocity(*np.mean(velocities, axis=0))


# The trace walks its trots at full stick, which the firmware slows; the model must say by how much.
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_model_is_the_body_velocity_the_firmware_walks(variant):
    trace = _trace(variant)
    checked = 0
    for gait, cmd, ticks in _walk_runs(trace):
        assert (cmd["s"], cmd["s1"]) == (SPEED, STEP_HEIGHT)
        model = velocity_of(variant, gait, Sticks(cmd["lx"], cmd["ly"], cmd["rx"]), cmd["h"])
        measured = _body_velocity(trace, ticks)
        # A turn's error weighs as much as a stride's at a tenth of a metre from the centre.
        scale = max(abs(model.vx), abs(model.vy), abs(model.yaw_rate) * 0.1)
        assert (measured.vx, measured.vy, measured.yaw_rate * 0.1) == pytest.approx(
            (model.vx, model.vy, model.yaw_rate * 0.1), abs=0.03 * scale), (gait, cmd)
        checked += 1
    assert checked >= 4  # forward, sideways and turning trot, and a crawl


@pytest.mark.parametrize("gait", list(Gait))
def test_sticks_invert_the_model(gait):
    v = Velocity(0.008, -0.004, 0.04)  # inside what the reach bound leaves the Pico's crawl in this direction
    sticks = sticks_for("SPOTMICRO_ESP32_MINI", gait, v, HEIGHT)
    assert astuple(velocity_of("SPOTMICRO_ESP32_MINI", gait, sticks, HEIGHT)) == pytest.approx(astuple(v))


def test_a_full_trot_on_the_pico_is_slowed_and_its_top_speed_says_so():
    full = Sticks(0.0, 1.0, 0.0)
    share = slowdown("SPOTMICRO_ESP32_MINI", Gait.TROT, full, HEIGHT)
    top = max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, HEIGHT)
    assert 0 < share < 1
    assert top.vx == pytest.approx(velocity_of("SPOTMICRO_ESP32_MINI", Gait.TROT, full, HEIGHT).vx)


def test_sticks_for_a_velocity_beyond_the_gait_reach_the_rim_in_its_direction():
    top = max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT, HEIGHT)
    sticks = sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, Velocity(10 * top.vx, 0, -10 * top.yaw_rate), HEIGHT)
    assert max(abs(sticks.ly), abs(sticks.rx)) == pytest.approx(1.0)
    assert sticks.lx == 0 and sticks.ly > 0 and sticks.rx > 0


def test_a_reported_variant_loses_its_version_and_an_unknown_one_is_refused():
    assert known_variant("SPOTMICRO_ESP32_MINI_V2") == "SPOTMICRO_ESP32_MINI"
    with pytest.raises(UnknownVariant):
        known_variant("SPOTMICRO_GIANT")


# A freshly flashed robot reports no variant and refuses to move until one is chosen.
def test_a_robot_without_a_variant_is_refused_with_the_way_out():
    with pytest.raises(UnknownVariant, match="choose it in the app"):
        known_variant("")
