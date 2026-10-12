"""Replays one velocity script on the simulation and on a robot and reports the drift: uv run python sim_vs_real.py 192.168.1.39

The script stands, walks forward, sideways, turns and stops, a few seconds each, through the Robot facade on both. Each
control tick records the commanded joint targets (the simulation's actuator targets; the robot's telemetry `targets`),
the mode, and the roll and pitch (the simulation's from physics; the robot's from its IMU). The two are aligned by script
time, each segment from the tick its command took effect, and compared per segment: the joints' motion, over the script
and over a mean gait cycle; the cadence, each foot's phase and the first lift-off, from the feet's lift-offs; the tilt;
and the mode. It exits 1 when a difference exceeds its threshold below, and 2 when the robot cannot be driven. `--virtual` runs the robot side on the virtual robot (virtual_robot.py), which needs no hardware.

The simulation models the Pico, so the robot must be a SPOTMICRO_ESP32_MINI: its joint targets are mapped onto the Pico's
MJCF joints with the map fit_pico_joint_map.py fitted. The robot walks: give it room, and keep a hand near the power.
"""
import argparse
import os
import sys
import tempfile
from dataclasses import dataclass

import numpy as np

from src.leika import Gait, Mode, Robot, RobotError, Velocity
from src.leika import speed_model
from src.leika.backends.base import ZERO
from src.leika.backends.real import RealBackend
from src.leika.backends.sim import SimBackend
from src.robot.firmware_gait import leg_fk
from src.sim.mj_runtime import CONTROL_DT, LEG_NAMES

VARIANT = "SPOTMICRO_ESP32_MINI"


@dataclass(frozen=True)
class Segment:
    name: str
    velocity: Velocity  # ZERO with mode STAND stands; ZERO with mode WALK stops in place
    mode: Mode
    seconds: float


# Well inside the MINI's trot top speed (0.128 m/s, 1.33 rad/s), which the simulation shares, so neither clamps.
SCRIPT = (
    Segment("stand", ZERO, Mode.STAND, 2.0),
    Segment("forward", Velocity(0.03, 0.0, 0.0), Mode.WALK, 4.0),
    Segment("sideways", Velocity(0.0, 0.015, 0.0), Mode.WALK, 4.0),
    Segment("turn", Velocity(0.0, 0.0, 0.5), Mode.WALK, 4.0),
    Segment("stop", ZERO, Mode.WALK, 2.0),
)

# The thresholds. The simulation's gait is the firmware's walk with the MINI's constants (src/robot/firmware_gait.py),
# placed on the Pico's own geometry: the feet go where the firmware puts them relative to each leg, in metres, and the
# Pico's IK turns that into joint angles where the robot's are the MINI's IK through the fitted joint map. That geometry
# is the gap that remains, and against the virtual robot it measures as each threshold's comment says. Each sits a margin
# above it, and well below what a single joint whose motion is reversed about the stand pose gives: about 15 degrees RMS
# or more in some segment, by script time and over a cycle alike (the hips' in the turn, the femurs' and tibias' in
# every walk).
#
# The stand pose, per joint: up to 4 degrees apart against the virtual robot (rr_femur).
MAX_STAND_OFFSET_DEG = 6.0
# The joints' motion from that stand pose, as an RMS over a segment aligned by script time, with the simulation holding
# each segment as long as the robot did: up to 3 degrees against the virtual robot (the tibias).
MAX_MOTION_RMS_DEG = 6.0
# The same over a mean cycle (CYCLE_BINS samples between the front-right foot's lift-offs), so the cadence and the phase
# drop out and the stride and lift remain: up to 3 degrees against the virtual robot.
MAX_CYCLE_RMS_DEG = 6.0
# Feet lifted per second: both step at 1 Hz (speed_model.SPEED), within 0.01 Hz; a step of 0.1 in the speed the client
# sends changes it by 0.2 Hz.
MAX_CADENCE_DIFF_HZ = 0.1
# Each foot's lift-off within the cycle, relative to the front-right foot's: both trot the diagonal pairs half a cycle
# apart and agree to 0.02 cycles. A leg in the wrong pair is 0.5 off, and the crawl's legs lift a quarter apart.
MAX_FOOT_PHASE_DIFF = 0.1
# Roll and pitch: the simulation sways up to 4 degrees under physics, the virtual robot reads level; a real robot adds
# its IMU's mounting error, so calibrate it level (robot.calibrate() or the app) before a run.
MAX_TILT_DIFF_DEG = 6.0
# The ticks in which the two report different modes, as a share of a segment: the robot takes a tick or two to switch.
MAX_MODE_MISMATCH = 0.05

