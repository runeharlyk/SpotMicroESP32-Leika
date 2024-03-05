from abc import abstractmethod

class HardwareInterfaceBase:
    def __init__(self):
        pass

    @abstractmethod
    def set_actuator_positions(self, joint_angles):
        raise NotImplementedError
    

