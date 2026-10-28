"""servo_envelope.py, servo_feasibility.py and servo_margin.py: the firmware's commanded joint angles against the MG92B."""
import numpy as np
import pytest

import servo_feasibility
import servo_margin
from src.leika import speed_model
from src.robot import servo_envelope as se

DT = 0.01
MINI = "SPOTMICRO_ESP32_MINI"


def segment(angles, start=None, variant=MINI, walking=False):
    angles = np.asarray(angles, dtype=float)
    return se.Segment(variant, "synthetic", angles, angles[0] if start is None else np.asarray(start, float), DT,
                      walking)


def one_joint(values, joint=1):
    """12 joints at the MINI's stand-like zero, `joint` moving through `values` (deg)."""
    rows = np.zeros((len(values), 12))
    rows[:, joint] = values
    return rows


def test_the_constants_are_the_firmwares_and_agree_with_the_speed_model():
    assert se.SERVO_MAX_SPEED_DEG_S == 720
    assert se.WALK_CAP_DEG_S == speed_model.MAX_JOINT_SPEED_DEG_S
    low, high = se.TRAVEL[MINI].T
    assert low[2] == 0 and high[2] == 153 and low[3] == -77  # joint_model.h: the Pico's knee fold and hip
    assert (low < high).all() and set(se.TRAVEL) == set(se.VARIANTS)


def test_a_ramp_reads_its_rate_and_a_slow_one_does_not_lag():
    ramp = segment(one_joint(np.arange(1, 11) * 5.0), start=np.zeros(12))
    assert se.rates(ramp)[:, 1] == pytest.approx(np.full(10, 500.0))
    assert se.servo_lag(ramp).max() == pytest.approx(0.0)
    assert se.speed_margin(ramp) == pytest.approx(500 / 720)


def test_a_step_trails_by_what_the_slew_limit_has_not_covered_yet():
    step = segment(one_joint([30.0] * 6), start=np.zeros(12))
    per_tick = 720 * DT
    assert se.servo_lag(step)[:, 1] == pytest.approx([30 - per_tick, 30 - 2 * per_tick, 30 - 3 * per_tick,
                                                      30 - 4 * per_tick, 0, 0])
    assert se.servo_lag(step, 0.5)[:, 1] == pytest.approx([30 - k * per_tick / 2 for k in range(1, 7)])


def test_travel_is_measured_per_joint_kind():
    # The Pico's knee folds at 153 and stops at 0; its femur reaches 45.
    knee = segment(one_joint([150.0, 155.0, -2.0], joint=5))
    assert se.travel_excess(knee)[:, 5] == pytest.approx([0, 2, 2])
    assert se.travel_margin(knee) == pytest.approx(-2)
    low, high = se.TRAVEL[MINI].T
    middle = (low + high) / 2
    middle[7] = 40.0  # 5 short of the femur's 45
    assert se.travel_margin(segment([middle])) == pytest.approx(5)


def test_the_margin_sweep_finds_the_speed_a_motion_starts_to_lag_at():
    ramp = segment(one_joint(np.arange(1, 21) * 4.0), start=np.zeros(12))  # 400 deg/s for 0.2 s
    lags, knee = servo_margin.sweep(ramp, (1.0, 0.6, 0.5))
    assert lags[0] == pytest.approx(0) and lags[1] == pytest.approx(0)
    assert lags[2] == pytest.approx(20 * (4.0 - 3.6))  # 360 deg/s falls 0.4 deg behind per tick
    assert knee == 0.5
    assert servo_margin.sweep(ramp, (1.0, 0.6))[1] is None


def test_the_feasibility_table_reads_rates_against_the_walk_cap():
    m = servo_feasibility.measure(segment(one_joint(np.arange(1, 11) * 5.4), start=np.zeros(12)))
    assert m["rate_max"] == pytest.approx(1.0) and m["at_servo_pct"] == 0 and m["lag_max"] == pytest.approx(0)


def walking_segments():
    return [s for v in se.VARIANTS for s in se.firmware_segments(v) if s.walking]


# The walk slows its own clock where a joint would pass the cap (walk_state.h step()), and it starts, and takes up
# another gait, at phase 0 with every foot down (restartCycle), so no walk in the traces passes the cap, from its first
# tick on, in any gait, AUTO's included, on any variant.
def test_the_walk_keeps_under_its_cap_from_its_first_tick():
    walks = walking_segments()
    assert sum("AUTO" in walk.label for walk in walks) >= 3 * len(se.VARIANTS)
    for walk in walks:
        assert se.rates(walk).max() <= se.WALK_CAP_DEG_S + 0.5, f"{walk.variant} {walk.label}"


# The clips that ship: on every variant the servos follow them at full speed and every joint stays in its travel.
def test_the_built_in_clips_stay_inside_the_servo_envelope():
    builtins = ("sit", "bow", "wave")
    clips = [s for v in se.VARIANTS for s in se.animation_segments(v)
             if any(s.label.startswith(f"clip: {name}") for name in builtins)]
    assert len(clips) == 4 * len(se.VARIANTS)
    for clip in clips:
        assert se.servo_lag(clip).max() <= servo_feasibility.LAG_DEG, f"{clip.variant} {clip.label}"
        assert se.travel_margin(clip) > 0, f"{clip.variant} {clip.label}"
