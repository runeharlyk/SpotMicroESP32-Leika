import asyncio
from enum import Enum
import struct
import threading
import websockets
import json

from src.controller_interface.ControllerState import ControllerState
from src.controller_interface.SharedControllerState import shared_state

clients = {}

class Command(Enum):
    ESTOP = 0
    CONTROLLER = 1

def get_controller(buffer):
    buffer = struct.unpack("<8b", buffer)
    state = ControllerState()
    state.command = buffer[0]
    state.estop = buffer[1]
    state.lx = buffer[2]
    state.ly = buffer[3]
    state.rx = buffer[4]
    state.ry = buffer[5]
    state.height = buffer[6]
    state.speed = buffer[7]

    return state

class WebsocketController:
    def __init__(self, shared_controller_state=shared_state, host="localhost", port=2096):
        self.shared_controller_state = shared_controller_state
        self.host = host
        self.port = port
        self.loop = None
        self.thread = None

    async def handle_message(self, websocket, path):
        client_id = id(websocket)
        clients[client_id] = {
            "websocket": websocket,
        }
        print("Got a new connection")
        try:
            async for message in websocket:
                if isinstance(message, bytes):
                    await self.handle_binary_message(clients[client_id], message)
                else:
                    await self.handle_json_message(clients[client_id], message)
        finally:
            del clients[client_id]

    async def handle_binary_message(self, client, data):
        message = get_controller(data)
        shared_state.update_state(message)
        command = Command(message.command)
        if command == Command.ESTOP:
            await client["websocket"].send(
                json.dumps({"type": "stop", "data": "Servos stopped"})
            )

        if command == Command.CONTROLLER:
            await client["websocket"].send(json.dumps({"type": "echo", "data": message.__dict__}))


    async def handle_json_message(self, client, message):
        data = json.loads(message)
        if data["type"] in ("stop", "mode_change"):
            client["model"][data["type"]] = data.get("data", False)
            await client["websocket"].send(
                json.dumps(
                    {"type": data["type"], "data": data.get("data", "Servos stopped")}
                )
            )

    def start_server(self):
        asyncio.set_event_loop(self.loop)
        start_server = websockets.serve(self.handle_message, self.host, self.port)
        self.loop.run_until_complete(start_server)
        self.loop.run_forever()

    def start(self):
        # Create a new event loop in a new thread
        if not self.thread or not self.thread.is_alive():
            self.loop = asyncio.new_event_loop()
            self.thread = threading.Thread(target=self.start_server)
            self.thread.start()

    def stop(self):
        if self.loop:
            self.loop.call_soon_threadsafe(self.loop.stop)
        if self.thread and self.thread.is_alive():
            self.thread.join()

if __name__ == "__main__":
    server = WebsocketController("localhost", 2096)
    server.run()

    try:
        # Your main thread logic here
        print("WebSocket server is running...")
        # Example: Keep the main thread doing something or waiting
        while True:
            pass
    except KeyboardInterrupt:
        print("Stopping WebSocket server...")
        server.stop()