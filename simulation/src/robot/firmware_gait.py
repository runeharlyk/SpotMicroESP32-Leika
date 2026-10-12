"""NumPy port of the ESP32 FIRMWARE gait, retargeted to the spot_pico robot.

The gait *engine* is a faithful port of `esp32/include/motion_states/walk_state.h`
(phase clock, per-leg offsets, stance/Bezier curves, stroke composition, command->gait mapping);
it applies no command smoothing. The *kinematics* are NOT the firmware's SPOTMICRO_ESP32 constants
(that is a different, larger robot) but are derived directly from the spot_pico MJCF, since
that is the model we train on. So a "zero residual" reproduces the firmware gait *shape*
executed on spot_pico's geometry.
tests/test_firmware_gait_traces.py replays the firmware's own traces through the engine and marks where it differs.

Coordinate frame (spot_pico `base_link`, MuJoCo world at identity):
    +X = left, +Y = rear, +Z = up   (so forward = -Y, right = -X).
Foot targets are expressed in the base frame. IK returns radians (MuJoCo position
actuators), unlike the firmware which writes degrees to servos.

Leg order (matches MJCF actuator/qpos order): 0=fr, 1=fl, 2=rr, 3=rl.
Each leg's 3 joints are (hip/abduction, femur, tibia).
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
import math
import os

import numpy as np

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

# Nominal stance depth (m): how far below the hip each foot rests. Chosen reachable
# (2-link span ~0.11 m) and stable; feet sit at their natural lateral offset, under the hip.
STANCE_DEPTH = 0.055


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


# Default stance feet (base frame): each foot at its natural lateral offset (hip x + coxa
# offset), directly under the hip fore-aft, STANCE_DEPTH below the hip.
_CFG = SpotPicoKinConfig()
DEFAULT_FEET = np.array(
    [np.array([_CFG.legs[n]["H"][0] + _CFG.legs[n]["x_off"],
               _CFG.legs[n]["H"][1],
               _CFG.legs[n]["H"][2] - STANCE_DEPTH]) for n in LEG_NAMES]
)
FOOT_RADIUS = 0.009
STAND_Z = float(FOOT_RADIUS - DEFAULT_FEET[:, 2].min())  # base height so lowest foot rests on floor


# ---------------------------------------------------------------------------
# Gait engine (port of walk_state.h). Foot deltas are computed in a stride frame
# (along-stride, vertical, cross-stride) exactly as the firmware curves, then mapped
# into the spot_pico base frame:  forward stride -> -Y, cross -> +X, lift -> +Z.
# ---------------------------------------------------------------------------
TROT_OFFSET = np.array([0.0, 0.5, 0.5, 0.0])   # fr, fl, rr, rl  (diagonal pairs {fr,rl},{fl,rr})
CRAWL_OFFSET = np.array([0.25, 0.75, 0.5, 0.0])
TROT_STAND_FRAC = 0.75
CRAWL_STAND_FRAC = 0.85
TROT_SPEED_FACTOR = 2.0
CRAWL_SPEED_FACTOR = 0.5

COMBINATORIAL_VALUES = np.array([1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1], dtype=float)
BEZIER_STEPS = np.array([-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0])
BEZIER_HEIGHTS = np.array([0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0])

TROT, CRAWL = 0, 1

# walk_state.h's cap on joint speed (a quarter below the servos' 720 deg/s), and the parts of a tick it slows by.
MAX_JOINT_SPEED = math.radians(0.75 * 720.0)
SLOWDOWN_PARTS = 64

# Firmware step parameters (meters). Scaled to spot_pico's small legs (~0.11 m 2-link span)
# and kept inside the reachable workspace so the zero-residual baseline stays feasible.
MAX_STEP_LENGTH = 0.030
MAX_LATERAL_STEP = 0.018       # abduction workspace is tighter than the sagittal stride
MAX_TURN_STEP = 0.026          # mean tangential half-stroke at full step_angle
# Yaw command -> rotation amplitude. A foot's tangential half-stroke is TURN_RATE * radius / 2,
# calibrated so the mean still lands on MAX_TURN_STEP. Note MAX_TURN_STEP is a half-stroke while
# MAX_STEP_LENGTH is a full stroke: the curve takes half-amplitudes, and the old code passed the
# turn amplitude straight through while halving the linear one.
TURN_RATE = 2.0 * MAX_TURN_STEP / float(np.hypot(DEFAULT_FEET[:, 0], DEFAULT_FEET[:, 1]).mean())
DEFAULT_STEP_HEIGHT = 0.015
DEFAULT_STEP_DEPTH = 0.002

# walk_state.h's reach bound: the bends it keeps every knee between.
MIN_KNEE_BEND_DEG, MAX_KNEE_BEND_DEG = 10.0, 148.0


def _bezier_samples(points: np.ndarray, samples: int) -> np.ndarray:
    phases = np.linspace(0.0, 1.0, samples + 1)[:, None]
    return (COMBINATORIAL_VALUES * phases ** np.arange(12) * (1 - phases) ** np.arange(12)[::-1]) @ points


# The swing's furthest reach along the stroke, in half strokes, and the swing at SWING_SAMPLES + 1 phases, how far
# along the stroke in half strokes and how high in step heights.
SWING_OVERSHOOT = float(np.abs(_bezier_samples(BEZIER_STEPS, 1024)).max())
SWING_SAMPLES = 64
SWING_ALONG = _bezier_samples(BEZIER_STEPS, SWING_SAMPLES)
SWING_LIFT = _bezier_samples(BEZIER_HEIGHTS, SWING_SAMPLES)


def _reach_spheres(leg: str):
    """walk_state.h's configure() for one of this robot's legs: its home's horizontal offset (X, Y) from the point on
    the hip axis level with the femur, and the squared distances from that point within which the knee is bent at least
    MIN_KNEE_BEND_DEG and beyond which at most MAX_KNEE_BEND_DEG. leg_ik's femur-to-foot distance D keeps
    D^2 = |P - (0, o0.y, 0)|^2 - x_off^2 while o0.z, the femur's offset along the leg's plane, is 0, as on every leg
    here; the inner sphere allows for it as the firmware's does for coxa_offset."""
    p = _CFG.legs[leg]
    x_off, (o0_y, o0_z), L1, L2 = p["x_off"], p["o0"], p["L1"], p["L2"]

    def femur_to_foot(knee_bend_deg: float) -> float:
        return math.sqrt(L1 * L1 + L2 * L2 + 2 * L1 * L2 * math.cos(math.radians(knee_bend_deg)))

    return (np.array([x_off, -o0_y]), x_off * x_off + femur_to_foot(MIN_KNEE_BEND_DEG) ** 2,
            x_off * x_off + (femur_to_foot(MAX_KNEE_BEND_DEG) + abs(o0_z)) ** 2)


