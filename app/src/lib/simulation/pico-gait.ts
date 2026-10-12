/**
 * TypeScript port of simulation/src/robot/firmware_gait.py: the firmware's walk_state.h gait
 * with the SPOTMICRO_ESP32_MINI's constants, placed on the Pico's geometry with inverse
 * kinematics derived from its MJCF, which is the controller the Python simulation and the
 * training use. tests/unit/pico-gait.spec.ts pins it to that file.
 *
 * Base frame of spot_pico: +X left, +Y rear, +Z up (forward is -Y). Angles are radians in MJCF
 * order [fr, fl, rr, rl] x [hip, femur, tibia].
 */

import { CONTROL_DT } from './timing'

export type Vec3 = [number, number, number]
/** The app's sticks: left x (strafing right), left y (forward) and right x (turning left). */
export type Sticks = [lx: number, ly: number, rx: number]
export type Leg = 'fr' | 'fl' | 'rr' | 'rl'

export const LEG_NAMES: Leg[] = ['fr', 'fl', 'rr', 'rl']
export const JOINT_NAMES = LEG_NAMES.flatMap(leg =>
    ['hip', 'femur', 'tibia'].map(joint => `${leg}_${joint}_joint`)
)

interface LegGeometry {
    H: Vec3
    sy: number
    pFemur: Vec3
    sf: number
    pTibia: Vec3
    st: number
    pFoot: Vec3
}

// From simulation/src/resources/spot_pico/scene.xml, as _LEG_GEOM in firmware_gait.py.
const LEG_GEOM: Record<Leg, LegGeometry> = {
    fr: {
        H: [-0.039922, -0.099805, 0.000058],
        sy: -1,
        pFemur: [-0.0513, 0.004305, 0],
        sf: -1,
        pTibia: [0.00215, 0.043903, -0.037783],
        st: -1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    fl: {
        H: [0.040078, -0.095155, 0.000058],
        sy: 1,
        pFemur: [0.0516, 0.004305, 0],
        sf: -1,
        pTibia: [-0.0143, 0.043903, -0.037783],
        st: 1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    rr: {
        H: [-0.039922, 0.077545, 0.000058],
        sy: -1,
        pFemur: [-0.0513, 0.004305, 0],
        sf: -1,
        pTibia: [0.00215, 0.043903, -0.037783],
        st: -1,
        pFoot: [0.006, -0.037123, -0.037123]
    },
    rl: {
        H: [0.040078, 0.077545, 0.000058],
        sy: 1,
        pFemur: [0.0516, 0.004305, 0],
        sf: -1,
        pTibia: [-0.0143, 0.043903, -0.037783],
        st: 1,
        pFoot: [0.006, -0.037123, -0.037123]
    }
}

// The firmware's KinConfig for SPOTMICRO_ESP32_MINI (esp32/include/kinematics.h) and what the
// client commands it (simulation/src/leika/speed_model.py): height 0.7, speed 0.5, s1 0.5.
const COXA = 0.035
const BODY_LENGTH = 0.16
const BODY_WIDTH = 0.08
const MAX_LEG_REACH = 0.06 + 0.06 - 0
const MIN_BODY_HEIGHT = 0.45 * MAX_LEG_REACH
const MAX_BODY_HEIGHT = 0.9 * MAX_LEG_REACH
const BODY_HEIGHT = MIN_BODY_HEIGHT + 0.7 * (MAX_BODY_HEIGHT - MIN_BODY_HEIGHT)
const MAX_STEP_LENGTH = 0.8 * MAX_LEG_REACH
const STEP_HEIGHT = (0.5 * MAX_LEG_REACH) / 2
const STEP_DEPTH = 0.002
const SPEED = 0.5
const FOOT_RADIUS = 0.009

const rotX = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [x, c * y - s * z, s * y + c * z]
}
const rotY = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [c * x + s * z, y, -s * x + c * z]
}
const rotZ = (a: number, [x, y, z]: Vec3): Vec3 => {
    const c = Math.cos(a)
    const s = Math.sin(a)
    return [c * x - s * y, s * x + c * y, z]
}
const clip = (value: number, low: number, high: number) => Math.min(Math.max(value, low), high)