CYCLE_BINS = 50
LIFT_M = 0.005  # a foot this far above its stand height is in the air: a sixth of the 30 mm lift
JOINT_NAMES = [f"{leg}_{joint}" for leg in LEG_NAMES for joint in ("hip", "femur", "tibia")]

# The MINI's servo-space joint angles onto the Pico's MJCF joints: mjcf = sign * angle + offset, per firmware joint (legs
# fl, fr, rl, rr). Fitted by fit_pico_joint_map.py; the same map as PICO_JOINTS in app/src/lib/simulation/robots.ts.
FIRMWARE_LEGS = ("fl", "fr", "rl", "rr")
MAP_SIGN = np.array([-1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1], dtype=float)
MAP_OFFSET = np.radians([7.3, 54.6, 101.2, 6.0, 52.9, -99.4, 5.8, 54.4, 100.4, 6.7, 50.6, -98.1])
TO_SIM_ORDER = np.array([FIRMWARE_LEGS.index(leg) * 3 + joint for leg in LEG_NAMES for joint in range(3)])


def to_mjcf(targets) -> np.ndarray:
    """A tick's 12 servo-space targets (rad) as the Pico's MJCF joints (rad), in the simulation's order."""
    return (MAP_SIGN * np.asarray(targets, dtype=float) + MAP_OFFSET)[TO_SIM_ORDER]


@dataclass
class Trace:
    """One segment on one side, from the tick its command took effect."""
    t: np.ndarray  # (n,) seconds since that tick
    joints: np.ndarray  # (n, 12) MJCF joint targets, rad, in the simulation's order
    mode: np.ndarray  # (n,) Mode values
    roll: np.ndarray  # (n,) degrees
    pitch: np.ndarray
    held: float  # seconds until the next segment's command took effect


def _command(robot: Robot, segment: Segment) -> None:
    if segment.mode is Mode.STAND:
        robot.stand()
        return
    commanded = robot.set_velocity(segment.velocity.vx, segment.velocity.vy, segment.velocity.yaw_rate)
    if not np.allclose([commanded.vx, commanded.vy, commanded.yaw_rate],
                       [segment.velocity.vx, segment.velocity.vy, segment.velocity.yaw_rate], atol=1e-9):
        raise RobotError(f"{segment.name}: the robot clamped {segment.velocity} to {commanded}")


def record_simulation(held: list[float] | None = None) -> list[Trace]:
    """The script on the simulation, each segment held as long as `held` says (the robot's) or as the script says.

    The gait's phase runs on from one segment into the next, so a segment the robot held a few ticks longer, as a
    client's sleep and the link make it, starts the next one that much further into its stride.
    """
    backend = SimBackend(realtime=False)
    robot = Robot(backend=backend)
    robot.connect()
    traces = []
    try:
        robot.set_gait(Gait.TROT)
        for segment, seconds in zip(SCRIPT, held or [segment.seconds for segment in SCRIPT]):
            _command(robot, segment)
            rows = []
            for tick in range(round(seconds / CONTROL_DT)):
                robot.sleep(CONTROL_DT)  # tick 0 is the first to act on the command, as on the robot
                if tick * CONTROL_DT < segment.seconds:
                    state = robot.state()
                    rows.append((tick * CONTROL_DT, backend.sim.data.ctrl[backend.sim.act_ids].copy(),
                                 state.mode.value, state.roll, state.pitch))
            traces.append(_trace(rows, seconds))
    finally:
        robot.close()
    return traces


def record_robot(robot: Robot, backend: RealBackend) -> list[Trace]:
    """Drives the robot through the script and splits its telemetry at the ticks each command took effect."""
    variant = speed_model.known_variant(backend.features.variant)
    if variant != VARIANT:
        raise RobotError(f"the simulation models the Pico ({VARIANT}), not a {variant}")
    backend.start_recording()
    robot.set_gait(Gait.TROT)
    for segment in SCRIPT:
        _command(robot, segment)
        robot.sleep(segment.seconds)
    ticks = backend.recorded()
    expected = [(segment.mode.value, speed_model.sticks_for(variant, Gait.TROT, segment.velocity)) for segment in SCRIPT]
    return split_ticks(ticks, expected, [segment.seconds for segment in SCRIPT])


