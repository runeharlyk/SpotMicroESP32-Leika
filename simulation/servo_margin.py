"""How much servo speed margin does each motion leave? Ported from the Hexapod's servo_margin.py.

A real MG92B is slower than its rating when it is loaded, hot or on a sagging battery: 750 deg/s unloaded at 6 V,
460 at 5 V (servo_output.h), and the servo controller's 720 deg/s assumes the better end. A motion that only follows
at the full speed will lag on the robot. The Hexapod sweeps its sim servo's stall torque down and watches the score;
the speed is what limits Leika's open-loop motion, so this sweeps the servo controller's speed limit down over the
firmware's own traces (src/robot/servo_envelope.py) and watches how far the servos trail the command.

Per segment: the largest lag (deg) at each share of the servo's speed, and the knee, the first share at which some
joint trails by more than KNEE_DEG; `margin` is the share at which the fastest command just starts to clip.

    uv run python servo_margin.py
    uv run python servo_margin.py --scales 1 0.75 0.64 0.5 --variant SPOTMICRO_ESP32_MINI
"""
import argparse

from src.robot import servo_envelope as se

SCALES = (1.0, 0.9, 0.8, 0.7, 0.64, 0.6, 0.5)
# A trail below this the servos' own deadband and the PWM's half-degree steps hide.
KNEE_DEG = 1.0


def sweep(segment: se.Segment, scales=SCALES) -> tuple[list[float], float | None]:
    """The largest lag at each scale, and the first scale whose lag passes KNEE_DEG, if any."""
    lags = [float(se.servo_lag(segment, scale).max()) for scale in scales]
    knee = next((scale for scale, lag in zip(scales, lags) if lag > KNEE_DEG), None)
    return lags, knee


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--variant", choices=se.VARIANTS, action="append", help="default: every variant")
    parser.add_argument("--scales", nargs="*", type=float, default=list(SCALES))
    args = parser.parse_args()

    print(f"servo speed sweep from {se.SERVO_MAX_SPEED_DEG_S:.0f} deg/s; largest lag in degrees")
    for variant in args.variant or se.VARIANTS:
        print(f"\n{variant}")
        print(f"{'segment':46s} " + " ".join(f"x{s:<5.2f}" for s in args.scales) + "  margin  knee")
        for segment in se.firmware_segments(variant) + se.animation_segments(variant):
            lags, knee = sweep(segment, args.scales)
            print(f"{segment.label[:46]:46s} " + " ".join(f"{lag:6.2f}" for lag in lags)
                  + f"  {se.speed_margin(segment):6.2f}  " + (f"x{knee:.2f}" if knee else "-"))
    print(f"\nmargin: the share of the servo speed the fastest command needs (above 1, the servos clip it even at "
          f"full speed); knee: the first share at which a joint trails by more than {KNEE_DEG:g} deg.")


if __name__ == "__main__":
    main()
