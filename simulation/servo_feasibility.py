"""Can the MG92B servos follow what the firmware commands? Ported from the Hexapod's servo_feasibility.py.

The Hexapod rolls its gaits out in MuJoCo and reads torque, speed and tracking lag off its servo model. Leika's walk,
stand, rest and animation player are pinned by traces of the firmware's own code for every variant, so this reads the
joint angles there (src/robot/servo_envelope.py) against the envelope the hardware has:

  rate      the commanded joint rate, as a share of the walk's cap (walk_state.h, 540 deg/s), mean over the joints
            and ticks, at the 99th percentile of each tick's fastest joint, and the fastest
  @servo    the share of ticks with a joint within 5% of the servo controller's limit (720 deg/s) or past it
  lag       how far the servo controller's output trails the command once it clips it (deg): what an open-loop
            robot inherits as a pose error
  travel    the least margin any joint keeps inside joint_model.h's travel (deg), and the ticks outside it

A segment whose fastest rate passes 1.33 (the servo limit) lags; a walking one past 1.00 has slipped the walk's own
cap, which only a jump it cannot slow (a foot snapped into the air) does.

    uv run python servo_feasibility.py                 # every variant
    uv run python servo_feasibility.py --variant SPOTMICRO_ESP32_MINI
"""
import argparse

import numpy as np

from src.robot import servo_envelope as se

# A trail below this is float rounding in the replayed slew limit, not a clipped command.
LAG_DEG = 0.01


def measure(segment: se.Segment) -> dict:
    rates = se.rates(segment)
    fastest = rates.max(axis=1)
    lag = se.servo_lag(segment)
    return {
        "rate_mean": float(rates.mean() / se.WALK_CAP_DEG_S),
        "rate_p99": float(np.percentile(fastest, 99) / se.WALK_CAP_DEG_S),
        "rate_max": float(fastest.max() / se.WALK_CAP_DEG_S),
        "at_servo_pct": float(100 * np.mean(fastest >= se.NEAR_LIMIT * se.SERVO_MAX_SPEED_DEG_S)),
        "lag_mean": float(lag.mean()),
        "lag_max": float(lag.max()),
        "travel_margin": se.travel_margin(segment),
        "outside": int(np.count_nonzero(se.travel_excess(segment).max(axis=1))),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--variant", choices=se.VARIANTS, action="append", help="default: every variant")
    args = parser.parse_args()

    print(f"MG92B envelope: walk cap {se.WALK_CAP_DEG_S:.0f} deg/s, servo limit {se.SERVO_MAX_SPEED_DEG_S:.0f} deg/s; "
          f"travel from joint_model.h")
    for variant in args.variant or se.VARIANTS:
        print(f"\n{variant}")
        print(f"{'segment':46s} {'rate~':>6s} {'rate99':>6s} {'rate^':>6s} {'@servo':>7s} {'lag~':>6s} {'lag^':>6s} "
              f"{'travel':>7s} {'out':>4s}")
        for segment in se.firmware_segments(variant) + se.animation_segments(variant):
            m = measure(segment)
            flag = " lags" if m["lag_max"] > LAG_DEG else ""
            flag += " over the walk cap" if segment.walking and m["rate_max"] > 1 + 1e-3 else ""
            flag += " outside travel" if m["outside"] else ""
            print(f"{segment.label[:46]:46s} {m['rate_mean']:6.2f} {m['rate_p99']:6.2f} {m['rate_max']:6.2f} "
                  f"{m['at_servo_pct']:6.1f}% {m['lag_mean']:6.2f} {m['lag_max']:6.2f} {m['travel_margin']:7.1f} "
                  f"{m['outside']:4d}{flag}")
    print("\nrates are shares of the walk cap; @servo is the share of ticks within 5% of the servo limit or past it.")


if __name__ == "__main__":
    main()
