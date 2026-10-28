// Where the firmware's body and feet land in a model's Three.js scene. The frame is fitted from the model's own
// standing toes rather than written down per model, so a model turned, scaled or shifted by setupRobot needs no table.
// Each toe is paired with its own leg, since the four feet form a rectangle that a half turn maps onto itself.
import type { KinConfig } from '$lib/simulation/firmware/kin-config'
import { bodyRotation, type BodyState } from '$lib/simulation/firmware/kinematics'
import type { Vec3 } from './player'

const MM = 0.001

/**
 * scene = scale * rotation * p + origin for a point p (m) in the firmware's body frame, the body at the origin. The
 * rotation is orthogonal and may be a reflection.
 */
export interface ModelFrame {
    rotation: number[][]
    scale: number
    origin: number[]
    /** The worst distance (scene units) between a firmware foot and its leg's toe. */
    residual: number
}

const times = (m: number[][], v: readonly number[]): Vec3 =>
    [0, 1, 2].map(r => m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2]) as Vec3
const transpose = (m: number[][]) => [0, 1, 2].map(r => [0, 1, 2].map(c => m[c][r]))
const product = (a: number[][], b: number[][]) =>
    [0, 1, 2].map(r =>
        [0, 1, 2].map(c => a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c])
    )
const distance = (a: readonly number[], b: readonly number[]) =>
    Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2])
const QUARTER_TURNS: number[][][] = [
    [
        [1, 0, 0],
        [0, 1, 0],
        [0, 0, 1]
    ],
    [
        [0, 0, 1],
        [0, 1, 0],
        [-1, 0, 0]
    ],
    [
        [-1, 0, 0],
        [0, 1, 0],
        [0, 0, -1]
    ],
    [
        [0, 0, -1],
        [0, 1, 0],
        [1, 0, 0]
    ]
]
const MIRROR_Z = [
    [1, 0, 0],
    [0, 1, 0],
    [0, 0, -1]
]

/**
 * The quarter turns about the vertical, plain and mirrored: the scene's up is +y, as the firmware's is. A model may be
 * the firmware's mirror image (the Pico's is: its fl leg stands where the firmware's leg 0 lands only reflected), and
 * mirroring the frame keeps each leg's own joints moving with its firmware leg. Turning upside down is not allowed:
 * the feet lie in one plane, where a half turn about a level axis would pass for a mirror.
 */
const UPRIGHT_FRAMES: number[][][] = [
    ...QUARTER_TURNS,
    ...QUARTER_TURNS.map(turn => product(turn, MIRROR_Z))
]

export const toScene = (frame: ModelFrame, p: readonly number[]): Vec3 =>
    times(frame.rotation, p).map((v, i) => frame.scale * v + frame.origin[i]) as Vec3

const fromScene = (frame: ModelFrame, s: readonly number[]): Vec3 =>
    times(
        transpose(frame.rotation),
        s.map((v, i) => (v - frame.origin[i]) / frame.scale)
    )

/** The model frame that best puts the firmware's standing feet (body frame, m) on their legs' toes (scene). */
export function fitModelFrame(feet: number[][], toes: number[][], scale: number): ModelFrame {
    let best: ModelFrame | undefined
    for (const rotation of UPRIGHT_FRAMES) {
        const mapped = feet.map(f => times(rotation, f).map(v => v * scale))
        const mean = (points: number[][]) =>
            [0, 1, 2].map(i => points.reduce((sum, p) => sum + p[i], 0) / points.length)
        const toeMean = mean(toes)
        const footMean = mean(mapped)
        const origin = toeMean.map((v, i) => v - footMean[i])
        const placed = mapped.map(p => p.map((v, i) => v + origin[i]))
        const residual = Math.max(...placed.map((p, leg) => distance(p, toes[leg])))
        if (!best || residual < best.residual) best = { rotation, scale, origin, residual }
    }
    return best!
}

/** The model's rotation and translation in the scene for a body state, with the feet as world points. */
export function bodyTransform(frame: ModelFrame, body: BodyState) {
    const rotation = product(product(frame.rotation, bodyRotation(body)), transpose(frame.rotation))
    const turnedOrigin = times(rotation, frame.origin)
    const shift = times(frame.rotation, [body.xm, body.ym, body.zm])
    const translation = frame.origin.map((o, i) => o - turnedOrigin[i] + frame.scale * shift[i])
    return { rotation, translation }
}

/** The foot target of a keyframe leg (mm, REP-103 offset from its standing foot) as a world point (m). */
const footWorld = (cfg: KinConfig, leg: number, offset: readonly number[]): Vec3 => {
    const [x, y, z] = cfg.defaultFeet[leg]
    return [x + offset[0] * MM, y + offset[2] * MM, z + offset[1] * MM]
}

export const footHandleScene = (
    frame: ModelFrame,
    cfg: KinConfig,
    leg: number,
    offset: readonly number[]
): Vec3 => toScene(frame, footWorld(cfg, leg, offset))

export const footHandleOffset = (
    frame: ModelFrame,
    cfg: KinConfig,
    leg: number,
    scene: readonly number[]
): Vec3 => {
    const [x, y, z] = fromScene(frame, scene)
    const [sx, sy, sz] = cfg.defaultFeet[leg]
    return [(x - sx) / MM, (z - sz) / MM, (y - sy) / MM]
}
