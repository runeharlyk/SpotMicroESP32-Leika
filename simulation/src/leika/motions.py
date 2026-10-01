"""Distance and turn motions, written once against the Robot's velocity, state and clock."""
import math

from .backends.base import RobotError, RobotTimeout

SETTLE_S = 0.5  # the robot's 333 ms command filter, settled
POLL_S = 0.05
SLOW_DOWN_DEG = 30.0
MIN_RATE_FRACTION = 0.2
TOLERANCE_DEG = 3.0


def move(robot, forward: float, sideways: float, speed: float | None) -> None:
    """Walks `forward` and `sideways` metres (one of them zero) at `speed`, by default half the gait's top speed.

    Open-loop: the command filter's lag while starting equals its overrun while stopping, so the distance is speed times
    time once the robot has settled.
    """
    distance = math.hypot(forward, sideways)
    if distance == 0:
        return
    top = robot.max_velocity()
    v = speed if speed is not None else (top.vx if sideways == 0 else top.vy) / 2
    if v <= 0:
        raise ValueError(f"a move needs a positive speed, not {v}")
    drops = robot.state().link_drops
    commanded = robot.set_velocity(v * forward / distance, v * sideways / distance, 0.0)
    robot.sleep(distance / math.hypot(commanded.vx, commanded.vy))
    robot.stop()
    robot.sleep(SETTLE_S)
    if robot.state().link_drops > drops:
        raise RobotError("the robot's dead-man stop held it during the move: it walked less than asked")


def rotate(robot, degrees: float, rate: float | None) -> None:
    """Turns `degrees` counter-clockwise on the measured yaw, at `rate` rad/s (by default half the gait's top), slowing
    over the last SLOW_DOWN_DEG and stopping within TOLERANCE_DEG."""
    if degrees == 0:
        return
    top = rate if rate is not None else robot.max_velocity().yaw_rate / 2
    if top <= 0:
        raise ValueError(f"a turn needs a positive rate, not {top}")
    expected = math.radians(abs(degrees)) / top + math.radians(SLOW_DOWN_DEG) / (MIN_RATE_FRACTION * top)
    start = robot.time()
    last = robot.state().yaw
    turned = 0.0
    while True:
        yaw = robot.state().yaw
        turned += (yaw - last + 180) % 360 - 180
        last = yaw
        remaining = degrees - turned
        if abs(remaining) <= TOLERANCE_DEG:
            break
        if robot.time() - start > 2 * expected:
            robot.stop()
            raise RobotTimeout(f"turned {turned:.0f} of {degrees:.0f} degrees in {2 * expected:.1f} s")
        fraction = min(1.0, max(MIN_RATE_FRACTION, abs(remaining) / SLOW_DOWN_DEG))
        robot.set_velocity(0.0, 0.0, math.copysign(top * fraction, remaining))
        robot.sleep(POLL_S)
    robot.stop()
    robot.sleep(SETTLE_S)
