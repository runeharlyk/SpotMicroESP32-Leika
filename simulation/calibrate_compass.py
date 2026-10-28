"""Fits the compass's hard-iron offset to a recording and optionally stores it on the robot.

  uv run python record.py --robot <address> --seconds 60      # turn the robot slowly through every orientation
  uv run python calibrate_compass.py recording.leika [--write <address>]

Run `uv run python gen_protos.py` once first. The fit undoes the settings the recording's header holds, so a robot that
was calibrated before is fitted afresh, not on top of the old offset. Without --write it only prints the result.
"""
import argparse
import sys

from src.sim.compass_fit import MIN_DIRECTION_SPREAD, fit_recording
from src.sim.recording import load

EARTH_FIELD_UT = (25.0, 65.0)  # the geomagnetic field's strength anywhere on Earth


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--write", metavar="ADDRESS", help="store the offset on the robot at this address")
    args = parser.parse_args()
    fit = fit_recording(load(args.path))
    print(f"offset {fit.offset[0]:.2f}, {fit.offset[1]:.2f}, {fit.offset[2]:.2f} uT from {fit.samples} samples")
    print(f"field {fit.radius:.1f} uT, residual {fit.residual:.2f} uT, direction spread {fit.direction_spread:.3f}")
    problems = []
    if not fit.covered:
        problems.append(f"the robot was not turned far enough (spread {fit.direction_spread:.3f} < "
                        f"{MIN_DIRECTION_SPREAD}); record again, turning it through every orientation")
    if not EARTH_FIELD_UT[0] <= fit.radius <= EARTH_FIELD_UT[1]:
        problems.append(f"a field of {fit.radius:.0f} uT is not Earth's; a magnet or soft iron nearby distorts it")
    for problem in problems:
        print(f"refused: {problem}", file=sys.stderr)
    if problems:
        return 1
    if args.write:
        from src.leika.backends.real import RealBackend

        robot = RealBackend(args.write)
        robot.connect()
        try:
            robot.set_mag_offset(fit.offset)
        finally:
            robot.close()
        print(f"stored on {args.write}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