_REACH_SPHERES = [_reach_spheres(leg) for leg in LEG_NAMES]


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
    step_height: float = DEFAULT_STEP_HEIGHT
    step_x: float = 0.0        # forward step (m)
    step_z: float = 0.0        # lateral step (m)
    step_angle: float = 0.0    # turn command (~-1..1)
    step_velocity: float = 0.5  # cadence scalar (0..1)
    step_depth: float = DEFAULT_STEP_DEPTH
    stand_frac: float = TROT_STAND_FRAC
    offset: np.ndarray = field(default_factory=lambda: TROT_OFFSET.copy())
    speed_factor: float = TROT_SPEED_FACTOR
    mode: int = TROT


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
        point[1] = -depth * math.cos((math.pi * step) / (2.0 * length))


def _bezier_curve(length, angle, height, phase, point):
    x_polar = math.cos(angle)
    z_polar = math.sin(angle)
    phase_powers = [phase ** i for i in range(12)]
    rest_powers = [(1.0 - phase) ** i for i in range(12)]
    for i in range(12):
        b = COMBINATORIAL_VALUES[i] * phase_powers[i] * rest_powers[11 - i]
        point[0] += b * BEZIER_STEPS[i] * length * x_polar
        point[2] += b * BEZIER_STEPS[i] * length * z_polar
        point[1] += b * BEZIER_HEIGHTS[i] * height


def _moving(gait) -> bool:
    return abs(gait.step_x) >= 0.001 or abs(gait.step_z) >= 0.001 or abs(gait.step_angle) >= 0.001


def _stroke_vector(gait, foot):
    """A foot's stroke in the base frame (X, Y): see _stroke."""
    turn = gait.step_angle * TURN_RATE
    return gait.step_z + turn * foot[1], -gait.step_x - turn * foot[0]


