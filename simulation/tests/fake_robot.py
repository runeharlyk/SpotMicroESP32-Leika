"""A robot on a local WebSocket or on a fake USB serial port: answers pings, the variant, the servo supply
(`supply_voltage`, 0 as a firmware from before it), the calibration, the peripheral settings and the odometry, reports its mode and gait in telemetry at 10 Hz, and records what it receives.
`obey=False` ignores mode changes; `silent=True` stops all sending. Replies carry status 200 as the firmware's do;
`status` changes it.

Over serial, `serial_class` stands in for pyserial's Serial: the client opens a port to this robot, which writes a log
line before its frames, as the firmware does on its console.

A goal move is answered 202 and runs for `goal_s`, then ends in `goal_end`, each change broadcast unless
`goal_quiet`; `goals=False` refuses goal requests as a firmware from before them does.

The animation clips are the built-ins and the files uploaded to /animations/<name>.pb in `files`, taken over the
WebSocket only, as the firmware takes file uploads; a file loads unless its schema is not 1 or it has no keyframes.
A clip plays as ANIMATE for `clip_s` from the mode it was played in, reporting its status every 100 ms to a client
subscribed to it: sit holds its end and wave loops until stopped, and the rest play once."""
import asyncio
import math
import threading
import time

from serial import SerialException
from websockets.asyncio.server import serve

from src.leika import serial_frame
from src.proto import animation_pb2, api_pb2, filesystem_pb2 as fs, message_pb2 as pb

SERIAL_PORT = "COM99"
BUILTIN_CLIPS = ("bow", "sit", "wave")
ANIMATION_STATUS = pb.Message.DESCRIPTOR.fields_by_name["animation_status"].number


