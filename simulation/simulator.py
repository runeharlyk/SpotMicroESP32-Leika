import copy
from os import sys

sys.path.append('../../')

from .util.gui import GUI


class Simulator():
    def __init__(self, env, spot) -> None:
        self.env = env
        self.state = env.reset()
        self.gui = GUI(env.spot.quadruped)
        self.action = env.action_space.sample()

        self.spot = spot

    def step(self, dt):
    # handle_inputs()
        self.gui.UserInput(self.spot.body_state, self.spot.gait_state)
        self.spot.gait_state.contacts = self.state[-4:]
        self.spot.body_state.worldFeetPositions = copy.deepcopy(self.spot.kinematic.WorldToFoot)

        self.spot.gait_state.update_gait_state(dt)

        # Generate next feet positions
        self.spot.gait_controller.generate_trajectory(self.spot.body_state, self.spot.gait_state, dt)

        # Calculate next joint angles
        joint_angles = self.spot.joint_angles()

        # Update environment
        self.env.pass_joint_angles(joint_angles.reshape(-1))
        self.state, _, done, _ = self.env.step(self.action)
        if done:
            print("DONE")
            return True
    
    def run(self, dt):
        while True:
            done = self.step(dt)
            if done:
                self.env.close()

