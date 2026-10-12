"""The real robot over its WebSocket (ws://<host>/api/ws) or its native USB port: modes, controller input with a
keep-alive, telemetry.

One thread runs an asyncio loop that owns the link; the public methods are synchronous and wait on what telemetry
reports. While the velocity is not zero the input is re-sent every KEEPALIVE_S, inside the firmware's 500 ms dead-man.
A goal move runs on the robot, which needs nothing from the link until it reports the end.
"""
import asyncio
import math
import threading
import time

from ...proto import api_pb2, message_pb2 as pb
from .. import speed_model
from ..link import LinkClosed, is_serial_port, open_link
from ..constants import Gait, Mode
from .base import (
    ZERO, Calibration, GoalCancelled, Odometry, RobotDisconnected, RobotError, RobotState, RobotTimeout, Velocity,
)

KEEPALIVE_S = 0.1
MODE_TIMEOUT_S = 3.0
REQUEST_TIMEOUT_S = 5.0
SILENCE_S = 3.0  # telemetry arrives every 100 ms: this long without a frame means the link is gone
NO_STICKS = speed_model.Sticks(0.0, 0.0, 0.0)
GOAL_POLL_S = 1.0  # asks for the goal's status this long after the last word of it, in case a broadcast went astray
GOAL_START_S = 3.0  # the control task takes a goal within a tick: this long without its status, it is lost
GOAL_MARGIN_S = 2.0  # waited beyond the robot's own timeout for the goal's end


def _tag(name: str) -> int:
    return pb.Message.DESCRIPTOR.fields_by_name[name].number


