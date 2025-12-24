export interface body_state_t {
    omega: number
    phi: number
    psi: number
    xm: number
    ym: number
    zm: number
    feet: number[][]
    cumulative_x: number
    cumulative_y: number
    cumulative_z: number
    cumulative_roll: number
    cumulative_pitch: number
    cumulative_yaw: number
}

export interface position {
    x: number
    y: number
    z: number
}

export interface target_position {
    x: number
    z: number
    yaw: number
}

export interface KinematicParams {
    coxa: number
    coxa_offset: number
    femur: number
    tibia: number
    L: number
    W: number
}

const { cos, sin, atan2, acos, sqrt, max, min } = Math

const DEG2RAD = 0.017453292519943

export default class Kinematic {
    coxa: number
    coxa_offset: number
    femur: number
    tibia: number

    L: number
    W: number

    DEG2RAD = DEG2RAD

    max_roll: number
    max_pitch: number
    max_body_shift_x: number
    max_body_shift_z: number
    max_leg_reach: number
    min_body_height: number
    max_body_height: number
    body_height_range: number
    max_step_length: number
    max_step_height: number
    default_step_depth: number
    default_body_height: number
    default_step_height: number

    mountOffsets: number[][]
    default_feet_positions: number[][]

    invMountRot = [
        [0, 0, -1],
        [0, 1, 0],
        [1, 0, 0]
    ]

    constructor(params: KinematicParams) {
        this.coxa = params.coxa
        this.coxa_offset = params.coxa_offset
        this.femur = params.femur
        this.tibia = params.tibia
        this.L = params.L
        this.W = params.W

        this.max_roll = 15 * (Math.PI / 2)
        this.max_pitch = 15 * (Math.PI / 2)
        this.max_body_shift_x = this.W / 3
        this.max_body_shift_z = this.W / 3
        this.max_leg_reach = this.femur + this.tibia - this.coxa_offset
        this.min_body_height = this.max_leg_reach * 0.45
        this.max_body_height = this.max_leg_reach * 1
        this.body_height_range = this.max_body_height - this.min_body_height
        this.max_step_length = this.max_leg_reach * 0.8
        this.max_step_height = this.max_leg_reach / 2
        this.default_step_depth = 0.002
        this.default_body_height = this.min_body_height + this.body_height_range / 2
        this.default_step_height = this.default_body_height / 2

        this.mountOffsets = [
            [this.L / 2, 0, this.W / 2],
            [this.L / 2, 0, -this.W / 2],
            [-this.L / 2, 0, this.W / 2],
            [-this.L / 2, 0, -this.W / 2]
        ]

        this.default_feet_positions = this.mountOffsets.map((offset, i) => {
            return [offset[0], 0, offset[2] + (i % 2 === 1 ? -this.coxa : this.coxa)]
        })
    }

    getDefaultFeetPos(): number[][] {
        return this.default_feet_positions.map(pos => [...pos])
    }

    calcIK(p: body_state_t): number[] {
        const roll = p.omega * this.DEG2RAD
        const pitch = p.phi * this.DEG2RAD
        const yaw = p.psi * this.DEG2RAD
        const rot = this.euler2R(roll, pitch, yaw)
        const inv_rot = [
            [rot[0][0], rot[1][0], rot[2][0]],
            [rot[0][1], rot[1][1], rot[2][1]],
            [rot[0][2], rot[1][2], rot[2][2]]
        ]
        const inv_trans = [
            -inv_rot[0][0] * p.xm - inv_rot[0][1] * p.ym - inv_rot[0][2] * p.zm,
            -inv_rot[1][0] * p.xm - inv_rot[1][1] * p.ym - inv_rot[1][2] * p.zm,
            -inv_rot[2][0] * p.xm - inv_rot[2][1] * p.ym - inv_rot[2][2] * p.zm
        ]
        return p.feet.flatMap((foot, i) => {
            const [wx, wy, wz] = foot
            const bx = inv_rot[0][0] * wx + inv_rot[0][1] * wy + inv_rot[0][2] * wz + inv_trans[0]
            const by = inv_rot[1][0] * wx + inv_rot[1][1] * wy + inv_rot[1][2] * wz + inv_trans[1]
            const bz = inv_rot[2][0] * wx + inv_rot[2][1] * wy + inv_rot[2][2] * wz + inv_trans[2]

            const [mx, my, mz] = this.mountOffsets[i]
            const px = bx - mx,
                py = by - my,
                pz = bz - mz

            const lx =
                this.invMountRot[0][0] * px +
                this.invMountRot[0][1] * py +
                this.invMountRot[0][2] * pz
            const ly =
                this.invMountRot[1][0] * px +
                this.invMountRot[1][1] * py +
                this.invMountRot[1][2] * pz
            const lz =
                this.invMountRot[2][0] * px +
                this.invMountRot[2][1] * py +
                this.invMountRot[2][2] * pz

            const xLocal = i % 2 === 1 ? -lx : lx
            return this.legIK(xLocal, ly, lz)
        })
    }

    private legIK(x: number, y: number, z: number): [number, number, number] {
        const F = sqrt(max(0, x * x + y * y - this.coxa * this.coxa))
        const G = F - this.coxa_offset
        const H = sqrt(G * G + z * z)
        const t1 = -atan2(y, x) - atan2(F, -this.coxa)
        const D =
            (H * H - this.femur * this.femur - this.tibia * this.tibia) /
            (2 * this.femur * this.tibia)
        const t3 = acos(max(-1, min(1, D)))
        const t2 = atan2(z, G) - atan2(this.tibia * sin(t3), this.femur + this.tibia * cos(t3))
        return [t1, t2, t3]
    }

    private euler2R(roll: number, pitch: number, yaw: number): number[][] {
        const cr = cos(roll),
            sr = sin(roll)
        const cp = cos(pitch),
            sp = sin(pitch)
        const cy = cos(yaw),
            sy = sin(yaw)
        return [
            [cp * cy, -cp * sy, sp],
            [sr * sp * cy + sy * cr, -sr * sp * sy + cr * cy, -sr * cp],
            [sr * sy - sp * cr * cy, sr * cy + sp * sy * cr, cr * cp]
        ]
    }
}
