import gymnasium as gym
import pybullet as p
import pybullet_data
import numpy as np
import os
from enum import Enum

from src.utils.gui import GUI


class TerrainType(Enum):
    FLAT = "flat"
    PLANAR_REFLECTION = "planar_reflection"
    TERRAIN = "terrain"
    MAZE = "maze"


class QuadrupedRobot:
    def __init__(self, urdf_path, position=[0, 0, 0.3], orientation=[0, 0, 0], use_fixed_base=False):
        print(f"Loading URDF from: {urdf_path}")
        print(f"URDF file exists: {os.path.exists(urdf_path)}")

        q_orientation = p.getQuaternionFromEuler(orientation)
        try:
            self.robot_id = p.loadURDF(urdf_path, position, q_orientation, useFixedBase=use_fixed_base)
            print(f"Robot loaded successfully with ID: {self.robot_id}")
            if self.robot_id >= 0:
                print(f"Number of joints: {p.getNumJoints(self.robot_id)}")
            else:
                print("ERROR: Robot ID is negative, URDF loading failed!")
        except Exception as e:
            print(f"Error loading URDF: {e}")
            raise

        self.movable_joint_indices = [2, 3, 5, 7, 8, 10, 12, 13, 15, 17, 18, 20]

    def get_movable_joint_names(self):
        return [p.getJointInfo(self.robot_id, idx)[1].decode("utf-8") for idx in self.movable_joint_indices]

    def get_observation(self):
        pos_w, quat_wb = p.getBasePositionAndOrientation(self.robot_id)
        v_w, w_w = p.getBaseVelocity(self.robot_id)

        R = np.array(p.getMatrixFromQuaternion(quat_wb), dtype=np.float32).reshape(3, 3)

        if hasattr(self, "prev_velocity") and self.prev_velocity is not None:
            dt = 1.0 / 240.0
            accel_world = (v_w - self.prev_velocity) / dt
        else:
            accel_world = np.array([0.0, 0.0, 0.0])

        accel_body = R.T @ np.asarray(accel_world, dtype=np.float32)
        gravity_body = R.T @ np.array([0, 0, -9.81], dtype=np.float32)
        accel_body += gravity_body

        gyro_body = np.degrees(R.T @ np.asarray(w_w, dtype=np.float32))

        euler = p.getEulerFromQuaternion(quat_wb)
        heading = np.degrees(euler[2])

        altitude = np.array([pos_w[2]], dtype=np.float32)

        self.prev_velocity = np.array(v_w)

        return np.concatenate([accel_body, gyro_body, [heading], altitude]).astype(np.float32)

    def apply_action(self, action):
        for i, position in enumerate(action):
            if i < len(self.movable_joint_indices):
                joint_index = self.movable_joint_indices[i]
                p.setJointMotorControl2(
                    bodyIndex=self.robot_id,
                    jointIndex=joint_index,
                    controlMode=p.POSITION_CONTROL,
                    targetPosition=position,
                    force=50,  # 343 #  / 100 for newtons - Fix mass
                    positionGain=0.5,
                    maxVelocity=13.09,
                )


