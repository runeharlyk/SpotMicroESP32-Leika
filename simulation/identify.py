"""Fits the simulation's servo model, actuator lag and IMU noise to a recording and writes them as the domain
randomisation's centre.

uv run python identify.py recording.leika [--out recording.sysid.json]
The recording should begin with the robot standing still for a few seconds (the IMU noise and the replay's start)
and then walk on flat ground. Train on the result with train_mj.py --randomize --dr-centre recording.sysid.json.
See src/sim/sysid.py for what a recording can determine and how.
"""
import argparse
import json
from pathlib import Path

from src.sim.recording import load
from src.sim.sysid import centre, identify


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--out", help="the JSON file domain_rand.Ranges.identified reads (default: <recording>.sysid.json)")
    args = parser.parse_args()
    rec = load(args.path)
    if rec.header is not None:
        print(f"{rec.header.variant or 'no variant'} {rec.header.firmware_version}, IMU {rec.header.imu_driver}, "
              f"{len(rec.ticks['seq'])} ticks")
    estimates = identify(rec)
    for name, e in estimates.items():
        if e.identified:
            spread = f" +- {e.std:.3g}" if e.std is not None else ""
            print(f"{name:24s} {e.value:10.4g}{spread} {e.unit}" + (f"  ({e.note})" if e.note else ""))
        else:
            print(f"{name:24s} not identifiable: {e.note}")
    out = Path(args.out) if args.out else Path(args.path).with_suffix(".sysid.json")
    out.write_text(json.dumps(centre(estimates, str(args.path)), indent=2))
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
