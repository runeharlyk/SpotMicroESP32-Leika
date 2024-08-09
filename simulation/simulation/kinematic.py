import numpy as np


class Kinematic:
    def __init__(self) -> None:

        self.l1 = 60.5
        self.l2 = 10
        self.l3 = 100.7
        self.l4 = 118.5

        self.L = 207.5
        self.W = 78

    def calculate_inverse_kinematics(self, omega, phi, psi, x, y, z, feet):
        Tlf, Trf, Tlb, Trb = self.bodyIK(omega, phi, psi, x, y, z)

        Q = np.linalg.inv(Tlf).dot(feet[0])[:3]

        IK = self.legIK(*Q)
        LF = (
            np.rad2deg(np.pi / 2 - IK[0]),
            np.rad2deg(np.pi / 3 - IK[1]),
            np.rad2deg(np.pi - IK[2]),
        )

        Q = self.Ix.dot(np.linalg.inv(Trf)).dot(feet[1])[:3]

        IK = self.legIK(*Q)
        RF = (
            np.rad2deg(np.pi / 2 + IK[0]),
            np.rad2deg(2 * np.pi / 3 + IK[1]),
            np.rad2deg(IK[2]),
        )

        Q = np.linalg.inv(Tlb).dot(feet[2])[:3]

        IK = self.legIK(*Q)
        LB = (
            np.rad2deg(np.pi / 2 + (IK[0])),
            np.rad2deg(np.pi / 3 - IK[1]),
            np.rad2deg(np.pi - IK[2]),
        )

        Q = self.Ix.dot(np.linalg.inv(Trb)).dot(feet[3])[:3]

        IK = self.legIK(*Q)
        RB = (
            np.rad2deg(np.pi / 2 - IK[0]),
            np.rad2deg(2 * np.pi / 3 + IK[1]),
            np.rad2deg(IK[2]),
        )
        return (LF, RF, LB, RB)

    def bodyIK(self, omega, phi, psi, xm, ym, zm):
        sHp = np.sin(np.pi / 2)
        cHp = np.cos(np.pi / 2)
        Rx = np.array(
            [
                [1, 0, 0, 0],
                [0, np.cos(omega), -np.sin(omega), 0],
                [0, np.sin(omega), np.cos(omega), 0],
                [0, 0, 0, 1],
            ]
        )
        Ry = np.array(
            [
                [np.cos(phi), 0, np.sin(phi), 0],
                [0, 1, 0, 0],
                [-np.sin(phi), 0, np.cos(phi), 0],
                [0, 0, 0, 1],
            ]
        )
        Rz = np.array(
            [
                [np.cos(psi), -np.sin(psi), 0, 0],
                [np.sin(psi), np.cos(psi), 0, 0],
                [0, 0, 1, 0],
                [0, 0, 0, 1],
            ]
        )
        Rxyz = Rx @ Ry @ Rz

        T = np.array([[0, 0, 0, xm], [0, 0, 0, ym], [0, 0, 0, zm], [0, 0, 0, 0]])
        Tm = T + Rxyz

        return [
            Tm
            @ np.array(
                [
                    [cHp, 0, sHp, self.L / 2],
                    [0, 1, 0, 0],
                    [-sHp, 0, cHp, self.W / 2],
                    [0, 0, 0, 1],
                ]
            ),
            Tm
            @ np.array(
                [
                    [cHp, 0, sHp, self.L / 2],
                    [0, 1, 0, 0],
                    [-sHp, 0, cHp, -self.W / 2],
                    [0, 0, 0, 1],
                ]
            ),
            Tm
            @ np.array(
                [
                    [cHp, 0, sHp, -self.L / 2],
                    [0, 1, 0, 0],
                    [-sHp, 0, cHp, self.W / 2],
                    [0, 0, 0, 1],
                ]
            ),
            Tm
            @ np.array(
                [
                    [cHp, 0, sHp, -self.L / 2],
                    [0, 1, 0, 0],
                    [-sHp, 0, cHp, -self.W / 2],
                    [0, 0, 0, 1],
                ]
            ),
        ]

    def legIK(self, x, y, z):
        """
        x/y/z=Position of the Foot in Leg-Space

        F=Length of shoulder-point to target-point on x/y only
        G=length we need to reach to the point on x/y
        H=3-Dimensional length we need to reach
        """

        F = np.sqrt(x**2 + y**2 - self.l1**2)
        G = F - self.l2
        H = np.sqrt(G**2 + z**2)

        theta1 = -np.atan2(y, x) - np.atan2(F, -self.l1)

        D = (H**2 - self.l3**2 - self.l4**2) / (2 * self.l3 * self.l4)
        theta3 = np.acos(D)

        theta2 = np.atan2(z, G) - np.atan2(
            self.l4 * np.sin(theta3), self.l3 + self.l4 * np.cos(theta3)
        )

        return (theta1, theta2, theta3)
