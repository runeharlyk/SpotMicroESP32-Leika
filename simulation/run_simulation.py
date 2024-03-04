import copy
from os import sys

sys.path.append('../')

from raspberry_pi.src.spot import Spot
from raspberry_pi.src.Kinematics.SpotKinematics import SpotModel
from raspberry_pi.src.IMU import IMU

from GymEnvs.spot_bezier_env import spotBezierEnv
from util.gui import GUI

kinematics = SpotModel()
imu = IMU()
env = spotBezierEnv(
    render=True,
    on_rack=False,
    height_field=False,
    draw_foot_path=False,
)

state = env.reset()
dt = 0.01
gui = GUI(env.spot.quadruped)
action = env.action_space.sample()

spot = Spot(kinematics, imu)

def step(state):
    # handle_inputs()
    gui.UserInput(spot.body_state, spot.gait_state)
    spot.gait_state.contacts = state[-4:]
    spot.body_state.worldFeetPositions = copy.deepcopy(spot.kinematic.WorldToFoot)

    spot.gait_state.update_gait_state(dt)

    # Generate next feet positions
    spot.gait_controller.generate_trajectory(spot.body_state, spot.gait_state, dt)

    # Calculate next joint angles
    joint_angles = spot.joint_angles()

    # Update environment
    env.pass_joint_angles(joint_angles.reshape(-1))
    state, _, done, _ = env.step(action)
    if done:
        print("DONE")
        return True


if __name__ == '__main__':
    while True:
        done = step(state)
        if done:
            env.close()