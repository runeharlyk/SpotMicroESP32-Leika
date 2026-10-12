"""NumPy port of the ESP32 FIRMWARE gait, retargeted to the spot_pico robot.

The gait is the firmware's `esp32/include/motion_states/walk_state.h` with the constants of the
variant the Pico runs, SPOTMICRO_ESP32_MINI (`esp32/include/kinematics.h`): its stand height at
the client's height command, its stride, lift and stance depth, its cadence, its 333 ms command
smoothing, and the velocity->sticks map the client sends the robot (src/leika/speed_model.py).
The *kinematics* are derived from the spot_pico MJCF, since that is the model we train on: a foot
target the firmware places relative to its leg's mount is placed the same, in metres, relative to
the Pico's leg. So a "zero residual" reproduces the firmware gait the Pico is sent, on spot_pico's
geometry; sim_vs_real.py measures how closely.

Coordinate frame (spot_pico `base_link`, MuJoCo world at identity):
    +X = left, +Y = rear, +Z = up   (so forward = -Y, right = -X).
Foot targets are expressed in the base frame. IK returns radians (MuJoCo position
actuators), unlike the firmware which writes degrees to servos.

Leg order (matches MJCF actuator/qpos order): 0=fr, 1=fl, 2=rr, 3=rl.
Each leg's 3 joints are (hip/abduction, femur, tibia).
"""

from __future__ import annotations

from dataclasses import dataclass, field
import math

import numpy as np

from src.leika import speed_model
from src.leika.backends.base import Velocity
from src.leika.constants import Gait


# ---------------------------------------------------------------------------
# spot_pico leg geometry (extracted from src/resources/spot_pico/scene.xml).
# Per leg: hip origin H (base frame), joint-axis signs, and the child-body
# offsets p_femur (coxa->femur), p_tibia (femur->tibia), p_foot (tibia->foot site).
# Axes are body-frame unit axes; the hinge rotates the subtree about sign*axis.
# ---------------------------------------------------------------------------
LEG_NAMES = ("fr", "fl", "rr", "rl")

_LEG_GEOM = {
    #        H (hip origin)                sy   p_femur                    sf   p_tibia                          st   p_foot
    "fr": (np.array([-0.039922, -0.099805, 0.000058]), -1.0, np.array([-0.0513, 0.004305, 0.0]), -1.0,
           np.array([0.00215, 0.043903, -0.037783]), -1.0, np.array([0.006, -0.037123, -0.037123])),
    "fl": (np.array([0.040078, -0.095155, 0.000058]),  1.0, np.array([0.0516, 0.004305, 0.0]), -1.0,
           np.array([-0.0143, 0.043903, -0.037783]),  1.0, np.array([0.006, -0.037123, -0.037123])),
    "rr": (np.array([-0.039922, 0.077545, 0.000058]), -1.0, np.array([-0.0513, 0.004305, 0.0]), -1.0,
           np.array([0.00215, 0.043903, -0.037783]), -1.0, np.array([0.006, -0.037123, -0.037123])),
    "rl": (np.array([0.040078, 0.077545, 0.000058]),   1.0, np.array([0.0516, 0.004305, 0.0]), -1.0,
           np.array([-0.0143, 0.043903, -0.037783]),  1.0, np.array([0.006, -0.037123, -0.037123])),
}

# The firmware's KinConfig for the variant the Pico runs (esp32/include/kinematics.h), and what the
# client commands it: Robot.stand's and the app's height 0.7, speed_model's speed and step height.
VARIANT = "SPOTMICRO_ESP32_MINI"
COXA, BODY_LENGTH, BODY_WIDTH = 0.035, 0.160, 0.080
MAX_LEG_REACH = speed_model.max_leg_reach(VARIANT)
MIN_BODY_HEIGHT, MAX_BODY_HEIGHT = 0.45 * MAX_LEG_REACH, 0.9 * MAX_LEG_REACH
STAND_HEIGHT = 0.7
BODY_HEIGHT = MIN_BODY_HEIGHT + STAND_HEIGHT * (MAX_BODY_HEIGHT - MIN_BODY_HEIGHT)  # 92 mm
MAX_STEP_LENGTH = speed_model.max_step_length(VARIANT)
STEP_HEIGHT = speed_model.STEP_HEIGHT * MAX_LEG_REACH / 2  # 30 mm
STEP_DEPTH = 0.002


