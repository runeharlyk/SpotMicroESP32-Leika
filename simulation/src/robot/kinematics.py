import numpy as np
from typing import TypedDict, List

import config


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
    def __init__(self, l1, l2, l3, l4, length, width):
        self.l1 = float(l1)
        self.l2 = float(l2)
        self.l3 = float(l3)
        self.l4 = float(l4)
        self.length = float(length)
        self.width = float(width)
        self.deg2rad = np.pi / 180

        self.mount_offsets = np.array([
            [self.length / 2, 0,  self.width / 2],
            [self.length / 2, 0, -self.width / 2],
            [-self.length / 2, 0,  self.width / 2],
            [-self.length / 2, 0, -self.width / 2]
        ])

        self.inv_mount_rot = np.array([
            [0, 0, -1],
            [0, 1,  0],
            [1, 0,  0]
        ])

    def get_default_feet_pos(self):
        feet = self.mount_offsets.copy()
        feet[:, 1] = -1
        feet[:, 2] += np.array([self.l1, -self.l1,  self.l1, -self.l1])
        return feet

    def inverse_kinematics(self, body_state):
        roll, pitch, yaw = np.deg2rad(body_state["omega"]), np.deg2rad(
            body_state["phi"]), np.deg2rad(body_state["psi"])
        xm, ym, zm = body_state["xm"], body_state["ym"], body_state["zm"]

        rot = self._rotation_matrix(roll, pitch, yaw)
        inv_rot = rot.T
        inv_tr = - \
            inv_rot @ np.array([xm, ym, zm])

        angles = []
        for idx, foot_world in enumerate(body_state["feet"]):
            foot_body = inv_rot @ foot_world + inv_tr
            foot_local = self.inv_mount_rot @ (foot_body -
                                               self.mount_offsets[idx])
            x_local = -foot_local[0] if idx % 2 else foot_local[0]
            angles.extend(self._leg_ik(x_local, foot_local[1], foot_local[2]))
        return angles

    def _leg_ik(self, x, y, z):
        f = np.sqrt(max(0.0, x*x + y*y - self.l1*self.l1))
        g = f - self.l2
        h = np.sqrt(g*g + z*z)

        t1 = -np.arctan2(y, x) - np.arctan2(f, -self.l1)

        d = (h*h - self.l3*self.l3 - self.l4*self.l4) / (2*self.l3*self.l4)
        d = max(-1.0, min(1.0, d))

        t3 = np.arccos(d)
        t2 = np.arctan2(z, g) - np.arctan2(self.l4*np.sin(t3),
                                           self.l3 + self.l4*np.cos(t3))

        return t1, t2, t3

    def _rotation_matrix(self, roll, pitch, yaw):
        cr, sr = np.cos(roll), np.sin(roll)
        cp, sp = np.cos(pitch), np.sin(pitch)
        cy, sy = np.cos(yaw), np.sin(yaw)
        return np.array([
            [cp*cy,            -cp*sy,            sp],
            [sr*sp*cy + cy*cr, -sr*sp*sy + cr*cy, -sr*cp],
            [sr*sy - sp*cr*cy,  sr*cy + sp*sy*cr,  cr*cp]
        ])