def split_ticks(ticks, expected, seconds) -> list[Trace]:
    """The ticks of each segment: from the first in its (mode, sticks) after the one before, for its length."""
    starts, start = [], 0
    for mode, sticks in expected:
        start = next((i for i in range(start, len(ticks)) if ticks[i].mode == mode
                      and np.allclose(ticks[i].command[:3], [sticks.lx, sticks.ly, sticks.rx], atol=1e-4)), None)
        if start is None:
            raise RobotError(f"the robot's telemetry never showed mode {Mode(mode).name} with sticks {sticks}")
        starts.append(start)
    traces = []
    for k, (start, length) in enumerate(zip(starts, seconds)):
        t0 = ticks[start].t_us
        rows = [((tick.t_us - t0) / 1e6, to_mjcf(tick.targets), tick.mode, *np.degrees(list(tick.imu.rpy)[:2] or [0, 0]))
                for tick in ticks[start:] if tick.t_us - t0 < length * 1e6]
        held = (ticks[starts[k + 1]].t_us - t0) / 1e6 if k + 1 < len(starts) else length
        traces.append(_trace(rows, held))
    return traces


def _trace(rows, held: float) -> Trace:
    t, joints, mode, roll, pitch = zip(*rows)
    return Trace(np.array(t), np.array(joints), np.array(mode), np.array(roll), np.array(pitch), held)


# --- comparison --------------------------------------------------------------------------------------------------------
@dataclass
class Stepping:
    """One side's stepping in a segment, from its feet's lift-offs."""
    cadence_hz: float  # lift-offs per second, per foot
    foot_phase: np.ndarray  # (4,) each foot's lift-off in the cycle the front-right foot starts, 0-1
    cycle_deg: np.ndarray  # (CYCLE_BINS, 12) the joints' motion from the stand pose over a mean cycle
    first_lift_s: np.ndarray  # (4,) each foot's first lift-off


@dataclass
class SegmentReport:
    name: str
    motion_rms_deg: np.ndarray  # (12,) per joint, aligned by script time
    motion_max_deg: np.ndarray
    sim_stepping: Stepping | None  # None where the feet did not step
    robot_stepping: Stepping | None
    tilt_rms_deg: tuple[float, float]  # roll, pitch
    tilt_max_deg: tuple[float, float]
    mode_mismatch: float  # share of the ticks

    @property
    def stepped(self) -> bool:
        return self.sim_stepping is not None and self.robot_stepping is not None

    @property
    def cycle_rms_deg(self) -> np.ndarray:
        """(12,) per joint over the two mean cycles, so neither the cadence nor the phase enters."""
        return np.sqrt(np.mean((self.sim_stepping.cycle_deg - self.robot_stepping.cycle_deg) ** 2, axis=0))

    @property
    def foot_phase_diff(self) -> np.ndarray:
        """(4,) cycles, each foot's lift-off phase on the simulation less the robot's, wrapped to -0.5..0.5."""
        return (self.sim_stepping.foot_phase - self.robot_stepping.foot_phase + 0.5) % 1.0 - 0.5

    @property
    def first_lift_lag_ms(self) -> float:
        """How much later the robot's feet first lifted than the simulation's, averaged over the feet."""
        return float(np.mean(self.robot_stepping.first_lift_s - self.sim_stepping.first_lift_s)) * 1000


@dataclass
class Report:
    stand_offset_deg: np.ndarray  # (12,) the simulation's stand pose minus the robot's
    segments: list[SegmentReport]


def stand_pose(traces: list[Trace]) -> np.ndarray:
    """The joints settled in the stand segment: the mean of its last half second."""
    stand = traces[0]
    return stand.joints[stand.t >= stand.t[-1] - 0.5].mean(axis=0)


def foot_heights(joints: np.ndarray) -> np.ndarray:
    """(n, 4) each foot's height in the base frame, m, from the MJCF joints by forward kinematics."""
    return np.array([[leg_fk(leg, row[i * 3:i * 3 + 3])[2] for i, leg in enumerate(LEG_NAMES)] for row in joints])


