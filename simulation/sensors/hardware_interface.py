import sys
import numpy as np

sys.path.append("../../../")

from raspberry_pi.src.hardware_interface.HardwareInterfaceBase import HardwareInterfaceBase

class PyBulletHardwareInterface(HardwareInterfaceBase):
    def __init__(self, env):
        self.env = env
        self.set_actuator_positions(np.array([0]*12))
        super().__init__()

    def set_actuator_positions(self, joint_angles):
        return self.env.pass_joint_angles(joint_angles)