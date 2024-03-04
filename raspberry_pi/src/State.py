import numpy as np
from enum import Enum


# https://github.com/stanfordroboticsclub/StanfordQuadruped/blob/master/src/State.py
class State:
    def __init__(self):
        self.horizontal_velocity = np.array([0.0, 0.0])
        self.yaw_rate = 0.0
        self.height = -0.16
        self.pitch = 0.0
        self.roll = 0.0
        self.activation = 0
        self.behavior_state = BehaviorState.REST

        self.ticks = 0
        self.foot_locations = np.zeros((3, 4))
        self.joint_angles = np.zeros((3, 4))

        self.behavior_state = BehaviorState.REST


class GaitState:
    def __init__(self) -> None:
        self.step_length = 0.0
        self.yaw_rate = 0
        self.lateral_fraction = 0
        self.step_velocity = 0.001
        self.swing_period = 0.2
        self.clearance_height = 0.045
        self.penetration_depth = 0.003
        self.contacts = [False] * 4

        self.target_step_length = 0
        self.target_yaw_rate = 0
        self.target_lateral_fraction = 0

    def update_gait_state(self, dt):
        self.step_length = self.step_length * (1 - dt) + self.target_step_length * dt
        self.lateral_fraction = (
            self.lateral_fraction * (1 - dt) + self.target_lateral_fraction * dt
        )
        self.yaw_rate = self.yaw_rate * (1 - dt) + self.target_yaw_rate * dt


class BodyState:
    def __init__(self) -> None:
        self.position = np.array([0, 0, 0])
        self.rotation = np.array([0, 0, 0])
        self.worldFeetPositions = {}


class BehaviorState(Enum):
    DEACTIVATED = -1
    REST = 0
    TROT = 1
    HOP = 2
    FINISHHOP = 3