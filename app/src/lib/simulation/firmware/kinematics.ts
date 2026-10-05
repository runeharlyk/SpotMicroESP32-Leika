import type { KinConfig } from './kin-config'

// The firmware's own constants, truncated as in utils/math_utils.h.
export const DEG2RAD_F = 0.0174532
export const RAD2DEG_F = 57.2957795

/** body_state_t: body pose (degrees, metres) and the four foot targets (x forward, y up, z sideways). */
export class BodyState {
    omega = 0
    phi = 0
    psi = 0
    xm = 0
    ym: number
    zm = 0
    feet: number[][]

    constructor(cfg: KinConfig) {
        this.ym = cfg.defaultBodyHeight
        this.feet = cfg.defaultFeet.map(foot => [...foot])
    }
}

const clampUnit = (value: number) => Math.max(-1, Math.min(1, value))

/** Kinematics::legIK: hip, femur and knee angles in degrees for a foot at (x, y, z) in the leg's frame. */
export function legIk(cfg: KinConfig, x: number, y: number, z: number): number[] {
    return solveLeg(cfg, x, y, z).angles
}

/** legIk, and whether the foot is in reach: legIK's return value. */
function solveLeg(cfg: KinConfig, x: number, y: number, z: number) {
    const { coxa, coxa_offset, femur, tibia } = cfg
    const F = Math.sqrt(Math.max(0, x * x + y * y - coxa * coxa))
    const G = F - coxa_offset
    const H = Math.sqrt(G * G + z * z)

    const theta1 = -Math.atan2(y, x) - Math.atan2(F, -coxa)
    const D = (H * H - femur * femur - tibia * tibia) / (2 * femur * tibia)
    const theta3 = Math.acos(clampUnit(D))
    const theta2 =
        Math.atan2(z, G) - Math.atan2(tibia * Math.sin(theta3), femur + tibia * Math.cos(theta3))
    // Yertle's knee servo is referenced to the femur's world angle, not to the femur.
    const knee = cfg.variant === 'SPOTMICRO_YERTLE' ? theta3 + theta2 : theta3
    return {
        angles: [theta1 * RAD2DEG_F, theta2 * RAD2DEG_F, knee * RAD2DEG_F],
        reachable: D >= -1 && D <= 1 && x * x + y * y >= coxa * coxa
    }
}

/**
 * Kinematics::calculate_inverse_kinematics: 12 joint angles in degrees, before MotionService's dir table. Bit `leg` of
 * `unreachable.mask` is set for a foot the leg cannot reach, which it then bends as far as it goes.
 */
export function inverseKinematics(
    cfg: KinConfig,
    body: BodyState,
    unreachable?: { mask: number }
): number[] {
    const roll = body.omega * DEG2RAD_F
    const pitch = body.phi * DEG2RAD_F
    const yaw = body.psi * DEG2RAD_F
    const [cr, sr, cp, sp, cy, sy] = [
        Math.cos(roll),
        Math.sin(roll),
        Math.cos(pitch),
        Math.sin(pitch),
        Math.cos(yaw),
        Math.sin(yaw)
    ]
    const rot = [
        [cp * cy, -sy * cp, sp],
        [sr * sp * cy + sy * cr, -sr * sp * sy + cr * cy, -sr * cp],
        [sr * sy - sp * cr * cy, sr * cy + sp * sy * cr, cr * cp]
    ]
    const inv = [0, 1, 2].map(r => [0, 1, 2].map(c => rot[c][r]))
    const invTrans = inv.map(row => -row[0] * body.xm - row[1] * body.ym - row[2] * body.zm)

    return cfg.mountOffsets.flatMap((mount, i) => {
        const [wx, wy, wz] = body.feet[i]
        const b = inv.map((row, r) => row[0] * wx + row[1] * wy + row[2] * wz + invTrans[r])
        const [px, py, pz] = [b[0] - mount[0], b[1] - mount[1], b[2] - mount[2]]
        // invMountRot = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}
        const lx = -pz
        const ly = py
        const lz = px
        const { angles, reachable } = solveLeg(cfg, i % 2 === 1 ? -lx : lx, ly, lz)
        if (!reachable && unreachable) unreachable.mask |= 1 << i
        return angles
    })
}
