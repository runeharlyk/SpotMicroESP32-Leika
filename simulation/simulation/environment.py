import pybullet as p
import pybullet_data
import numpy as np
from simulation.robot import QuadrupedRobot


class QuadrupedEnv:
    def __init__(self, urdf_path):
        p.connect(p.GUI)
        p.setAdditionalSearchPath(pybullet_data.getDataPath())
        p.setGravity(0, 0, -9.8)
        p.setTimeStep(1 / 240)

        self.robot = QuadrupedRobot(urdf_path)
        self.reset()

    def reset(self):
        p.resetSimulation()
        self.robot.load()
        return self.robot.get_observation()

    def step(self, action):
        self.robot.apply_action(action)
        p.stepSimulation()
        obs = self.robot.get_observation()
        reward = self.calculate_reward(obs)
        done = self.is_done(obs)
        return obs, reward, done

    def calculate_reward(self, observation):
        # Define your reward function here
        return 0

    def is_done(self, observation):
        roll, pitch = observation[0:2]
        return abs(roll) > 0.5 or abs(pitch) > 0.5
