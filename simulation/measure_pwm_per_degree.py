"""Measures the servos' PWM counts per degree with the IMU alone: uv run python measure_pwm_per_degree.py 192.168.1.39

Every variant's joint model assumes 2.0 counts per degree (esp32/include/joint_model.h). A servo that sweeps 180
degrees over 500-2500 us takes 2.28 at the PCA9685's 50 Hz (4.88 us per count), and every commanded angle would then
come out 12 % short of what the firmware meant. This script finds the true figure.

Procedure. The robot must already stand (STAND) on a flat, level floor, still; the script refuses otherwise, checking
that it is still and that the floor under its feet slopes less than MAX_FLOOR_SLOPE_DEG (the IMU's tilt against the
tilt the legs hold the body at). It reads the PWM each joint stands at, then writes both left hips raw PWMs through
the calibration message (ServoPWMData), one count every RAMP_S_PER_COUNT, in steps of +-STEPS counts with a return to
the stand's PWM between them, each held DWELL_S while the IMU's roll is averaged. Every other servo holds its stand PWM
meanwhile. The left feet rise or sink together, the body rolls on the floor, and the roll each step adds, against the
roll the firmware's kinematics predict for it at a given counts per degree, fits the counts per degree. At the end the
hips ramp back to the stand's PWM and STAND is applied afresh, which hands the servos back to the motion. Nothing here
walks; the largest step turns the hips about 8 degrees and rolls a MINI about 2 degrees. Keep a hand near it all the
same. It exits 0 with a fit, 1 when the fit does not explain the measurement, and 2 when it refuses or loses the robot.

Identifiability. Each step's roll is the average of DWELL_S / SAMPLE_S samples less the mean of the baselines on either
side, so with the IMU's roll noise sigma (0.22 degrees per sample, recorded standing on the Pico) and N samples it is
known to sigma * sqrt(1.5 / N): 0.06 degrees for N = 20. The fit's relative error is that over the root sum of squares
of the steps' rolls, so a precision p needs sqrt(sum of roll_k^2) >= sigma * sqrt(1.5 / N) / p: 3 degrees for 2 %, 6
for 1 %. The default steps roll a MINI about 0.1 degrees per count, 3.2 degrees root sum of squares at 2.28 counts per
degree, for about 2 %: enough to tell 2.0 from 2.28 six times over. More precision costs larger steps or longer dwells
(N goes as the square of the precision). The IMU's fused angles are correlated from tick to tick, which is why the
samples are 100 ms apart rather than every 10 ms tick. Beyond the noise lies the model: the fit takes the firmware's
leg geometry for the robot's, so a coxa or a mount off by x % scales the predicted roll, and s, by about x %. In the
MuJoCo model of the Pico, whose legs the firmware's geometry only approximates, the scale comes back 1 to 1.5 % high
without IMU noise and within 4 % with the Pico's (tests/test_measure_pwm_per_degree.py). On the robot, friction under
the feet, which slide sideways as the hips turn, a loose horn and the servos' deadband add to it; a residual well
above the IMU's noise says the body did not roll as the kinematics expect (a foot off the floor, or slipping).

`simulation` as the address runs it on the MuJoCo model, where --sim-scale sets the servos' true counts per degree.
"""
import argparse
import contextlib
import sys
from dataclasses import dataclass

import numpy as np

from src.leika import joint_scale, speed_model
from src.leika.backends.base import RobotError
from src.leika.constants import Mode

SAMPLE_S = 0.1  # telemetry batches arrive every 100 ms
DWELL_S = 2.0
SETTLE_S = 0.5
RAMP_S_PER_COUNT = 0.04  # 25 counts a second: about 11 degrees a second
STEPS = (6, 12, 18)  # counts, each taken up and down
MAX_FLOOR_SLOPE_DEG = 1.5
MAX_STILL_SPREAD_DEG = 0.5  # a robot standing still wavers less than this in roll and pitch (standard deviation)
MAX_ROLL_DEG = 5.0  # a step that rolls the body further than this stops the measurement
MAX_RESIDUAL_DEG = 0.25  # a fit that leaves more than this has not explained the tilt


@dataclass(frozen=True)
class Measurement:
    offsets: np.ndarray  # counts on each hip, signed so both feet move alike
    tilts: np.ndarray  # the roll and pitch each step added (degrees), one row per offset
    fit: joint_scale.ScaleFit


class Refused(Exception):
    pass


def tilt_samples(backend, seconds: float) -> np.ndarray:
    """Roll and pitch (degrees, rows) every SAMPLE_S for `seconds`."""
    samples = []
    for _ in range(max(1, round(seconds / SAMPLE_S))):
        backend.sleep(SAMPLE_S)
        state = backend.state()
        samples.append((state.roll, state.pitch))
    return np.asarray(samples)


