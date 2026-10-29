import re
import sys
from pathlib import Path

import pytest

import report_recording
from src.proto import message_pb2 as pb
from src.sim.recording import RecordingWriter, load
from src.sim.telemetry_stats import profile_by_mode, summarize

FIXTURE = Path(__file__).parent / "fixtures" / "telemetry.leika"


def test_the_fixture_timings_come_out_as_written():
    stats = summarize(load(FIXTURE))
    assert stats["period_ms_mean"] == pytest.approx(10.0)
    assert stats["imu_age_ms_p50"] == pytest.approx(3.0)
    # sensor to servo: IMU age 3 ms + compute 1.5 ms + servo write 2 ms
    assert stats["sensor_to_servo_ms_p50"] == pytest.approx(6.5)
    assert stats["command_age_ms_p50"] == pytest.approx(20.0)
    assert stats["servo_ok_fraction"] == pytest.approx(1.0)


def test_the_fixture_tick_parts_come_out_as_written():
    stats = summarize(load(FIXTURE))
    assert stats["motion_ms_mean"] == pytest.approx(1.2)
    assert stats["motion_step_ms_p50"] == pytest.approx(0.3)
    assert stats["odometry_ms_max"] == pytest.approx(0.1)
    assert stats["kinematics_ms_p99"] == pytest.approx(0.5)
    assert stats["goal_ms_mean"] == pytest.approx(0.2 / 3)  # only ticks 30-39 of the 30 recorded drove a goal
    assert stats["goal_ms_max"] == pytest.approx(0.2)
    assert stats["servo_bus_wait_ms_mean"] == pytest.approx(0.06)  # the last tick of each batch waited 0.6 ms
    assert stats["servo_bus_wait_ms_max"] == pytest.approx(0.6)
    assert stats["servo_transfer_ms_p50"] == pytest.approx(1.1)


def test_a_still_recording_gives_the_gyro_bias_and_noise():
    stats = summarize(load(FIXTURE))
    assert stats["gyro_bias_x"] == pytest.approx(0.01, abs=1e-6)
    assert stats["gyro_bias_y"] == pytest.approx(-0.02, abs=1e-6)
    assert stats["gyro_noise_std"] == pytest.approx(0.0, abs=1e-6)


def test_ticks_before_the_first_command_do_not_count_as_command_age():
    stats = summarize(load(FIXTURE))
    assert stats["commanded_ticks"] == 25  # ticks 5-19 and 30-39; batch 20-29 was lost


def _recording_of(path, ticks):
    writer = RecordingWriter(path)
    for first in range(0, len(ticks), 10):
        batch = pb.TelemetryBatch(batch_seq=first // 10, ticks=ticks[first:first + 10])
        writer.write(pb.Message(telemetry_batch=batch).SerializeToString(), host_rx_us=first)
    writer.close()
    return load(path)


# Stand, then walk: the walk's ticks run longer, every tenth comes late, one write waited 5 ms for the bus and one
# tick made no write at all.
def _stand_then_walk(k: int) -> pb.TickSample:
    walking = k >= 100
    late = walking and k % 10 == 0
    return pb.TickSample(
        seq=k, t_us=10_000 * k, period_us=0 if k == 0 else 12_000 if late else 10_000,
        mode=pb.WALK if walking else pb.STAND,
        compute_us=1_600 if walking else 600, motion_us=1_500 if walking else 500,
        motion_step_us=300 if walking else 100, odometry_us=200 if walking else 100, goal_us=0,
        kinematics_us=900 if walking else 250,
        servo_write_us=0 if k == 150 else 6_300 if k == 120 else 1_300,
        servo_bus_wait_us=5_000 if k == 120 else 0, servo_transfer_us=0 if k == 150 else 1_100)


def test_each_mode_profiles_its_own_ticks_and_the_servo_parts_count_only_writes(tmp_path):
    profile = profile_by_mode(_recording_of(tmp_path / "modes.leika", [_stand_then_walk(k) for k in range(200)]))
    assert list(profile) == ["STAND", "WALK"]
    stand, walk = profile["STAND"], profile["WALK"]
    assert stand["ticks"] == walk["ticks"] == 100
    assert stand["late_fraction"] == 0.0
    assert walk["late_fraction"] == pytest.approx(0.1)
    assert walk["period_ms_max"] == pytest.approx(12.0)
    assert stand["compute_ms_mean"] == pytest.approx(0.6)
    assert walk["compute_ms_mean"] == pytest.approx(1.6)
    assert walk["kinematics_ms_max"] == pytest.approx(0.9)
    assert stand["motion_step_ms_mean"] == pytest.approx(0.1)
    assert walk["goal_ms_max"] == 0.0
    # 99 walking ticks wrote; the one without a write is no zero-length wait or transfer.
    assert walk["servo_bus_wait_ms_mean"] == pytest.approx(5.0 / 99)
    assert walk["servo_bus_wait_ms_max"] == pytest.approx(5.0)
    assert walk["servo_transfer_ms_mean"] == pytest.approx(1.1)
    assert walk["servo_write_ms_max"] == pytest.approx(6.3)
    assert stand["servo_bus_wait_ms_max"] == 0.0


def test_the_report_prints_each_modes_profile(tmp_path, capsys, monkeypatch):
    path = tmp_path / "modes.leika"
    _recording_of(path, [_stand_then_walk(k) for k in range(200)])
    monkeypatch.setattr(sys, "argv", ["report_recording.py", str(path)])
    report_recording.main()
    out = capsys.readouterr().out
    walk = out[out.index("WALK: 100 ticks, 10.0% more than 10.5 ms apart"):]
    assert re.search(r"servo_bus_wait\s+0\.051\s+\S+\s+5\.000", walk)
    assert re.search(r"kinematics\s+0\.900\s+0\.900\s+0\.900", walk)
