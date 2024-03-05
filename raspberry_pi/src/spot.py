
import numpy as np
import copy
import sys

from .GaitGenerator.Bezier import BezierGait
from .State import BodyState, GaitState

sys.path.append("../../..")

class Spot:
    def __init__(self, kinematic, camera, imu, hardware_interface, controller_interface):
        self.kinematic = kinematic
        self.camera = camera
        self.imu = imu
        self.hardware_interface = hardware_interface
        self.controller_interface = controller_interface

        self.body_state = BodyState()
        self.gait_state = GaitState()
        self.gait_controller = BezierGait()

        self.body_state.worldFeetPositions = copy.deepcopy(self.kinematic.WorldToFoot)

    def start(self):
        self.controller_interface.start()

    def stop(self):
        self.controller_interface.stop()

    def joint_angles(self):
        return self.kinematic.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )

    def run(self, dt=.01):
        self.gait_state.update_gait_state(dt)
        # self.gui.UserInput(self.body_state, self.gait_state)

        self.gait_controller.generate_trajectory(self.body_state, self.gait_state, dt)

        self.update_environment()

    def update_environment(self):
        joint_angles = self.kinematic.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )
        self.hardware_interface.set_actuator_positions(joint_angles.reshape(-1))