// Per-leg constants for the analytic IK, as SpotPicoKinConfig.
const KIN = Object.fromEntries(
    LEG_NAMES.map(leg => {
        const { pFemur, pTibia, pFoot } = LEG_GEOM[leg]
        const v1 = [pTibia[1], pTibia[2]]
        const v2 = [pFoot[1], pFoot[2]]
        return [
            leg,
            {
                xOff: pFemur[0] + pTibia[0] + pFoot[0],
                o0: [pFemur[1], pFemur[2]],
                L1: Math.hypot(v1[0], v1[1]),
                L2: Math.hypot(v2[0], v2[1]),
                a1: Math.atan2(v1[1], v1[0]),
                a2: Math.atan2(v2[1], v2[0])
            }
        ]
    })
) as Record<Leg, { xOff: number; o0: number[]; L1: number; L2: number; a1: number; a2: number }>

/** Base-frame foot target to (hip, femur, tibia) radians; the leg reaches downward, knee back. */
export function legIk(leg: Leg, target: Vec3): Vec3 {
    const { H, sy, sf, st } = LEG_GEOM[leg]
    const { xOff, o0, L1, L2, a1, a2 } = KIN[leg]

    const P = [target[0] - H[0], target[1] - H[1], target[2] - H[2]]
    const Ay = P[1]
    const r2 = P[0] ** 2 + P[2] ** 2
    const Az = -Math.sqrt(Math.max(r2 - xOff ** 2, 0))

    const denom = r2 > 1e-12 ? r2 : 1e-12
    const c = (xOff * P[0] + Az * P[2]) / denom
    const s = (Az * P[0] - xOff * P[2]) / denom
    const q1 = Math.atan2(s, c) / sy

    const T = [Ay - o0[0], Az - o0[1]]
    const D = Math.min(Math.hypot(T[0], T[1]), L1 + L2 - 1e-6)
    const cosPsi = clip((D * D - L1 * L1 - L2 * L2) / (2 * L1 * L2), -1, 1)
    const psi = -Math.acos(cosPsi)
    const alpha =
        Math.atan2(T[1], T[0]) - Math.atan2(L2 * Math.sin(psi), L1 + L2 * Math.cos(psi)) - a1
    const beta = psi - (a2 - a1)
    return [q1, alpha / sf, beta / st]
}

// Each foot BODY_HEIGHT below its leg's mount and COXA outboard of it: the Pico's hip axis
// laterally, its femur pivot fore-aft.
export const DEFAULT_FEET: Vec3[] = LEG_NAMES.map(leg => {
    const { H, pFemur } = LEG_GEOM[leg]
    return [H[0] + Math.sign(KIN[leg].xOff) * COXA, H[1] + pFemur[1], H[2] - BODY_HEIGHT]
})
export const STAND_Z = FOOT_RADIUS - Math.min(...DEFAULT_FEET.map(foot => foot[2]))

// The firmware's feet about its body centre, where a turn takes each foot's stroke.
const FIRMWARE_FEET = LEG_NAMES.map(leg => [
    Math.sign(KIN[leg].xOff) * (BODY_WIDTH / 2 + COXA),
    Math.sign(LEG_GEOM[leg].H[1]) * (BODY_LENGTH / 2)
])

// walk_state.h's offsets, from its leg order (fl, fr, rl, rr) into this one.
const TROT_OFFSET = [0.5, 0, 0, 0.5]
const TROT_STAND_FRAC = 0.75
const TROT_SPEED_FACTOR = 2.0
const COMBINATORIAL_VALUES = [1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1]
const BEZIER_STEPS = [-1.0, -1.4, -1.5, -1.5, -1.5, 0.0, 0.0, 0.0, 1.5, 1.5, 1.4, 1.0]
const BEZIER_HEIGHTS = [0.0, 0.0, 0.9, 0.9, 0.9, 0.9, 0.9, 1.1, 1.1, 1.1, 0.0, 0.0]
// The 333 ms smoothing of state.h and utils/critical_damper.h.
const SMOOTHING_OMEGA = 4.744 / 0.333

// walk_state.h's reach bound, as firmware_gait.py: the bends it keeps every knee between, the swing's furthest reach
// along the stroke in half strokes, and the swing at SWING_SAMPLES + 1 phases (along the stroke and up).
const MIN_KNEE_BEND_DEG = 10
const MAX_KNEE_BEND_DEG = 148
const bezierSamples = (points: number[], samples: number) =>
    Array.from({ length: samples + 1 }, (_, sample) =>
        points.reduce(
            (sum, point, i) =>
                sum +
                COMBINATORIAL_VALUES[i] *
                    (sample / samples) ** i *
                    (1 - sample / samples) ** (11 - i) *
                    point,
            0
        )
    )
