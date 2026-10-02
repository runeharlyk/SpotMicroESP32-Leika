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
