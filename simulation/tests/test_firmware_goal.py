"""The port of the goal controller against the firmware's own trace (esp32/test/host/export_goal_trace.py)."""
import json
import math
from pathlib import Path

import pytest

from src.robot.firmware_goal import BodyStep, Goal, GoalController, GoalState, body_step
from src.robot.firmware_odometry import OdometryEstimate

TRACE = Path(__file__).parent / "fixtures" / "firmware-goal-trace.json"
SCENARIOS = json.loads(TRACE.read_text())["scenarios"]


# Every tick's sticks, state and progress, from the same body steps. The firmware sums in float, the port in double.
@pytest.mark.parametrize("scenario", SCENARIOS, ids=lambda s: ",".join(f"{k}={v:g}" for k, v in s["goal"].items()))
def test_the_port_reproduces_the_firmwares_controller_tick_by_tick(scenario):
    goal = GoalController()
    goal.start(Goal(**scenario["goal"]), scenario["top_speed"], scenario["top_turn"])
    assert goal.timeout == pytest.approx(scenario["timeout"], rel=1e-6)
    for tick in scenario["ticks"]:
        sticks = goal.update(BodyStep(*tick["step"]), tick["dt"])
        assert (sticks.forward, sticks.left, sticks.turn) == pytest.approx(tick["sticks"], abs=1e-5)
        assert goal.state == tick["state"]
        assert goal.progress == pytest.approx(tick["progress"], abs=1e-5)
    assert goal.state is not GoalState.RUNNING


def test_the_trace_covers_every_axis_and_both_endings():
    endings = {s["ticks"][-1]["state"] for s in SCENARIOS}
    assert endings == {GoalState.REACHED, GoalState.TIMED_OUT}
    for axis in ("forward", "sideways", "turn"):
        assert any(s["goal"][axis] != 0 for s in SCENARIOS)


def test_only_one_finite_target_at_a_sane_speed_is_valid():
    assert Goal(forward=0.2).valid() and Goal(turn=-3, speed=1).valid()
    for goal in (Goal(), Goal(0.2, 0.1), Goal(math.nan), Goal(0.2, speed=0), Goal(0.2, speed=1.01)):
        assert not goal.valid()


def test_a_body_step_is_taken_in_the_frame_it_started_in():
    c, s = math.cos(3.0), math.sin(3.0)
    before = OdometryEstimate(1.0, 2.0, 3.0)
    after = OdometryEstimate(1 + c * 0.02 - s * 0.01, 2 + s * 0.02 + c * 0.01, 3.5 - 2 * math.pi)
    step = body_step(before, after)
    assert (step.forward, step.left, step.turn) == pytest.approx((0.02, 0.01, 0.5))