const SWING_OVERSHOOT = Math.max(...bezierSamples(BEZIER_STEPS, 1024).map(Math.abs))
const SWING_SAMPLES = 64
const SWING_ALONG = bezierSamples(BEZIER_STEPS, SWING_SAMPLES)
const SWING_LIFT = bezierSamples(BEZIER_HEIGHTS, SWING_SAMPLES)

// walk_state.h's reach spheres about each leg's mount, through the MINI's legs as the firmware
// bounds them (speed_model.reach_spheres, with the firmware's truncated DEG2RAD_F).
const MINI_FEMUR = 0.06
const MINI_TIBIA = 0.06
const DEG2RAD_F = 0.0174532
const femurToFoot = (kneeBendDeg: number) =>
    Math.sqrt(
        MINI_FEMUR ** 2 +
            MINI_TIBIA ** 2 +
            2 * MINI_FEMUR * MINI_TIBIA * Math.cos(kneeBendDeg * DEG2RAD_F)
    )
const REACH_OUT_SQ = COXA ** 2 + femurToFoot(MIN_KNEE_BEND_DEG) ** 2
const REACH_IN_SQ = COXA ** 2 + femurToFoot(MAX_KNEE_BEND_DEG) ** 2

// The legs in walk_state.h's order (fl, fr, rl, rr), from this one, and which side each is on.
const FIRMWARE_ORDER = [1, 0, 3, 2]
const FIRMWARE_LEFT = [1, -1, 1, -1]

/** Kinematics::legIK for the MINI in degrees, for a foot at (x, y, z) in its leg's frame. */
const miniLegAngles = (x: number, y: number, z: number): Vec3 => {
    const f = Math.sqrt(Math.max(0, x * x + y * y - COXA * COXA))
    const h = Math.sqrt(f * f + z * z)
    const hip = -Math.atan2(y, x) - Math.atan2(f, -COXA)
    const knee = Math.acos(
        clip((h * h - MINI_FEMUR ** 2 - MINI_TIBIA ** 2) / (2 * MINI_FEMUR * MINI_TIBIA), -1, 1)
    )
    const thigh =
        Math.atan2(z, f) -
        Math.atan2(MINI_TIBIA * Math.sin(knee), MINI_FEMUR + MINI_TIBIA * Math.cos(knee))
    return [hip, thigh, knee].map(angle => (angle * 180) / Math.PI) as Vec3
}

/** firmware_joint_angles: the firmware's joint angles (deg) for these feet, which walk_state.h caps. */
const firmwareJointAngles = (feet: Vec3[]) =>
    FIRMWARE_ORDER.flatMap((leg, i) => {
        const offset = feet[leg].map((value, axis) => value - DEFAULT_FEET[leg][axis])
        const left = FIRMWARE_LEFT[i] * COXA + offset[0]
        return miniLegAngles(-FIRMWARE_LEFT[i] * left, offset[2] - BODY_HEIGHT, -offset[1])
    })

type Pose = { omega: number; phi: number; psi: number; xm: number; ym: number; zm: number }

export class BodyState {
    omega = 0
    phi = 0
    psi = 0
    xm = 0
    ym = 0
    zm = 0
    feet: Vec3[] = DEFAULT_FEET.map(foot => [...foot] as Vec3)

    constructor(pose: Partial<Pose> = {}) {
        Object.assign(this, pose)
    }
}

export class GaitState {
    stepHeight = STEP_HEIGHT
    stepX = 0
    stepZ = 0
    stepAngle = 0
    stepVelocity = SPEED
    stepDepth = STEP_DEPTH
    standFrac = TROT_STAND_FRAC
    offset = [...TROT_OFFSET]
    speedFactor = TROT_SPEED_FACTOR
    /** [stepX, stepZ, stepAngle] as commanded, which the smoothing follows, and its velocities. */
    target = [0, 0, 0]
    smoothing = [0, 0, 0]
}

const stanceCurve = (length: number, angle: number, depth: number, phase: number, point: Vec3) => {
    const step = length * (1 - 2 * phase)
    point[0] += step * Math.cos(angle)
    point[2] += step * Math.sin(angle)
    if (length !== 0) point[1] = -depth * Math.cos((Math.PI * step) / (2 * length))
}

