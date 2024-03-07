
import numpy as np
import copy
import sys

from .camera.CameraBase import CameraBase
from .camera_server import StreamingServerThread
from .GaitGenerator.Bezier import BezierGait
from .State import BodyState, GaitState

sys.path.append("../../..")

def map_value(x, from_low, from_high, to_low, to_high):
    return ((x - from_low) / (from_high - from_low)) * (to_high - to_low) + to_low

class Spot:
    def __init__(self, kinematic, camera:CameraBase, imu, hardware_interface, controller_interface, shared_controller_state):
        self.kinematic = kinematic
        self.camera = camera
        self.imu = imu
        self.hardware_interface = hardware_interface
        self.controller_interface = controller_interface
        self.shared_controller_state = shared_controller_state

        self.camera_stream = StreamingServerThread(self.camera)

        self.body_state = BodyState()
        self.gait_state = GaitState()
        self.gait_controller = BezierGait()

        self.body_state.worldFeetPositions = copy.deepcopy(self.kinematic.WorldToFoot)

    def start(self):
        
        self.controller_interface.start()
        self.camera_stream.start()

    def stop(self):
        self.controller_interface.stop()
        self.camera_stream.stop()

    def joint_angles(self):
        return self.kinematic.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )
    
    def handle_input(self, controller):
        self.gait_state.target_step_length = map_value(max(controller.lx, controller.ly, key=abs), -127, 128, -0.1, 0.1)
        self.gait_state.target_lateral_fraction = map_value(controller.lx, -127, 128, -np.pi / 2.0, np.pi / 2.0)
        # self.gait_state.target_yaw_rate = map_value(controller.rx, -127, 128, -2.0, 2.0)

    def run(self, dt=.01):
        # self.camera.update()
        controller = self.shared_controller_state.get_latest_state()
        self.handle_input(controller)
        self.gait_state.update_gait_state(dt)

        self.body_state.worldFeetPositions = copy.deepcopy(self.kinematic.WorldToFoot)

        self.gait_controller.generate_trajectory(self.body_state, self.gait_state, dt)

        joint_angles = self.kinematic.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )
        self.hardware_interface.set_actuator_positions(joint_angles.reshape(-1))


