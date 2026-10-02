"""The firmware walk gait's velocity model, against the firmware's own motion traces."""
import json
from pathlib import Path

import numpy as np
import pytest

from src.leika.backends.base import UnknownVariant, Velocity
from src.leika.constants import Gait
from src.leika.speed_model import (
    LEGS, Sticks, known_variant, max_step_length, max_velocity, sticks_for, velocity_of,
)

TRACES = Path(__file__).resolve().parents[2] / "app" / "tests" / "fixtures"
VARIANTS = ["SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE"]
WALK, SETTLE_TICKS = 5, 60  # the 333 ms command filter has settled after 60 ticks


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
def test_the_leg_lengths_are_the_firmwares(variant):
    kin = _trace(variant)["kin"]
    assert LEGS[variant] == pytest.approx((kin["femur"], kin["tibia"], kin["coxa_offset"]), abs=1e-6)


# A stance foot is fixed on the ground, so in the body frame it moves as -(v + w x r): the stance feet's motion in the
# trace is the body velocity the firmware commands. Stance is most of each cycle at constant speed, so the median of a
# foot's velocity is its stance velocity. The trace's body frame is x forward, y up, z right; REP-103 is x, -z.
@pytest.mark.parametrize("variant", VARIANTS)
def test_the_model_is_the_body_velocity_the_stance_feet_imply(variant):
    trace = _trace(variant)
    rest = np.array(trace["ticks"][0]["body"]["feet"])
    checked = 0
    for gait, cmd, ticks in _walk_runs(trace):
        model = velocity_of(variant, gait, Sticks(cmd["lx"], cmd["ly"], cmd["rx"]))
        feet = np.array([t["body"]["feet"] for t in ticks])
        velocity = np.diff(feet, axis=0) / trace["dt"]
        for leg in range(4):
            x, y = rest[leg, 0], -rest[leg, 2]
            expected = (-(model.vx - model.yaw_rate * y), -(model.vy + model.yaw_rate * x))
            measured = (np.median(velocity[:, leg, 0]), -np.median(velocity[:, leg, 2]))
            scale = max(abs(expected[0]), abs(expected[1]))
            assert measured == pytest.approx(expected, abs=0.02 * scale + 1e-4), (gait, cmd, leg)
        checked += 1
    assert checked >= 4  # forward, sideways and turning trot, and a crawl


def test_sticks_invert_the_model():
    v = Velocity(0.05, -0.02, 0.3)
    sticks = sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, v)
    assert velocity_of("SPOTMICRO_ESP32_MINI", Gait.TROT, sticks) == pytest.approx(v)


def test_sticks_clamp_and_report_what_the_gait_can_do():
    top = max_velocity("SPOTMICRO_ESP32_MINI", Gait.TROT)
    assert top.vx == pytest.approx(max_step_length("SPOTMICRO_ESP32_MINI") * 1.0 / 0.75)
    sticks = sticks_for("SPOTMICRO_ESP32_MINI", Gait.TROT, Velocity(10 * top.vx, 0, -10 * top.yaw_rate))
    assert (sticks.ly, sticks.rx) == (1.0, 1.0)


def test_a_reported_variant_loses_its_version_and_an_unknown_one_is_refused():
    assert known_variant("SPOTMICRO_ESP32_MINI_V2") == "SPOTMICRO_ESP32_MINI"
    with pytest.raises(UnknownVariant):
        known_variant("SPOTMICRO_GIANT")