def _reach_bound(gait) -> tuple[float, float]:
    """walk_state.h's boundToReach with the body STANCE_DEPTH above the feet: the share of the stroke, and the step
    height, that keep every foot of the stance and the swing within reach. This port has no crawl body shift."""
    body_height = STANCE_DEPTH
    deepest = body_height + gait.step_depth
    scale, legs = 1.0, []
    for (home, reach_out_sq, reach_in_sq), foot in zip(_REACH_SPHERES, DEFAULT_FEET):
        sideways_home = abs(home[0])
        room = math.sqrt(max(0.0, reach_out_sq - deepest * deepest))
        least_offset = math.sqrt(max(0.0, reach_in_sq - body_height * body_height))
        stroke_x, stroke_y = _stroke_vector(gait, foot)
        stroke = math.hypot(stroke_x, stroke_y)
        half = stroke / 2
        reach = SWING_OVERSHOOT * half
        # The share of the stroke that points away from the body (+X is left), and along the home's offset.
        outward = math.copysign(1.0, home[0]) * stroke_x / stroke if stroke > 0 else 0.0
        along = abs(home[0] * stroke_x + home[1] * stroke_y) / stroke if stroke > 0 else 0.0
        a, b, c = reach * reach, along * reach, float(home @ home) - room * room
        discriminant = b * b - a * c
        if c >= 0 or least_offset > sideways_home:
            scale = 0.0
        elif reach > 0 and discriminant >= 0:
            scale = min(scale, -c / (b + math.sqrt(discriminant)))
        inward = abs(outward) * reach
        if least_offset > 0 and inward > 0:
            scale = min(scale, (sideways_home - least_offset) / inward)
        legs.append((sideways_home, half, outward, reach_in_sq))
    step_height = gait.step_height
    for sideways_home, half, outward, reach_in_sq in legs:
        offset = sideways_home + scale * half * SWING_ALONG[1:SWING_SAMPLES] * outward
        clear = np.sqrt(np.maximum(0.0, reach_in_sq - np.where(offset > 0, offset, 0.0) ** 2))
        allowed = np.maximum(0.0, body_height - clear) / SWING_LIFT[1:SWING_SAMPLES]
        step_height = min(step_height, float(allowed.min()))
    return scale, step_height


def _stroke(gait, foot):
    """Rigid-body velocity field at a foot's stance position, as (amplitude, stride heading).

    The commanded translation and the rotation about the body centre compose into one vector
    before the curve is evaluated, matching walk_state.h. In the base frame a stride of length
    ``step`` at heading ``a`` displaces the foot by (step*sin a, -step*cos a), so forward
    (+step_x) maps to -Y and lateral (+step_z) to +X. The rotational part is omega x r, signed
    so that a positive step_angle keeps its historical turn direction.
    """
    sx, sy = _stroke_vector(gait, foot)
    return math.hypot(sx, sy), math.atan2(sx, -sy)


