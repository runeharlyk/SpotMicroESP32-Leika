"""A robot on a local WebSocket: answers the variant and the calibration, reports its mode and gait in telemetry at
10 Hz, and records what it receives. `obey=False` ignores mode changes; `silent=True` stops all sending."""
import asyncio
import threading
import time

from websockets.asyncio.server import serve

from src.proto import message_pb2 as pb


class FakeRobot:
    def __init__(self, variant="SPOTMICRO_ESP32_MINI_V2", obey=True):
        self.variant, self.obey, self.silent = variant, obey, False
        self.mode, self.gait = pb.DEACTIVATED, pb.TROT
        self.received = []  # (monotonic time, Message)
        self._loop = asyncio.new_event_loop()
        self._socket = None
        threading.Thread(target=self._loop.run_forever, daemon=True).start()
        self.port = asyncio.run_coroutine_threadsafe(self._start(), self._loop).result(5)

    async def _start(self):
        self._server = await serve(self._handle, "127.0.0.1", 0)
        return self._server.sockets[0].getsockname()[1]

    async def _handle(self, socket):
        self._socket = socket
        sender = asyncio.create_task(self._telemetry(socket))
        try:
            async for frame in socket:
                message = pb.Message.FromString(frame)
                self.received.append((time.monotonic(), message))
                kind = message.WhichOneof("message")
                if kind == "mode" and self.obey:
                    self.mode = message.mode.mode
                elif kind == "walk_gait":
                    self.gait = message.walk_gait.gait
                elif kind == "correlation_request":
                    await socket.send(self._reply(message.correlation_request).SerializeToString())
        finally:
            sender.cancel()

    def _reply(self, request):
        response = pb.CorrelationResponse(correlation_id=request.correlation_id)
        if request.WhichOneof("request") == "features_data_request":
            response.features_data_response.variant = self.variant
        else:
            response.imu_calibrate_data.CopyFrom(pb.IMUCalibrateData(success=True, levelled=True, tilt_deg=4.2))
        return pb.Message(correlation_response=response)

    async def _telemetry(self, socket):
        while True:
            if not self.silent:
                tick = pb.TickSample(t_us=int(time.monotonic() * 1e6), mode=self.mode, gait=self.gait)
                tick.imu.rpy.extend([0.1, -0.05, 1.0])
                tick.imu.gyro.extend([0.0, 0.0, 0.2])
                tick.imu.accel.extend([0.0, 0.0, 9.81])
                tick.angles.extend([0.5] * 12)
                await socket.send(pb.Message(telemetry_batch=pb.TelemetryBatch(ticks=[tick])).SerializeToString())
            await asyncio.sleep(0.1)

    def kinds(self):
        return [message.WhichOneof("message") for _, message in self.received]

    def inputs(self):
        return [(t, message.controller_data) for t, message in self.received if message.WhichOneof("message") == "controller_data"]

    def drop(self):
        asyncio.run_coroutine_threadsafe(self._socket.close(), self._loop).result(5)

    def stop(self):
        asyncio.run_coroutine_threadsafe(self._stop(), self._loop).result(5)
        self._loop.call_soon_threadsafe(self._loop.stop)

    async def _stop(self):
        self._server.close()
        await self._server.wait_closed()
