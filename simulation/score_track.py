"""Scores the robot's odometry and goal moves against an overhead camera's track:
uv run python score_track.py recording.leika track.csv

The track (ground_truth.py) and the telemetry recording (record.py) run on different clocks: the camera's frames carry
the host's monotonic time or a video's own, the ticks the robot's. The ticks go onto the host's clock through the
recording's batch arrivals (by the least delay any batch arrived with), and the offset from that to the track's clock
is searched for: the body's velocity and yaw rate, from the odometry and from the track, are correlated over the start
motion, from SYNC_LEAD_S before the odometry's first move to SYNC_S after it, at every offset that keeps it inside the
track. A course should therefore open with a distinct start motion from standing, such as a quarter turn in place.

Then each stretch of odometry between resets is laid on the floor at its start (the track's and the odometry's poses
averaged over its first ALIGN_S, while the robot stands) and compared with the track at every tick the track covers:
the position error's RMS and end value, the heading error's RMS, end value and drift per metre walked (each end the
mean over the last ALIGN_S), and the ratio of the path the odometry counted to the path the track measured. Both paths
are sampled every PATH_STEP_S and summed over the steps in which the odometry moved at least WALKING_STEP_M, so that
neither counts much of the gait's sway and the camera's noise counts nothing while the robot stands. Each goal move
(from the goal statuses record.py keeps) is scored from where the track had the robot up to GOAL_LEAD_S before its
first status to where it had it from GOAL_SETTLE_S after its end, each the mean over GOAL_POSE_S: the end point
against the goal's target, in the frame the robot faced at the start, and the odometry's end point against the
track's.
"""
import argparse
import sys
from dataclasses import dataclass

import numpy as np

import ground_truth
from src.proto import message_pb2 as pb
from src.sim import recording

GRID_S = 0.01
SMOOTH_S = 0.2
SYNC_S = 10.0
SYNC_LEAD_S = 1.0
SIGNAL_SCALES = (0.05, 0.05, 0.5)  # m/s, m/s, rad/s: a walk's and a turn's typical speeds weigh alike
MOVE_M, MOVE_RAD = 0.01, np.radians(3.0)  # what counts as the odometry's first move
ALIGN_S = 0.5
MAX_GAP_S = 0.2  # the track is interpolated across no longer a gap than this
PATH_STEP_S = 0.25
WALKING_STEP_M = 0.005
MIN_DRIFT_PATH_M = 0.2  # a heading drift per metre needs at least this much walking
GOAL_LEAD_S = 0.1
GOAL_SETTLE_S = 0.5
GOAL_POSE_S = 0.5  # each end of a goal is the mean pose over this long
ENDED = {pb.GOAL_REACHED: "reached", pb.GOAL_CANCELLED: "cancelled", pb.GOAL_TIMED_OUT: "timed out"}


def wrap(angle):
    return np.angle(np.exp(1j * np.asarray(angle, dtype=float)))


def rotate(theta, vectors) -> np.ndarray:
    c, s = np.cos(theta), np.sin(theta)
    vectors = np.asarray(vectors, dtype=float)
    return np.stack([c * vectors[..., 0] - s * vectors[..., 1], s * vectors[..., 0] + c * vectors[..., 1]], axis=-1)


@dataclass(frozen=True)
class Track:
    t: np.ndarray
    x: np.ndarray
    y: np.ndarray
    heading: np.ndarray  # unwrapped

    @classmethod
    def load(cls, path) -> "Track":
        rows = ground_truth.load_track(path)
        return cls(rows[:, 0], rows[:, 1], rows[:, 2], np.unwrap(rows[:, 3]))

    def at(self, t) -> np.ndarray:
        """x, y and heading (columns) at track times t; NaN outside the track and across gaps over MAX_GAP_S."""
        t = np.atleast_1d(np.asarray(t, dtype=float))
        poses = np.column_stack([np.interp(t, self.t, v) for v in (self.x, self.y, self.heading)])
        after = np.clip(np.searchsorted(self.t, t), 1, len(self.t) - 1)
        covered = (t >= self.t[0]) & (t <= self.t[-1]) & (self.t[after] - self.t[after - 1] <= MAX_GAP_S)
        poses[~covered] = np.nan
        return poses

    def mean(self, start: float, end: float) -> np.ndarray:
        """The mean pose over the frames from start to end; NaN for none."""
        frames = (self.t >= start) & (self.t <= end)
        if not frames.any():
            return np.full(3, np.nan)
        return np.array([v[frames].mean() for v in (self.x, self.y, self.heading)])


