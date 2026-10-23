"""Port of the firmware's goal controller (esp32/include/goal_controller.h), pinned to it by tests/test_firmware_goal.py.

It drives one goal - a distance forward, a distance sideways or a turn - on the body's own motion, fed one step per
control tick, in the frame the goal started in, and judges the pose the robot would come to rest in were its sticks
centred now: where the body is, carried on by the step the walk's stop would still take. The remainder along the
target's axis sets the stick: the asked speed, slowed in proportion over the slowdown zone (SLOW_DOWN, or two stops'
coasts at the asked speed when longer), down to MIN_FRACTION of it. Once the resting pose reaches the target it centres
the sticks, waits for the walk to have stood still for STILL_S and judges where the robot stands: within REACHED of the target, and a
straight goal within CROSS_REACHED_M of its start line, it has REACHED; otherwise a corrective pass walks the rest, up
to MAX_PASSES in all, and then it has TIMED_OUT. So has a pass that takes longer than twice its expected time plus
TIMEOUT_MARGIN_S. A straight goal holds its start heading with the turn stick and its start line with the stick across.

Units are REP-103: metres forward and to the left, radians counter-clockwise; sticks are -1 to 1 of the gait's top.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from enum import IntEnum

import numpy as np

from .firmware_odometry import BodyStep

SLOW_DOWN_M = 0.05
REACHED_M = 0.01
SLOW_DOWN_RAD = math.radians(30.0)
REACHED_RAD = math.radians(3.0)
MIN_FRACTION = 0.2
MAX_PASSES = 3
HEADING_GAIN = 2.0  # turn stick per rad off the start heading
HEADING_STICK_MAX = 0.3
CROSS_GAIN = 10.0  # stick across the line per m off it
CROSS_STICK_MAX = 0.3
CROSS_REACHED_M = 0.02
TIMEOUT_MARGIN_S = 2.0
STILL_S = 0.2  # s the walk stands still before the pose is judged: the body coasts on, and the servos lag
# The walk's coast once its sticks centre, as a time at the speed it walked: 2 / omega of the gait's critical damper,
# which settles in a third of a second (firmware_gait.SMOOTHING_OMEGA).
STOP_S = 2 / (4.744 / 0.333)


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


def _clamp(value: float, low: float, high: float) -> float:
    return min(max(value, low), high)


class GoalController:
    def __init__(self):
        self.goal = Goal()
        self.state = GoalState.IDLE
        self.passes = 0
        self._top_speed = self._top_turn = 0.0
        self._x = self._y = self._heading = 0.0
        self._rest = (0.0, 0.0, 0.0)
        self._direction = 0.0
        self._settling = False
        self._elapsed = self._deadline = self._spent = 0.0
        self._still = 0.0  # how long the walk has stood still

    def start(self, goal: Goal, top_speed: float, top_turn: float) -> None:
        """`top_speed` (m/s) and `top_turn` (rad/s) are what a full stick gives."""
        self.goal = goal
        self._top_speed, self._top_turn = top_speed, top_turn
        self._x = self._y = self._heading = 0.0
        self._rest = (0.0, 0.0, 0.0)
        self._spent = self._elapsed = self._still = 0.0
        self.passes = 0
        self.state = GoalState.RUNNING
        self._begin_pass()

    @property
    def running(self) -> bool:
        return self.state is GoalState.RUNNING

    @property
    def progress(self) -> float:
        """How far along the target's axis it has come: m, or rad for a turn."""
        g = self.goal
        return self._heading if g.turn != 0 else self._y if g.sideways != 0 else self._x

    @property
    def timeout(self) -> float:
        """How long it may take, in seconds: the passes so far and the longest the one under way may take."""
        return self._spent + self._deadline

    def update(self, step: BodyStep, to_rest: BodyStep, at_rest: bool, dt: float) -> GoalSticks:
        """One tick: the body's step and the time it took, the step the walk's stop would still take from here and
        whether the walk stands still. Returns the sticks for the next tick."""
        if not self.running:
            return GoalSticks()
        c, s = math.cos(self._heading), math.sin(self._heading)
        self._x += c * step.forward - s * step.left
        self._y += s * step.forward + c * step.left
        self._heading += step.turn
        c, s = math.cos(self._heading), math.sin(self._heading)
        self._rest = (self._x + c * to_rest.forward - s * to_rest.left, self._y + s * to_rest.forward + c * to_rest.left,
                      self._heading + to_rest.turn)
        self._elapsed += dt
        # Summed in float, as the firmware sums it: the tick it reaches STILL_S on turns on the sum's last bit.
        self._still = float(np.float32(self._still) + np.float32(dt)) if at_rest else 0.0
        if self._elapsed > self._deadline:
            self.state = GoalState.TIMED_OUT
            return GoalSticks()
        if self._settling:
            if self._still < STILL_S:
                return GoalSticks()
            if self._within():
                self.state = GoalState.REACHED
                return GoalSticks()
            if self.passes == MAX_PASSES:
                self.state = GoalState.TIMED_OUT
                return GoalSticks()
            self._begin_pass()
        if self._arriving() and abs(self._cross()) <= CROSS_REACHED_M:
            self._settling = True
            return GoalSticks()
        return self._sticks()

    def cancel(self) -> None:
        if self.running:
            self.state = GoalState.CANCELLED

    def _linear(self) -> bool:
        return self.goal.turn == 0

    def _aim(self) -> float:
        g = self.goal
        return g.turn if g.turn != 0 else g.sideways if g.sideways != 0 else g.forward

    def _remaining(self) -> float:
        g, (x, y, heading) = self.goal, self._rest
        return self._aim() - (heading if g.turn != 0 else y if g.sideways != 0 else x)

    def _cross(self) -> float:
        g, (x, y, _) = self.goal, self._rest
        return 0.0 if g.turn != 0 else x if g.sideways != 0 else y

    def _pace(self) -> float:
        return self.goal.speed * (self._top_speed if self._linear() else self._top_turn)

    def _zone(self) -> float:
        return max(SLOW_DOWN_M if self._linear() else SLOW_DOWN_RAD, 2 * self._pace() * STOP_S)

    def _share(self) -> float:
        return _clamp(abs(self._remaining()) / self._zone(), MIN_FRACTION, 1.0)

    def _arriving(self) -> bool:
        return self._remaining() * self._direction <= 0

    def _within(self) -> bool:
        if not self._linear():
            return abs(self._remaining()) <= REACHED_RAD
        return abs(self._remaining()) <= REACHED_M and abs(self._cross()) <= CROSS_REACHED_M

    # The first pass walks its target however short; a later one whose axis is within the tolerance walks only across.
    def _begin_pass(self) -> None:
        self.passes += 1
        left = self._remaining()
        across_only = self.passes > 1 and self._linear() and abs(left) <= REACHED_M
        self._direction = 0.0 if across_only else math.copysign(1.0, left)
        self._settling = False
        self._spent += self._elapsed
        self._elapsed = 0.0
        distance, zone = abs(left), self._zone()
        expected = (max(distance - zone, 0.0) + min(distance, zone) / MIN_FRACTION) / self._pace()
        self._deadline = 2 * expected + TIMEOUT_MARGIN_S

    def _sticks(self) -> GoalSticks:
        g = self.goal
        drive = 0.0 if self._arriving() else math.copysign(g.speed * self._share(), self._remaining())
        if g.turn != 0:
            return GoalSticks(turn=drive)
        across = _clamp(CROSS_GAIN * self._cross(), -CROSS_STICK_MAX, CROSS_STICK_MAX)
        hold = _clamp(-HEADING_GAIN * self._heading, -HEADING_STICK_MAX, HEADING_STICK_MAX)
        if g.sideways != 0:
            return GoalSticks(forward=-across, left=drive, turn=hold)
        return GoalSticks(forward=drive, left=-across, turn=hold)
