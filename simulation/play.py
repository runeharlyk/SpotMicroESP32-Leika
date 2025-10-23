#!/usr/bin/env python3
import time
import numpy as np
import pybullet as p
import asyncio
import argparse
import sys
from typing import Optional

from src.robot.kinematics import Kinematics, BodyStateT, KinConfig
from src.robot.gait import GaitController, GaitStateT, GaitType, default_offset, default_stand_frac
from src.envs.quadruped_env import QuadrupedEnv, TerrainType
from src.controllers import Controller, GUIController, WebSocketController


class SpotMicroSimulation:
    def __init__(
        self,
        controller: Controller,
        env: Optional[QuadrupedEnv] = None,
        terrain_type: TerrainType = TerrainType.FLAT,
        dt: float = 1.0 / 240,
    ):
        print("Initializing Spot Micro simulation...")
        try:
            if env is not None:
                self.env = env
                print("Using existing environment")
            else:
                self.env = QuadrupedEnv(terrain_type=terrain_type, dt=dt)
                print("Environment created successfully")

            print(f"Robot ID: {self.env.robot.robot_id}")
            print(f"Number of joints: {self.env.robot.get_observation().shape[0]}")

            num_joints = p.getNumJoints(self.env.robot.robot_id)
            print("\nJoint names:")
            for i in range(num_joints):
                joint_info = p.getJointInfo(self.env.robot.robot_id, i)
                joint_name = joint_info[1].decode("utf-8")
                joint_type = joint_info[2]
                print(f"Joint {i}: {joint_name} (type: {joint_type})")

            print("Simulation ready!")
        except Exception as e:
            print(f"Error creating environment: {e}")
            import traceback

            traceback.print_exc()
            sys.exit(1)

        self.controller = controller
        self.joint_directions = np.array([-1, 1, 1, 1, 1, 1, -1, 1, 1, 1, 1, 1])
        self.kinematics = Kinematics()

        standby = KinConfig.default_feet_positions[:4, :3]

        self.body_state = BodyStateT(
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

        self.gait_state = GaitStateT(
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

        self.gait = GaitController(standby)
        self.dt = dt

    def step(self):
        self.controller.update(self.body_state, self.gait_state, self.dt)

        self.gait.step(self.gait_state, self.body_state, self.dt)
        joints = self.kinematics.inverse_kinematics(self.body_state)
        joints = joints * self.joint_directions

        obs, _, done, truncated, _ = self.env.step(joints)

        self._print_mpu6050_data(obs)

        return joints, done, truncated

    def _print_mpu6050_data(self, obs):
        accel = obs[0:3]
        gyro = obs[3:6]
        heading = obs[6]
        altitude = obs[7]

        print(
            f"MPU6050: Accel({accel[0]:8.3f}, {accel[1]:8.3f}, {accel[2]:8.3f}) "
            f"Gyro({gyro[0]:8.3f}, {gyro[1]:8.3f}, {gyro[2]:8.3f}) "
            f"Mag({heading:8.3f}) Baro({altitude:8.3f})"
        )

    def run_sync(self):
        try:
            while self.controller.is_running():
                joints, done, truncated = self.step()
                time.sleep(self.dt)
        except KeyboardInterrupt:
            print("\n[*] Shutting down...")

    async def run_async(self):
        try:
            while self.controller.is_running():
                joints, done, truncated = self.step()

                if isinstance(self.controller, WebSocketController):
                    await self.controller.broadcast_angles(joints)

                await asyncio.sleep(self.dt)
        except KeyboardInterrupt:
            print("\n[*] Shutting down...")


def main():
    parser = argparse.ArgumentParser(description="Spot Micro Interactive Control Server")
    parser.add_argument("--port", type=int, default=8765, help="WebSocket server port (default: 8765)")
    parser.add_argument("--mode", choices=["gui", "websocket"], default="gui", help="Control mode (default: gui)")
    parser.add_argument("--terrain", choices=["flat", "maze", "terrain"], default="flat", help="Terrain type")

    args = parser.parse_args()

    terrain_map = {"flat": TerrainType.FLAT, "maze": TerrainType.MAZE, "terrain": TerrainType.TERRAIN}
    terrain_type = terrain_map.get(args.terrain, TerrainType.FLAT)

    if args.mode == "websocket":
        controller = WebSocketController(port=args.port)
        sim = SpotMicroSimulation(controller, terrain_type=terrain_type)

        async def run():
            server = await controller.start_server()
            try:
                await sim.run_async()
            except KeyboardInterrupt:
                print("\n[!] Shutting down server...")
                controller.running = False
                server.close()
                await server.wait_closed()
                print("[+] Server stopped")

        asyncio.run(run())
    else:
        from src.envs.quadruped_env import QuadrupedEnv

        env = QuadrupedEnv(terrain_type=terrain_type)
        controller = GUIController(env)
        sim = SpotMicroSimulation(controller, env=env)
        print("Use the GUI sliders to control the robot.")
        sim.run_sync()


if __name__ == "__main__":
    main()
