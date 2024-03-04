
import numpy as np
import copy
import sys

from .GaitGenerator.Bezier import BezierGait
from .State import BodyState, GaitState

sys.path.append("../../..")

class Spot:
    def __init__(self, kinematic, IMU=None):
        self.body_state = BodyState()
        self.gait_state = GaitState()
        self.kinematic = kinematic
        self.gait_controller = BezierGait()
        self.IMU = IMU

        self.body_state.worldFeetPositions = copy.deepcopy(self.kinematic.WorldToFoot)

    def joint_angles(self):
        return self.kinematic.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )

    def run(self, state, command):
        self.gait_state.update_gait_state(self.dt)
        self.gui.UserInput(self.body_state, self.gait_state)
        self.gait_state.contacts = self.state[-4:]
        self.body_state.worldFeetPositions = copy.deepcopy(self.spot.WorldToFoot)

        self.bezier_gait.generate_trajectory(self.body_state, self.gait_state, self.dt)

        self.update_environment()

        self.state, _, done, _ = self.env.step(self.action)
        if done:
            print("DONE")
            return True

    def update_environment(self):
        joint_angles = self.spot.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )
        self.env.pass_joint_angles(joint_angles.reshape(-1))