class FakeRobot:
    def __init__(self, variant="SPOTMICRO_ESP32_MINI_V2", obey=True, transport="websocket", goals=True,
                 supply_voltage=6.0):
        self.variant, self.obey, self.silent = variant, obey, False
        self.supply_voltage = supply_voltage
        self.status = 200
        self.connections = 0
        self.mode, self.gait = pb.DEACTIVATED, pb.TROT
        self.peripheral_settings = api_pb2.PeripheralSettings(sda=21, scl=22, frequency=400000)
        self.peripheral_settings.imu.mounting[:] = [0, 1, 0, -1, 0, 0, 0, 0, 1]
        self.odometry = pb.OdometryData(x=0.5, y=-0.25, yaw=math.pi / 2, distance=0.75, kinematic_yaw=math.pi / 3)
        self.goals, self.goal_s, self.goal_end, self.goal_quiet = goals, 0.3, pb.GOAL_REACHED, False
        self.goal = pb.GoalStatus()
        self.transport = transport
        self.subscribed = set()  # the Message tags the client subscribed to
        self.files = {}  # path -> bytes, the robot's file system
        self.uploads = {}  # transfer id -> (path, chunks expected, chunks received)
        self.clip_s = 0.4
        self.played = []  # every AnimationPlay the robot took
        self._stopping = False
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
                elif kind == "sub_notif":
                    self.subscribed.add(message.sub_notif.tag)
                elif kind == "unsub_notif":
                    self.subscribed.discard(message.unsub_notif.tag)
                elif kind == "fs_upload_data" and self.transport == "websocket":
                    await self._upload_data(send, message.fs_upload_data)
                elif kind == "correlation_request":
                    request = message.correlation_request
                    reply = self._reply(request)
                    await send(reply.SerializeToString())
                    status = reply.correlation_response.status_code
                    if status == 202:
                        asyncio.create_task(self._walk_goal(send, self.goal.id))
                    elif request.HasField("animation_play") and status == 200:
                        asyncio.create_task(self._play(request.animation_play.name))
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
        elif kind == "servo_settings_request":
            response.servo_settings.supply_voltage = self.supply_voltage
        elif kind in ("peripheral_settings_request", "peripheral_settings"):
            if kind == "peripheral_settings":
                self.peripheral_settings.CopyFrom(request.peripheral_settings)
            response.peripheral_settings.CopyFrom(self.peripheral_settings)
        elif kind == "odometry_request":
            response.odometry.CopyFrom(self.odometry)
            if request.odometry_request.reset:
                self.odometry = pb.OdometryData()
        elif kind == "animation_list_request":
            for name in BUILTIN_CLIPS:
                response.animation_list.animations.add(name=name, builtin=True)
            for path, data in self.files.items():
                response.animation_list.animations.add(name=path.removeprefix("/animations/").removesuffix(".pb"),
                                                       size=len(data))
        elif kind == "animation_validate":
            error = self._clip_error(request.animation_validate.name)
            response.animation_report.ok = error is None
            if error:
                response.status_code, response.animation_report.error = 422, error
            else:
                response.animation_report.duration = self.clip_s
        elif kind == "animation_play":
            error = self._clip_error(request.animation_play.name)
            if error:
                response.status_code, response.error_message = 404 if error == "no such clip" else 422, error
            elif self.mode == pb.DEACTIVATED:
                response.status_code, response.error_message = 409, "Stand up first"
            else:
                self.played.append(request.animation_play)
        elif kind == "animation_stop":
            self._stopping = True
        elif kind == "fs_upload_start":
            start = request.fs_upload_start
            transfer = len(self.uploads) + 1
            self.uploads[transfer] = (start.path, start.total_chunks, [])
            response.fs_upload_start_response.success = True
            response.fs_upload_start_response.transfer_id = transfer
        elif kind == "fs_delete_request":
            response.fs_delete_response.success = self.files.pop(request.fs_delete_request.path, None) is not None
        else:
            response.imu_calibrate_data.CopyFrom(pb.IMUCalibrateData(success=True, levelled=True, tilt_deg=4.2))
        return pb.Message(correlation_response=response)

    def _clip_error(self, name):
        if name in BUILTIN_CLIPS:
            return None
        data = self.files.get(f"/animations/{name}.pb")
        if data is None:
            return "no such clip"
        clip = animation_pb2.Animation.FromString(data)
        if clip.schema != 1:
            return "schema must be 1"
        return None if clip.keyframes else "no keyframes"

    async def _upload_data(self, send, data):
        path, expected, chunks = self.uploads[data.transfer_id]
        chunks.append(data.data)
        if len(chunks) == expected:
            self.files[path] = b"".join(chunks)
            complete = fs.FSUploadComplete(transfer_id=data.transfer_id, success=True)
            await send(pb.Message(fs_upload_complete=complete).SerializeToString())

    # Reported to whichever client is connected, as the robot reports to every subscriber.
    async def _play(self, name):
        """Entry, the clip, then exit back to the mode it was played in, STAND from WALK."""
        before = self.mode
        self.mode, self._stopping = pb.ANIMATE, False
        await self._report(name, pb.ANIM_ENTRY, 0.0)
        t = 0.0
        while not self._stopping:
            held = t >= self.clip_s and name == "sit"
            await self._report(name, pb.ANIM_HOLD if held else pb.ANIM_PLAYING, min(t, self.clip_s))
            t += 0.1
            if t > self.clip_s and name == "wave":
                t = 0.0
            elif t > self.clip_s and name != "sit":
                break
        await self._report(name, pb.ANIM_EXIT, min(t, self.clip_s))
        await self._report(name, pb.ANIM_IDLE, 0.0, wait=False)
        self.mode = pb.STAND if before == pb.WALK else before

    async def _report(self, name, state, t, wait=True):
        if ANIMATION_STATUS in self.subscribed:
            status = pb.AnimationStatus(name=name, state=state, t=t)
            await self._send(pb.Message(animation_status=status).SerializeToString())
        if wait:
            await asyncio.sleep(0.1)

    async def _walk_goal(self, send, goal):
        await asyncio.sleep(0.05)
        self.goal.timeout_s = self.goal_s + 1.0
        await self._broadcast_goal(send)
        await asyncio.sleep(self.goal_s)
        if self.goal.id == goal and self.goal.state == pb.GOAL_RUNNING:
            self.goal.state = self.goal_end
            await self._broadcast_goal(send)

    async def _broadcast_goal(self, send):
        if not self.goal_quiet:
            await send(pb.Message(goal_status=self.goal).SerializeToString())

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
