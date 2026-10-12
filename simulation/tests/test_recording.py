from pathlib import Path

import numpy as np

from src.sim.recording import BODY_TO_MODEL, RecordingWriter, load, to_model_frame

FIXTURE = Path(__file__).parent / "fixtures" / "telemetry.leika"


def test_the_firmware_fixture_loads_with_its_header_and_ticks():
    rec = load(FIXTURE)
    assert rec.header.imu_driver == "MPU6050"
    assert rec.header.batch_ticks == 10
    assert len(rec.ticks["seq"]) == 30
    assert rec.ticks["angles"].shape == (30, 12)
    assert rec.ticks["accel"].shape == (30, 3)
    np.testing.assert_allclose(rec.ticks["angles"][0], 0.1 * np.arange(12), rtol=1e-6)
    assert rec.network["rssi"].tolist() == [-57]


def test_a_lost_batch_and_a_gap_marker_are_reported():
    rec = load(FIXTURE)
    assert rec.missing_batches == 1
    assert rec.missing_ticks == 10
    assert rec.link_gaps == 1


def test_a_truncated_tail_is_ignored(tmp_path):
    data = FIXTURE.read_bytes()
    cut = tmp_path / "cut.leika"
    cut.write_bytes(data[:-7])  # killed in the middle of the last record
    rec = load(cut)
    assert len(rec.ticks["seq"]) == 30
    assert len(rec.network["rssi"]) == 0


def test_the_writer_round_trips_frames_and_gaps(tmp_path):
    frames = [record for record in _records(FIXTURE) if record]
    path = tmp_path / "copy.leika"
    writer = RecordingWriter(path)
    for frame in frames[:2]:
        writer.write(frame, host_rx_us=1)
    writer.gap(host_rx_us=2)
    writer.write(frames[2], host_rx_us=3)
    writer.close()
    rec = load(path)
    assert rec.link_gaps == 1
    assert len(rec.ticks["seq"]) == 20


def test_body_vectors_map_into_the_spot_pico_model_frame():
    # Model frame: forward is -y, left is +x, up is +z.
    forward, left, up = np.eye(3)
    np.testing.assert_allclose(to_model_frame(forward), [0, -1, 0])
    np.testing.assert_allclose(to_model_frame(left), [1, 0, 0])
    np.testing.assert_allclose(to_model_frame(up), [0, 0, 1])
    assert np.isclose(np.linalg.det(BODY_TO_MODEL), 1)


def _records(path):
    from src.sim.recording import _read_records

    return [frame for _, frame in _read_records(Path(path).read_bytes())]


def _batch(batch_seq: int, first_tick: int, dropped_since_boot: int) -> bytes:
    from src.proto import message_pb2

    batch = message_pb2.TelemetryBatch(batch_seq=batch_seq, dropped_ticks=dropped_since_boot)
    for seq in range(first_tick, first_tick + 10):
        batch.ticks.add(seq=seq)
    return message_pb2.Message(telemetry_batch=batch).SerializeToString()


def _write(path, frames):
    writer = RecordingWriter(path)
    for frame in frames:
        writer.write(frame, host_rx_us=1)
    writer.close()
    return load(path)


# The robot's counters start over at a reboot: the restart is neither a run of missing ticks nor a negative one, and
# the dropped count is what dropped during the recording, not since the robot's boot.
def test_a_reboot_mid_recording_counts_neither_missing_nor_earlier_drops(tmp_path):
    rec = _write(tmp_path / "reboot.leika", [
        _batch(500, 5000, dropped_since_boot=40),
        _batch(502, 5020, dropped_since_boot=43),  # batch 501 lost
        _batch(0, 0, dropped_since_boot=0),  # rebooted
        _batch(1, 10, dropped_since_boot=2),
    ])
    assert rec.reboots == 1
    assert rec.missing_batches == 1
    assert rec.missing_ticks == 10
    assert rec.dropped_ticks == 3 + 2


def test_a_recording_without_ticks_loads_and_summarizes(tmp_path):
    from src.sim.telemetry_stats import summarize

    path = tmp_path / "empty.leika"
    RecordingWriter(path).close()
    stats = summarize(load(path))
    assert stats["ticks"] == 0
    assert stats["dropped_ticks"] == 0


def test_each_ticks_odometry_and_each_goal_status_load_with_their_arrival(tmp_path):
    from src.proto import message_pb2 as pb

    path = tmp_path / "goal.leika"
    writer = RecordingWriter(path)
    ticks = [pb.TickSample(seq=k, t_us=10_000 * k, odometry=pb.OdometryData(x=0.01 * k, yaw=-0.02 * k, distance=0.01 * k))
             for k in range(3)]
    writer.write(pb.Message(telemetry_batch=pb.TelemetryBatch(batch_seq=1, ticks=ticks)).SerializeToString(), 100)
    for host_rx_us, state in ((150, pb.GOAL_RUNNING), (900, pb.GOAL_REACHED)):
        status = pb.GoalStatus(id=4, state=state, forward_m=0.5, progress=0.5 if state == pb.GOAL_REACHED else 0.0)
        writer.write(pb.Message(goal_status=status).SerializeToString(), host_rx_us)
    writer.close()
    rec = load(path)
    np.testing.assert_allclose(rec.ticks["odometry_x"], [0.0, 0.01, 0.02], rtol=1e-6)
    np.testing.assert_allclose(rec.ticks["odometry_yaw"], [0.0, -0.02, -0.04], rtol=1e-6)
    assert rec.goals["host_rx_us"].tolist() == [150, 900]
    assert rec.goals["state"].tolist() == [pb.GOAL_RUNNING, pb.GOAL_REACHED]
    assert rec.goals["id"].tolist() == [4, 4]
    np.testing.assert_allclose(rec.goals["forward_m"], [0.5, 0.5])