@dataclass(frozen=True)
class Telemetry:
    host_s: np.ndarray  # each tick on the host's clock
    x: np.ndarray
    y: np.ndarray
    yaw: np.ndarray  # unwrapped
    distance: np.ndarray
    goals: dict[str, np.ndarray]  # the goal statuses, with host_s for when each arrived

    @classmethod
    def load(cls, path) -> "Telemetry":
        rec = recording.load(path)
        if rec.reboots:
            raise ValueError("the robot rebooted during the recording: score one boot at a time")
        ticks = rec.ticks
        offset_us = float(np.nanmin(rec.batch_rx_us - rec.batch_t_us))
        goals = {name: values for name, values in rec.goals.items() if name != "host_rx_us"}
        goals["host_s"] = rec.goals["host_rx_us"] / 1e6
        return cls((ticks["t_us"] + offset_us) / 1e6, ticks["odometry_x"], ticks["odometry_y"],
                   np.unwrap(ticks["odometry_yaw"]), ticks["odometry_distance"], goals)

    def at(self, host_s) -> np.ndarray:
        return np.column_stack([np.interp(host_s, self.host_s, v) for v in (self.x, self.y, self.yaw)])

    def mean(self, start: float, end: float) -> np.ndarray:
        return self.at(np.arange(start, end, GRID_S)).mean(axis=0)