class GaitController:
    """Port of WalkState gait generation (walk_state.h)."""

    def __init__(self):
        self.phase = 0.0
        self.default_position = DEFAULT_FEET.copy()
        self.kinematics = Kinematics()

    def set_phase(self, phase: float) -> None:
        self.phase = math.fmod(phase, 1.0)

    def stance(self, gait: GaitState) -> np.ndarray:
        """Which feet are in stance at the current phase, so on the floor (WalkState::stance)."""
        return np.fmod(self.phase + gait.offset, 1.0) <= gait.stand_frac

    def advance_phase(self, gait: GaitState, dt: float) -> None:
        """Advances the phase by dt, or by the most 64ths of it the joints can follow at MAX_JOINT_SPEED, as
        walk_state.h's step does; a jump not even a 64th keeps under the cap is walked at full time. The joints are
        those of the feet in the base frame: this port's body pose is applied only by the caller's IK."""
        start = self.phase
        before = self._joint_angles(gait)

        def followable(parts: int) -> bool:
            self.phase = start
            self._advance(gait, dt * parts / SLOWDOWN_PARTS)
            return float(np.abs(self._joint_angles(gait) - before).max()) <= MAX_JOINT_SPEED * dt

        if followable(SLOWDOWN_PARTS):
            return
        fast_enough, too_fast = 0, SLOWDOWN_PARTS
        while too_fast - fast_enough > 1:
            parts = (fast_enough + too_fast) // 2
            if followable(parts):
                fast_enough = parts
            else:
                too_fast = parts
        followable(fast_enough if fast_enough > 0 else SLOWDOWN_PARTS)

    def _advance(self, gait: GaitState, dt: float) -> None:
        # walk_state.h: a stopped walk holds its phase once every foot is down; a foot in the air lands first.
        if not _moving(gait) and not any(self._swinging(gait, i) for i in range(4)):
            return
        velocity = max(gait.step_velocity, 0.5)
        self.phase = math.fmod(self.phase + dt * velocity * gait.speed_factor, 1.0)

    def _swinging(self, gait: GaitState, leg: int) -> bool:
        return math.fmod(self.phase + gait.offset[leg], 1.0) > gait.stand_frac

    def _joint_angles(self, gait: GaitState) -> np.ndarray:
        body = BodyState()
        self.generate_feet(gait, body)
        return self.kinematics.inverse_kinematics(body)

    def generate_feet(self, gait: GaitState, body: BodyState) -> None:
        """Write body.feet at the CURRENT phase (does not advance it). Base-frame targets."""
        new_feet = self.default_position.copy()
        scale, step_height = _reach_bound(gait)
        for i in range(4):
            leg_phase = math.fmod(self.phase + gait.offset[i], 1.0)
            contact = leg_phase <= gait.stand_frac
            if contact:
                ph = leg_phase / gait.stand_frac
                curve, amp = _stance_curve, gait.step_depth
            else:
                ph = (leg_phase - gait.stand_frac) / (1.0 - gait.stand_frac)
                curve, amp = _bezier_curve, step_height

            # stride frame delta: [0]=along-stride, [1]=vertical, [2]=cross-stride
            stroke, heading = _stroke(gait, self.default_position[i])
            delta = [0.0, 0.0, 0.0]
            curve(scale * stroke * 0.5, heading, amp, ph, delta)

            # map stride frame -> base frame: forward(+stride)-> -Y, cross-> +X, lift(+)-> +Z
            dx = delta[2]     # cross-stride -> lateral X
            dy = -delta[0]    # along-stride -> fore-aft Y (forward = -Y)
            dz = delta[1]     # vertical -> Z
            new_feet[i] = self.default_position[i] + (dx, dy, dz)
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
# Command -> gait mapping. RL command is body-frame [vx, vy, yaw]. The firmware maps
# joystick directly to step params; we mirror that with tunable gains (optimize_gait.py
# writes gait_coef.json so a zero-residual rollout roughly tracks commanded velocity).
# ---------------------------------------------------------------------------
GAIT_COEF = {
    "gain_x": 0.075,       # (m/s) command per unit step_x fraction; step_x = vx/gain_x * MAX_STEP_LENGTH
    "gain_y": 0.075,
    "gain_yaw": 3.0,       # (rad/s) command per unit step_angle
    "speed_base": 0.4,     # cadence floor
    "speed_slope": 1.2,    # cadence rises with speed
    "step_height": DEFAULT_STEP_HEIGHT,
    "step_depth": DEFAULT_STEP_DEPTH,
}
_coef_file = os.path.join(os.path.dirname(__file__), "..", "resources", "spot_pico", "gait_coef.json")
if os.path.exists(_coef_file):
    try:
        GAIT_COEF.update(json.load(open(_coef_file)))
    except Exception:
        pass


def analytic_gait_action(cmd, gait: GaitState) -> None:
    """Deterministic command [vx, vy, yaw] -> firmware gait_state (in place)."""
    c = GAIT_COEF
    vx, vy, yaw = float(cmd[0]), float(cmd[1]), float(cmd[2])
    gait.step_x = float(np.clip(vx / c["gain_x"], -1.0, 1.0)) * MAX_STEP_LENGTH
    gait.step_z = float(np.clip(vy / c["gain_y"], -1.0, 1.0)) * MAX_LATERAL_STEP
    # negative: +yaw (CCW/+Z) command needs the opposite tangential stride sign
    gait.step_angle = float(np.clip(-yaw / c["gain_yaw"], -1.0, 1.0))
    speed = math.hypot(vx, vy) + abs(yaw) * 0.1
    gait.step_velocity = float(np.clip(c["speed_base"] + c["speed_slope"] * speed, 0.0, 1.0))
    gait.step_height = c["step_height"]
    gait.step_depth = c["step_depth"]


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
