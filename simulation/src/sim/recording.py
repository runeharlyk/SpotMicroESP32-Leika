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

_TICK_SCALARS = ("seq", "t_us", "period_us", "compute_us", "servo_write_us", "servo_ok", "command_age_us",
                 "command_rx_us", "mode", "gait", "link_lost")
_TICK_VECTORS = ("angles", "targets", "pwm", "command")
_IMU_SCALARS = ("t_us", "mag_t_us", "temperature", "valid")
_IMU_VECTORS = ("accel", "gyro", "mag", "gravity", "quat", "rpy")
_NETWORK = ("t_us", "rssi", "channel", "batches_sent", "batches_failed", "ticks_dropped")


def to_model_frame(vectors) -> np.ndarray:
    return np.asarray(vectors) @ BODY_TO_MODEL.T


@dataclass
class Recording:
    header: message_pb2.TelemetryHeader | None
    ticks: dict[str, np.ndarray]
    network: dict[str, np.ndarray]
    batch_rx_us: np.ndarray
    link_gaps: int = 0
    missing_batches: int = 0
    missing_ticks: int = 0
    dropped_ticks: int = 0


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
    batch_rx_us, batch_seqs = [], []
    link_gaps = dropped = 0
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
            dropped = batch.dropped_ticks
            for tick in batch.ticks:
                for name in _TICK_SCALARS:
                    ticks[name].append(getattr(tick, name))
                for name in _TICK_VECTORS:
                    ticks[name].append(list(getattr(tick, name)))
                for name in _IMU_SCALARS:
                    imu[name].append(getattr(tick.imu, name))
                for name in _IMU_VECTORS:
                    imu[name].append(list(getattr(tick.imu, name)))
        elif kind == "telemetry_network":
            for name in _NETWORK:
                network[name].append(getattr(message.telemetry_network, name))
    arrays = {name: np.asarray(values) for name, values in ticks.items()}
    arrays.update({("imu_" + name if name in _IMU_SCALARS else name): np.asarray(values)
                   for name, values in imu.items()})
    seq = arrays["seq"].astype(np.int64)
    batches = np.asarray(batch_seqs, dtype=np.int64)
    return Recording(
        header=header,
        ticks=arrays,
        network={name: np.asarray(values) for name, values in network.items()},
        batch_rx_us=np.asarray(batch_rx_us),
        link_gaps=link_gaps,
        missing_batches=int(np.sum(np.diff(batches) - 1)) if len(batches) > 1 else 0,
        missing_ticks=int(np.sum(np.diff(seq) - 1)) if len(seq) > 1 else 0,
        dropped_ticks=int(dropped),
    )