class RealBackend:
    def __init__(self, target: str):
        self._target = target
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._link = None
        self._tasks: list[asyncio.Task] = []
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
        self._features: pb.FeaturesDataResponse | None = None
        self._recording: list[pb.TickSample] | None = None
        self._goal_status = pb.GoalStatus()
        self._goals: bool | None = None
        self._goal_heard_at = 0.0

    # --- connection --------------------------------------------------------------------------------------------
    # A connect that fails partway closes what it opened: the robot would otherwise keep streaming to a dead subscriber.
    def connect(self) -> None:
        self._thread.start()
        try:
            self._run(self._open(), REQUEST_TIMEOUT_S)
            self._features = self.request(pb.CorrelationRequest(features_data_request=pb.FeaturesDataRequest())).features_data_response
            self._variant = speed_model.known_variant(self._features.variant)
            # A USB-serial chip's port reaches the console UART, which answers requests but streams nothing.
            hint = " (driving and telemetry need the board's native USB port)" if is_serial_port(self._target) else ""
            self._wait(lambda: self._tick is not None, MODE_TIMEOUT_S, "no telemetry from the robot" + hint)
        except BaseException:
            self.close()
            raise

    # Closes the link even after it failed, so a silent robot's connection is not left open; closing again does nothing.
    def close(self) -> None:
        if not self._thread.is_alive():
            return
        if self._link is not None:
            closing = asyncio.run_coroutine_threadsafe(self._close(), self._loop)
            try:
                closing.result(2.0)
            except (TimeoutError, OSError):
                pass  # already gone
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(2.0)

    # A robot learns of a closed USB port only when its buffer stays full, so the subscription is ended first.
    async def _close(self) -> None:
        try:
            await self._link.send(pb.Message(unsub_notif=pb.UnsubscribeNotification(tag=_tag("telemetry_batch"))).SerializeToString())
        except LinkClosed:
            pass
        for task in self._tasks:
            task.cancel()
        await asyncio.gather(*self._tasks, return_exceptions=True)
        await self._link.close()

    async def _open(self) -> None:
        self._link = await open_link(self._target, REQUEST_TIMEOUT_S)
        for stream in ("telemetry_batch", "goal_status"):
            await self._link.send(pb.Message(sub_notif=pb.SubscribeNotification(tag=_tag(stream))).SerializeToString())
        self._tasks = [self._loop.create_task(self._read()), self._loop.create_task(self._keep_alive())]

    async def _read(self) -> None:
        try:
            while True:
                frame = await asyncio.wait_for(self._link.recv(), SILENCE_S)
                message = pb.Message.FromString(frame)
                kind = message.WhichOneof("message")
                if kind == "telemetry_batch" and message.telemetry_batch.ticks:
                    with self._changed:
                        # Every tick, not only the newest: a short stall trips and clears the dead-man within a batch.
                        for tick in message.telemetry_batch.ticks:
                            self._drops += tick.link_lost and not self._lost
                            self._lost = tick.link_lost
                        if self._recording is not None:
                            self._recording.extend(message.telemetry_batch.ticks)
                        self._tick = message.telemetry_batch.ticks[-1]
                        self._changed.notify_all()
                elif kind == "goal_status":
                    self._heard_goal(message.goal_status)
                elif kind == "correlation_response":
                    future = self._replies.pop(message.correlation_response.correlation_id, None)
                    if future is not None and not future.done():
                        future.set_result(message.correlation_response)
        except TimeoutError:
            reason = f"nothing from the robot at {self._link.name} for {SILENCE_S:g} s"
        except LinkClosed as error:
            reason = f"the robot is gone: {error}"
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
            height=self._height, speed=speed_model.SPEED, s1=speed_model.STEP_HEIGHT,
        )
        try:
            await self._link.send(pb.Message(controller_data=data).SerializeToString())
        except LinkClosed:
            pass  # the reader reports it

    async def _send(self, message: pb.Message) -> None:
        await self._link.send(message.SerializeToString())

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
        except LinkClosed as error:
            raise RobotDisconnected(f"the robot is gone: {error}") from error
        except (TimeoutError, OSError) as error:
            raise RobotTimeout(f"no answer from the robot at {self._target}") from error

    def request(self, request: pb.CorrelationRequest) -> pb.CorrelationResponse:
        """Sends a request and returns the robot's reply; a refusal raises RobotError."""
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
        self.set_velocity(self._requested)  # the servos follow other sticks at another height

    def set_velocity(self, velocity: Velocity) -> Velocity:
        self._requested = velocity
        self._sticks = speed_model.sticks_for(self._variant, self._gait, velocity, self._height)
        self._run(self._send_input(), REQUEST_TIMEOUT_S)
        return speed_model.velocity_of(self._variant, self._gait, self._sticks, self._height)

    def max_velocity(self) -> Velocity:
        return speed_model.max_velocity(self._variant, self._gait, self._height)

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

    def set_mag_offset(self, offset) -> None:
        """Stores the compass's hard-iron offset (microtesla, chip frame), keeping every other peripheral setting."""
        reply = self.request(pb.CorrelationRequest(peripheral_settings_request=api_pb2.PeripheralSettingsRequest()))
        settings = reply.peripheral_settings
        settings.imu.mag_offset[:] = [float(value) for value in offset]
        self.request(pb.CorrelationRequest(peripheral_settings=settings))

    def calibrate(self) -> Calibration:
        data = self.request(pb.CorrelationRequest(imu_calibrate_execute=pb.IMUCalibrateExecute())).imu_calibrate_data
        return Calibration(still=data.success, levelled=data.levelled, tilt_deg=data.tilt_deg)

    # --- beyond the backend interface: what a check of the firmware reads -------------------------------------
    @property
    def features(self) -> pb.FeaturesDataResponse:
        """What the robot reported about itself on connecting."""
        return self._features

    def start_recording(self) -> None:
        """Keeps every telemetry tick from now on."""
        with self._changed:
            self._recording = []

    def recorded(self) -> list[pb.TickSample]:
        """The ticks kept since start_recording(), oldest first."""
        with self._changed:
            return list(self._recording or [])

    # Asked for rather than read from telemetry: a batch in flight would still show the estimate from before a reset.
    def odometry(self) -> Odometry:
        return self._odometry(reset=False)

    def reset_odometry(self) -> Odometry:
        return self._odometry(reset=True)

    def _odometry(self, reset: bool) -> Odometry:
        data = self.request(pb.CorrelationRequest(odometry_request=pb.OdometryRequest(reset=reset))).odometry
        return Odometry(x=data.x, y=data.y, yaw=math.degrees(data.yaw), distance=data.distance)

    # --- goal moves ----------------------------------------------------------------------------------------------
    def supports_goals(self) -> bool:
        """Whether the firmware takes goal moves; one from before them refuses the status request as unknown."""
        if self._goals is None:
            request = pb.CorrelationRequest(goal_status_request=pb.GoalStatusRequest())
            reply = self._run(self._ask(request), REQUEST_TIMEOUT_S + 1)
            self._goals = reply.status_code < 400
        return self._goals

    def goal_move(self, forward: float, sideways: float, turn_deg: float, speed: float) -> None:
        move = pb.GoalMove(forward_m=forward, sideways_m=sideways, turn_deg=turn_deg, speed=speed)
        goal = self.request(pb.CorrelationRequest(goal_move=move)).goal_status.id
        try:
            status = self._await_goal(goal)
        except BaseException:
            # Ctrl-C or a lost reply: the robot would otherwise walk on with nobody watching.
            try:
                self.request(pb.CorrelationRequest(goal_cancel=pb.GoalCancel()))
            except RobotError:
                pass  # the link is gone, and the robot cancels the goal of a client that left
            raise
        if status.state == pb.GOAL_TIMED_OUT:
            raise RobotTimeout(f"the robot gave up on goal {goal} after {status.timeout_s:.1f} s, "
                               f"{status.progress:.3g} along ({_target(status)})")
        if status.state != pb.GOAL_REACHED:
            raise GoalCancelled(f"the robot cancelled goal {goal} {status.progress:.3g} along ({_target(status)})")

    def _heard_goal(self, status: pb.GoalStatus) -> None:
        with self._changed:
            if status.id >= self._goal_status.id:
                self._goal_status = status
                self._goal_heard_at = time.monotonic()
                self._changed.notify_all()

    # The goal's last status: its end, or that a newer goal replaced it.
    def _await_goal(self, goal: int) -> pb.GoalStatus:
        with self._changed:
            started = self._goal_heard_at = time.monotonic()
        deadline = started + GOAL_START_S
        while True:
            with self._changed:
                status = self._goal_status
            if status.id > goal:
                return pb.GoalStatus(id=goal, state=pb.GOAL_CANCELLED)
            if status.id == goal:
                if status.state != pb.GOAL_RUNNING:
                    return status
                if status.timeout_s:
                    deadline = started + status.timeout_s + GOAL_MARGIN_S
            now = time.monotonic()
            if now > deadline:
                raise RobotTimeout(f"the robot never reported the end of goal {goal}")
            if now - self._goal_heard_at >= GOAL_POLL_S:
                reply = self.request(pb.CorrelationRequest(goal_status_request=pb.GoalStatusRequest()))
                self._heard_goal(reply.goal_status)
                self._goal_heard_at = time.monotonic()
                continue
            with self._changed:
                if self._goal_status is status:
                    if self._error is not None:
                        raise self._error
                    self._changed.wait(min(GOAL_POLL_S, max(0.0, deadline - now)))

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


def _target(status: pb.GoalStatus) -> str:
    if status.turn_deg:
        return f"{status.turn_deg:g} degrees"
    return f"{status.forward_m:g} m forward" if status.forward_m else f"{status.sideways_m:g} m to the left"