def stepping_of(t: np.ndarray, joints: np.ndarray, stand: np.ndarray) -> Stepping | None:
    """The stepping in one side's segment; None unless every foot lifted at least twice."""
    airborne = foot_heights(joints) - foot_heights(stand[None])[0] > LIFT_M
    rising = airborne[1:] & ~airborne[:-1]
    lifts = [t[1:][rising[:, foot]] for foot in range(4)]
    if min(len(times) for times in lifts) < 2:
        return None
    starts = lifts[0]
    period = float(np.mean(np.diff(starts)))
    # Each lift-off's phase after the front-right foot's latest; a circular mean, so 0.98 and 0.02 average to 0.
    phase = []
    for times in lifts:
        latest = starts[np.clip(np.searchsorted(starts, times, side="right") - 1, 0, None)]
        phase.append(np.angle(np.exp(2j * np.pi * (times - latest) / period).mean()) / (2 * np.pi) % 1.0)
    cycles = [np.column_stack([np.interp(np.linspace(a, b, CYCLE_BINS, endpoint=False), t, joints[:, j])
                               for j in range(12)]) for a, b in zip(starts, starts[1:])]
    return Stepping(
        cadence_hz=float(np.mean([(len(times) - 1) / (times[-1] - times[0]) for times in lifts])),
        foot_phase=np.array(phase),
        cycle_deg=np.degrees(np.mean(cycles, axis=0) - stand),
        first_lift_s=np.array([times[0] for times in lifts]),
    )


def compare_segment(name: str, sim: Trace, robot: Trace, sim_stand: np.ndarray, robot_stand: np.ndarray) -> SegmentReport:
    overlap = sim.t <= min(sim.t[-1], robot.t[-1])
    t = sim.t[overlap]
    robot_joints = np.column_stack([np.interp(t, robot.t, robot.joints[:, j]) for j in range(12)])
    motion = np.degrees((sim.joints[overlap] - sim_stand) - (robot_joints - robot_stand))
    roll = sim.roll[overlap] - np.interp(t, robot.t, robot.roll)
    pitch = sim.pitch[overlap] - np.interp(t, robot.t, robot.pitch)
    robot_mode = robot.mode[np.clip(np.searchsorted(robot.t, t, side="right") - 1, 0, len(robot.t) - 1)]
    return SegmentReport(
        name=name,
        motion_rms_deg=np.sqrt(np.mean(motion ** 2, axis=0)),
        motion_max_deg=np.abs(motion).max(axis=0),
        sim_stepping=stepping_of(t, sim.joints[overlap], sim_stand),
        robot_stepping=stepping_of(t, robot_joints, robot_stand),
        tilt_rms_deg=(float(np.sqrt(np.mean(roll ** 2))), float(np.sqrt(np.mean(pitch ** 2)))),
        tilt_max_deg=(float(np.abs(roll).max()), float(np.abs(pitch).max())),
        mode_mismatch=float(np.mean(sim.mode[overlap] != robot_mode)),
    )


def compare(sim: list[Trace], robot: list[Trace]) -> Report:
    sim_stand, robot_stand = stand_pose(sim), stand_pose(robot)
    return Report(
        stand_offset_deg=np.degrees(sim_stand - robot_stand),
        segments=[compare_segment(segment.name, s, r, sim_stand, robot_stand) for segment, s, r in zip(SCRIPT, sim, robot)],
    )


def breaches(report: Report) -> list[str]:
    found = []
    worst = int(np.argmax(np.abs(report.stand_offset_deg)))
    if abs(report.stand_offset_deg[worst]) > MAX_STAND_OFFSET_DEG:
        found.append(f"stand pose: {JOINT_NAMES[worst]} differs by {report.stand_offset_deg[worst]:.1f} degrees, "
                     f"over {MAX_STAND_OFFSET_DEG}")
    for segment in report.segments:
        joint = int(np.argmax(segment.motion_rms_deg))
        if segment.motion_rms_deg[joint] > MAX_MOTION_RMS_DEG:
            found.append(f"{segment.name}: {JOINT_NAMES[joint]} moved {segment.motion_rms_deg[joint]:.1f} degrees RMS "
                         f"apart, over {MAX_MOTION_RMS_DEG}")
        if (segment.sim_stepping is None) != (segment.robot_stepping is None):
            found.append(f"{segment.name}: only the {'simulation' if segment.sim_stepping else 'robot'} stepped")
        if segment.stepped:
            joint = int(np.argmax(segment.cycle_rms_deg))
            if segment.cycle_rms_deg[joint] > MAX_CYCLE_RMS_DEG:
                found.append(f"{segment.name}: {JOINT_NAMES[joint]} {segment.cycle_rms_deg[joint]:.1f} degrees RMS "
                             f"apart over a cycle, over {MAX_CYCLE_RMS_DEG}")
            sim_rate, robot_rate = segment.sim_stepping.cadence_hz, segment.robot_stepping.cadence_hz
            if abs(sim_rate - robot_rate) > MAX_CADENCE_DIFF_HZ:
                found.append(f"{segment.name}: cadence {sim_rate:.2f} Hz against {robot_rate:.2f} Hz, "
                             f"over {MAX_CADENCE_DIFF_HZ} Hz apart")
            foot = int(np.argmax(np.abs(segment.foot_phase_diff)))
            if abs(segment.foot_phase_diff[foot]) > MAX_FOOT_PHASE_DIFF:
                found.append(f"{segment.name}: the {LEG_NAMES[foot]} foot lifts {segment.foot_phase_diff[foot]:+.2f} "
                             f"cycles apart, over {MAX_FOOT_PHASE_DIFF}")
        if max(segment.tilt_max_deg) > MAX_TILT_DIFF_DEG:
            found.append(f"{segment.name}: tilt {max(segment.tilt_max_deg):.1f} degrees apart, over {MAX_TILT_DIFF_DEG}")
        if segment.mode_mismatch > MAX_MODE_MISMATCH:
            found.append(f"{segment.name}: the modes differed in {segment.mode_mismatch:.0%} of the ticks, "
                         f"over {MAX_MODE_MISMATCH:.0%}")
    return found