const bezierCurve = (length: number, angle: number, height: number, phase: number, point: Vec3) => {
    const xPolar = Math.cos(angle)
    const zPolar = Math.sin(angle)
    const phasePowers = [1]
    const restPowers = [1]
    for (let i = 1; i < 12; i++) {
        phasePowers.push(phasePowers[i - 1] * phase)
        restPowers.push(restPowers[i - 1] * (1 - phase))
    }
    for (let i = 0; i < 12; i++) {
        const b = COMBINATORIAL_VALUES[i] * phasePowers[i] * restPowers[11 - i]
        point[0] += b * BEZIER_STEPS[i] * length * xPolar
        point[2] += b * BEZIER_STEPS[i] * length * zPolar
        point[1] += b * BEZIER_HEIGHTS[i] * height
    }
}

/** A foot's stroke in the base frame (X, Y): the translation and the rotation about the firmware's body centre. */
const strokeVector = (gait: GaitState, radius: number[]): [number, number] => [
    gait.stepZ - gait.stepAngle * radius[1],
    -gait.stepX + gait.stepAngle * radius[0]
]

/** The stroke as one stride vector at a foot: (amplitude, heading). */
const stroke = (gait: GaitState, radius: number[]): [number, number] => {
    const [sx, sy] = strokeVector(gait, radius)
    return [Math.hypot(sx, sy), Math.atan2(sx, -sy)]
}

/** _reach_bound: the share of the stroke and the step height that keep every foot within reach. */
const reachBound = (gait: GaitState): [number, number] => {
    const bodyHeight = BODY_HEIGHT
    const room = Math.sqrt(Math.max(0, REACH_OUT_SQ - (bodyHeight + gait.stepDepth) ** 2))
    const leastOffset = Math.sqrt(Math.max(0, REACH_IN_SQ - bodyHeight * bodyHeight))
    let scale = COXA < room && leastOffset <= COXA ? 1 : 0
    const legs = FIRMWARE_FEET.map(radius => {
        const [strokeX, strokeY] = strokeVector(gait, radius)
        const length = Math.hypot(strokeX, strokeY)
        const half = length / 2
        const reach = SWING_OVERSHOOT * half
        const outward = length > 0 ? (Math.sign(radius[0]) * strokeX) / length : 0
        const a = reach * reach
        const b = COXA * Math.abs(outward) * reach
        const c = COXA * COXA - room * room
        const discriminant = b * b - a * c
        if (c < 0 && reach > 0 && discriminant >= 0)
            scale = Math.min(scale, -c / (b + Math.sqrt(discriminant)))
        const inward = Math.abs(outward) * reach
        if (leastOffset > 0 && inward > 0) scale = Math.min(scale, (COXA - leastOffset) / inward)
        return { half, outward }
    })
    let stepHeight = gait.stepHeight
    for (const { half, outward } of legs)
        for (let sample = 1; sample < SWING_SAMPLES; sample++) {
            const offset = COXA + scale * half * SWING_ALONG[sample] * outward
            const clear = Math.sqrt(Math.max(0, REACH_IN_SQ - (offset > 0 ? offset * offset : 0)))
            stepHeight = Math.min(stepHeight, Math.max(0, bodyHeight - clear) / SWING_LIFT[sample])
        }
    return [scale, stepHeight]
}

// walk_state.h's cap on joint speed (deg/s, a quarter below the servos' 720 deg/s), and the parts of a tick it slows by.
const MAX_JOINT_SPEED_DEG_S = 0.75 * 720
const SLOWDOWN_PARTS = 64

export class GaitController {
    phase = 0
    private readonly defaultPosition = DEFAULT_FEET

    /**
     * The stroke follows the command, then the phase advances, both by dt or by the most 64ths of it the joints can
     * follow at MAX_JOINT_SPEED_DEG_S, as step does; a jump not even a 64th keeps under the cap is walked at full time.
     */
    step(gait: GaitState, dt: number) {
        const startPhase = this.phase
        const startSteps = [gait.stepX, gait.stepZ, gait.stepAngle]
        const startSmoothing = [...gait.smoothing]
        const before = this.jointAngles(gait)
        const followable = (parts: number) => {
            this.phase = startPhase
            ;[gait.stepX, gait.stepZ, gait.stepAngle] = startSteps
            gait.smoothing = [...startSmoothing]
            this.advance(gait, (dt * parts) / SLOWDOWN_PARTS)
            const after = this.jointAngles(gait)
            return (
                Math.max(...after.map((angle, i) => Math.abs(angle - before[i]))) <=
                MAX_JOINT_SPEED_DEG_S * dt
            )
        }
        if (followable(SLOWDOWN_PARTS)) return
        let fastEnough = 0
        let tooFast = SLOWDOWN_PARTS
        while (tooFast - fastEnough > 1) {
            const parts = Math.trunc((fastEnough + tooFast) / 2)
            if (followable(parts)) fastEnough = parts
            else tooFast = parts
        }
        followable(fastEnough > 0 ? fastEnough : SLOWDOWN_PARTS)
    }