def _rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def _rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def _rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def leg_fk(leg: str, q) -> np.ndarray:
    """Foot position in base frame for one leg given (q_hip, q_femur, q_tibia) radians.

    Exact composition of the MJCF chain:
        foot = H + Ry(sy*q1) @ (p_femur + Rx(sf*q2) @ (p_tibia + Rx(st*q3) @ p_foot))
    """
    H, sy, p_femur, sf, p_tibia, st, p_foot = _LEG_GEOM[leg]
    q1, q2, q3 = float(q[0]), float(q[1]), float(q[2])
    inner = p_tibia + _rot_x(st * q3) @ p_foot
    arm = p_femur + _rot_x(sf * q2) @ inner
    return H + _rot_y(sy * q1) @ arm


class SpotPicoKinConfig:
    """Per-leg constants derived once from _LEG_GEOM for the analytic IK."""

    def __init__(self):
        self.legs = {}
        for name, (H, sy, p_femur, sf, p_tibia, st, p_foot) in _LEG_GEOM.items():
            # Foot lateral (x) offset in coxa frame is invariant under the two X-rotations.
            x_off = p_femur[0] + p_tibia[0] + p_foot[0]
            # 2-link planar arm in the coxa (y, z) plane, based at o0 = p_femur.yz.
            o0 = p_femur[1:3].copy()
            v1 = p_tibia[1:3].copy()            # femur link vector (rest)
            v2 = p_foot[1:3].copy()             # tibia link vector (rest)
            L1 = float(np.linalg.norm(v1))
            L2 = float(np.linalg.norm(v2))
            a1 = math.atan2(v1[1], v1[0])        # rest angle of link1
            a2 = math.atan2(v2[1], v2[0])        # rest angle of link2
            self.legs[name] = dict(H=H, sy=sy, sf=sf, st=st, x_off=x_off,
                                   o0=o0, L1=L1, L2=L2, a1=a1, a2=a2)


def leg_ik(cfg: SpotPicoKinConfig, leg: str, target: np.ndarray, knee_up: bool = False) -> np.ndarray:
    """Analytic IK: base-frame foot target -> (q_hip, q_femur, q_tibia) radians.

    Decomposition: the hip rotates about Y; the foot's coxa-frame x-coordinate is the
    invariant `x_off`, so the hip angle is fixed by matching |(P.x, P.z)| = |(x_off, Az)|.
    The remaining (Ay, Az) is solved as a planar 2-link arm.
    """
    p = cfg.legs[leg]
    H, sy, sf, st = p["H"], p["sy"], p["sf"], p["st"]
    x_off, o0, L1, L2, a1, a2 = p["x_off"], p["o0"], p["L1"], p["L2"], p["a1"], p["a2"]

    P = np.asarray(target, dtype=float) - H          # coxa frame (aligned with base at q1=0)
    Ay = P[1]                                          # invariant under Ry
    r2 = P[0] ** 2 + P[2] ** 2
    Az2 = r2 - x_off ** 2
    if Az2 < 0.0:
        Az2 = 0.0                                      # target outside abduction reach; clamp
    Az = -math.sqrt(Az2)                               # -branch: the leg reaches downward (foot below hip)

    # hip angle phi (about +Y): [P.x; P.z] = [[c, s],[-s, c]] @ [x_off; Az]
    denom = r2 if r2 > 1e-12 else 1e-12
    c = (x_off * P[0] + Az * P[2]) / denom
    s = (Az * P[0] - x_off * P[2]) / denom
    phi = math.atan2(s, c)
    q1 = phi / sy

    # planar 2-link: T = (Ay, Az) - o0 = L1*u(a1+alpha) + L2*u(a2+alpha+beta)
    T = np.array([Ay, Az]) - o0
    D = float(np.linalg.norm(T))
    D = min(D, L1 + L2 - 1e-6)                         # clamp to reachable
    cos_psi = (D * D - L1 * L1 - L2 * L2) / (2.0 * L1 * L2)
    cos_psi = max(-1.0, min(1.0, cos_psi))
    psi = math.acos(cos_psi)
    if not knee_up:
        psi = -psi
    phi_T = math.atan2(T[1], T[0])
    alpha = phi_T - math.atan2(L2 * math.sin(psi), L1 + L2 * math.cos(psi)) - a1
    beta = psi - (a2 - a1)

    q2 = alpha / sf
    q3 = beta / st
    return np.array([q1, q2, q3])



