"""score_track.py on a synthetic course: a known floor path, the odometry a robot with known errors would count along
it, written as the robot's telemetry with goal statuses, and a camera's track of the path on a clock of its own."""
import numpy as np
import pytest

import score_track
from score_track import Telemetry, Track
from src.proto import message_pb2 as pb
from src.sim.recording import RecordingWriter

TICK_S = 0.01
HOST_LESS_ROBOT_S = 3.2  # the host's clock against the robot's
LEAST_DELAY_S, MOST_DELAY_S = 0.015, 0.035  # a batch's arrival after its newest tick
START = (0.4, 0.5, 0.3)  # x, y, heading on the floor
TRACK_FPS = 30.0
TRACK_NOISE_M, TRACK_NOISE_RAD = 0.001, np.radians(0.3)


def smooth_step(n: int) -> np.ndarray:
    """A move's share done at each of n ticks, from rest to rest."""
    s = np.linspace(0.0, 1.0, n)
    return s * s * (3 - 2 * s)


def course(distance_scale: float, turn_scale: float):
    """The floor path at every tick (x, y, heading), the robot's odometry along it, and its goals: a quarter turn in
    place to start with, then goals forward, a turn and sideways, each stopped where the odometry reaches its target."""
    segments = [("stand", 2.0, None), ("turn", 3.0, np.pi / 2), ("stand", 1.0, None),
                (1, 4.0, ("forward", 0.5)), ("stand", 1.0, None), (2, 3.0, ("turn", -60.0)), ("stand", 1.0, None),
                (3, 3.0, ("sideways", 0.15)), ("stand", 2.0, None)]
    body = []  # per tick: forward, left, turn increments in the body frame
    goals = []  # (id, start tick, end tick, axis, target)
    for kind, seconds, what in segments:
        n = round(seconds / TICK_S)
        moves = np.zeros((n, 3))
        if kind == "turn":
            moves[:, 2] = np.diff(smooth_step(n + 1)) * what
        elif kind != "stand":
            axis, target = what
            # The robot stops where its odometry says the target is: the floor gets target / scale.
            truth = target / (turn_scale if axis == "turn" else distance_scale)
            column = {"forward": 0, "sideways": 1, "turn": 2}[axis]
            moves[:, column] = np.diff(smooth_step(n + 1)) * (np.radians(truth) if axis == "turn" else truth)
            goals.append((kind, len(body), len(body) + n, axis, target))
        body.extend(moves)
    body = np.array(body)
    floor = np.zeros((len(body) + 1, 3))
    odometry = np.zeros((len(body) + 1, 4))  # x, y, yaw, distance
    floor[0] = START
    for k, (forward, left, turn) in enumerate(body):
        floor[k + 1, :2] = floor[k, :2] + score_track.rotate(floor[k, 2], [forward, left])
        floor[k + 1, 2] = floor[k, 2] + turn
        step = distance_scale * np.array([forward, left])
        odometry[k + 1, :2] = odometry[k, :2] + score_track.rotate(odometry[k, 2], step)
        odometry[k + 1, 2] = odometry[k, 2] + turn_scale * turn
        odometry[k + 1, 3] = odometry[k, 3] + np.hypot(*step)
    return floor, odometry, goals