def motion_signals(x, y, heading) -> np.ndarray:
    """The body's forward and leftward speed and its yaw rate (rows) for poses sampled every GRID_S, smoothed over
    SMOOTH_S: in the body's frame, so the same in the odometry's frame as on the floor, and signed, so that noise does
    not add up to a speed while the robot stands."""
    width = round(SMOOTH_S / GRID_S)
    pad = (width // 2, width - 1 - width // 2)  # the ends held, so they do not sink towards zero
    smooth = [np.convolve(np.pad(v, pad, mode="edge"), np.ones(width) / width, mode="valid") for v in (x, y, heading)]
    vx, vy, yaw_rate = (np.gradient(v, GRID_S) for v in smooth)
    forward, left = rotate(-smooth[2], np.stack([vx, vy], axis=-1)).T
    return np.vstack([forward, left, yaw_rate])


def first_move(telemetry: Telemetry) -> float:
    moved = (np.hypot(telemetry.x - telemetry.x[0], telemetry.y - telemetry.y[0]) > MOVE_M) | (
        np.abs(telemetry.yaw - telemetry.yaw[0]) > MOVE_RAD)
    if not moved.any():
        raise ValueError("the odometry never moves: there is no start motion to align the clocks on")
    return float(telemetry.host_s[np.argmax(moved)])


def clock_offset(track: Track, telemetry: Telemetry, sync_s: float = SYNC_S) -> tuple[float, float]:
    """The offset to add to host time for track time, and the correlation it reaches (1 at best)."""
    start = first_move(telemetry) - SYNC_LEAD_S
    window_t = start + np.arange(round((SYNC_LEAD_S + sync_s) / GRID_S)) * GRID_S
    window = motion_signals(*telemetry.at(window_t).T)
    track_t = np.arange(track.t[0], track.t[-1], GRID_S)
    n = len(window_t)
    if len(track_t) < n:
        raise ValueError("the track is shorter than the start motion's window")
    signals = motion_signals(*(np.interp(track_t, track.t, v) for v in (track.x, track.y, track.heading)))
    numerator = np.zeros(len(track_t) - n + 1)
    track_power, window_power = np.zeros_like(numerator), 0.0
    for w, s, scale in zip(window, signals, SIGNAL_SCALES):
        w, s = (w - w.mean()) / scale, s / scale
        numerator += np.correlate(s, w, mode="valid")
        sums, squares = (np.concatenate([[0.0], np.cumsum(v)]) for v in (s, s * s))
        track_power += (squares[n:] - squares[:-n]) - (sums[n:] - sums[:-n]) ** 2 / n
        window_power += float(w @ w)
    correlation = numerator / np.sqrt(window_power * np.maximum(track_power, 1e-12))
    best = int(np.argmax(correlation))
    shift = 0.0
    if 0 < best < len(correlation) - 1:
        before, peak, after = correlation[best - 1 : best + 2]
        shift = 0.5 * (before - after) / (before - 2 * peak + after)
    return float(track_t[0] + (best + shift) * GRID_S - window_t[0]), float(correlation[best])


@dataclass(frozen=True)
class Stretch:
    start_s: float  # host time
    seconds: float
    position_rms: float  # m
    position_end: float
    heading_rms: float  # degrees
    heading_end: float
    heading_drift: float  # degrees per metre walked, NaN under MIN_DRIFT_PATH_M
    track_path: float  # m
    odometry_path: float

    @property
    def path_scale(self) -> float:
        return self.odometry_path / self.track_path if self.track_path > 0 else float("nan")


def cumulative_path(xy: np.ndarray, walking: np.ndarray) -> np.ndarray:
    """The path up to each point over the walking steps, counting none from or to a point the track does not cover."""
    steps = np.where(walking, np.nan_to_num(np.hypot(*np.diff(xy, axis=0).T)), 0.0)
    return np.concatenate([[0.0], np.cumsum(steps)])


def score_stretch(track: Track, telemetry: Telemetry, ticks: slice, offset: float) -> Stretch | None:
    """None when the track does not see the stretch's start."""
    host = telemetry.host_s[ticks]
    truth = track.at(host + offset)
    odometry = np.column_stack([telemetry.x[ticks], telemetry.y[ticks], telemetry.yaw[ticks]])
    covered = ~np.isnan(truth[:, 0])
    still = covered & (host < host[0] + ALIGN_S)
    if not still.any():
        return None
    truth0, odometry0 = truth[still].mean(axis=0), odometry[still].mean(axis=0)
    turn = truth0[2] - odometry0[2]
    position = rotate(turn, odometry[:, :2] - odometry0[:2]) + truth0[:2]
    position_error = np.hypot(*(position - truth[:, :2])[covered].T)
    heading_error = np.degrees(wrap(odometry[covered, 2] + turn - truth[covered, 2]))
    samples = np.arange(host[0], host[-1], PATH_STEP_S)
    odometry_xy = telemetry.at(samples)[:, :2]
    walking = np.hypot(*np.diff(odometry_xy, axis=0).T) >= WALKING_STEP_M
    track_path = cumulative_path(track.at(samples + offset)[:, :2], walking)
    walked = np.interp(host[covered], samples, track_path)
    drift = np.polyfit(walked, heading_error, 1)[0] if track_path[-1] >= MIN_DRIFT_PATH_M else np.nan
    rms = lambda values: float(np.sqrt(np.mean(np.square(values))))
    last = host[covered] >= host[covered][-1] - ALIGN_S
    return Stretch(float(host[0]), float(host[-1] - host[0]), rms(position_error), float(position_error[last].mean()),
                   rms(heading_error), float(heading_error[last].mean()), float(drift), float(track_path[-1]),
                   float(cumulative_path(odometry_xy, walking)[-1]))


@dataclass(frozen=True)
class GoalScore:
    id: int
    outcome: str
    target: tuple[float, float, float]  # forward m, left m, turn degrees
    track: tuple[float, float, float]  # where the track had it end, in the frame it faced at the start
    odometry: tuple[float, float, float]  # where its odometry had it end

    @property
    def error(self) -> np.ndarray:
        """The track's end point less the target."""
        return np.subtract(self.track, self.target)

    @property
    def odometry_error(self) -> np.ndarray:
        """The odometry's end point less the track's."""
        return np.subtract(self.odometry, self.track)


def displacement(start, end) -> tuple[float, float, float]:
    forward, left = rotate(-start[2], np.asarray(end[:2]) - start[:2])
    return float(forward), float(left), float(np.degrees(wrap(end[2] - start[2])))


def score_goals(track: Track, telemetry: Telemetry, offset: float) -> list[GoalScore]:
    """Every goal the recording saw end while the track saw the robot."""
    goals = telemetry.goals
    scores = []
    for goal_id in sorted(set(goals["id"].tolist()) - {0}):
        mine = np.flatnonzero(goals["id"] == goal_id)
        ends = [i for i in mine if goals["state"][i] in ENDED]
        if not ends:
            continue
        end = ends[0]
        start_s = goals["host_s"][mine[0]] - GOAL_LEAD_S - GOAL_POSE_S
        end_s = goals["host_s"][end] + GOAL_SETTLE_S
        truth = [track.mean(at + offset, at + offset + GOAL_POSE_S) for at in (start_s, end_s)]
        if np.isnan(truth).any():
            continue
        odometry = [telemetry.mean(at, at + GOAL_POSE_S) for at in (start_s, end_s)]
        target = (float(goals["forward_m"][end]), float(goals["sideways_m"][end]), float(goals["turn_deg"][end]))
        scores.append(GoalScore(goal_id, ENDED[goals["state"][end]], target, displacement(*truth),
                                displacement(*odometry)))
    return scores


def resets(telemetry: Telemetry) -> list[slice]:
    """The stretches between odometry resets, each of which counts its distance afresh."""
    starts = [0, *(np.flatnonzero(np.diff(telemetry.distance) < -1e-4) + 1), len(telemetry.distance)]
    return [slice(a, b) for a, b in zip(starts, starts[1:]) if b - a > 1]


@dataclass(frozen=True)
class Score:
    offset: float  # track time less host time
    correlation: float
    stretches: list[Stretch]
    goals: list[GoalScore]


def score(track: Track, telemetry: Telemetry, sync_s: float = SYNC_S) -> Score:
    offset, correlation = clock_offset(track, telemetry, sync_s)
    stretches = [score_stretch(track, telemetry, ticks, offset) for ticks in resets(telemetry)]
    return Score(offset, correlation, [s for s in stretches if s], score_goals(track, telemetry, offset))


def report(result: Score) -> None:
    print(f"clock offset {result.offset:+.3f} s (track less host), correlation {result.correlation:.2f}")
    for s in result.stretches:
        print(f"odometry over {s.seconds:.1f} s: position {100 * s.position_rms:.1f} cm RMS, "
              f"{100 * s.position_end:.1f} cm at the end; heading {s.heading_rms:.1f} degrees RMS, "
              f"{s.heading_end:+.1f} at the end, drift {s.heading_drift:+.2f} degrees/m; path "
              f"{s.odometry_path:.3f} m counted for {s.track_path:.3f} m walked (x{s.path_scale:.3f})")
    for g in result.goals:
        error, odometry = g.error, g.odometry_error
        print(f"goal {g.id} ({g.outcome}) to {g.target[0]:g} m forward, {g.target[1]:g} m left, {g.target[2]:g} "
              f"degrees: ended {100 * error[0]:+.1f} cm forward, {100 * error[1]:+.1f} cm left, {error[2]:+.1f} "
              f"degrees off it; the odometry put the end {100 * odometry[0]:+.1f} cm, {100 * odometry[1]:+.1f} cm, "
              f"{odometry[2]:+.1f} degrees from the track's")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("recording", help="the .leika telemetry recording (record.py)")
    parser.add_argument("track", help="the track file (ground_truth.py)")
    parser.add_argument("--sync-s", type=float, default=SYNC_S, help="how long the start motion lasts (s)")
    args = parser.parse_args()
    report(score(Track.load(args.track), Telemetry.load(args.recording), args.sync_s))
    return 0


if __name__ == "__main__":
    sys.exit(main())
