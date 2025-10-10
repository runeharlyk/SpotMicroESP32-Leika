import time
import numpy as np
import pybullet as p

from src.robot.kinematics import Kinematics, BodyStateT, KinConfig
from src.robot.gait import GaitController, GaitStateT, GaitType, default_offset, default_stand_frac
from src.envs.quadruped_env import QuadrupedEnv

print("Initializing Spot Micro simulation...")

env = QuadrupedEnv()

joint_directions = np.array([-1, 1, 1, 1, 1, 1, -1, 1, 1, 1, 1, 1])

kinematics = Kinematics()

standby = kinematics.get_default_feet_pos()

body_state = BodyStateT(
    omega=0,
    phi=0,
    psi=0,
    xm=0,
    ym=KinConfig.default_body_height,
    zm=0,
    px=0,
    py=0,
    pz=0,
    feet=standby,
    default_feet=standby,
)

dt = 1.0 / 240
while True:
    env.gui.update_gait_state(gait_state)
    env.gui.update()

    joints = kinematics.inverse_kinematics(body_state)
    joints *= joint_directions

    _, _, done, truncated, _ = env.step(joints)
    if done or truncated:
        env.reset()

    time.sleep(dt)
