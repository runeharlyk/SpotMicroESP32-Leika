import time
import numpy as np

from src.robot.kinematics import Kinematics, BodyStateT
from src.robot.gait import GaitController, GaitStateT
from src.envs.quadruped_env import QuadrupedEnv

env = QuadrupedEnv()

leg_order = [3, 0, 4, 1, 5, 2]

kinematics = Kinematics(0.605, 0.01, 1.112, 1.185, 2.075, 0.78)

standby = kinematics.get_default_feet_pos()

body_state = BodyStateT(omega=0, phi=0, psi=0, xm=0, ym=0,
                        zm=0, px=0, py=0, pz=0, feet=standby, default_feet=standby)

gait_state = GaitStateT(step_height=30, step_x=0, step_z=0,
                        step_angle=0, step_velocity=1, step_depth=0.002)

gait = GaitController(standby)

dt = 1.0 / 240
while True:
    env.gui.update_gait_state(gait_state)
    env.gui.update_body_state(body_state)
    env.gui.update()

    gait.step(gait_state, body_state, dt)
    angles = kinematics.inverse_kinematics(body_state)
    print(angles)
    joints = angles  # angles.reshape(4, 3)[leg_order].flatten()

    _, _, done, truncated, _ = env.step(joints)
    # if done or truncated:
    #     env.reset()

    time.sleep(dt)