# Default stance feet (base frame). The firmware stands each foot BODY_HEIGHT below its leg's mount
# and COXA outboard of it, level with it fore-aft; the Pico's mount is its hip axis laterally and
# its femur pivot fore-aft (p_femur's y), where the MINI's leg plane sits.
_CFG = SpotPicoKinConfig()
DEFAULT_FEET = np.array(
    [np.array([_CFG.legs[n]["H"][0] + math.copysign(COXA, _CFG.legs[n]["x_off"]),
               _CFG.legs[n]["H"][1] + _LEG_GEOM[n][2][1],
               _CFG.legs[n]["H"][2] - BODY_HEIGHT]) for n in LEG_NAMES]
)
FOOT_RADIUS = 0.009
STAND_Z = float(FOOT_RADIUS - DEFAULT_FEET[:, 2].min())  # base height so lowest foot rests on floor

# The firmware's default feet about its body centre, in this base frame: a turn strokes each foot
# along the rotation's velocity field at these, as the firmware does, not at the Pico's own feet.
FIRMWARE_FEET = np.array(
    [[math.copysign(BODY_WIDTH / 2 + COXA, _CFG.legs[n]["x_off"]),
      math.copysign(BODY_LENGTH / 2, _CFG.legs[n]["H"][1])] for n in LEG_NAMES]
)


# ---------------------------------------------------------------------------
# Gait engine (port of walk_state.h). Foot deltas are computed in a stride frame
# (along-stride, vertical, cross-stride) exactly as the firmware curves, then mapped
# into the spot_pico base frame:  forward stride -> -Y, cross -> +X, lift -> +Z.
# ---------------------------------------------------------------------------
# walk_state.h's phase offsets, from its leg order (fl, fr, rl, rr) into this one (fr, fl, rr, rl):
# the trot lifts the diagonal pair {fr, rl} first, the crawl the legs fr, rl, fl, rr in turn.
TROT_OFFSET = np.array([0.5, 0.0, 0.0, 0.5])
CRAWL_OFFSET = np.array([0.75, 0.25, 0.0, 0.5])
TROT_STAND_FRAC = 0.75
CRAWL_STAND_FRAC = 0.85
TROT_SPEED_FACTOR = 2.0
CRAWL_SPEED_FACTOR = 0.5

COMBINATORIAL_VALUES = np.array([1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1], dtype=float)
BEZIER_STEPS = np.array([-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0])
BEZIER_HEIGHTS = np.array([0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0])

TROT, CRAWL = 0, 1

# MotionState's smoothing (state.h, utils/critical_damper.h): a step in the stroke settles to 95%
# in a third of a second, without overshoot.
SMOOTHING_OMEGA = 4.744 / 0.333


@dataclass
class BodyState:
    omega: float = 0.0  # roll  (rad)
    phi: float = 0.0    # pitch (rad)
    psi: float = 0.0    # yaw   (rad)
    xm: float = 0.0
    ym: float = 0.0
    zm: float = 0.0
    feet: np.ndarray = field(default_factory=lambda: DEFAULT_FEET.copy())


@dataclass
class GaitState:
    step_height: float = STEP_HEIGHT
    step_x: float = 0.0        # forward stroke (m)
    step_z: float = 0.0        # leftward stroke (m)
    step_angle: float = 0.0    # turn: each foot strokes step_angle times its FIRMWARE_FEET radius
    step_velocity: float = speed_model.SPEED  # cadence scalar (0..1)
    step_depth: float = STEP_DEPTH
    stand_frac: float = TROT_STAND_FRAC
    offset: np.ndarray = field(default_factory=lambda: TROT_OFFSET.copy())
    speed_factor: float = TROT_SPEED_FACTOR
    mode: int = TROT
    # (step_x, step_z, step_angle) as commanded, which the smoothing follows, and its velocities
    target: np.ndarray = field(default_factory=lambda: np.zeros(3))
    smoothing: np.ndarray = field(default_factory=lambda: np.zeros(3))


