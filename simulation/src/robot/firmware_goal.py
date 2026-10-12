"""Port of the firmware's goal controller (esp32/include/goal_controller.h), pinned to it by tests/test_firmware_goal.py.

It drives one goal - a distance forward, a distance sideways or a turn - on the body's own motion, fed one step per
control tick: the remaining distance along the target's axis in the frame the goal started in, or the remaining turn,
sets the stick. It slows in proportion over the last SLOW_DOWN, down to MIN_FRACTION of the asked speed, and walks on
until it is past the target; within REACHED of it then, or when time runs out, it has arrived. One that takes twice as
long as it should, plus TIMEOUT_MARGIN_S, and is not within REACHED gives up.

Units are REP-103: metres forward and to the left, radians counter-clockwise; sticks are -1 to 1 of the gait's top.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from enum import IntEnum

from .firmware_odometry import BodyStep

SLOW_DOWN_M = 0.05
REACHED_M = 0.01
SLOW_DOWN_RAD = math.radians(30.0)
REACHED_RAD = math.radians(3.0)
MIN_FRACTION = 0.2
TIMEOUT_MARGIN_S = 2.0


class GoalState(IntEnum):  # the protocol's GoalState
    IDLE = 0
    RUNNING = 1
    REACHED = 2
    CANCELLED = 3
    TIMED_OUT = 4


@dataclass(frozen=True)
class Goal:
    forward: float = 0.0
    sideways: float = 0.0
    turn: float = 0.0
    speed: float = 0.5

    def valid(self) -> bool:
        targets = (self.forward, self.sideways, self.turn)
        return (all(math.isfinite(t) for t in targets) and sum(t != 0 for t in targets) == 1
                and math.isfinite(self.speed) and 0 < self.speed <= 1)


@dataclass(frozen=True)
class GoalSticks:
    forward: float = 0.0
    left: float = 0.0
    turn: float = 0.0


class GoalController:
    def __init__(self):
        self.goal = Goal()
        self.state = GoalState.IDLE
        self.timeout = 0.0
        self._x = self._y = self._heading = self._elapsed = 0.0

    def start(self, goal: Goal, top_speed: float, top_turn: float) -> None:
        """`top_speed` (m/s) and `top_turn` (rad/s) are what a full stick gives."""
        self.goal = goal
        self._x = self._y = self._heading = self._elapsed = 0.0
        turning = goal.turn != 0
        distance = abs(goal.turn if turning else goal.forward + goal.sideways)
        zone = SLOW_DOWN_RAD if turning else SLOW_DOWN_M
        pace = goal.speed * (top_turn if turning else top_speed)
        expected = (max(distance - zone, 0.0) + min(distance, zone) / MIN_FRACTION) / pace
        self.timeout = 2 * expected + TIMEOUT_MARGIN_S
        self.state = GoalState.RUNNING

    @property
    def running(self) -> bool:
        return self.state is GoalState.RUNNING

    @property
    def progress(self) -> float:
        """How far along the target's axis it has come: m, or rad for a turn."""
        g = self.goal
        return self._heading if g.turn != 0 else self._y if g.sideways != 0 else self._x

    def update(self, step: BodyStep, dt: float) -> GoalSticks:
        """One tick: the body's step and the time it took. Returns the sticks for the next tick."""
        if not self.running:
            return GoalSticks()
        self._x += math.cos(self._heading) * step.forward - math.sin(self._heading) * step.left
        self._y += math.sin(self._heading) * step.forward + math.cos(self._heading) * step.left
        self._heading += step.turn
        self._elapsed += dt
        g = self.goal
        aim = g.turn if g.turn != 0 else g.sideways if g.sideways != 0 else g.forward
        remaining = aim - self.progress
        past, late = remaining * aim <= 0, self._elapsed > self.timeout
        if abs(remaining) <= (REACHED_RAD if g.turn != 0 else REACHED_M) and (past or late):
            self.state = GoalState.REACHED
            return GoalSticks()
        if late:
            self.state = GoalState.TIMED_OUT
            return GoalSticks()
        zone = SLOW_DOWN_RAD if g.turn != 0 else SLOW_DOWN_M
        stick = math.copysign(g.speed * min(max(abs(remaining) / zone, MIN_FRACTION), 1.0), remaining)
        if g.turn != 0:
            return GoalSticks(turn=stick)
        if g.sideways != 0:
            return GoalSticks(left=stick)
        return GoalSticks(forward=stick)

    def cancel(self) -> None:
        if self.running:
            self.state = GoalState.CANCELLED