def write_recording(path, odometry, goals, rng, reset_tick=None) -> None:
    """The ticks in batches of ten from robot time 1 s, each batch arriving after a delay, and each goal's statuses."""
    if reset_tick is not None:  # from there on, the frame the robot then faced, and the distance afresh
        odometry, at = odometry.copy(), odometry[reset_tick].copy()
        odometry[reset_tick:, :2] = score_track.rotate(-at[2], odometry[reset_tick:, :2] - at[:2])
        odometry[reset_tick:, 2:] -= at[2:]
    robot_s = 1.0 + np.arange(len(odometry)) * TICK_S
    frames = []
    for first in range(0, len(odometry), 10):
        ticks = [pb.TickSample(seq=k, t_us=round(robot_s[k] * 1e6),
                               odometry=pb.OdometryData(x=odometry[k, 0], y=odometry[k, 1],
                                                        yaw=float(score_track.wrap(odometry[k, 2])),
                                                        distance=odometry[k, 3]))
                 for k in range(first, min(first + 10, len(odometry)))]
        delay = LEAST_DELAY_S if first == 0 else rng.uniform(LEAST_DELAY_S, MOST_DELAY_S)
        arrival = robot_s[first + len(ticks) - 1] + HOST_LESS_ROBOT_S + delay
        frames.append((arrival, pb.Message(telemetry_batch=pb.TelemetryBatch(batch_seq=first // 10 + 1, ticks=ticks))))
    for goal_id, start, end, axis, target in goals:
        status = dict(id=goal_id, forward_m=target if axis == "forward" else 0.0,
                      sideways_m=target if axis == "sideways" else 0.0, turn_deg=target if axis == "turn" else 0.0)
        for tick, state in ((start, pb.GOAL_RUNNING), (end, pb.GOAL_REACHED)):
            frames.append((robot_s[tick] + HOST_LESS_ROBOT_S + LEAST_DELAY_S,
                           pb.Message(goal_status=pb.GoalStatus(state=state, **status))))
    writer = RecordingWriter(path)
    for arrival, message in sorted(frames, key=lambda frame: frame[0]):
        writer.write(message.SerializeToString(), round(arrival * 1e6))
    writer.close()


def write_track(path, floor, track_less_host: float, rng) -> None:
    """The floor path seen at TRACK_FPS with the camera's noise, on a clock track_less_host ahead of the host's."""
    host_s = 1.0 + np.arange(len(floor)) * TICK_S + HOST_LESS_ROBOT_S
    t = np.arange(host_s[0] + track_less_host, host_s[-1] + track_less_host, 1 / TRACK_FPS)
    rows = np.column_stack([t] + [np.interp(t - track_less_host, host_s, floor[:, i]) for i in range(3)])
    rows[:, 1:3] += rng.normal(0.0, TRACK_NOISE_M, (len(t), 2))
    rows[:, 3] = score_track.wrap(rows[:, 3] + rng.normal(0.0, TRACK_NOISE_RAD, len(t)))
    with open(path, "w") as out:
        out.write("# synthetic; clock=video\nt,x,y,heading\n")
        np.savetxt(out, rows, delimiter=",", fmt="%.6f")


def scored(tmp_path, distance_scale, turn_scale, track_less_host, reset_tick=None):
    rng = np.random.default_rng(3)
    floor, odometry, goals = course(distance_scale, turn_scale)
    write_recording(tmp_path / "run.leika", odometry, goals, rng, reset_tick)
    write_track(tmp_path / "track.csv", floor, track_less_host, rng)
    result = score_track.score(Track.load(tmp_path / "track.csv"), Telemetry.load(tmp_path / "run.leika"))
    return result, floor, odometry


# A video's clock starts near zero, far from the host's. The ticks go onto the host's clock as though each had
# arrived with the least delay, which the offset found takes back.
@pytest.mark.parametrize("track_less_host", [-1003.21, 0.4])
def test_the_clock_offset_is_found_on_the_start_motion(tmp_path, track_less_host):
    result, _, _ = scored(tmp_path, 1.0, 1.0, track_less_host)
    assert result.offset == pytest.approx(track_less_host - LEAST_DELAY_S, abs=0.008)
    assert result.correlation > 0.9


def test_exact_odometry_scores_within_the_cameras_noise(tmp_path):
    result, _, _ = scored(tmp_path, 1.0, 1.0, 12.5)
    (stretch,) = result.stretches
    assert stretch.position_rms < 0.003 and stretch.heading_rms < 0.5
    assert stretch.path_scale == pytest.approx(1.0, abs=0.01)
    for goal in result.goals:
        assert np.abs(goal.error[:2]).max() < 0.003 and abs(goal.error[2]) < 0.4


def test_odometry_that_counts_long_and_turns_wide_is_scored_by_how_much(tmp_path):
    result, floor, odometry = scored(tmp_path, 1.1, 1.02, 12.5)
    (stretch,) = result.stretches
    assert stretch.path_scale == pytest.approx(1.1, abs=0.01)
    # The turns: +90 degrees and -60 / 1.02 on the floor, each counted 2 % wide.
    assert stretch.heading_end == pytest.approx(0.02 * (90 - 60 / 1.02), abs=0.3)
    start = floor[0]
    laid = score_track.rotate(start[2], odometry[-1, :2]) + start[:2]
    assert stretch.position_end == pytest.approx(np.hypot(*(laid - floor[-1, :2])), abs=0.004)
    forward, turn, sideways = result.goals
    assert [g.id for g in result.goals] == [1, 2, 3] and all(g.outcome == "reached" for g in result.goals)
    assert forward.error[:2] == pytest.approx([0.5 / 1.1 - 0.5, 0.0], abs=0.004) and abs(forward.error[2]) < 0.4
    assert forward.odometry_error[0] == pytest.approx(0.5 - 0.5 / 1.1, abs=0.004)
    assert turn.error[2] == pytest.approx(60 - 60 / 1.02, abs=0.4)
    assert turn.odometry_error[2] == pytest.approx(-(60 - 60 / 1.02), abs=0.4)
    assert sideways.error[:2] == pytest.approx([0.0, 0.15 / 1.1 - 0.15], abs=0.004) and abs(sideways.error[2]) < 0.4


def test_each_odometry_reset_starts_a_stretch_laid_on_the_floor_afresh(tmp_path):
    reset_tick = round(10.5 / TICK_S)  # standing between the forward goal and the turn
    result, _, _ = scored(tmp_path, 1.1, 1.0, 12.5, reset_tick)
    first, second = result.stretches
    assert second.start_s - first.start_s == pytest.approx(10.5, abs=0.02)
    # The forward goal's 4.5 cm is the first stretch's; the second starts from nothing and gains only the sideways
    # 1.4.
    assert first.position_end == pytest.approx(0.5 - 0.5 / 1.1, abs=0.004)
    assert second.position_end == pytest.approx(0.15 - 0.15 / 1.1, abs=0.004)
