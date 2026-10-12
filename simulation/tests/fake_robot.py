"""A robot on a local WebSocket: answers the variant, the calibration and the odometry, reports its mode and gait in telemetry at
10 Hz, and records what it receives. `obey=False` ignores mode changes; `silent=True` stops all sending. Replies carry
status 200 as the firmware's do; `status` changes it.

A goal move is answered 202 and runs for `goal_s`, then ends in `goal_end`, each change broadcast unless
`goal_quiet`; `goals=False` refuses goal requests as a firmware from before them does."""
import asyncio
import math
import threading
import time

from websockets.asyncio.server import serve

from src.proto import message_pb2 as pb


class FakeRobot:
    def __init__(self, variant="SPOTMICRO_ESP32_MINI_V2", obey=True, goals=True):
        self.variant, self.obey, self.silent = variant, obey, False
        self.status = 200
        self.connections = 0
        self.mode, self.gait = pb.DEACTIVATED, pb.TROT
        self.odometry = pb.OdometryData(x=0.5, y=-0.25, yaw=math.pi / 2, distance=0.75, kinematic_yaw=math.pi / 3)
        self.goals, self.goal_s, self.goal_end, self.goal_quiet = goals, 0.3, pb.GOAL_REACHED, False
        self.goal = pb.GoalStatus()
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
        self.connections += 1
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
                    reply = self._reply(message.correlation_request)
                    await socket.send(reply.SerializeToString())
                    if reply.correlation_response.status_code == 202:
                        asyncio.create_task(self._walk_goal(socket, self.goal.id))
        finally:
            sender.cancel()
            self.connections -= 1

    def _reply(self, request):
        response = pb.CorrelationResponse(correlation_id=request.correlation_id, status_code=self.status)
        kind = request.WhichOneof("request")
        if kind.startswith("goal_") and not self.goals:
            response.status_code, response.error_message = 400, "Unknown request"
        elif kind == "goal_move" and self.status == 200:
            move = request.goal_move
            self.goal = pb.GoalStatus(id=self.goal.id + 1, state=pb.GOAL_RUNNING, forward_m=move.forward_m,
                                      sideways_m=move.sideways_m, turn_deg=move.turn_deg)
            response.status_code = 202
            response.goal_status.CopyFrom(self.goal)
        elif kind == "goal_status_request":
            response.goal_status.CopyFrom(self.goal)
        elif kind == "goal_cancel":
            self.goal.state = pb.GOAL_CANCELLED
        elif kind == "features_data_request":
            response.features_data_response.variant = self.variant
        elif kind == "odometry_request":
            response.odometry.CopyFrom(self.odometry)
            if request.odometry_request.reset:
                self.odometry = pb.OdometryData()
        else:
            response.imu_calibrate_data.CopyFrom(pb.IMUCalibrateData(success=True, levelled=True, tilt_deg=4.2))
        return pb.Message(correlation_response=response)

    async def _walk_goal(self, socket, goal):
        await asyncio.sleep(0.05)
        self.goal.timeout_s = self.goal_s + 1.0
        await self._broadcast_goal(socket)
        await asyncio.sleep(self.goal_s)
        if self.goal.id == goal and self.goal.state == pb.GOAL_RUNNING:
            self.goal.state = self.goal_end
            await self._broadcast_goal(socket)

    async def _broadcast_goal(self, socket):
        if not self.goal_quiet:
            await socket.send(pb.Message(goal_status=self.goal).SerializeToString())

    def _tick(self, link_lost=False):
        tick = pb.TickSample(t_us=int(time.monotonic() * 1e6), mode=self.mode, gait=self.gait, link_lost=link_lost)
        tick.imu.rpy.extend([0.1, -0.05, 1.0])
        tick.imu.gyro.extend([0.0, 0.0, 0.2])
        tick.imu.accel.extend([0.0, 0.0, 9.81])
        tick.angles.extend([0.5] * 12)
        return tick

    async def _telemetry(self, socket):
        while True:
            if not self.silent:
                await socket.send(pb.Message(telemetry_batch=pb.TelemetryBatch(ticks=[self._tick()])).SerializeToString())
            await asyncio.sleep(0.1)

    def blip(self):
        """One batch in which the dead-man stop fires and clears again, as after a short WiFi stall."""
        batch = pb.TelemetryBatch(ticks=[self._tick(link_lost=True), self._tick()])
        frame = pb.Message(telemetry_batch=batch).SerializeToString()
        asyncio.run_coroutine_threadsafe(self._socket.send(frame), self._loop).result(5)

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
