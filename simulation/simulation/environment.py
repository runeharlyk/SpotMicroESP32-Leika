import pybullet as p
import pybullet_data
import numpy as np
from simulation.robot import QuadrupedRobot
from simulation.gui import GUI


roll_pitch_reward_weight = 0.1


class QuadrupedEnv:
    def __init__(self, urdf_path):
        p.connect(p.GUI)
        p.setAdditionalSearchPath(pybullet_data.getDataPath())
        p.setGravity(0, 0, -9.8)
        p.setTimeStep(1 / 240)
        self.urdf_path = urdf_path

        self.setupWorld()
        self.gui = GUI(self.robot.robot_id)

        self.envStartState = p.saveState()

    def setupWorld(self):
        p.resetSimulation()
        p.setGravity(0, 0, -9.8)

        self.plane_id = p.loadURDF("plane.urdf")

        self.robot = QuadrupedRobot(self.urdf_path)

    def reset(self):
        p.restoreState(self.envStartState)
        return self.robot.get_observation()

    def step(self, action):
        self.gui.update()
        self.robot.apply_action(action)
        p.stepSimulation()
        obs = self.robot.get_observation()
        reward = self.calculate_reward(obs)
        done = self.is_done(obs)
        return obs, reward, done

    def calculate_reward(self, observation):
        reward = 0
        reward += (
            -(abs(observation[0]) + abs(observation[1])) * roll_pitch_reward_weight
        )
        return reward

    def is_done(self, observation):
        roll, pitch = observation[0:2]
        return abs(roll) > 0.5 or abs(pitch) > 0.5
