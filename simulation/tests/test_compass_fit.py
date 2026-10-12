import numpy as np
import pytest

from src.proto import message_pb2
from src.sim.compass_fit import fit_recording, fit_sphere
from src.sim.recording import RecordingWriter, load

PICO_MOUNTING = np.array([[0, 1, 0], [-1, 0, 0], [0, 0, 1]], dtype=float)  # chip turned 90 degrees about z
TRUE_OFFSET = np.array([60.0, -85.0, 30.0])  # a hard iron like the Pico's 128 uT reading
EARTH = 48.0


def _directions(n, rng, full=True):
    v = rng.normal(size=(n, 3))
    if not full:
        v[:, 2] = 0.05 * v[:, 2]  # turned about the vertical only
    return v / np.linalg.norm(v, axis=1, keepdims=True)


def _recording(path, raw, old_offset, mounting=PICO_MOUNTING, repeat=1):
    header = message_pb2.TelemetryHeader(batch_ticks=10)
    header.imu_settings.mounting[:] = mounting.flatten().tolist()
    header.imu_settings.mag_offset[:] = old_offset.tolist()
    body = (raw - old_offset) @ mounting.T
    writer = RecordingWriter(path)
    writer.write(message_pb2.Message(telemetry_header=header).SerializeToString(), host_rx_us=0)
    seq = 0
    for start in range(0, len(body), 10):
        batch = message_pb2.TelemetryBatch(batch_seq=start // 10)
        for i in range(start, min(start + 10, len(body))):
            for _ in range(repeat):  # the compass is slower than the control loop: a reading repeats over ticks
                tick = batch.ticks.add(seq=seq)
                seq += 1
                tick.imu.valid = 4 | 1
                tick.imu.mag[:] = body[i].tolist()
                tick.imu.mag_t_us = 1000 + i
        writer.write(message_pb2.Message(telemetry_batch=batch).SerializeToString(), host_rx_us=1)
    writer.close()
    return load(path)


# The recording holds the field after the old calibration and the mounting; the fit must undo both, not fit on top.
def test_the_offset_is_found_through_the_mounting_and_an_old_offset(tmp_path):
    rng = np.random.default_rng(1)
    raw = TRUE_OFFSET + EARTH * _directions(400, rng) + rng.normal(scale=0.3, size=(400, 3))
    fit = fit_recording(_recording(tmp_path / "turned.leika", raw, old_offset=np.array([5.0, 5.0, -5.0])))
    np.testing.assert_allclose(fit.offset, TRUE_OFFSET, atol=0.2)
    assert fit.radius == pytest.approx(EARTH, abs=0.2)
    assert fit.residual < 0.5
    assert fit.covered


def test_a_reading_repeated_over_ticks_counts_once(tmp_path):
    rng = np.random.default_rng(2)
    raw = TRUE_OFFSET + EARTH * _directions(100, rng)
    fit = fit_recording(_recording(tmp_path / "slow.leika", raw, old_offset=np.zeros(3), repeat=3))
    assert fit.samples == 100


# Turned only about the vertical, the readings lie near a circle: the centre along z is a guess, which must show.
def test_a_robot_turned_only_about_the_vertical_is_not_covered():
    rng = np.random.default_rng(3)
    fit = fit_sphere(TRUE_OFFSET + EARTH * _directions(400, rng, full=False))
    assert not fit.covered


def test_too_few_samples_are_refused():
    with pytest.raises(ValueError):
        fit_sphere(np.ones((5, 3)))
