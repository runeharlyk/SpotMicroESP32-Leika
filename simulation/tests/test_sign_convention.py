"""The body-frame sign convention against platform_shared/sign_convention.json, which the firmware's host test
(esp32/test/host/sign_convention_test.cpp) and the app's sign-convention.spec.ts pass too: on the NumPy port of the
firmware's gait and odometry, on the client's stick mapping, and on goal moves in MuJoCo. The port has no STAND sticks
and turns the body only by the levelling's omega and psi, so the stand rows and the phi attitude are the firmware's and
the app's to check. docs/conventions.md derives the signs."""
import json
import math
from pathlib import Path

import numpy as np
import pytest

from src.leika import Robot
from src.leika import speed_model
from src.leika.backends.base import Velocity
from src.leika.backends.sim import MODEL_TO_FIRMWARE, TO_FIRMWARE_FEET
from src.leika.constants import Gait
from src.robot import firmware_gait as fg
from src.robot.firmware_odometry import FusedOdometry
from src.robot.stand_compensation import MODEL_TO_REP, apply_firmware_attitude

FIXTURE = json.loads((Path(__file__).resolve().parents[2] / "platform_shared" / "sign_convention.json").read_text())
LEGS = FIXTURE["legs"]
SIDES = np.array([leg["side"] for leg in LEGS])
WALK = FIXTURE["walk"]
COMMANDS = {c["name"]: c for c in WALK["commands"]}
ATTITUDES = {p["name"]: p for p in FIXTURE["attitude"]["poses"] if p["degrees"]["phi"] == 0}
GOALS = FIXTURE["goals"]
MOVES = {m["name"]: m for m in GOALS["moves"]}


def _sign(value: float, zero: float) -> int:
    return 1 if value > zero else (-1 if value < -zero else 0)


def _rep(model_points) -> np.ndarray:
    """Points of the port's base frame (+X left, +Y rear, +Z up) in the fixture's (forward, left, up), in the
    firmware's leg order."""
    return (np.asarray(model_points)[:, :3] @ MODEL_TO_REP.T)[fg.FIRMWARE_ORDER]


def _rotation_signs(feet: np.ndarray, rest: np.ndarray, zero: float) -> list[int]:
    """[roll, pitch, yaw] of the body over its feet (forward, left, up in the body frame): world up is the normal of
    the ground the feet span, and the yaw the turn that carries the feet of `rest` about their centre onto these,
    negated, as the feet stand still while the body turns. Roll lowers the right side, so it tips world up toward
    body left; pitch lowers the nose, so it tips world up toward body backward."""
    across = SIDES[:, 1] @ feet / 2
    along = SIDES[:, 0] @ feet / 2
    up = np.cross(along, across)
    up /= np.linalg.norm(up)
    p = rest[:, :2] - rest[:, :2].mean(axis=0)
    q = feet[:, :2] - feet[:, :2].mean(axis=0)
    yaw = -math.atan2(float(np.sum(p[:, 0] * q[:, 1] - p[:, 1] * q[:, 0])), float(np.sum(p * q)))
    return [_sign(up[1], zero), _sign(-up[0], zero), _sign(yaw, zero)]


def _body_frame_feet(body: fg.BodyState) -> np.ndarray:
    """The feet in the body frame as the port's Kinematics.inverse_kinematics moves them before it solves each leg,
    checked against it: a level body at the origin standing on them gets the same legs."""
    rotation = fg._rot_z(body.psi) @ fg._rot_y(body.phi) @ fg._rot_x(body.omega)
    local = (np.asarray(body.feet)[:, :3] - [body.xm, body.ym, body.zm]) @ rotation
    kinematics = fg.Kinematics()
    np.testing.assert_allclose(kinematics.inverse_kinematics(fg.BodyState(feet=local)),
                               kinematics.inverse_kinematics(body), atol=1e-9)
    return local


def test_the_legs_sit_on_their_fixture_sides_in_the_firmwares_order():
    for feet in (fg.DEFAULT_FEET, np.column_stack([fg.FIRMWARE_FEET, np.zeros(4)])):
        np.testing.assert_array_equal(np.sign(_rep(feet)[:, :2]), SIDES)
    np.testing.assert_array_equal(fg.FIRMWARE_LEFT, SIDES[:, 1])
    initials = ["".join(word[0] for word in leg["name"].split()) for leg in LEGS]
    assert [fg.LEG_NAMES[i] for i in fg.FIRMWARE_ORDER] == initials


@pytest.mark.parametrize("name", list(ATTITUDES))
def test_the_levellings_attitude_rotates_the_body_with_the_fixture_signs(name):
    row = ATTITUDES[name]
    rest = _rep(_body_frame_feet(fg.BodyState()))
    body = fg.BodyState()
    apply_firmware_attitude(body, row["degrees"]["omega"], row["degrees"]["psi"])
    feet = _rep(_body_frame_feet(body))
    assert _rotation_signs(feet, rest, FIXTURE["attitude"]["zero_rad"]) == row["rotation_sign"]


