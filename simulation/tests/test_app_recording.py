import json
from pathlib import Path

import pytest

from src.sim.recording import load
from src.sim.telemetry_stats import summarize

# Both written by the app's recording writer and its port of summarize (app/tests/unit/leika-recording.spec.ts).
FIXTURE = Path(__file__).parent / "fixtures" / "app-recording.leika"
APP_STATS = json.loads((Path(__file__).parent / "fixtures" / "app-recording.stats.json").read_text())


def test_a_recording_made_by_the_app_loads():
    rec = load(FIXTURE)
    assert rec.header.firmware_version == "app-fixture"
    assert rec.header.imu_rate_hz == 200
    assert len(rec.ticks["seq"]) == 40
    assert len(rec.network["rssi"]) == 1
    assert rec.link_gaps == 1
    assert rec.missing_batches == 1


def test_the_app_summarizes_a_recording_as_the_report_does():
    stats = summarize(load(FIXTURE))
    assert stats.keys() == APP_STATS.keys()
    for name, value in APP_STATS.items():
        assert stats[name] == pytest.approx(value, rel=1e-9, abs=1e-12), name
