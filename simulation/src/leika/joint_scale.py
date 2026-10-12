"""The servos' PWM counts per degree from the body tilt a known PWM step gives, through the firmware's kinematics.

The firmware writes a joint at (direction * angle + centre angle) * pwm_per_degree + centre PWM (servo_output.h), with
one pwm_per_degree per variant (joint_model.h). A step of n counts on a joint turns it n / s degrees, s the servos'
true counts per degree, whatever the firmware assumes. With the robot standing on four feet, the same step on both hips
of one side lifts (or lowers) both feet of that side alike, so the feet stay in one plane and the body rolls on the
floor by an angle the kinematics give for every s: the measured roll against the predicted one fits s.

The hips, because they drive their legs directly and tilt the body most: about 0.25 degrees of roll per degree of hip
on the MINI, as much as the knees, whose servo reaches the Pico's tibia through a four-bar linkage of about 1.6 times
gain (src/resources/spot_pico/linkage.py) that would fold into s. The femurs mostly swing the feet fore and aft, at a
tenth of that tilt.

Angles here are degrees; the body frame is the firmware's (x forward, y up, z left), the tilt REP-103's roll and pitch.
"""
from dataclasses import dataclass

import numpy as np
from scipy.optimize import least_squares

from .speed_model import GEOMETRY, KNEE_FOLLOWS_FEMUR

# The sign MotionService turns each IK angle into servo space with (joint_model.h).
JOINT_DIRECTION = np.array([1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1], dtype=float)
LEFT_HIPS = (0, 6)  # front left and rear left, in IK order (leg * 3 + hip, femur, knee)
PWM_MIN, PWM_MAX = 125, 600  # servo_output.h


@dataclass(frozen=True)
class ServoSetup:
    """What the robot's servo settings report: its calibration and its variant's joint model, in joint order."""
    center_pwm: tuple[float, ...]
    channels: tuple[int, ...]  # the PCA9685 channel of each joint
    direction: tuple[float, ...]
    center_angle: tuple[float, ...]
    pwm_per_degree: float

    def pwm(self, servo_angles) -> np.ndarray:
        """servo_output.h's servoPwm: the PWM the firmware writes for each joint's angle in servo space."""
        servo_degrees = np.asarray(self.direction) * servo_angles + np.asarray(self.center_angle)
        return np.floor(np.clip(servo_degrees * self.pwm_per_degree + np.asarray(self.center_pwm), PWM_MIN, PWM_MAX))

    def servo_angles(self, pwm, scale: float) -> np.ndarray:
        """The angle in servo space each joint goes to at these PWMs, for servos that turn one degree per `scale`
        counts."""
        return np.asarray(self.direction) * ((np.asarray(pwm) - np.asarray(self.center_pwm)) / scale
                                             - np.asarray(self.center_angle))

    def pwm_sign(self, joint: int) -> float:
        """The sign of a PWM step that raises the joint's IK angle: steps of this sign move mirrored legs alike."""
        return JOINT_DIRECTION[joint] * self.direction[joint]


def leg_point(variant: str, hip: float, femur: float, knee: float) -> np.ndarray:
    """Where Kinematics::legIK puts the foot for these angles, in the leg's frame: its inverse (as the app's
    legPoints). At zero the coxa points out and the leg hangs straight down."""
    coxa, coxa_offset, femur_len, tibia_len, _, _ = GEOMETRY[variant]
    theta1, theta2 = np.radians(hip), np.radians(femur)
    tibia_angle = np.radians(knee if variant in KNEE_FOLLOWS_FEMUR else femur + knee)
    along = coxa_offset + femur_len * np.cos(theta2) + tibia_len * np.cos(tibia_angle)
    forward = femur_len * np.sin(theta2) + tibia_len * np.sin(tibia_angle)
    outwards = np.array([-np.cos(theta1), np.sin(theta1)])
    down = np.array([-np.sin(theta1), -np.cos(theta1)])
    return np.array([*(coxa * outwards + along * down), forward])


def feet_in_body(variant: str, ik_angles) -> np.ndarray:
    """The four feet (rows) in the body frame for twelve IK angles: calculate_inverse_kinematics run backwards, its
    mount offsets, mount rotation and the mirrored right legs."""
    _, _, _, _, length, width = GEOMETRY[variant]
    angles = np.asarray(ik_angles, dtype=float).reshape(4, 3)
    feet = np.empty((4, 3))
    for leg in range(4):
        x, y, z = leg_point(variant, *angles[leg])
        if leg % 2:
            x = -x
        mount = np.array([length / 2 if leg < 2 else -length / 2, 0.0, width / 2 if leg % 2 == 0 else -width / 2])
        feet[leg] = mount + np.array([z, y, -x])  # the transpose of invMountRot
    return feet


def floor_tilt(feet) -> np.ndarray:
    """REP-103 roll and pitch of a body standing on a floor through these body-frame feet (a least-squares plane)."""
    feet = np.asarray(feet, dtype=float)
    a = np.column_stack([np.ones(len(feet)), feet[:, 0], feet[:, 2]])
    _, slope_x, slope_z = np.linalg.lstsq(a, feet[:, 1], rcond=None)[0]
    up = np.array([-slope_x, -slope_z, 1.0])  # REP-103: forward, left, up
    up /= np.linalg.norm(up)
    return np.degrees([np.arctan2(up[1], up[2]), np.arcsin(-up[0])])


def stand_tilt(variant: str, servo_angles) -> np.ndarray:
    """The roll and pitch the legs alone hold the body at on a level floor, for joint angles in servo space."""
    return floor_tilt(feet_in_body(variant, JOINT_DIRECTION * np.asarray(servo_angles, dtype=float)))


def predicted_tilt(variant: str, servo_angles, joints, offset_pwm: float, scale: float) -> np.ndarray:
    """The roll and pitch a step of `offset_pwm` counts on each of `joints`, signed by ServoSetup.pwm_sign, adds to a
    body standing at these servo-space angles, for servos that turn one degree per `scale` counts."""
    stand = JOINT_DIRECTION * np.asarray(servo_angles, dtype=float)
    stepped = stand.copy()
    stepped[list(joints)] += offset_pwm / scale
    return floor_tilt(feet_in_body(variant, stepped)) - floor_tilt(feet_in_body(variant, stand))


@dataclass(frozen=True)
class ScaleFit:
    scale: float  # PWM counts per degree
    sigma: float  # its standard error, from the residuals
    residual_rms: float  # degrees of roll and pitch the fit leaves


def fit_scale(variant: str, servo_angles, joints, offsets_pwm, measured, start: float = 2.0) -> ScaleFit:
    """The counts per degree that best predict the measured tilt changes (rows of roll and pitch, one per offset)."""
    offsets = np.asarray(offsets_pwm, dtype=float)
    measured = np.asarray(measured, dtype=float).reshape(len(offsets), 2)

    def residuals(params):
        predicted = [predicted_tilt(variant, servo_angles, joints, offset, params[0]) for offset in offsets]
        return (np.asarray(predicted) - measured).ravel()

    result = least_squares(residuals, [start], bounds=([0.5], [8.0]))
    variance = float(result.fun @ result.fun) / max(1, result.fun.size - 1)
    jacobian = result.jac[:, 0]
    sigma = float(np.sqrt(variance / max(float(jacobian @ jacobian), 1e-12)))
    return ScaleFit(float(result.x[0]), sigma, float(np.sqrt(np.mean(result.fun ** 2))))