def _walk(command: dict):
    """The sticks held in the port as the simulation's control tick holds them, from a stand: the mean per-tick
    displacement of each planted foot (forward, left; firmware leg order) and the odometry over the ticks."""
    stick = command["stick"]
    sticks = speed_model.Sticks(stick["lx"], stick["ly"], stick["rx"])
    gait = fg.GaitState()
    fg.set_mode(gait, fg.TROT)
    controller = fg.GaitController()
    body = fg.BodyState()
    odometry = FusedOdometry()
    dt = WALK["dt_s"]

    def tick():
        fg.gait_action(sticks, gait)
        controller.step(gait, dt)
        controller.generate_feet(gait, body)
        stance = controller.stance(gait)
        odometry.update((body.feet + TO_FIRMWARE_FEET) @ MODEL_TO_FIRMWARE.T, stance, dt=dt)
        return _rep(body.feet), stance[fg.FIRMWARE_ORDER]

    for _ in range(WALK["settle_ticks"]):
        feet, stance = tick()
    odometry.reset()
    total, planted = np.zeros((4, 2)), np.zeros(4, dtype=int)
    for _ in range(WALK["ticks"]):
        before, was = feet, stance
        feet, stance = tick()
        both = was & stance
        total[both] += (feet - before)[both, :2]
        planted += both
    return total / np.maximum(planted, 1)[:, None], planted, odometry


@pytest.mark.parametrize("name", list(COMMANDS))
def test_the_port_moves_each_stance_foot_with_the_fixture_signs(name):
    foot, planted, _ = _walk(COMMANDS[name])
    for i, leg in enumerate(LEGS):
        assert planted[i] > 0, leg["name"]
        assert [_sign(v, WALK["zero_m_per_tick"]) for v in foot[i]] == COMMANDS[name]["stance_foot_sign"][i], leg["name"]


@pytest.mark.parametrize("name", list(COMMANDS))
def test_the_ported_odometry_follows_the_walk_with_the_fixture_signs(name):
    _, _, odometry = _walk(COMMANDS[name])
    e = odometry.estimate
    expected = COMMANDS[name]["odometry_sign"]
    assert [_sign(e.x, WALK["zero_m"]), _sign(e.y, WALK["zero_m"]), _sign(e.yaw, WALK["zero_rad"])] == expected
    assert _sign(odometry.kinematic_yaw, WALK["zero_rad"]) == expected[2]


# The client walks a velocity on the sticks the speed model finds for it, and reads the velocity sticks walk from it.
@pytest.mark.parametrize("name", list(COMMANDS))
def test_the_client_maps_the_walk_velocity_to_the_fixture_sticks_and_back(name):
    command = COMMANDS[name]
    stick = command["stick"]
    sticks = speed_model.Sticks(stick["lx"], stick["ly"], stick["rx"])
    walked = speed_model.velocity_of(fg.VARIANT, Gait.TROT, sticks, fg.STAND_HEIGHT)
    assert [_sign(v, 0) for v in (walked.vx, walked.vy, walked.yaw_rate)] == command["odometry_sign"]
    found = speed_model.sticks_for(fg.VARIANT, Gait.TROT, walked, fg.STAND_HEIGHT)
    assert [_sign(v, 0) for v in (found.lx, found.ly, found.rx)] == [_sign(v, 0) for v in stick.values()]


@pytest.fixture
def robot():
    client = Robot("simulation", realtime=False)
    client.connect()
    client.stand()
    client.sleep(1.0)
    yield client
    client.close()


@pytest.mark.parametrize("name", list(MOVES))
def test_a_goal_move_walks_with_the_fixture_sticks_and_ends_on_its_side(robot, monkeypatch, name):
    row = MOVES[name]
    goal = row["goal"]
    backend = robot._backend
    sim = backend.sim
    # The goal sets its first sticks before it waits on the walk for the first time.
    first_sticks = []
    tick = backend.sleep

    def sleep(seconds):
        if not first_sticks:
            first_sticks.append(backend._sticks)
        tick(seconds)

    monkeypatch.setattr(backend, "sleep", sleep)
    robot.reset_odometry()
    a = sim.base_qposadr
    start, start_yaw = MODEL_TO_REP @ sim.data.qpos[a : a + 3], robot.state().yaw
    robot.goal_move(forward=goal["forward_m"], sideways=goal["sideways_m"], turn_deg=goal["turn_deg"])

    sticks = first_sticks[0]
    zero = GOALS["zero_stick"]
    assert [_sign(sticks.lx, zero), _sign(sticks.ly, zero), _sign(sticks.rx, zero)] == list(row["stick_sign"].values())
    expected = row["odometry_sign"]
    reached = robot.odometry()
    assert [_sign(reached.x, GOALS["zero_m"]), _sign(reached.y, GOALS["zero_m"]),
            _sign(math.radians(reached.yaw), GOALS["zero_rad"])] == expected
    # MuJoCo's base, in the frame it faced at the start, goes the same way.
    moved = (MODEL_TO_REP @ sim.data.qpos[a : a + 3] - start)[:2]
    c, s = math.cos(math.radians(start_yaw)), math.sin(math.radians(start_yaw))
    turned = math.radians((robot.state().yaw - start_yaw + 180) % 360 - 180)
    assert [_sign(c * moved[0] + s * moved[1], GOALS["zero_m"]), _sign(-s * moved[0] + c * moved[1], GOALS["zero_m"]),
            _sign(turned, GOALS["zero_rad"])] == expected
