from time import sleep
import torch
import torch.optim as optim
import pybullet as p
import numpy as np
from tqdm import tqdm, trange
from collections import namedtuple

from training.model import SimpleNN

Experience = namedtuple("Experience", ["observation", "action", "reward", "log_prob"])


class Trainer:

    def __init__(self, env, render):
        self.env = env
        self.should_render = render
        self.model = SimpleNN(
            input_size=env.robot.get_observation().shape[0],
            output_size=p.getNumJoints(env.robot.robot_id),
        )
        self.optimizer = optim.Adam(self.model.parameters(), lr=0.001)

    def train(self, episodes=1000):
        for episode in trange(episodes):
            observation = self.env.reset()
            done = False
            total_reward = 0

            while not done:
                action = self.select_action(observation)
                observation, reward, done = self.env.step(action)
                total_reward += reward

                if self.should_render:
                    sleep(0.005)

                # Train the neural network
                # loss = self.compute_loss(observation, action, reward)
                # self.optimizer.zero_grad()
                # loss.backward()
                # self.optimizer.step()

            print(f"Episode {episode}: Total Reward: {total_reward}")

    def select_action(self, observation):
        with torch.no_grad():
            observation_tensor = torch.tensor(observation, dtype=torch.float32)
            action = self.model(observation_tensor)
            return action.numpy()

    def compute_loss(self, observation, action, reward):
        # Define your loss function here
        return torch.tensor(0.0)
