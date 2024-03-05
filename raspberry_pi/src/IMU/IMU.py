import numpy as np
from .IMUBase import IMUBase

class IMU(IMUBase):
    def __init__(self) -> None:
        pass

    def read_orientation(self):
        return np.array([1, 0, 0, 0])