    private advance(gait: GaitState, dt: number) {
        const decay = Math.exp(-SMOOTHING_OMEGA * dt)
        const [stepX, stepZ, stepAngle] = [gait.stepX, gait.stepZ, gait.stepAngle].map(
            (value, i) => {
                const offset = value - gait.target[i]
                const drive = gait.smoothing[i] + SMOOTHING_OMEGA * offset
                gait.smoothing[i] = (gait.smoothing[i] - SMOOTHING_OMEGA * drive * dt) * decay
                return gait.target[i] + (offset + drive * dt) * decay
            }
        )
        Object.assign(gait, { stepX, stepZ, stepAngle })
        // walk_state.h: a stopped walk holds its phase once every foot is down; a foot in the air lands first.
        const swinging = gait.offset.some(offset => (this.phase + offset) % 1 > gait.standFrac)
        if (!moving(gait) && !swinging) return
        const velocity = Math.max(gait.stepVelocity, 0.5)
        this.phase = (this.phase + dt * velocity * gait.speedFactor) % 1
    }

    private jointAngles(gait: GaitState) {
        const body = new BodyState()
        this.generateFeet(gait, body)
        return firmwareJointAngles(body.feet)
    }

    /** Writes body.feet at the current phase, in the base frame. */
    generateFeet(gait: GaitState, body: BodyState) {
        const [scale, stepHeight] = reachBound(gait)
        body.feet = this.defaultPosition.map((home, i) => {
            const legPhase = (this.phase + gait.offset[i]) % 1
            const contact = legPhase <= gait.standFrac
            const phase =
                contact ?
                    legPhase / gait.standFrac
                :   (legPhase - gait.standFrac) / (1 - gait.standFrac)
            const [amplitude, heading] = stroke(gait, FIRMWARE_FEET[i])
            const delta: Vec3 = [0, 0, 0]
            if (contact) stanceCurve(scale * amplitude * 0.5, heading, gait.stepDepth, phase, delta)
            else bezierCurve(scale * amplitude * 0.5, heading, stepHeight, phase, delta)
            // Stride frame to base frame: forward stride to -Y, cross-stride to +X, lift to +Z.
            return [home[0] + delta[2], home[1] - delta[0], home[2] + delta[1]]
        })
    }
}

const moving = (gait: GaitState) =>
    Math.abs(gait.stepX) >= 0.001 ||
    Math.abs(gait.stepZ) >= 0.001 ||
    Math.abs(gait.stepAngle) >= 0.001

/** 12 joint angles for the feet, after shifting them by the body pose (identity by default). */
export function inverseKinematics(body: BodyState): number[] {
    return LEG_NAMES.flatMap((leg, i) => {
        const foot = body.feet[i]
        const shifted: Vec3 = [foot[0] - body.xm, foot[1] - body.ym, foot[2] - body.zm]
        const local = rotX(-body.omega, rotY(-body.phi, rotZ(-body.psi, shifted)))
        return legIk(leg, local)
    })
}

/**
 * The sticks to the stroke the gait smooths towards, as gait_action: walk_state.h's handleCommand,
 * step_x = ly, step_z = -lx, step_angle = rx, at the client's speed and step height.
 */
export function gaitAction([lx, ly, rx]: Sticks, gait: GaitState) {
    gait.target = [ly * MAX_STEP_LENGTH, -lx * MAX_STEP_LENGTH, rx]
    gait.stepVelocity = SPEED
    gait.stepHeight = STEP_HEIGHT
    gait.stepDepth = STEP_DEPTH
}

/** One controller instance per simulated robot: the loop body of leika/robot.py. */
export class PicoController {
    readonly gait = new GaitState()
    readonly body = new BodyState()
    private readonly gaitController = new GaitController()

    get phase() {
        return this.gaitController.phase
    }

    /** One 10 ms control tick; returns the 12 actuator targets in radians. */
    tick(sticks: Sticks): number[] {
        gaitAction(sticks, this.gait)
        this.gaitController.step(this.gait, CONTROL_DT)
        this.gaitController.generateFeet(this.gait, this.body)
        return inverseKinematics(this.body)
    }
}