def check_standing(backend, variant: str) -> tuple[np.ndarray, np.ndarray]:
    """The stand's joint angles (servo space, degrees) and PWMs, once the robot is seen standing still on a flat floor."""
    state = backend.state()
    if state.mode is not Mode.STAND:
        raise Refused(f"the robot is {state.mode.name}: stand it on a flat floor first")
    samples = tilt_samples(backend, 2.0)
    spread = samples.std(axis=0).max()
    if spread > MAX_STILL_SPREAD_DEG:
        raise Refused(f"the robot is not still: its tilt wavers by {spread:.2f} degrees")
    angles, pwm = backend.servo_output()
    slope = samples.mean(axis=0) - joint_scale.stand_tilt(variant, angles)
    if np.max(np.abs(slope)) > MAX_FLOOR_SLOPE_DEG:
        raise Refused(f"the floor slopes {slope[0]:.1f} degrees in roll and {slope[1]:.1f} in pitch under the robot; "
                      f"over {MAX_FLOOR_SLOPE_DEG} it is not flat enough")
    return angles, pwm


class HipSteps:
    """Writes the hips raw PWMs about the stand's, one count at a time."""

    def __init__(self, backend, setup: joint_scale.ServoSetup, stand_pwm, joints):
        self.backend, self.setup, self.stand_pwm, self.joints = backend, setup, stand_pwm, joints
        self.offset = 0

    def ramp(self, offset: int) -> None:
        step = 1 if offset > self.offset else -1
        while self.offset != offset:
            self.offset += step
            for joint in self.joints:
                pwm = self.stand_pwm[joint] + self.setup.pwm_sign(joint) * self.offset
                self.backend.set_servo_pwm(self.setup.channels[joint], int(pwm))
            self.backend.sleep(RAMP_S_PER_COUNT)

    def held(self, offset: int) -> np.ndarray:
        self.ramp(offset)
        self.backend.sleep(SETTLE_S)
        return tilt_samples(self.backend, DWELL_S).mean(axis=0)


def measure(backend, variant: str, steps=STEPS, joints=joint_scale.LEFT_HIPS) -> Measurement:
    """Steps the hips and fits the counts per degree; leaves the robot standing under the motion's control again."""
    angles, stand_pwm = check_standing(backend, variant)
    setup = backend.servo_settings()
    offsets = np.array([sign * n for n in steps for sign in (1, -1)])
    for joint in joints:
        reach = stand_pwm[joint] + setup.pwm_sign(joint) * offsets
        if reach.min() < joint_scale.PWM_MIN or reach.max() > joint_scale.PWM_MAX:
            raise Refused(f"joint {joint} would leave the servos' PWM range ({reach.min():.0f}-{reach.max():.0f})")
    hips = HipSteps(backend, setup, stand_pwm, joints)
    tilts = []
    try:
        before = hips.held(0)
        for offset in offsets:
            stepped = hips.held(int(offset))
            after = hips.held(0)
            tilts.append(stepped - (before + after) / 2)
            print(f"{offset:+4d} counts: roll {tilts[-1][0]:+.2f}, pitch {tilts[-1][1]:+.2f} degrees")
            if np.max(np.abs(tilts[-1])) > MAX_ROLL_DEG:
                raise RobotError(f"the body rolled {tilts[-1][0]:.1f} degrees for {offset} counts, over {MAX_ROLL_DEG}")
            before = after
    finally:
        with contextlib.suppress(RobotError):  # without a link the robot's dead-man stop has halted it
            hips.ramp(0)
            backend.set_mode(Mode.STAND)  # applies afresh, which hands the servos back to the motion
    fit = joint_scale.fit_scale(variant, angles, joints, offsets, tilts, setup.pwm_per_degree)
    return Measurement(offsets, np.asarray(tilts), fit)


def report(result: Measurement, assumed: float) -> int:
    fit = result.fit
    print(f"{fit.scale:.3f} +- {fit.sigma:.3f} PWM counts per degree (the firmware assumes {assumed:g}); the fit leaves "
          f"{fit.residual_rms:.2f} degrees RMS")
    print(f"every commanded angle turns {assumed / fit.scale:.3f} times what the firmware means")
    if fit.residual_rms > MAX_RESIDUAL_DEG:
        print(f"the residual is over {MAX_RESIDUAL_DEG} degrees: the body did not roll as the kinematics expect")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("address", help="the robot's address or serial port, or `simulation`")
    parser.add_argument("--sim-scale", type=float, default=2.28,
                        help="the simulated servos' true counts per degree (simulation only)")
    args = parser.parse_args()
    if args.address == "simulation":
        from src.leika.backends.sim import SimBackend

        backend, variant = SimBackend(realtime=False, servo_scale=args.sim_scale), "SPOTMICRO_ESP32_MINI"
        backend.connect()
        backend.set_mode(Mode.STAND)
        backend.sleep(2.0)
    else:
        from src.leika.backends.real import RealBackend

        backend = RealBackend(args.address)
        backend.connect()
        variant = speed_model.known_variant(backend.features.variant)
    try:
        result = measure(backend, variant)
        return report(result, backend.servo_settings().pwm_per_degree)
    except Refused as refusal:
        print(f"refused: {refusal}")
        return 2
    except RobotError as error:
        print(f"stopped: {error}")
        return 2
    finally:
        backend.close()


if __name__ == "__main__":
    sys.exit(main())
