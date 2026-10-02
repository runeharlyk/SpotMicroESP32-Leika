from pathlib import Path

import pytest

from src.sim.recording import load
from src.sim.telemetry_stats import summarize

FIXTURE = Path(__file__).parent / "fixtures" / "telemetry.leika"


def test_the_fixture_timings_come_out_as_written():
    stats = summarize(load(FIXTURE))
    assert stats["period_ms_mean"] == pytest.approx(10.0)
    assert stats["imu_age_ms_p50"] == pytest.approx(3.0)
    # sensor to servo: IMU age 3 ms + compute 1.5 ms + servo write 2 ms
    assert stats["sensor_to_servo_ms_p50"] == pytest.approx(6.5)
    assert stats["command_age_ms_p50"] == pytest.approx(20.0)
    assert stats["servo_ok_fraction"] == pytest.approx(1.0)


def test_a_still_recording_gives_the_gyro_bias_and_noise():
    stats = summarize(load(FIXTURE))
    assert stats["gyro_bias_x"] == pytest.approx(0.01, abs=1e-6)
    assert stats["gyro_bias_y"] == pytest.approx(-0.02, abs=1e-6)
    assert stats["gyro_noise_std"] == pytest.approx(0.0, abs=1e-6)


def test_ticks_before_the_first_command_do_not_count_as_command_age():
    stats = summarize(load(FIXTURE))
    assert stats["commanded_ticks"] == 25  # ticks 5-19 and 30-39; batch 20-29 was lost