def set_mode(gait: GaitState, mode: int) -> None:
    gait.mode = mode
    if mode == CRAWL:
        gait.offset = CRAWL_OFFSET.copy()
        gait.stand_frac = CRAWL_STAND_FRAC
        gait.speed_factor = CRAWL_SPEED_FACTOR
    else:
        gait.offset = TROT_OFFSET.copy()
        gait.stand_frac = TROT_STAND_FRAC
        gait.speed_factor = TROT_SPEED_FACTOR


def _stance_curve(length, angle, depth, phase, point):
    step = length * (1.0 - 2.0 * phase)
    point[0] += step * math.cos(angle)
    point[2] += step * math.sin(angle)
    if length != 0.0:
        point[1] = -depth * math.cos((math.pi * (point[0] + point[2])) / (2.0 * length))


def _bezier_curve(length, angle, height, phase, point):
    x_polar = math.cos(angle)
    z_polar = math.sin(angle)
    t = min(max(phase, 1e-4), 1.0 - 1e-4)
    phase_power = 1.0
    inv_phase_power = (1.0 - t) ** 11
    one_minus = 1.0 - t
    for i in range(12):
        b = COMBINATORIAL_VALUES[i] * phase_power * inv_phase_power
        point[0] += b * BEZIER_STEPS[i] * length * x_polar
        point[2] += b * BEZIER_STEPS[i] * length * z_polar
        point[1] += b * BEZIER_HEIGHTS[i] * height
        phase_power *= t
        if one_minus != 0.0:
            inv_phase_power /= one_minus


def _stroke(gait, radius):
    """Rigid-body velocity field at a foot's firmware stance position, as (amplitude, stride heading).

    The commanded translation and the rotation about the body centre compose into one vector
    before the curve is evaluated, matching walk_state.h. In the base frame a stride of length
    ``step`` at heading ``a`` displaces the foot by (step*sin a, -step*cos a), so forward
    (+step_x) maps to -Y and lateral (+step_z) to +X. The rotational part is omega x r, as
    walk_state.h composes it: a positive step_angle turns counter-clockwise.
    """
    sx = gait.step_z - gait.step_angle * radius[1]
    sy = -gait.step_x + gait.step_angle * radius[0]
    return math.hypot(sx, sy), math.atan2(sx, -sy)


def _moving(gait: GaitState) -> bool:
    return abs(gait.step_x) >= 0.001 or abs(gait.step_z) >= 0.001 or abs(gait.step_angle) >= 0.001


class GaitController:
    """Port of WalkState gait generation (walk_state.h)."""

    def __init__(self):
        self.phase = 0.0
        self.default_position = DEFAULT_FEET.copy()

    def set_phase(self, phase: float) -> None:
        self.phase = math.fmod(phase, 1.0)

    def step(self, gait: GaitState, dt: float) -> None:
        """WalkState::step up to the feet: the stroke follows the command, then the phase advances.

        Standing still holds the phase at 0, so every walk starts on the same feet, as on the robot.
        """
        offset = np.array([gait.step_x, gait.step_z, gait.step_angle]) - gait.target
        drive = gait.smoothing + SMOOTHING_OMEGA * offset
        decay = math.exp(-SMOOTHING_OMEGA * dt)
        gait.smoothing = (gait.smoothing - SMOOTHING_OMEGA * drive * dt) * decay
        gait.step_x, gait.step_z, gait.step_angle = (gait.target + (offset + drive * dt) * decay).tolist()
        if not _moving(gait):
            self.phase = 0.0
            return
        velocity = max(gait.step_velocity, 0.5)
        self.phase = math.fmod(self.phase + dt * velocity * gait.speed_factor, 1.0)

    def generate_feet(self, gait: GaitState, body: BodyState) -> None:
        """Write body.feet at the CURRENT phase (does not advance it). Base-frame targets."""
        new_feet = self.default_position.copy()
        for i in range(4):
            leg_phase = math.fmod(self.phase + gait.offset[i], 1.0)
            contact = leg_phase <= gait.stand_frac
            if contact:
                ph = leg_phase / gait.stand_frac
                curve, amp = _stance_curve, gait.step_depth
            else:
                ph = (leg_phase - gait.stand_frac) / (1.0 - gait.stand_frac)
                curve, amp = _bezier_curve, gait.step_height

            # stride frame delta: [0]=along-stride, [1]=vertical, [2]=cross-stride
            stroke, heading = _stroke(gait, FIRMWARE_FEET[i])
            delta = [0.0, 0.0, 0.0]
            curve(stroke * 0.5, heading, amp, ph, delta)

            # map stride frame -> base frame: forward(+stride)-> -Y, cross-> +X, lift(+)-> +Z
            new_feet[i, 0] += delta[2]
            new_feet[i, 1] -= delta[0]
            if stroke != 0.0:
                new_feet[i, 2] += delta[1]
        body.feet = new_feet


