"""The real robot over its WebSocket (ws://<host>/api/ws): modes, controller input with a keep-alive, telemetry.

One thread runs an asyncio loop that owns the socket; the public methods are synchronous and wait on what telemetry
reports. While the velocity is not zero the input is re-sent every KEEPALIVE_S, inside the firmware's 500 ms dead-man.
"""
import asyncio
import math
import threading
import time

import websockets

from ...proto import message_pb2 as pb
from .. import speed_model
from ..constants import Gait, Mode
from .base import ZERO, Calibration, RobotDisconnected, RobotError, RobotState, RobotTimeout, Velocity

KEEPALIVE_S = 0.1
MODE_TIMEOUT_S = 3.0
REQUEST_TIMEOUT_S = 5.0
SILENCE_S = 3.0  # telemetry arrives every 100 ms: this long without a frame means the link is gone
STEP_HEIGHT = 0.5
NO_STICKS = speed_model.Sticks(0.0, 0.0, 0.0)


def _tag(name: str) -> int:
    return pb.Message.DESCRIPTOR.fields_by_name[name].number


class RealBackend:
    def __init__(self, host: str):
        self._url = f"ws://{host}/api/ws"
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._socket = None
        self._changed = threading.Condition()
        self._tick = None
        self._error: RobotError | None = None
        self._replies: dict[int, asyncio.Future] = {}
        self._next_id = 1
        self._sticks = NO_STICKS
        self._requested = ZERO
        self._lost = False
        self._drops = 0
        self._height = 0.7
        self._gait = Gait.TROT
        self._variant = None

    # --- connection --------------------------------------------------------------------------------------------
    # A connect that fails partway closes what it opened: the robot would otherwise keep streaming to a dead subscriber.
    def connect(self) -> None:
        self._thread.start()
        try:
            self._run(self._open(), REQUEST_TIMEOUT_S)
            reply = self._request(pb.CorrelationRequest(features_data_request=pb.FeaturesDataRequest()))
            self._variant = speed_model.known_variant(reply.features_data_response.variant)
            self._wait(lambda: self._tick is not None, MODE_TIMEOUT_S, "no telemetry from the robot")
        except BaseException:
            self.close()
            raise

    # Closes the socket even after the link failed, so a silent robot's connection is not left open.
    def close(self) -> None:
        if self._socket is not None:
            closing = asyncio.run_coroutine_threadsafe(self._socket.close(), self._loop)
            try:
                closing.result(2.0)
            except (TimeoutError, OSError, websockets.ConnectionClosed):
                pass  # already gone
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(2.0)

    async def _open(self) -> None:
        self._socket = await websockets.connect(
            self._url, open_timeout=REQUEST_TIMEOUT_S, close_timeout=1, ping_interval=None, max_size=None
        )
        await self._socket.send(pb.Message(sub_notif=pb.SubscribeNotification(tag=_tag("telemetry_batch"))).SerializeToString())
        self._loop.create_task(self._read())
        self._loop.create_task(self._keep_alive())

    async def _read(self) -> None:
        reason = f"the robot at {self._url} closed the connection"
        try:
            while True:
                frame = await asyncio.wait_for(self._socket.recv(), SILENCE_S)
                message = pb.Message.FromString(frame)
                kind = message.WhichOneof("message")
                if kind == "telemetry_batch" and message.telemetry_batch.ticks:
                    with self._changed:
                        # Every tick, not only the newest: a short stall trips and clears the dead-man within a batch.
                        for tick in message.telemetry_batch.ticks:
                            self._drops += tick.link_lost and not self._lost
                            self._lost = tick.link_lost
                        self._tick = message.telemetry_batch.ticks[-1]
                        self._changed.notify_all()
                elif kind == "correlation_response":
                    future = self._replies.pop(message.correlation_response.correlation_id, None)
                    if future is not None and not future.done():
                        future.set_result(message.correlation_response)
        except TimeoutError:
            reason = f"nothing from the robot at {self._url} for {SILENCE_S:g} s"
        except websockets.ConnectionClosed:
            pass
        with self._changed:
            self._error = RobotDisconnected(reason)
            self._changed.notify_all()
        for future in self._replies.values():
            if not future.done():
                future.set_exception(self._error)

    async def _keep_alive(self) -> None:
        while self._error is None:
            await asyncio.sleep(KEEPALIVE_S)
            if self._sticks != NO_STICKS and self._error is None:
                await self._send_input()

    async def _send_input(self) -> None:
        s = self._sticks
        data = pb.ControllerData(
            left=pb.Vector(x=s.lx, y=s.ly), right=pb.Vector(x=s.rx, y=0.0),
            height=self._height, speed=speed_model.SPEED, s1=STEP_HEIGHT,
        )
        try:
            await self._socket.send(pb.Message(controller_data=data).SerializeToString())
        except websockets.ConnectionClosed:
            pass  # the reader reports it

    async def _send(self, message: pb.Message) -> None:
        await self._socket.send(message.SerializeToString())

    async def _ask(self, request: pb.CorrelationRequest) -> pb.CorrelationResponse:
        request.correlation_id = self._next_id
        self._next_id += 1
        future = self._loop.create_future()
        self._replies[request.correlation_id] = future
        await self._send(pb.Message(correlation_request=request))
        return await asyncio.wait_for(future, REQUEST_TIMEOUT_S)

    # --- synchronous helpers -----------------------------------------------------------------------------------
    def _run(self, coroutine, timeout: float):
        if self._error is not None:
            coroutine.close()
            raise self._error
        try:
            return asyncio.run_coroutine_threadsafe(coroutine, self._loop).result(timeout)
        except websockets.ConnectionClosed as error:
            raise RobotDisconnected(f"the robot at {self._url} closed the connection") from error
        except (TimeoutError, OSError) as error:
            raise RobotTimeout(f"no answer from the robot at {self._url}") from error

    def _request(self, request: pb.CorrelationRequest) -> pb.CorrelationResponse:
        reply = self._run(self._ask(request), REQUEST_TIMEOUT_S + 1)
        if reply.status_code >= 400:  # the firmware answers 200 (or 202 while it works) on success
            raise RobotError(f"the robot refused the request: {reply.status_code} {reply.error_message}")
        return reply

    def _wait(self, done, timeout: float, what: str) -> None:
        deadline = time.monotonic() + timeout
        with self._changed:
            while not done():
                if self._error is not None:
                    raise self._error
                left = deadline - time.monotonic()
                if left <= 0:
                    raise RobotTimeout(what)
                self._changed.wait(left)

    # --- the backend interface ---------------------------------------------------------------------------------
    def set_mode(self, mode: Mode) -> None:
        self._run(self._send(pb.Message(mode=pb.ModeData(mode=mode.value))), REQUEST_TIMEOUT_S)
        self._wait(lambda: self._tick.mode == mode.value, MODE_TIMEOUT_S, f"the robot did not report {mode.name}")

    def set_gait(self, gait: Gait) -> None:
        self._run(self._send(pb.Message(walk_gait=pb.WalkGaitData(gait=gait.value))), REQUEST_TIMEOUT_S)
        self._wait(lambda: self._tick.gait == gait.value, MODE_TIMEOUT_S, f"the robot did not report {gait.name}")
        self._gait = gait
        self.set_velocity(self._requested)  # the same velocity takes other sticks in this gait

    def set_height(self, height: float) -> None:
        self._height = height
        self._run(self._send_input(), REQUEST_TIMEOUT_S)

    def set_velocity(self, velocity: Velocity) -> Velocity:
        self._requested = velocity
        self._sticks = speed_model.sticks_for(self._variant, self._gait, velocity)
        self._run(self._send_input(), REQUEST_TIMEOUT_S)
        return speed_model.velocity_of(self._variant, self._gait, self._sticks)

    def max_velocity(self) -> Velocity:
        return speed_model.max_velocity(self._variant, self._gait)

    def state(self) -> RobotState:
        with self._changed:
            if self._error is not None:
                raise self._error
            tick, drops = self._tick, self._drops
        rpy = list(tick.imu.rpy) or [0.0, 0.0, 0.0]
        return RobotState(
            t=tick.t_us / 1e6,
            mode=Mode(tick.mode),
            roll=math.degrees(rpy[0]),
            pitch=math.degrees(rpy[1]),
            yaw=math.degrees(rpy[2]),
            gyro=tuple(tick.imu.gyro) or (0.0, 0.0, 0.0),
            accel=tuple(tick.imu.accel) or (0.0, 0.0, 0.0),
            joints=tuple(math.degrees(a) for a in tick.angles),
            link_lost=tick.link_lost,
            link_drops=drops,
        )

    def calibrate(self) -> Calibration:
        data = self._request(pb.CorrelationRequest(imu_calibrate_execute=pb.IMUCalibrateExecute())).imu_calibrate_data
        return Calibration(still=data.success, levelled=data.levelled, tilt_deg=data.tilt_deg)

    def now(self) -> float:
        return time.monotonic()

    def sleep(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        with self._changed:
            while (left := deadline - time.monotonic()) > 0:
                if self._error is not None:
                    raise self._error
                self._changed.wait(left)
        if self._error is not None:
            raise self._error
