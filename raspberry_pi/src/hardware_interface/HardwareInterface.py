from src.hardware_interface.HardwareInterfaceBase import HardwareInterfaceBase


class HardwareInterface(HardwareInterfaceBase):
    def __init__(self):
        super().__init__()

    def set_actuator_positions(self, joint_angles):
        pass
    