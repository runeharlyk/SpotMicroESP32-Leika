import numpy as np
from typing import TypedDict, List


class KinConfig:
    # SPOTMICRO_ESP32 configuration (matching C++ version)
    coxa = 60.5 / 100.0
    coxa_offset = 10.0 / 100.0
    femur = 111.2 / 100.0
    tibia = 118.5 / 100.0
    L = 207.5 / 100.0
    W = 78.0 / 100.0

    mountOffsets = [[L / 2, 0, W / 2], [L / 2, 0, -W / 2], [-L / 2, 0, W / 2], [-L / 2, 0, -W / 2]]

    default_feet_positions = np.array(
        [
            [mountOffsets[0][0], 0, mountOffsets[0][2] + coxa, 1],
            [mountOffsets[1][0], 0, mountOffsets[1][2] - coxa, 1],
            [mountOffsets[2][0], 0, mountOffsets[2][2] + coxa, 1],
            [mountOffsets[3][0], 0, mountOffsets[3][2] - coxa, 1],
        ]
    )

    # Max constants
    max_roll = 15 * np.pi / 2
    max_pitch = 15 * np.pi / 2
    max_body_shift_x = W / 3
    max_body_shift_z = W / 3
    max_leg_reach = femur + tibia - coxa_offset
    min_body_height = max_leg_reach * 0.45
    max_body_height = max_leg_reach * 0.9
    body_height_range = max_body_height - min_body_height
    max_step_length = max_leg_reach * 0.8
    max_step_height = max_leg_reach / 2

    # Default constants
    default_step_depth = 0.002
    default_body_height = min_body_height + body_height_range / 2
    default_step_height = default_body_height / 2


class BodyStateT(TypedDict):
    omega: float
    phi: float
    psi: float
    xm: float
    ym: float
    zm: float
    feet: List[List[float]]
    default_feet: List[List[float]]
    px: float
    py: float
    pz: float


def rot_x(theta):
    c = np.cos(theta)
    s = np.sin(theta)
    return np.array([[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1]])


def rot_y(theta):
    c = np.cos(theta)
    s = np.sin(theta)
    return np.array([[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]])


def rot_z(theta):
    c = np.cos(theta)
    s = np.sin(theta)
    return np.array([[c, -s, 0, 0], [s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])


def rot(omega, phi, psi):
    return rot_z(psi) @ rot_y(phi) @ rot_x(omega)


def translation(x, y, z):
    return np.array([[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]])


def transformation(omega, phi, psi, x, y, z):
    return rot(omega, phi, psi) @ translation(x, y, z)


def get_transformation_matrix(body_state):
    omega, phi, psi = body_state["omega"], body_state["phi"], body_state["psi"]
    xm, ym, zm = body_state["xm"], body_state["ym"], body_state["zm"]

    return transformation(omega, phi, psi, xm, ym, zm)


class Kinematics:
    def __init__(self):
        # Use KinConfig constants (matching C++ version)
        self.coxa = KinConfig.coxa
        self.coxa_offset = KinConfig.coxa_offset
        self.femur = KinConfig.femur
        self.tibia = KinConfig.tibia
        self.L = KinConfig.L
        self.W = KinConfig.W

        self.mount_offsets = np.array(KinConfig.mountOffsets)

        self.inv_mount_rot = np.array([[0, 0, -1], [0, 1, 0], [1, 0, 0]])

        # Cache for current state (matching C++ optimization)
        self.current_state = None

    def get_default_feet_pos(self):
        # Return default feet positions matching C++ version exactly
        feet = []
        for i, offset in enumerate(self.mount_offsets):
            y_pos = 0  # Ground level (matching C++ default_feet_positions)
            z_offset = -self.coxa if i % 2 == 1 else self.coxa
            feet.append([offset[0], y_pos, offset[2] + z_offset])
        return np.array(feet)

    def inverse_kinematics(self, body_state):
        # Check if state has changed (optimization from C++ version)
        # if self.current_state == body_state:
        #     return self.current_angles if hasattr(self, "current_angles") else []

        self.current_state = body_state.copy()

        roll, pitch, yaw = np.deg2rad(body_state["omega"]), np.deg2rad(body_state["phi"]), np.deg2rad(body_state["psi"])
        xm, ym, zm = body_state["xm"], body_state["ym"], body_state["zm"]

        rot = self._rotation_matrix(roll, pitch, yaw)
        inv_rot = rot.T
        inv_tr = -inv_rot @ np.array([xm, ym, zm])

        angles = []
        for idx, foot_world in enumerate(body_state["feet"]):
            foot_body = inv_rot @ foot_world + inv_tr
            foot_local = self.inv_mount_rot @ (foot_body - self.mount_offsets[idx])
            x_local = -foot_local[0] if idx % 2 else foot_local[0]
            leg_angles = self._leg_ik(x_local, foot_local[1], foot_local[2])
            angles.extend(leg_angles)

        self.current_angles = angles
        return angles

    def _leg_ik(self, x, y, z):
        # Match C++ implementation exactly
        F = np.sqrt(max(0.0, x * x + y * y - self.coxa * self.coxa))
        G = F - self.coxa_offset
        H = np.sqrt(G * G + z * z)

        theta1 = -np.arctan2(y, x) - np.arctan2(F, -self.coxa)
        D = (H * H - self.femur * self.femur - self.tibia * self.tibia) / (2 * self.femur * self.tibia)
        theta3 = np.arccos(max(-1.0, min(1.0, D)))
        theta2 = np.arctan2(z, G) - np.arctan2(self.tibia * np.sin(theta3), self.femur + self.tibia * np.cos(theta3))

        # Return angles in radians (matching web app)
        return theta1, theta2, theta3

    def _rotation_matrix(self, roll, pitch, yaw):
        cr, sr = np.cos(roll), np.sin(roll)
        cp, sp = np.cos(pitch), np.sin(pitch)
        cy, sy = np.cos(yaw), np.sin(yaw)
        return np.array(
            [
                [cp * cy, -cp * sy, sp],
                [sr * sp * cy + sy * cr, -sr * sp * sy + cr * cy, -sr * cp],
                [sr * sy - sp * cr * cy, sr * cy + sp * sy * cr, cr * cp],
            ]
        )
