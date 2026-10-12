"""Does the stand's IMU compensation tip the body on the rise from REST to STAND?

On the Pico (2026-10-01) the rise tipped the body to roll -13 and pitch +8 degrees at 0.85 s, back by 1.3 s. This
rises the simulated robot on a flat floor, with every servo at the firmware's 720 deg/s limit and with the rear right
leg's servos lagging at 30 deg/s, and reports the peak tilt, the time it takes to settle within 0.5 degrees of level
and the tilt left at the end: with the compensation off, levelling from the first tick, and held until the takeover
has settled (the firmware's behaviour).

  uv run python check_rise.py
"""

import numpy as np

from src.robot.stand_compensation import FEET_EASE_S
from src.sim.rise_stand import LAGGING_LEG, SERVO_MAX_SPEED_DEG_S, rise_to_stand, settle_time


def main() -> None:
    configs = (("off", False, 0.0), ("levelling at once", True, 0.0), ("levelling held", True, FEET_EASE_S))
    for label, speeds in (("all servos 720 deg/s", SERVO_MAX_SPEED_DEG_S), ("rear right leg 30 deg/s", LAGGING_LEG)):
        print(label)
        for name, compensate, hold_s in configs:
            readings = rise_to_stand(compensate, servo_speed_deg_s=speeds, hold_s=hold_s)
            roll, pitch = np.abs(readings[:, 1:]).max(axis=0)
            end = np.abs(readings[-1, 1:]).max()
            print(f"  {name:18s} peak roll {roll:5.2f}  pitch {pitch:5.2f}  settled {settle_time(readings):4.2f} s"
                  f"  left {end:4.2f} deg")


if __name__ == "__main__":
    main()
