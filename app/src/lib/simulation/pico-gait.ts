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
export type Command = [vx: number, vy: number, yaw: number]
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
    if (length !== 0) point[1] = -depth * Math.cos((Math.PI * (point[0] + point[2])) / (2 * length))
}

const bezierCurve = (length: number, angle: number, height: number, phase: number, point: Vec3) => {
    const xPolar = Math.cos(angle)
    const zPolar = Math.sin(angle)
    const t = Math.min(Math.max(phase, 1e-4), 1 - 1e-4)
    const oneMinus = 1 - t
    let phasePower = 1
    let invPhasePower = oneMinus ** 11
    for (let i = 0; i < 12; i++) {
        const b = COMBINATORIAL_VALUES[i] * phasePower * invPhasePower
        point[0] += b * BEZIER_STEPS[i] * length * xPolar
        point[2] += b * BEZIER_STEPS[i] * length * zPolar
        point[1] += b * BEZIER_HEIGHTS[i] * height
        phasePower *= t
        if (oneMinus !== 0) invPhasePower /= oneMinus
    }
}

/** Translation and rotation composed into one stride vector at a foot: (amplitude, heading). */
const stroke = (gait: GaitState, radius: number[]): [number, number] => {
    const sx = gait.stepZ - gait.stepAngle * radius[1]
    const sy = -gait.stepX + gait.stepAngle * radius[0]
    return [Math.hypot(sx, sy), Math.atan2(sx, -sy)]
}

const moving = (gait: GaitState) =>
    Math.abs(gait.stepX) >= 0.001 ||
    Math.abs(gait.stepZ) >= 0.001 ||
    Math.abs(gait.stepAngle) >= 0.001

export class GaitController {
    phase = 0
    private readonly defaultPosition = DEFAULT_FEET

    /** The stroke follows the command, then the phase advances; standing still rests it at 0. */
    step(gait: GaitState, dt: number) {
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
        if (!moving(gait)) {
            this.phase = 0
            return
        }
        const velocity = Math.max(gait.stepVelocity, 0.5)
        this.phase = (this.phase + dt * velocity * gait.speedFactor) % 1
    }

    /** Writes body.feet at the current phase, in the base frame. */
    generateFeet(gait: GaitState, body: BodyState) {
        body.feet = this.defaultPosition.map((home, i) => {
            const legPhase = (this.phase + gait.offset[i]) % 1
            const contact = legPhase <= gait.standFrac
            const phase =
                contact ?
                    legPhase / gait.standFrac
                :   (legPhase - gait.standFrac) / (1 - gait.standFrac)
            const [amplitude, heading] = stroke(gait, FIRMWARE_FEET[i])
            const delta: Vec3 = [0, 0, 0]
            if (contact) stanceCurve(amplitude * 0.5, heading, gait.stepDepth, phase, delta)
            else bezierCurve(amplitude * 0.5, heading, gait.stepHeight, phase, delta)
            // Stride frame to base frame: forward stride to -Y, cross-stride to +X, lift to +Z.
            return [
                home[0] + delta[2],
                home[1] - delta[0],
                home[2] + (amplitude !== 0 ? delta[1] : 0)
            ]
        })
    }
}

/** 12 joint angles for the feet, after shifting them by the body pose (identity by default). */
export function inverseKinematics(body: BodyState): number[] {
    return LEG_NAMES.flatMap((leg, i) => {
        const foot = body.feet[i]
        const shifted: Vec3 = [foot[0] - body.xm, foot[1] - body.ym, foot[2] - body.zm]
        const local = rotX(-body.omega, rotY(-body.phi, rotZ(-body.psi, shifted)))
        return legIk(leg, local)
    })
}

const RATE = Math.max(SPEED, 0.5) * TROT_SPEED_FACTOR
/** The trot's top speed, a full stick on each axis, as speed_model.max_velocity. */
export const MAX_VELOCITY: Command = [
    (MAX_STEP_LENGTH * RATE) / TROT_STAND_FRAC,
    (MAX_STEP_LENGTH * RATE) / TROT_STAND_FRAC,
    RATE / TROT_STAND_FRAC
]

/**
 * Command [vx, vy, yaw] (m/s, m/s, rad/s) to the stroke the gait smooths towards, as
 * analytic_gait_action: the sticks of speed_model.sticks_for, then walk_state.h's step_x = ly,
 * step_z = -lx, step_angle = rx.
 */
export function analyticGaitAction([vx, vy, yaw]: Command, gait: GaitState) {
    const lx = clip(-vy / MAX_VELOCITY[1], -1, 1)
    const ly = clip(vx / MAX_VELOCITY[0], -1, 1)
    const rx = clip(yaw / MAX_VELOCITY[2], -1, 1)
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
    tick(cmd: Command): number[] {
        analyticGaitAction(cmd, this.gait)
        this.gaitController.step(this.gait, CONTROL_DT)
        this.gaitController.generateFeet(this.gait, this.body)
        return inverseKinematics(this.body)
    }
}
