"""The two ways to reach the robot, as one link of protobuf messages: its WebSocket over WiFi (a host name or address),
or its native USB port (COM10, /dev/ttyACM0), which carries the same protocol beside the robot's log.

A link is used from one asyncio loop: `send` and `recv` raise LinkClosed once the link is gone.
"""
import asyncio
import logging
import re
import threading

import websockets
from serial import Serial, SerialException

from ..proto import message_pb2 as pb
from . import serial_frame

# The robot's log lines, which share its USB port with the frames.
ROBOT_LOG = logging.getLogger("leika.robot_log")
# A robot that has not answered yet is pinged this often: one that reset when its port opened is still booting.
PROBE_INTERVAL_S = 0.5
_READ_TIMEOUT_S = 0.05


class LinkClosed(Exception):
    pass


def is_serial_port(target: str) -> bool:
    return re.fullmatch(r"COM\d+", target, re.IGNORECASE) is not None or target.startswith("/dev/")


async def open_link(target: str, timeout: float):
    """Opens the link `target` names; raises TimeoutError when the robot does not answer within `timeout`."""
    if not is_serial_port(target):
        return await WebSocketLink.open(f"ws://{target}/api/ws", timeout)
    link = SerialLink(target)
    try:
        await asyncio.wait_for(link.answered(), timeout)
    except BaseException:
        await link.close()
        raise
    return link


class WebSocketLink:
    def __init__(self, socket, url: str):
        self.name = url
        self._socket = socket

    @classmethod
    async def open(cls, url: str, timeout: float) -> "WebSocketLink":
        try:
            socket = await websockets.connect(url, open_timeout=timeout, close_timeout=1, ping_interval=None,
                                              max_size=None)
        except websockets.InvalidHandshake as error:  # a robot still booting, or an httpd out of sockets
            raise LinkClosed(f"{url} refused the connection: {error}") from error
        return cls(socket, url)

    async def send(self, message: bytes) -> None:
        try:
            await self._socket.send(message)
        except websockets.ConnectionClosed as error:
            raise LinkClosed(f"{self.name} closed the connection") from error

    async def recv(self) -> bytes:
        try:
            return await self._socket.recv()
        except websockets.ConnectionClosed as error:
            raise LinkClosed(f"{self.name} closed the connection") from error

    async def close(self) -> None:
        await self._socket.close()


def open_port(name: str) -> Serial:
    """Opens a serial port without resetting the board behind it."""
    port = Serial(baudrate=115200, timeout=_READ_TIMEOUT_S, write_timeout=1)
    port.port = name
    # Opening asserts both by default; an ESP32's USB-serial chip or USB-Serial-JTAG port resets it on that.
    port.dtr = False
    port.rts = False
    port.open()
    return port


class SerialLink:
    """A thread reads the port, so the loop never blocks on it; frames reach `recv` in order, log lines ROBOT_LOG."""

    def __init__(self, name: str):
        self.name = name
        self._port = open_port(name)
        self._loop = asyncio.get_running_loop()
        self._received: asyncio.Queue[bytes | LinkClosed] = asyncio.Queue()
        self._decoder = serial_frame.Decoder(self._frame, ROBOT_LOG.info)
        self._closing = False
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _frame(self, message: bytes) -> None:
        self._loop.call_soon_threadsafe(self._received.put_nowait, message)

    def _read(self) -> None:
        try:
            while not self._closing:
                self._decoder.feed(self._port.read(self._port.in_waiting or 1))
        except (SerialException, OSError) as error:
            if not self._closing:
                self._loop.call_soon_threadsafe(self._received.put_nowait, LinkClosed(f"{self.name}: {error}"))

    async def answered(self) -> None:
        """Waits for the robot's first frame, pinging until it comes; the frame itself, a pong or a frame of a stream
        an earlier session left subscribed, is dropped."""
        ping = pb.Message(pingmsg=pb.PingMsg()).SerializeToString()
        while True:
            await self.send(ping)
            try:
                await asyncio.wait_for(self.recv(), PROBE_INTERVAL_S)
                return
            except TimeoutError:
                continue

    async def send(self, message: bytes) -> None:
        try:
            self._port.write(serial_frame.encode(message))
        except (SerialException, OSError) as error:
            raise LinkClosed(f"{self.name}: {error}") from error

    async def recv(self) -> bytes:
        received = await self._received.get()
        if isinstance(received, LinkClosed):
            self._received.put_nowait(received)  # every later recv fails alike
            raise received
        return received

    async def close(self) -> None:
        self._closing = True
        await asyncio.to_thread(self._reader.join)
        self._port.close()
