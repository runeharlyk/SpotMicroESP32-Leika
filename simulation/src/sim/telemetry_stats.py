"""Statistics of a telemetry recording, in the units domain_rand.py uses."""
import numpy as np

from src.sim.recording import Recording


def _percentiles(name: str, values_ms: np.ndarray, stats: dict) -> None:
    if len(values_ms) == 0:
        return
    stats[f"{name}_ms_mean"] = float(np.mean(values_ms))
    stats[f"{name}_ms_p50"] = float(np.percentile(values_ms, 50))
    stats[f"{name}_ms_p99"] = float(np.percentile(values_ms, 99))
    stats[f"{name}_ms_max"] = float(np.max(values_ms))


def summarize(rec: Recording) -> dict[str, float]:
    t = rec.ticks
    stats: dict[str, float] = {"ticks": len(t["seq"]), "missing_ticks": rec.missing_ticks,
                               "dropped_ticks": rec.dropped_ticks, "link_gaps": rec.link_gaps}
    periods = t["period_us"][t["period_us"] > 0] / 1000.0
    _percentiles("period", periods, stats)
    if len(periods):
        stats["period_ms_std"] = float(np.std(periods))
    _percentiles("compute", t["compute_us"] / 1000.0, stats)
    written = t["servo_write_us"] > 0
    _percentiles("servo_write", t["servo_write_us"][written] / 1000.0, stats)
    stats["servo_ok_fraction"] = float(np.mean(t["servo_ok"][written])) if written.any() else 0.0
    has_imu = (t["imu_valid"] & 1) != 0
    imu_age = (t["t_us"] - t["imu_t_us"])[has_imu] / 1000.0
    _percentiles("imu_age", imu_age, stats)
    _percentiles("sensor_to_servo", imu_age + (t["compute_us"] + t["servo_write_us"])[has_imu] / 1000.0, stats)
    commanded = t["command_rx_us"] > 0
    stats["commanded_ticks"] = int(np.sum(commanded))
    _percentiles("command_age", t["command_age_us"][commanded] / 1000.0, stats)
    if has_imu.any():
        gyro = t["gyro"][has_imu]
        for axis, name in enumerate("xyz"):
            stats[f"gyro_bias_{name}"] = float(np.mean(gyro[:, axis]))
        stats["gyro_noise_std"] = float(np.mean(np.std(gyro, axis=0)))
        stats["rpy_noise_deg"] = float(np.degrees(np.mean(np.std(t["rpy"][has_imu][:, :2], axis=0))))
    return stats
