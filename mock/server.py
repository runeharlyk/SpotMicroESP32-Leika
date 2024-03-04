import asyncio
from enum import Enum
import json
import sys
import websockets
from model import model
from MotionController import GaitState
from simulator.GaitGenerator.Bezier import BezierGait
from simulator.GymEnvs.spot_bezier_env import spotBezierEnv
from simulator.Kinematics.SpotKinematics import SpotModel
from simulator.util.gui import GUI
from simulator.simulator import BodyState, Simulator
import struct

sys.path.append("./simulator/GymEnvs")

clients = {}

env = spotBezierEnv(
    render=True,
    on_rack=False,
    height_field=False,
    draw_foot_path=False,
    env_randomizer=None,
)
gui = GUI(env.spot.quadruped)
bodyState = BodyState()
gaitState = GaitState()
spot = SpotModel()
bezierGait = BezierGait()
simulator = Simulator()

class Command(Enum):
    ESTOP = 0
    CONTROLLER = 1


def get_controller(buffer):
    buffer = struct.unpack("<8b", buffer)
    return {
        "command": buffer[0],
        "estop": buffer[1],
        "lx": buffer[2],
        "ly": buffer[3],
        "rx": buffer[4],
        "ry": buffer[5],
        "height": buffer[6],
        "speed": buffer[7],
    }


async def handle_binary_message(client, data):
    message = get_controller(data)
    command = Command(message["command"])
    if command == Command.ESTOP:
        client["model"]["running"] = False
        await client["websocket"].send(
            json.dumps({"type": "stop", "data": "Servos stopped"})
        )

    if command == Command.CONTROLLER:
        await client["websocket"].send(json.dumps({"type": "echo", "data": message}))


async def handle_json_message(client, message):
    data = json.loads(message)
    client = client["clientState"]
    if data["type"] in ("stop", "mode_change"):
        client["model"][data["type"]] = data.get("data", False)
        await client["websocket"].send(
            json.dumps(
                {"type": data["type"], "data": data.get("data", "Servos stopped")}
            )
        )


async def handle_message(websocket, path):
    client_id = id(websocket)
    clients[client_id] = {
        "clientState": model(),
        "websocket": websocket,
    }
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                await handle_binary_message(clients[client_id], message)
            else:
                await handle_json_message(clients[client_id], message)
    finally:
        del clients[client_id]


async def main():
    async with websockets.serve(handle_message, "localhost", 2096):
        print("Server starting")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
