import pybullet as p
import numpy as np


class QuadrupedRobot:
    def __init__(self, urdf_path):
        self.urdf_path = urdf_path
        self.robot_id = None

    def load(self):
        position = [0, 0, 0.3]
        orientation = p.getQuaternionFromEuler([0, 0, 0])
        self.robot_id = p.loadURDF(self.urdf_path, position, orientation)

    def get_observation(self):
        _, orientation = p.getBasePositionAndOrientation(self.robot_id)
        orientation = p.getEulerFromQuaternion(orientation)[:2]
        velocity, angular_velocity = p.getBaseVelocity(self.robot_id)
        joint_states = p.getJointStates(
            self.robot_id, range(p.getNumJoints(self.robot_id))
        )
        joint_positions = [state[0] for state in joint_states]
        joint_velocities = [state[1] for state in joint_states]
        return np.concatenate(
            [
                orientation,
                velocity,
                angular_velocity,
                joint_positions,
                joint_velocities,
            ]
        )

    def apply_action(self, action):
        print(action)
        for i, position in enumerate(action):
            p.setJointMotorControl2(
                bodyIndex=self.robot_id,
                jointIndex=i,
                controlMode=p.POSITION_CONTROL,
                targetPosition=position,
            )
