import time
import numpy as np
import pybullet as p

from src.robot.kinematics import Kinematics, BodyStateT, KinConfig
from src.robot.gait import GaitController, GaitStateT, GaitType, default_offset, default_stand_frac
from src.envs.quadruped_env import QuadrupedEnv, TerrainType

print("Initializing Spot Micro simulation...")
try:
    env = QuadrupedEnv(terrain_type=TerrainType.FLAT)
    print("Environment created successfully")
    print(f"Robot ID: {env.robot.robot_id}")
    print(f"Number of joints: {env.robot.get_observation().shape[0]}")

    # Print joint names
    print("\nJoint names:")
    num_joints = p.getNumJoints(env.robot.robot_id)
    for i in range(num_joints):
        joint_info = p.getJointInfo(env.robot.robot_id, i)
        joint_name = joint_info[1].decode("utf-8")
        joint_type = joint_info[2]
        print(f"Joint {i}: {joint_name} (type: {joint_type})")

    print("Simulation ready! Use the GUI sliders to control the robot.")
except Exception as e:
    print(f"Error creating environment: {e}")
    import traceback

    traceback.print_exc()
    exit(1)

joint_directions = np.array([-1, 1, 1, 1, 1, 1, -1, 1, 1, 1, 1, 1])

kinematics = Kinematics()

standby = KinConfig.default_feet_positions[:4, :3]

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

gait_state = GaitStateT(
    step_height=KinConfig.default_step_height,
    step_x=0,
    step_z=0,
    step_angle=0,
    step_velocity=1,
    step_depth=KinConfig.default_step_depth,
    stand_frac=default_stand_frac[GaitType.TROT_GATE],
    offset=default_offset[GaitType.TROT_GATE],
    gait_type=GaitType.TROT_GATE,
)

gait = GaitController(standby)

dt = 1.0 / 240
while True:
    env.gui.update_gait_state(gait_state)
    env.gui.update_body_state(body_state)
    env.gui.update()

    gait.step(gait_state, body_state, dt)
    joints = kinematics.inverse_kinematics(body_state)
    joints = joints * joint_directions

    _, _, done, truncated, _ = env.step(joints)
    # if done or truncated:
    #    env.reset()

    time.sleep(dt)
