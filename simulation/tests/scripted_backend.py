"""A backend on a virtual clock: it integrates the commanded velocity into a pose, optionally with a lagging yaw and a
gait that turns slower than commanded, and records every command. With `goal_scale` it takes goal moves too, landing
at that multiple of each target at once, as a robot whose odometry undercounts does, and each walk turns the heading by
`walk_drift_deg` on the way. Its odometry is its pose."""
import math
from collections import deque

from src.leika.backends.base import ZERO, Calibration, Odometry, RobotState, Velocity
from src.leika.constants import Mode

DT = 0.01


class ScriptedBackend:
    def __init__(self, top=Velocity(0.1, 0.06, 1.2), yaw=0.0, yaw_lag_s=0.0, turn_scale=1.0, drop_at=None,
                 goal_scale=None, walk_drift_deg=0.0):
        self.top, self.turn_scale, self.drop_at, self.goal_scale = top, turn_scale, drop_at, goal_scale
        self.walk_drift_deg = walk_drift_deg
        self.goals = []  # (forward, sideways, turn_deg, speed)
        self.link_drops = 0
        self.t, self.x, self.y, self.yaw = 0.0, 0.0, 0.0, yaw
        self.mode = Mode.REST
        self.velocity = ZERO
        self.commands = []  # (t, Velocity)
        self.modes = []
        self.closed = False
        self._seen = deque([yaw] * (round(yaw_lag_s / DT) + 1))

    def connect(self):
        pass

    def close(self):
        self.closed = True

    def set_mode(self, mode):
        self.mode = mode
        self.modes.append(mode)

    def set_gait(self, gait):
        pass

    def set_height(self, height):
        pass

    def max_velocity(self):
        return self.top

    def set_velocity(self, v):
        clamp = lambda value, limit: max(-limit, min(limit, value))
        self.velocity = Velocity(clamp(v.vx, self.top.vx), clamp(v.vy, self.top.vy), clamp(v.yaw_rate, self.top.yaw_rate))
        self.commands.append((self.t, self.velocity))
        return self.velocity

    def state(self):
        seen = (self._seen[0] + 180) % 360 - 180
        return RobotState(self.t, self.mode, 0.0, 0.0, seen, (0.0, 0.0, 0.0), (0.0, 0.0, 9.81), (), False, self.link_drops)

    def supports_goals(self):
        return self.goal_scale is not None

    def goal_move(self, forward, sideways, turn_deg, speed):
        self.goals.append((forward, sideways, turn_deg, speed))
        heading = math.radians(self.yaw)
        forward, sideways = forward * self.goal_scale, sideways * self.goal_scale
        self.x += forward * math.cos(heading) - sideways * math.sin(heading)
        self.y += forward * math.sin(heading) + sideways * math.cos(heading)
        self.yaw += turn_deg * self.goal_scale + (self.walk_drift_deg if forward or sideways else 0.0)
        self.mode = Mode.STAND
        self._seen = deque([self.yaw] * len(self._seen))

    def odometry(self):
        yaw = (self.yaw + 180) % 360 - 180
        return Odometry(self.x, self.y, yaw, 0.0, yaw)

    def calibrate(self):
        return Calibration(True, True, 1.5)

    def now(self):
        return self.t

    # Whole 10 ms steps, then the remainder, so a move's distance comes out exact.
    def sleep(self, seconds):
        steps, rest = divmod(seconds, DT)
        for dt in [DT] * int(steps) + ([rest] if rest > 1e-9 else []):
            self._advance(dt)

    def _advance(self, dt):
        v = self.velocity if self.mode is Mode.WALK else ZERO
        heading = math.radians(self.yaw)
        self.x += (v.vx * math.cos(heading) - v.vy * math.sin(heading)) * dt
        self.y += (v.vx * math.sin(heading) + v.vy * math.cos(heading)) * dt
        self.yaw += math.degrees(v.yaw_rate * self.turn_scale) * dt
        if self.drop_at is not None and self.t < self.drop_at <= self.t + dt:
            self.link_drops += 1
        self.t += dt
        self._seen.append(self.yaw)
        self._seen.popleft()
