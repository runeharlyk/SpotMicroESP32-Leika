"""A robot on this computer, built from the firmware's own C++: uv run python virtual_robot.py --port 8765

Builds esp32/test/host/virtual_robot.cpp (MotionService, the ServoController on a fake I2C bus, the socket's decoder and
subscriptions, the telemetry) with the host compiler and serves it on ws://localhost:<port>/api/ws, so the app (address
localhost:<port>) and Robot("localhost:<port>") connect to it as to a robot. The IMU reads level; WiFi, the web server,
I2C timing and FreeRTOS scheduling are not part of it.

Needs a C++20 compiler ($CXX, or g++; on Windows MSYS2's UCRT64 g++) and the nanopb headers, which
`uv run python ../esp32/scripts/compile_protos.py` generates.
"""
import argparse
import asyncio
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import threading
from http import HTTPStatus

import websockets
from websockets.asyncio.server import serve

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
HOST_TESTS = os.path.join(REPO, "esp32", "test", "host")
NANOPB = os.path.join(REPO, "submodules", "nanopb")
GENERATED = os.path.join(REPO, "esp32", "src", "platform_shared")
SOURCES = [
    os.path.join(HOST_TESTS, "virtual_robot.cpp"),
    os.path.join(REPO, "esp32", "src", "motion.cpp"),
    *[os.path.join(NANOPB, name) for name in ("pb_common.c", "pb_encode.c", "pb_decode.c")],
    *[os.path.join(GENERATED, f"{name}.pb.c") for name in ("api", "message", "filesystem", "animation")],
]
VARIANTS = ("SPOTMICRO_ESP32", "SPOTMICRO_ESP32_MINI", "SPOTMICRO_YERTLE")
HEADER = struct.Struct("<BII")  # kind, client, length: the frame header virtual_robot.cpp reads and writes


# The name and version the firmware is built with, from the build flags in esp32/factory_settings.ini.
def _firmware_identity() -> list[str]:
    with open(os.path.join(REPO, "esp32", "factory_settings.ini")) as f:
        flags = re.findall(r'-D (APP_NAME|APP_VERSION)=\\"([^\\"]*)\\"', f.read())
    return [f'-D{name}="{value}"' for name, value in flags]


def build(binary: str) -> str:
    """Compiles the virtual robot to `binary` with the host test programs' flags (esp32/test/host/test_host_programs.py)."""
    if not os.path.exists(os.path.join(GENERATED, "message.pb.h")):
        raise RuntimeError("no nanopb headers: run uv run python ../esp32/scripts/compile_protos.py first")
    firmware_includes = [os.path.join(REPO, "esp32", "include"), os.path.join(REPO, "esp32", "src")]
    command = [os.environ.get("CXX", "g++"), "-std=gnu++20", "-Ofast", "-DPB_ENABLE_MALLOC", *_firmware_identity(),
               f"-I{os.path.join(HOST_TESTS, 'stubs')}", f"-I{NANOPB}", *[f"-idirafter{path}" for path in firmware_includes],
               *SOURCES, "-pthread", "-o", binary]
    subprocess.run(command, check=True)
    return binary


def _sha256(path: str) -> str:
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def _only_the_robot_socket(connection, request):
    if request.path != "/api/ws":
        return connection.respond(HTTPStatus.NOT_FOUND, "The robot's socket is /api/ws\n")
    return None


class VirtualRobot:
    """Runs the built program and serves it on 127.0.0.1:`port` (0 picks a free one), relaying each client's frames.

    One thread runs an asyncio loop with the server and the program's pipes; the program owns the clock and the robot.
    """

    def __init__(self, binary: str, variant: str = "SPOTMICRO_ESP32", port: int = 0, host: str = "127.0.0.1"):
        self._binary, self._variant, self._host = binary, variant, host
        self._clients: dict[int, websockets.ServerConnection] = {}
        self._next_client = 1
        self._stopping = False
        self.exited = threading.Event()  # set once the program ended, by stop() or on its own
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()
        self.port = self._call(self._start(port))

    def _call(self, coroutine):
        return asyncio.run_coroutine_threadsafe(coroutine, self._loop).result(10)

    async def _start(self, port: int) -> int:
        self._process = await asyncio.create_subprocess_exec(
            self._binary, self._variant, _sha256(self._binary), stdin=subprocess.PIPE, stdout=subprocess.PIPE
        )
        self._relay = self._loop.create_task(self._relay_output())
        self._server = await serve(self._serve, self._host, port, process_request=_only_the_robot_socket,
                                   max_size=None, ping_interval=None)
        return self._server.sockets[0].getsockname()[1]

    async def _serve(self, socket) -> None:
        client = self._next_client
        self._next_client += 1
        self._clients[client] = socket
        try:
            async for frame in socket:
                await self._to_program(b"m", client, frame)
        except websockets.ConnectionClosed:
            pass
        finally:
            del self._clients[client]
            await self._to_program(b"c", client, b"")

    async def _to_program(self, kind: bytes, client: int, payload: bytes) -> None:
        self._process.stdin.write(HEADER.pack(kind[0], client, len(payload)) + payload)
        try:
            await self._process.stdin.drain()
        except ConnectionError:
            pass  # the program ended; _relay_output closes the server

    async def _relay_output(self) -> None:
        try:
            while True:
                _, client, length = HEADER.unpack(await self._process.stdout.readexactly(HEADER.size))
                frame = await self._process.stdout.readexactly(length)
                socket = self._clients.get(client)
                if socket is not None:
                    try:
                        await socket.send(frame)
                    except websockets.ConnectionClosed:
                        pass  # _serve forgets it and tells the program
        except asyncio.IncompleteReadError:
            if not self._stopping:
                print(f"the virtual robot exited with {await self._process.wait()}", file=sys.stderr)
                self._server.close()
        finally:
            self.exited.set()

    def stop(self) -> None:
        self._call(self._stop())
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(5)

    async def _stop(self) -> None:
        self._stopping = True
        self._server.close()
        await self._server.wait_closed()
        self._process.stdin.close()  # the program ends when its input does
        await self._process.wait()
        await self._relay


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--host", default="127.0.0.1", help="the address to listen on; 0.0.0.0 for other machines")
    parser.add_argument("--variant", choices=VARIANTS, default="SPOTMICRO_ESP32")
    parser.add_argument("--binary", help="a virtual robot built before (by --build); without it, one is built now")
    parser.add_argument("--build", metavar="PATH", help="only build the program to PATH")
    args = parser.parse_args()
    if args.build:
        build(args.build)
        return
    with tempfile.TemporaryDirectory() as workdir:
        binary = args.binary or build(os.path.join(workdir, "virtual_robot.exe"))
        robot = VirtualRobot(binary, args.variant, args.port, args.host)
        print(f"virtual {args.variant} on ws://{args.host}:{robot.port}/api/ws; Ctrl-C stops it", flush=True)
        try:
            while not robot.exited.wait(0.5):  # in steps, so Ctrl-C gets through on Windows too
                pass
            sys.exit(1)  # the program ended on its own
        except KeyboardInterrupt:
            pass
        finally:
            robot.stop()


if __name__ == "__main__":
    main()
