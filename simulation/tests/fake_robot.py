"""A robot on a local WebSocket or on a fake USB serial port: answers pings, the variant and the calibration, reports its
mode and gait in telemetry at 10 Hz, and records what it receives. `obey=False` ignores mode changes; `silent=True`
stops all sending. Replies carry status 200 as the firmware's do; `status` changes it.

Over serial, `serial_class` stands in for pyserial's Serial: the client opens a port to this robot, which writes a log
line before its frames, as the firmware does on its console."""
import asyncio
import threading
import time

from serial import SerialException
from websockets.asyncio.server import serve

from src.leika import serial_frame
from src.proto import message_pb2 as pb

SERIAL_PORT = "COM99"


class FakeRobot:
    def __init__(self, variant="SPOTMICRO_ESP32_MINI_V2", obey=True, transport="websocket"):
        self.variant, self.obey, self.silent = variant, obey, False
        self.status = 200
        self.connections = 0
        self.mode, self.gait = pb.DEACTIVATED, pb.TROT
        self.received = []  # (monotonic time, Message)
        self.ports = []  # the serial ports opened to this robot
        self._loop = asyncio.new_event_loop()
        self._server = None
        self._send = None
        self._drop = None
        threading.Thread(target=self._loop.run_forever, daemon=True).start()
        if transport == "websocket":
            port = asyncio.run_coroutine_threadsafe(self._start(), self._loop).result(5)
            self.target = f"127.0.0.1:{port}"
        else:
            self.target = SERIAL_PORT

    async def _start(self):
        self._server = await serve(self._handle, "127.0.0.1", 0)
        return self._server.sockets[0].getsockname()[1]

    async def _handle(self, socket):
        self._drop = socket.close
        await self._serve(socket.send, socket)

    async def _serve(self, send, frames):
        self._send = send
        self.connections += 1
        sender = asyncio.create_task(self._telemetry(send))
        try:
            async for frame in frames:
                message = pb.Message.FromString(frame)
                self.received.append((time.monotonic(), message))
                kind = message.WhichOneof("message")
                if kind == "mode" and self.obey:
                    self.mode = message.mode.mode
                elif kind == "walk_gait":
                    self.gait = message.walk_gait.gait
                elif kind == "pingmsg":
                    await send(pb.Message(pongmsg=pb.PongMsg()).SerializeToString())
                elif kind == "correlation_request":
                    await send(self._reply(message.correlation_request).SerializeToString())
        finally:
            sender.cancel()
            self.connections -= 1

    def _reply(self, request):
        response = pb.CorrelationResponse(correlation_id=request.correlation_id, status_code=self.status)
        if request.WhichOneof("request") == "features_data_request":
            response.features_data_response.variant = self.variant
        else:
            response.imu_calibrate_data.CopyFrom(pb.IMUCalibrateData(success=True, levelled=True, tilt_deg=4.2))
        return pb.Message(correlation_response=response)

    def _tick(self, link_lost=False):
        tick = pb.TickSample(t_us=int(time.monotonic() * 1e6), mode=self.mode, gait=self.gait, link_lost=link_lost)
        tick.imu.rpy.extend([0.1, -0.05, 1.0])
        tick.imu.gyro.extend([0.0, 0.0, 0.2])
        tick.imu.accel.extend([0.0, 0.0, 9.81])
        tick.angles.extend([0.5] * 12)
        return tick

    async def _telemetry(self, send):
        while True:
            if not self.silent:
                await send(pb.Message(telemetry_batch=pb.TelemetryBatch(ticks=[self._tick()])).SerializeToString())
            await asyncio.sleep(0.1)

    def blip(self):
        """One batch in which the dead-man stop fires and clears again, as after a short WiFi stall."""
        batch = pb.TelemetryBatch(ticks=[self._tick(link_lost=True), self._tick()])
        frame = pb.Message(telemetry_batch=batch).SerializeToString()
        asyncio.run_coroutine_threadsafe(self._send(frame), self._loop).result(5)

    def kinds(self):
        return [message.WhichOneof("message") for _, message in self.received]

    def inputs(self):
        return [(t, message.controller_data) for t, message in self.received if message.WhichOneof("message") == "controller_data"]

    def drop(self):
        """Closes the socket, or unplugs the USB cable."""
        if self.ports:
            self.ports[-1].unplug()
        else:
            asyncio.run_coroutine_threadsafe(self._drop(), self._loop).result(5)

    def serial_class(self, **settings):
        return _FakeSerial(self, **settings)

    def stop(self):
        asyncio.run_coroutine_threadsafe(self._stop(), self._loop).result(5)
        self._loop.call_soon_threadsafe(self._loop.stop)

    async def _stop(self):
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()


class _FakeSerial:
    """The robot's USB port as pyserial hands it out: unopened until open(), DTR and RTS asserted unless cleared first,
    reads that time out, and every read and write failing once unplugged."""

    def __init__(self, robot, timeout=None, **_):
        self.port = None
        self.dtr = self.rts = True
        self.opened_with = None  # (dtr, rts) as the port opened
        self._robot, self._timeout = robot, timeout
        self._out = bytearray()
        self._ready = threading.Condition()
        self._lost = False
        self._frames = None
        self._decoder = None

    def open(self):
        assert self.port == SERIAL_PORT, f"no robot on {self.port}"
        self.opened_with = (self.dtr, self.rts)
        self._robot.ports.append(self)
        self.deliver(b"I (812) main: Booting robot\r\n")
        asyncio.run_coroutine_threadsafe(self._attach(), self._robot._loop).result(5)

    async def _attach(self):
        self._frames = asyncio.Queue()
        self._decoder = serial_frame.Decoder(self._frames.put_nowait, lambda line: None)
        asyncio.get_running_loop().create_task(self._robot._serve(self._send, self._received()))

    async def _received(self):
        while (frame := await self._frames.get()) is not None:
            yield frame

    async def _send(self, message):
        self.deliver(serial_frame.encode(message))

    def deliver(self, data):
        with self._ready:
            self._out += data
            self._ready.notify_all()

    def unplug(self):
        with self._ready:
            self._lost = True
            self._ready.notify_all()
        self._robot._loop.call_soon_threadsafe(self._frames.put_nowait, None)

    @property
    def in_waiting(self):
        return len(self._out)

    def read(self, size=1):
        with self._ready:
            if not self._out and not self._lost:
                self._ready.wait(self._timeout)
            if self._lost:
                raise SerialException("the device has been lost")
            data = bytes(self._out[:size])
            del self._out[:size]
            return data

    def write(self, data):
        if self._lost:
            raise SerialException("the device has been lost")
        self._robot._loop.call_soon_threadsafe(self._decoder.feed, bytes(data))
        return len(data)

    def close(self):
        if not self._lost:
            self._robot._loop.call_soon_threadsafe(self._frames.put_nowait, None)
