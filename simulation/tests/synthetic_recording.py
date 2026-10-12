"""Synthetic .leika recordings: the firmware gait walked in the Pico model at known servo parameters and written as the
robot writes its telemetry, with a known gyro bias and IMU noise.

The physics loop is written independently of sysid.Replay: the servo write and the actuator lag fall on physics step
boundaries, so holding each written angle from the step its lag ends at is exact, and the IMU is read at a step
boundary without interpolation.
"""
import mujoco
import numpy as np

from src.proto import message_pb2 as pb
from src.robot.firmware_gait import BodyState, GaitController, GaitState, TROT, analytic_gait_action, set_mode
from src.sim.mj_runtime import CONTROL_DT, SpotPicoSim
from src.sim.recording import BODY_TO_MODEL, RecordingWriter, from_model_joints

TICK_US = 10_000
COMPUTE_US = 1_000
SERVO_WRITE_US = 1_000
IMU_AGE_US = 4_000
BATCH_TICKS = 10
PRE_SETTLE_S = 1.0  # the robot stood before the recording began


def joint_commands(stand_s: float, walk_s: float, command) -> np.ndarray:
    sim = SpotPicoSim()
    stand = [sim.stand_pose] * round(stand_s / CONTROL_DT)
    gait, controller, body = GaitState(), GaitController(), BodyState()
    set_mode(gait, TROT)
    walk = []
    for _ in range(round(walk_s / CONTROL_DT)):
        analytic_gait_action(np.asarray(command, dtype=float), gait)
        controller.step(gait, CONTROL_DT)
        controller.generate_feet(gait, body)
        walk.append(sim.body_targets_from_feet(body))
    return np.array(stand + walk)


def write(path, *, volts, strength, lag_ms, gyro_bias, gyro_noise, tilt_noise_deg, stand_s, walk_s,
          command=(0.06, 0.0, 0.4), seed=0, variant="SPOTMICRO_ESP32_MINI"):
    rng = np.random.default_rng(seed)
    ctrl = joint_commands(stand_s, walk_s, command)
    sim = SpotPicoSim()
    h_us = round(sim.model.opt.timestep * 1e6)
    lag_us = round(lag_ms * 1000)
    assert (COMPUTE_US + SERVO_WRITE_US + lag_us) % h_us == 0 and IMU_AGE_US % h_us == 0
    sim.reset_to_stand()
    sim.servo.set_supply(volts, strength)
    sim.set_joint_targets(ctrl[0])
    sim.step_physics(round(PRE_SETTLE_S * 1e6 / h_us))

    t0 = 1_000_000
    acts_us = t0 + np.arange(len(ctrl)) * TICK_US + COMPUTE_US + SERVO_WRITE_US + lag_us
    imu_us = t0 + np.arange(len(ctrl)) * TICK_US - IMU_AGE_US
    now_us = t0 - IMU_AGE_US - h_us  # the sim's clock: t0 - IMU_AGE_US is the first sample, read after one step
    gyro, gravity = [], []
    for k in range(len(ctrl)):
        while now_us < imu_us[k]:
            written = np.searchsorted(acts_us, now_us, side="right") - 1
            sim.set_joint_targets(ctrl[max(written, 0)])
            sim.step_physics(1)
            now_us += h_us
        gyro.append(sim.gyro())
        gravity.append(-sim.data.xmat[sim.base_id, 6:9])

    to_body = lambda model_vectors: np.asarray(model_vectors) @ BODY_TO_MODEL
    tilt = np.radians(tilt_noise_deg)
    gyro = to_body(gyro) + np.asarray(gyro_bias) + rng.normal(0.0, gyro_noise, (len(ctrl), 3))
    gravity = to_body(gravity)
    rpy = np.column_stack([np.arctan2(-gravity[:, 1], -gravity[:, 2]), np.arcsin(np.clip(gravity[:, 0], -1, 1)),
                           np.zeros(len(ctrl))]) + rng.normal(0.0, tilt, (len(ctrl), 3))
    gravity = gravity + rng.normal(0.0, tilt, (len(ctrl), 3))
    angles = from_model_joints(ctrl)

    writer = RecordingWriter(path)
    header = pb.TelemetryHeader(variant=variant, imu_driver="synthetic", control_rate_hz=100, batch_ticks=BATCH_TICKS)
    writer.write(pb.Message(telemetry_header=header).SerializeToString(), host_rx_us=0)
    for batch_seq, first in enumerate(range(0, len(ctrl), BATCH_TICKS)):
        batch = pb.TelemetryBatch(batch_seq=batch_seq)
        for k in range(first, min(first + BATCH_TICKS, len(ctrl))):
            tick = batch.ticks.add(seq=k, t_us=t0 + k * TICK_US, period_us=TICK_US if k else 0, compute_us=COMPUTE_US,
                                   servo_write_us=SERVO_WRITE_US, servo_ok=True)
            tick.angles.extend(angles[k])
            tick.targets.extend(angles[k])
            tick.imu.t_us = int(imu_us[k])
            tick.imu.valid = 1 | 2 | 8
            tick.imu.gyro.extend(gyro[k])
            tick.imu.gravity.extend(gravity[k])
            tick.imu.rpy.extend(rpy[k])
        writer.write(pb.Message(telemetry_batch=batch).SerializeToString(), host_rx_us=batch_seq)
    writer.close()
