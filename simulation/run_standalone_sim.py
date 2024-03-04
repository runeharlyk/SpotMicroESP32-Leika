# SCRIPT FOR SHOWING WALKING GAIT
import numpy as np
import copy
import sys

sys.path.append("../../..")

from GymEnvs.spot_bezier_env import spotBezierEnv
from util.gui import GUI
from raspberry_pi.src.Kinematics.SpotKinematics import SpotModel
from raspberry_pi.src.GaitGenerator.Bezier import BezierGait


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


class Gait:
    def __init__(
        self,
        env: spotBezierEnv,
        gui: GUI,
        bodyState: BodyState,
        gaitState: GaitState,
        spotModel: SpotModel,
        bezierGait: BezierGait,
    ) -> None:
        self.env = env
        self.gui = gui
        self.body_state = bodyState
        self.gait_state = gaitState
        self.spot = spotModel
        self.bezier_gait = bezierGait

        self.state = self.env.reset()
        self.action = self.env.action_space.sample()
        self.body_state.worldFeetPositions = copy.deepcopy(self.spot.WorldToFoot)

        self.dt = 0.01

    def step(self):
        self.gait_state.update_gait_state(self.dt)
        self.gui.UserInput(self.body_state, self.gait_state)
        self.gait_state.contacts = self.state[-4:]
        self.body_state.worldFeetPositions = copy.deepcopy(self.spot.WorldToFoot)

        self.bezier_gait.generate_trajectory(self.body_state, self.gait_state, self.dt)

        self.update_environment()

        self.state, _, done, _ = self.env.step(self.action)
        if done:
            print("DONE")
            return True

    def update_environment(self):
        joint_angles = self.spot.IK(
            self.body_state.rotation,
            self.body_state.position,
            self.body_state.worldFeetPositions,
        )
        self.env.pass_joint_angles(joint_angles.reshape(-1))


if __name__ == "__main__":
    env = spotBezierEnv(
        render=True,
        on_rack=False,
        height_field=False,
        draw_foot_path=False,
    )
    gui = GUI(env.spot.quadruped)
    bodyState = BodyState()
    gaitState = GaitState()
    spot = SpotModel()
    bezierGait = BezierGait()

    gait = Gait(env, gui, bodyState, gaitState, spot, bezierGait)

    while True:
        done = gait.step()
        if done:
            gait.env.close()
            break