class QuadrupedEnv(gym.Env):
    def __init__(
        self,
        terrain_type: TerrainType = TerrainType.FLAT,
        render_mode: str = "human",
        target_velocity: float = 0.5,
        max_steps: int = 1000,
        distance_limit: float = 10.0,
        dt: float = 1.0 / 240,
    ):
        super().__init__()
        if render_mode == "human":
            p.connect(p.GUI)
        else:
            p.connect(p.DIRECT)

        self.observation_space = gym.spaces.Box(low=-np.inf, high=np.inf, shape=(8,), dtype=np.float32)
        self.action_space = gym.spaces.Box(low=-1, high=1, shape=(12,), dtype=np.float32)
        p.setAdditionalSearchPath(pybullet_data.getDataPath())

        self.terrain_type = terrain_type
        self.render_mode = render_mode
        self.target_velocity = target_velocity
        self.max_steps = max_steps
        self.prev_accel = np.zeros(3)
        self.estimated_velocity = np.zeros(3)
        self.current_step = 0
        self.dt = dt

        self._setup_world()
        if render_mode == "human":
            self.env_start_state = p.saveState()

        self._distance_limit = distance_limit

    def _setup_world(self):
        self.robot = QuadrupedRobot("src/resources/spot.urdf")
        self._load_terrain(self.terrain_type)
        p.setGravity(0, 0, -9.8)
        p.setTimeStep(self.dt)
        if self.render_mode == "human":
            self.gui = GUI(self.robot.robot_id)
        else:
            self.gui = None

    def _load_terrain(self, terrain_type: TerrainType):
        if terrain_type == TerrainType.FLAT:
            self.terrain = p.loadURDF("plane.urdf")
        elif terrain_type == TerrainType.PLANAR_REFLECTION:
            p.configureDebugVisualizer(p.COV_ENABLE_RENDERING, 1)
            p.configureDebugVisualizer(p.COV_ENABLE_PLANAR_REFLECTION, 1)
            p.configureDebugVisualizer(p.COV_ENABLE_TINY_RENDERER, 0)
            self.terrain = p.loadURDF("plane_transparent.urdf", useMaximalCoordinates=True)
        elif terrain_type == TerrainType.TERRAIN:
            terrainShape = p.createCollisionShape(
                shapeType=p.GEOM_HEIGHTFIELD, meshScale=[0.1, 0.1, 24], fileName="heightmaps/wm_height_out.png"
            )
            textureId = p.loadTexture("heightmaps/gimp_overlay_out.png")
            self.terrain = p.createMultiBody(0, terrainShape)
            p.changeVisualShape(self.terrain, -1, textureUniqueId=textureId)
        elif terrain_type == TerrainType.MAZE:
            terrainShape = p.createCollisionShape(
                shapeType=p.GEOM_HEIGHTFIELD, meshScale=[0.2, 0.2, 0.5], fileName="heightmaps/Maze.png"
            )
            textureId = p.loadTexture("heightmaps/Maze.png")
            maze = p.createMultiBody(0, terrainShape)
            self.terrain = [p.loadURDF("plane.urdf"), maze]
            p.changeVisualShape(self.terrain[1], -1, textureUniqueId=textureId)

    def reset(self, *, seed: int | None = None):
        super().reset(seed=seed)
        if self.render_mode == "human":
            p.restoreState(self.env_start_state)
        else:
            p.resetSimulation()
            self._setup_world()
        self.current_step = 0
        self.prev_velocity = None
        return self.robot.get_observation(), {}

    def step(self, action):
        self.current_step += 1
        if self.gui:
            self.gui.update()
        self.robot.apply_action(action)
        p.stepSimulation()

        obs = self.robot.get_observation()
        reward = self.calculate_reward(obs)
        done = self.is_done()
        truncated = self.current_step >= self.max_steps

        return obs, reward, done, truncated, {}

    def close(self):
        pass
        # p.disconnect()

    def calculate_reward(self, obs):
        accel = obs[0:3]
        gyro = obs[3:6]
        heading = obs[6]
        altitude = obs[7]

        self.estimated_velocity = self.estimated_velocity + self.prev_accel * self.dt

        self.prev_accel = accel.copy()

        forward_velocity = self.estimated_velocity[0]
        velocity_reward = -abs(forward_velocity - self.target_velocity)

        height_penalty = -abs(altitude - 0.3) * 0.5

        orientation_penalty = -(abs(gyro[0]) + abs(gyro[1])) * 0.1

        angular_penalty = -np.sum(np.square(gyro)) * 0.01

        lateral_acc_penalty = -abs(accel[1]) * 0.01

        vertical_acc_penalty = -abs(accel[2] + 9.81) * 0.01

        total_reward = (
            velocity_reward
            + height_penalty
            + orientation_penalty
            + angular_penalty
            + lateral_acc_penalty
            + vertical_acc_penalty
        )
        return total_reward

    def is_done(self):
        position, orientation = p.getBasePositionAndOrientation(self.robot.robot_id)
        orientation = p.getEulerFromQuaternion(orientation)

        return self._is_fallen(orientation) or self._is_distance_limit_exceeded(position)

    def _is_distance_limit_exceeded(self, position):
        distance = np.hypot(position[0], position[1])
        return distance > self._distance_limit

    def _is_fallen(self, orientation):
        return abs(orientation[0]) > 0.85 or abs(orientation[1]) > 0.85
