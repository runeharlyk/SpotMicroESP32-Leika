import time
import asyncio
import websockets
import json
import numpy as np
from typing import Dict, Any
from abc import ABC, abstractmethod

from src.robot.kinematics import BodyStateT, KinConfig
from src.robot.gait import GaitStateT, GaitType, default_offset, default_stand_frac


class Controller(ABC):
    @abstractmethod
    def update(self, body_state: BodyStateT, gait_state: GaitStateT, dt: float):
        pass

    @abstractmethod
    def is_running(self) -> bool:
        pass


class GUIController(Controller):
    def __init__(self, env):
        self.env = env

    def update(self, body_state: BodyStateT, gait_state: GaitStateT, dt: float):
        self.env.gui.update_gait_state(gait_state)
        self.env.gui.update_body_state(body_state)
        self.env.gui.update()

    def is_running(self) -> bool:
        return True


class WebSocketController(Controller):
    def __init__(self, port: int = 8765):
        self.port = port
        self.running = False
        self.connected_clients = set()

        self.cmd_lx = 0.0
        self.cmd_ly = 0.0
        self.cmd_rx = 0.0
        self.cmd_ry = 0.0
        self.cmd_h = 0.0
        self.cmd_s = 0.0
        self.cmd_s1 = 0.0

        self.motion_mode = "rest"
        self.current_gait_type = GaitType.TROT_GATE

        self.last_broadcast_time = time.time()

        print(f"[*] WebSocket Controller initialized")
        print(f"    Port: {port}")

    def handle_input(self, input_data: list):
        if len(input_data) >= 7:
            self.cmd_lx = float(input_data[0])
            self.cmd_ly = float(input_data[1])
            self.cmd_rx = float(input_data[2])
            self.cmd_ry = float(input_data[3])
            self.cmd_h = float(input_data[4])
            self.cmd_s = float(input_data[5])
            self.cmd_s1 = float(input_data[6])

    def handle_mode(self, mode: int):
        modes = {0: "deactivated", 1: "idle", 2: "calibration", 3: "rest", 4: "stand", 5: "walk"}
        if mode in modes:
            self.motion_mode = modes[mode]
            print(f"[*] Mode changed to: {self.motion_mode}")
        else:
            print(f"[!] Invalid mode: {mode}")

    def handle_walk_gait(self, gait: int):
        if gait == 0:
            self.current_gait_type = GaitType.TROT_GATE
            print(f"[*] Gait changed to: TROT")
            return default_offset[GaitType.TROT_GATE], default_stand_frac[GaitType.TROT_GATE]
        elif gait == 1:
            self.current_gait_type = GaitType.CRAWL_GATE
            print(f"[*] Gait changed to: CRAWL")
            return default_offset[GaitType.CRAWL_GATE], default_stand_frac[GaitType.CRAWL_GATE]
        else:
            print(f"[!] Invalid gait: {gait}")
            return None, None

    def update(self, body_state: BodyStateT, gait_state: GaitStateT, dt: float):
        if self.motion_mode == "walk":
            body_state["ym"] = KinConfig.min_body_height + self.cmd_h * KinConfig.body_height_range
            body_state["psi"] = self.cmd_ry * KinConfig.max_pitch

            gait_state["step_height"] = (
                self.cmd_s1 * KinConfig.max_step_height if self.cmd_s1 != 0 else KinConfig.default_step_height
            )
            gait_state["step_x"] = self.cmd_ly * KinConfig.max_step_length
            gait_state["step_z"] = -self.cmd_lx * KinConfig.max_step_length
            gait_state["step_velocity"] = self.cmd_s if self.cmd_s != 0 else 1.0
            gait_state["step_angle"] = self.cmd_rx
            gait_state["step_depth"] = KinConfig.default_step_depth

        elif self.motion_mode == "stand":
            body_state["ym"] = KinConfig.min_body_height + self.cmd_h * KinConfig.body_height_range
            body_state["xm"] = self.cmd_ly * KinConfig.max_body_shift_x
            body_state["zm"] = self.cmd_lx * KinConfig.max_body_shift_z
            body_state["phi"] = self.cmd_rx * KinConfig.max_roll
            body_state["psi"] = self.cmd_ry * KinConfig.max_pitch

        elif self.motion_mode in ["rest", "idle", "calibration"]:
            gait_state["step_x"] = 0.0
            gait_state["step_z"] = 0.0
            gait_state["step_angle"] = 0.0

    async def handle_client(self, websocket, path):
        client_addr = websocket.remote_address
        print(f"[+] Client connected: {client_addr}")
        self.connected_clients.add(websocket)

        try:
            async for message in websocket:
                try:
                    data = json.loads(message)

                    if not isinstance(data, list) or len(data) < 1:
                        continue

                    msg_type = data[0]

                    if msg_type == 0:
                        if len(data) >= 2:
                            event = data[1]
                            print(f"[*] Client subscribed to: {event}")

                    elif msg_type == 1:
                        if len(data) >= 2:
                            event = data[1]
                            print(f"[*] Client unsubscribed from: {event}")

                    elif msg_type == 2:
                        if len(data) >= 3:
                            event = data[1]
                            payload = data[2]
                            await self.handle_event(websocket, event, payload)

                    elif msg_type == 4:
                        await websocket.send(json.dumps([4]))

                except json.JSONDecodeError:
                    print(f"[!] Invalid JSON from {client_addr}")
                except Exception as e:
                    print(f"[!] Error handling message: {e}")

        except websockets.exceptions.ConnectionClosed:
            print(f"[-] Client disconnected: {client_addr}")
        finally:
            self.connected_clients.discard(websocket)

    async def handle_event(self, websocket, event: str, data: Any):
        if event == "input":
            if isinstance(data, list) and len(data) >= 7:
                self.handle_input(data)

        elif event == "mode":
            self.handle_mode(data)

        elif event == "walk_gait":
            self.handle_walk_gait(data)

    async def broadcast_angles(self, joint_angles: np.ndarray):
        if self.connected_clients and time.time() - self.last_broadcast_time > 0.1:
            state_message = json.dumps([2, "angles", joint_angles.tolist()])

            disconnected = set()
            for client in self.connected_clients:
                try:
                    await client.send(state_message)
                except websockets.exceptions.ConnectionClosed:
                    disconnected.add(client)

            self.connected_clients -= disconnected
            self.last_broadcast_time = time.time()

    async def start_server(self):
        print(f"[*] Starting WebSocket server on port {self.port}")
        self.running = True

        server = await websockets.serve(self.handle_client, "0.0.0.0", self.port, ping_interval=20, ping_timeout=10)

        print(f"[+] Server running on ws://0.0.0.0:{self.port}")
        print(f"[*] Connect from same PC: ws://localhost:{self.port}")
        print(f"[*] Connect from network: ws://<your-ip>:{self.port}")
        print("[*] Ready for controller connections!")
        print("[*] Use the controller app to connect and control the robot")

        return server

    def is_running(self) -> bool:
        return self.running