def _pair(a: float, b: float) -> str:
    return f"{a:.1f}/{b:.1f}"


def print_report(report: Report) -> None:
    stepped = [segment for segment in report.segments if segment.stepped]
    print("Joints, simulation minus robot, in degrees: the stand pose; each segment's motion from it, RMS/max aligned "
          "by script time; and the RMS over a mean cycle where both stepped")
    print(f"{'joint':<10}{'stand':>7}" + "".join(f"{s.name:>12}" for s in report.segments)
          + "".join(f"{f'{s.name} cycle':>16}" for s in stepped))
    for j, joint in enumerate(JOINT_NAMES):
        aligned = "".join(f"{_pair(s.motion_rms_deg[j], s.motion_max_deg[j]):>12}" for s in report.segments)
        cycle = "".join(f"{s.cycle_rms_deg[j]:>16.1f}" for s in stepped)
        print(f"{joint:<10}{report.stand_offset_deg[j]:>7.1f}{aligned}{cycle}")
    print(f"\n{'segment':<10}{'cadence Hz sim/robot':>21}{'foot phase diff':>24}{'first lift lag ms':>19}"
          f"{'roll RMS/max':>14}{'pitch RMS/max':>15}{'mode differs':>14}")
    for s in report.segments:
        cadence = "/".join("-" if side is None else f"{side.cadence_hz:.2f}" for side in (s.sim_stepping, s.robot_stepping))
        phases = " ".join(f"{d:+.2f}" for d in s.foot_phase_diff) if s.stepped else "-"
        lag = f"{s.first_lift_lag_ms:.0f}" if s.stepped else "-"
        print(f"{s.name:<10}{cadence:>21}{phases:>24}{lag:>19}{_pair(s.tilt_rms_deg[0], s.tilt_max_deg[0]):>14}"
              f"{_pair(s.tilt_rms_deg[1], s.tilt_max_deg[1]):>15}{s.mode_mismatch:>14.0%}")


# --- running it --------------------------------------------------------------------------------------------------------
def drive(address: str) -> list[Trace]:
    """The robot's side; leaving it stops the robot and lets it rest, as the Robot facade's `with` block does."""
    backend = RealBackend(address)
    with Robot(backend=backend) as robot:
        return record_robot(robot, backend)


def drive_virtual(binary: str | None) -> list[Trace]:
    import virtual_robot

    with tempfile.TemporaryDirectory() as workdir:
        served = virtual_robot.VirtualRobot(binary or virtual_robot.build(os.path.join(workdir, "virtual_robot.exe")),
                                            VARIANT)
        try:
            return drive(f"127.0.0.1:{served.port}")
        finally:
            served.stop()


def run(robot_traces: list[Trace]) -> int:
    report = compare(record_simulation([trace.held for trace in robot_traces]), robot_traces)
    print_report(report)
    found = breaches(report)
    for breach in found:
        print(f"BREACH: {breach}")
    print("FAIL" if found else "PASS")
    return 1 if found else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("address", nargs="?", help="the robot's host name or address, e.g. 192.168.1.39")
    target.add_argument("--virtual", action="store_true", help="run the robot side on the virtual robot")
    parser.add_argument("--binary", help="with --virtual: a virtual robot built before (virtual_robot.py --build)")
    args = parser.parse_args()
    try:
        sys.exit(run(drive_virtual(args.binary) if args.virtual else drive(args.address)))
    except RobotError as error:
        print(f"FAIL: {error}")
        sys.exit(2)


if __name__ == "__main__":
    main()
