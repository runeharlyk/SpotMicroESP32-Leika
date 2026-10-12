"""Telemetry recordings from the robot (.leika files) as NumPy arrays.

Format: b"LEIKAREC", uint16 version 1, then records of uint64 host receive time (us), uint32 length and one encoded
socket_message.Message; a record of length 0 marks a gap where the link dropped. All integers are little-endian.
Vectors arrive in the robot's body frame (x forward, y left, z up); to_model_frame turns them into the spot_pico
MJCF's base frame, whose forward is -y. Times are the robot's esp_timer microseconds unless named host_*.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from src.proto import message_pb2

MAGIC = b"LEIKAREC"
VERSION = 1
_RECORD = struct.Struct("<QI")

# Rows give the model axes in body coordinates: model x = body y (left), model y = -body x, model z = body z.
BODY_TO_MODEL = np.array([[0.0, 1.0, 0.0], [-1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])

# The MINI firmware's joint angles (legs fl, fr, rl, rr) to the spot_pico MJCF joints: sign * angle + offset, as
# fit_pico_joint_map.py fitted them for the web app (app/src/lib/simulation/robots.ts). Linear in the tibia, so the
# four-bar linkage is approximated over the gait's range; the feet follow the firmware's to about 1.3 mm.
PICO_JOINT_SIGN = np.array([-1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1], dtype=float)
PICO_JOINT_OFFSET = np.radians([7.3, 54.6, 101.2, 6.0, 52.9, -99.4, 5.8, 54.4, 100.4, 6.7, 50.6, -98.1])
FIRMWARE_LEGS_IN_MODEL_ORDER = [1, 0, 3, 2]  # the model's joints run fr, fl, rr, rl

_TICK_SCALARS = ("seq", "t_us", "period_us", "compute_us", "servo_write_us", "servo_ok", "command_age_us",
                 "command_rx_us", "mode", "gait", "link_lost")
_TICK_VECTORS = ("angles", "targets", "pwm", "command")
_IMU_SCALARS = ("t_us", "mag_t_us", "temperature", "valid")
_IMU_VECTORS = ("accel", "gyro", "mag", "gravity", "quat", "rpy")
_NETWORK = ("t_us", "rssi", "channel", "batches_sent", "batches_failed", "ticks_dropped", "rail_voltage",
            "rail_voltage_min", "rail_current")
_ODOMETRY = ("x", "y", "yaw", "distance", "kinematic_yaw")
_GOAL = ("id", "state", "forward_m", "sideways_m", "turn_deg", "progress", "timeout_s")


def to_model_frame(vectors) -> np.ndarray:
    return np.asarray(vectors) @ BODY_TO_MODEL.T


def to_model_joints(angles) -> np.ndarray:
    """The MINI firmware's 12 joint angles (rad, per row) as the spot_pico model's actuator targets (rad)."""
    mjcf = np.asarray(angles) * PICO_JOINT_SIGN + PICO_JOINT_OFFSET
    legs = mjcf.reshape(*mjcf.shape[:-1], 4, 3)[..., FIRMWARE_LEGS_IN_MODEL_ORDER, :]
    return legs.reshape(mjcf.shape)


def from_model_joints(targets) -> np.ndarray:
    """to_model_joints inverted: the spot_pico model's actuator targets (rad) as the MINI firmware's joint angles."""
    mjcf = np.asarray(targets, dtype=float)
    legs = mjcf.reshape(*mjcf.shape[:-1], 4, 3)[..., FIRMWARE_LEGS_IN_MODEL_ORDER, :]  # the swap is its own inverse
    return (legs.reshape(mjcf.shape) - PICO_JOINT_OFFSET) * PICO_JOINT_SIGN


@dataclass
class Recording:
    header: message_pb2.TelemetryHeader | None
    ticks: dict[str, np.ndarray]
    network: dict[str, np.ndarray]
    batch_rx_us: np.ndarray
    batch_t_us: np.ndarray  # each batch's newest tick, NaN for none
    # Each goal status as it arrived (host_rx_us, and GoalStatus's fields): the robot stamps none with its own clock.
    goals: dict[str, np.ndarray]
    link_gaps: int = 0
    missing_batches: int = 0
    missing_ticks: int = 0
    dropped_ticks: int = 0
    reboots: int = 0


class RecordingWriter:
    """Appends frames as they arrive, so an interrupted recording keeps everything written before."""

    def __init__(self, path):
        self._file = open(path, "wb")
        self._file.write(MAGIC + struct.pack("<H", VERSION))

    def write(self, frame: bytes, host_rx_us: int) -> None:
        self._file.write(_RECORD.pack(host_rx_us, len(frame)) + frame)
        self._file.flush()

    def gap(self, host_rx_us: int) -> None:
        self._file.write(_RECORD.pack(host_rx_us, 0))
        self._file.flush()

    def close(self) -> None:
        self._file.close()


def _read_records(data: bytes):
    if data[: len(MAGIC)] != MAGIC:
        raise ValueError("not a Leika recording")
    (version,) = struct.unpack_from("<H", data, len(MAGIC))
    if version != VERSION:
        raise ValueError(f"recording version {version}, expected {VERSION}")
    offset = len(MAGIC) + 2
    while offset + _RECORD.size <= len(data):
        host_rx_us, length = _RECORD.unpack_from(data, offset)
        start = offset + _RECORD.size
        if start + length > len(data):
            return  # the recorder stopped in the middle of this record
        yield host_rx_us, data[start : start + length]
        offset = start + length


def load(path) -> Recording:
    header = None
    ticks = {name: [] for name in (*_TICK_SCALARS, *_TICK_VECTORS)}
    imu = {name: [] for name in (*_IMU_SCALARS, *_IMU_VECTORS)}
    network = {name: [] for name in _NETWORK}
    odometry = {name: [] for name in _ODOMETRY}
    goals = {name: [] for name in ("host_rx_us", *_GOAL)}
    batch_rx_us, batch_t_us, batch_seqs, batch_dropped = [], [], [], []
    link_gaps = 0
    for host_rx_us, frame in _read_records(Path(path).read_bytes()):
        if not frame:
            link_gaps += 1
            continue
        message = message_pb2.Message.FromString(frame)
        kind = message.WhichOneof("message")
        if kind == "telemetry_header":
            header = message.telemetry_header
        elif kind == "telemetry_batch":
            batch = message.telemetry_batch
            batch_seqs.append(batch.batch_seq)
            batch_rx_us.append(host_rx_us)
            batch_t_us.append(batch.ticks[-1].t_us if batch.ticks else np.nan)
            batch_dropped.append(batch.dropped_ticks)
            for tick in batch.ticks:
                for name in _TICK_SCALARS:
                    ticks[name].append(getattr(tick, name))
                for name in _TICK_VECTORS:
                    ticks[name].append(list(getattr(tick, name)))
                for name in _IMU_SCALARS:
                    imu[name].append(getattr(tick.imu, name))
                for name in _IMU_VECTORS:
                    imu[name].append(list(getattr(tick.imu, name)))
                for name in _ODOMETRY:
                    odometry[name].append(getattr(tick.odometry, name))
        elif kind == "telemetry_network":
            for name in _NETWORK:
                network[name].append(getattr(message.telemetry_network, name))
        elif kind == "goal_status":
            goals["host_rx_us"].append(host_rx_us)
            for name in _GOAL:
                goals[name].append(getattr(message.goal_status, name))
    arrays = {name: np.asarray(values) for name, values in ticks.items()}
    arrays.update({("imu_" + name if name in _IMU_SCALARS else name): np.asarray(values)
                   for name, values in imu.items()})
    arrays.update({"odometry_" + name: np.asarray(values) for name, values in odometry.items()})
    seq = arrays["seq"].astype(np.int64)
    batches = np.asarray(batch_seqs, dtype=np.int64)
    return Recording(
        header=header,
        ticks=arrays,
        network={name: np.asarray(values) for name, values in network.items()},
        batch_rx_us=np.asarray(batch_rx_us),
        batch_t_us=np.asarray(batch_t_us, dtype=float),
        goals={name: np.asarray(values) for name, values in goals.items()},
        link_gaps=link_gaps,
        missing_batches=_missing(batches),
        missing_ticks=_missing(seq),
        dropped_ticks=_dropped_while_recording(batches, np.asarray(batch_dropped, dtype=np.int64)),
        reboots=int(np.sum(np.diff(batches) <= 0)),
    )


def _missing(sequence: np.ndarray) -> int:
    """Numbers skipped by a counter; a counter that starts over after a reboot skips nothing."""
    steps = np.diff(sequence)
    return int(np.sum(steps[steps > 0] - 1))


def _dropped_while_recording(batch_seqs: np.ndarray, dropped_since_boot: np.ndarray) -> int:
    """The robot counts dropped ticks since its boot: the recording's share is what the count grew by, plus the
    whole count of every boot that began during the recording."""
    if len(dropped_since_boot) == 0:
        return 0
    boots = np.flatnonzero(np.diff(batch_seqs) <= 0) + 1
    total = 0
    for i, segment in enumerate(np.split(dropped_since_boot, boots)):
        total += int(segment[-1] - (segment[0] if i == 0 else 0))
    return total
