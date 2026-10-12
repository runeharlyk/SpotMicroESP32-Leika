"""Checks a robot before a release: uv run python release_smoke.py 192.168.1.39

Reads the features and the firmware's ELF hash, stands, walks two seconds in each gait, plays `wave` where the firmware
has animations, and records at least MIN_TICKS ticks of telemetry throughout. It then checks the control loop's period,
the servo writes, the PWM limits and the dead-man stop, deactivates, and exits 1 on a breach (2 when the robot cannot
be driven at all). The robot walks: give it room, and keep a hand near the power.
"""
import argparse
import contextlib
import math
import sys
import time

from src.leika import Gait, Mode, Robot, RobotError
from src.leika.backends.real import RealBackend
from src.proto import message_pb2 as pb

MIN_TICKS = 500
PERIOD_US = 10_000
OVERRUN_US = 15_000  # a tick that started this long after the one before overran the 10 ms period
WALK_S = 2.0
SETTLE_S = 1.0
ANIMATION_TIMEOUT_S = 30.0
# The PWM the servo output clamps to (esp32/include/peripherals/servo_output.h): a joint written there presses on its stop.
SERVO_PWM_MIN, SERVO_PWM_MAX = 125, 600
ANIMATE = 6  # ModesEnum.ANIMATE where the firmware has animations
# The modes in which the servos are driven, and each tick writes them.
DRIVEN = {Mode.REST.value, Mode.STAND.value, Mode.WALK.value, ANIMATE}
WALKING_SWING_DEG = 5.0  # a joint swings at least this far in two seconds of walking
# AUTO walks one of these, picked from the sticks, so a velocity has no sticks under it to walk it by.
WALKED_GAITS = (Gait.TROT, Gait.CRAWL)


def play_wave(robot: Robot, backend: RealBackend) -> str:
    """Plays the built-in wave and waits for it to end; says why when the firmware cannot."""
    if "animation_play" not in pb.CorrelationRequest.DESCRIPTOR.fields_by_name:
        return "skipped: this protocol has no animations"
    try:
        listing = backend.request(pb.CorrelationRequest(animation_list_request=pb.AnimationListRequest()))
        if "wave" not in [entry.name for entry in listing.animation_list.animations]:
            return "skipped: the robot has no wave"
        robot.stand()
        backend.request(pb.CorrelationRequest(animation_play=pb.AnimationPlay(name="wave")))
    except RobotError as error:
        return f"skipped: {error}"
    deadline = time.monotonic() + ANIMATION_TIMEOUT_S
    started = False
    while time.monotonic() < deadline:
        recorded = backend.recorded()
        animating = bool(recorded) and recorded[-1].mode == ANIMATE
        if started and not animating:
            return "played"
        started |= animating
        robot.sleep(0.1)
    raise RobotError(f"wave did not {'end' if started else 'start'} within {ANIMATION_TIMEOUT_S:g} s")


def swing_deg(ticks: list[pb.TickSample]) -> float:
    """The largest swing of any joint over the ticks, in degrees."""
    if not ticks:
        return 0.0
    return math.degrees(max(max(joint) - min(joint) for joint in zip(*(tick.angles for tick in ticks))))


def breaches(ticks: list[pb.TickSample], max_overruns: int) -> list[str]:
    found = []
    if len(ticks) < MIN_TICKS:
        found.append(f"{len(ticks)} ticks recorded, fewer than {MIN_TICKS}")
    periods = [tick.period_us for tick in ticks if tick.period_us]
    overruns = sum(period > OVERRUN_US for period in periods)
    if overruns > max_overruns:
        found.append(f"{overruns} ticks overran {OVERRUN_US} us (longest {max(periods)} us); at most {max_overruns} allowed")
    driven = [tick for tick in ticks if tick.mode in DRIVEN]
    failed = sum(not tick.servo_ok for tick in driven)
    if failed:
        found.append(f"{failed} of {len(driven)} servo writes failed")
    clamped = sorted({joint for tick in driven for joint, pwm in enumerate(tick.pwm) if pwm in (SERVO_PWM_MIN, SERVO_PWM_MAX)})
    if clamped:
        found.append(f"joints {clamped} were written to a PWM limit")
    if any(not math.isfinite(value) for tick in ticks for value in (*tick.angles, *tick.targets)):
        found.append("a joint angle was not finite")
    lost = sum(tick.link_lost and not previous.link_lost for previous, tick in zip(ticks, ticks[1:]))
    if lost:
        found.append(f"the dead-man stop fired {lost} times")
    for gait in WALKED_GAITS:
        walking = [tick for tick in ticks if tick.mode == Mode.WALK.value and tick.gait == gait.value and not tick.link_lost]
        if swing_deg(walking) < WALKING_SWING_DEG:
            found.append(f"the legs swung {swing_deg(walking):.1f} degrees walking in {gait.name}, under {WALKING_SWING_DEG}")
    return found


def run(address: str, max_overruns: int) -> int:
    backend = RealBackend(address)
    robot = Robot(backend=backend)
    robot.connect()
    try:
        features = backend.features
        print(f"{features.firmware_name} {features.firmware_version} for {features.firmware_built_target}, "
              f"{features.variant}, ELF {features.firmware_elf_sha256 or 'unknown'}")
        backend.start_recording()
        robot.stand()
        robot.sleep(SETTLE_S)
        for gait in WALKED_GAITS:
            robot.set_gait(gait)
            robot.set_velocity(robot.max_velocity().vx / 2, 0, 0)
            robot.sleep(WALK_S)
            robot.stand()
            robot.sleep(SETTLE_S)
            print(f"walked {WALK_S:g} s in {gait.name}")
        print(f"wave: {play_wave(robot, backend)}")
        while len(backend.recorded()) < MIN_TICKS:
            robot.sleep(0.1)
        ticks = backend.recorded()
        robot.deactivate()
    except BaseException:
        with contextlib.suppress(RobotError):  # without a link the robot's dead-man stop has halted it
            robot.deactivate()
        raise
    finally:
        robot.close()

    periods = [tick.period_us for tick in ticks if tick.period_us]
    print(f"{len(ticks)} ticks: period {min(periods)}-{max(periods)} us (nominal {PERIOD_US}), "
          f"compute up to {max(tick.compute_us for tick in ticks)} us, "
          f"servo write up to {max(tick.servo_write_us for tick in ticks)} us")
    found = breaches(ticks, max_overruns)
    for breach in found:
        print(f"BREACH: {breach}")
    print("FAIL" if found else "PASS")
    return 1 if found else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("address", help="the robot's host name or address, e.g. 192.168.1.39 or localhost:8765")
    parser.add_argument("--max-overruns", type=int, default=MIN_TICKS // 100,
                        help="overrun ticks tolerated (default: 1%% of the minimum recording)")
    args = parser.parse_args()
    try:
        sys.exit(run(args.address, args.max_overruns))
    except RobotError as error:
        print(f"FAIL: {error}")
        sys.exit(2)


if __name__ == "__main__":
    main()