class Kinematics:
    """spot_pico IK: base-frame foot targets (with optional body pose) -> 12 joint angles."""

    def __init__(self):
        self.cfg = SpotPicoKinConfig()

    def inverse_kinematics(self, body: BodyState) -> np.ndarray:
        """Returns 12 joint angles (rad) in MJCF order [fr,fl,rr,rl] x [hip,femur,tibia].

        Body orientation (omega/phi/psi) and translation (xm,ym,zm) shift the foot targets
        in the base frame before per-leg IK (identity at the default pose).
        """
        R = _rot_z(body.psi) @ _rot_y(body.phi) @ _rot_x(body.omega)
        t = np.array([body.xm, body.ym, body.zm])
        Rt = R.T
        ang = np.zeros((4, 3))
        for i, name in enumerate(LEG_NAMES):
            foot = np.asarray(body.feet[i][:3], dtype=float)
            local = Rt @ (foot - t)                 # express target in body-fixed frame
            ang[i] = leg_ik(self.cfg, name, local)
        return ang.flatten()


# ---------------------------------------------------------------------------
# Command -> gait mapping. RL command is body-frame [vx, vy, yaw]. The client turns a velocity
# into the sticks the robot is sent (speed_model.sticks_for), and WalkState::handleCommand turns
# those into the stroke: step_x = ly, step_z = -lx, step_angle = rx (times the max step length).
# ---------------------------------------------------------------------------
def analytic_gait_action(cmd, gait: GaitState) -> None:
    """Deterministic command [vx, vy, yaw] -> the stroke the gait smooths towards (in place)."""
    sticks = speed_model.sticks_for(VARIANT, Gait.CRAWL if gait.mode == CRAWL else Gait.TROT,
                                    Velocity(float(cmd[0]), float(cmd[1]), float(cmd[2])))
    gait.target = np.array([sticks.ly * MAX_STEP_LENGTH, -sticks.lx * MAX_STEP_LENGTH, sticks.rx])
    gait.step_velocity = speed_model.SPEED
    gait.step_height = STEP_HEIGHT
    gait.step_depth = STEP_DEPTH


if __name__ == "__main__":
    np.set_printoptions(suppress=True, precision=5)
    cfg = SpotPicoKinConfig()
    print(f"STAND_Z (base height) = {STAND_Z:.4f} m")
    print("DEFAULT_FEET (base frame):\n", DEFAULT_FEET)

    # IK/FK round-trip on random reachable targets around the default stance.
    rng = np.random.default_rng(0)
    max_err = 0.0
    for _ in range(2000):
        for i, name in enumerate(LEG_NAMES):
            tgt = DEFAULT_FEET[i] + rng.uniform(-0.012, 0.012, 3)  # within reachable workspace
            q = leg_ik(cfg, name, tgt)
            foot = leg_fk(name, q)
            max_err = max(max_err, float(np.linalg.norm(foot - tgt)))
    print(f"IK/FK round-trip max foot error: {max_err * 1000:.4f} mm  (expect ~0)")

    # Stance angles recovered from DEFAULT_FEET:
    body = BodyState()
    print("Stance joint angles (deg):\n", np.rad2deg(Kinematics().inverse_kinematics(body)).reshape(4, 3))
