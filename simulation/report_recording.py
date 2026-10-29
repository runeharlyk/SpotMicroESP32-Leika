"""Prints a recording's timing and IMU statistics beside the ranges domain_rand.py assumes, and each mode's tick profile.

uv run python report_recording.py recording.leika [--still]
--still: the robot stood still throughout, so the gyro mean is its bias and the spread its noise.
The profile splits each mode's ticks into the parts TickSample times: compute holds motion, which holds motion_step,
odometry, goal and kinematics; servo_write holds servo_bus_wait and servo_transfer. Firmware older than these parts
records them as zero.
Run `uv run python gen_protos.py` once first: it generates the protocol classes in src/proto/.
"""
import argparse

from src.sim import domain_rand
from src.sim.recording import load
from src.sim.telemetry_stats import COMPUTE_PARTS, LATE_PERIOD_MS, SERVO_PARTS, profile_by_mode, summarize

PROFILE_ROWS = ("period", "compute", *COMPUTE_PARTS, "servo_write", *SERVO_PARTS)


def print_profile(profile: dict[str, dict[str, float]]) -> None:
    for mode, stats in profile.items():
        late = stats.get("late_fraction")
        print(f"\n{mode}: {stats['ticks']} ticks" +
              (f", {late:.1%} more than {LATE_PERIOD_MS} ms apart" if late is not None else ""))
        print(f"  {'ms':16s} {'mean':>8s} {'p99':>8s} {'max':>8s}")
        for row in PROFILE_ROWS:
            if f"{row}_ms_mean" in stats:
                print(f"  {row:16s} {stats[f'{row}_ms_mean']:8.3f} {stats[f'{row}_ms_p99']:8.3f} "
                      f"{stats[f'{row}_ms_max']:8.3f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--still", action="store_true")
    args = parser.parse_args()
    rec = load(args.path)
    stats = summarize(rec)
    if rec.header is not None:
        print(f"{rec.header.variant} {rec.header.firmware_version}, IMU {rec.header.imu_driver} "
              f"at {rec.header.imu_rate_hz} Hz, compass {rec.header.mag_rate_hz} Hz")
    for name, value in stats.items():
        print(f"{name:28s} {value:10.3f}")
    low, high = domain_rand.ACTION_LATENCY_STEPS
    print(f"\nassumed action latency       {low * domain_rand.CONTROL_DT * 1000:.0f}-"
          f"{(high - 1) * domain_rand.CONTROL_DT * 1000:.0f} ms; measured sensor to servo p50-p99 "
          f"{stats.get('sensor_to_servo_ms_p50', float('nan')):.1f}-{stats.get('sensor_to_servo_ms_p99', float('nan')):.1f} ms")
    if args.still:
        print(f"assumed gyro noise std       {domain_rand.GYRO_NOISE_STD:.3f} rad/s; "
              f"measured {stats.get('gyro_noise_std', float('nan')):.4f}")
        print(f"assumed gyro bias std        {domain_rand.GYRO_BIAS_STD:.3f} rad/s; measured this robot's bias "
              f"{stats.get('gyro_bias_x', float('nan')):.4f}, {stats.get('gyro_bias_y', float('nan')):.4f}, "
              f"{stats.get('gyro_bias_z', float('nan')):.4f} (after the boot calibration)")
        print(f"assumed roll/pitch noise     {domain_rand.RPY_NOISE_DEG:.2f} deg; "
              f"measured {stats.get('rpy_noise_deg', float('nan')):.3f}")
    print_profile(profile_by_mode(rec))


if __name__ == "__main__":
    main()
