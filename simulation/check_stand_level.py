"""Does the firmware's stand IMU compensation reduce the body's tilt on a slope, or increase it?

The firmware subtracts the reversed IMU angles from the commanded attitude (motion_states/state.h). Whether that
levels depends on the IMU's convention and on which side the firmware's +z is, and the latter is the open "+z side"
question. This stands the simulated robot on a tilted floor, pitch and roll separately, with the compensation off,
with the repository's +z (left) and with the mirrored +z (right), and reports the settled IMU tilt.

  uv run python check_stand_level.py
"""

import numpy as np

from src.robot.stand_compensation import FIRMWARE_FROM_REP, MIRRORED_FIRMWARE_FROM_REP
from src.sim.slope_stand import LEFT_UP, NOSE_DOWN, stand_on_slope

SLOPE_DEG = 8.0
SECONDS = 10.0
SETTLED_TICKS = 100


def main() -> None:
    configs = (("off", False, FIRMWARE_FROM_REP), ("+z left (repo)", True, FIRMWARE_FROM_REP),
               ("+z right (mirror)", True, MIRRORED_FIRMWARE_FROM_REP))
    for label, axis, column in (("pitch slope", NOSE_DOWN, 1), ("roll slope", LEFT_UP, 0)):
        print(f"{label} {SLOPE_DEG:.0f} deg: settled IMU {'pitch' if column else 'roll'}")
        off = None
        for name, levelling, frame in configs:
            tilt = stand_on_slope(axis, SLOPE_DEG, levelling, SECONDS, frame)[-SETTLED_TICKS:, column]
            settled = float(np.mean(tilt))
            off = settled if off is None else off
            verdict = "" if not levelling else ("reduces" if abs(settled) < abs(off) else "increases")
            print(f"  {name:18s} {settled:7.2f} deg  ripple {np.ptp(tilt):.3f}  {verdict}")


if __name__ == "__main__":
    main()
