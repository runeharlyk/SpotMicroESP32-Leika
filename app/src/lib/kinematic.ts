export interface body_state_t {
  omega: number
  phi: number
  psi: number
  xm: number
  ym: number
  zm: number
  feet: number[][]
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

const { cos, sin, atan2, acos, sqrt, max, min } = Math

const DEG2RAD = 0.017453292519943

export default class Kinematic {
  l1: number
  l2: number
  l3: number
  l4: number

  L: number
  W: number

  DEG2RAD = DEG2RAD

  mountOffsets: number[][]

  invMountRot = [
    [0, 0, -1],
    [0, 1, 0],
    [1, 0, 0]
  ]

  constructor() {
    this.l1 = 60.5 / 100
    this.l2 = 10 / 100
    this.l3 = 111.7 / 100
    this.l4 = 118.5 / 100

    this.L = 207.5 / 100
    this.W = 78 / 100

    this.mountOffsets = [
      [this.L / 2, 0, this.W / 2],
      [this.L / 2, 0, -this.W / 2],
      [-this.L / 2, 0, this.W / 2],
      [-this.L / 2, 0, -this.W / 2]
    ]
  }

  getDefaultFeetPos(): number[][] {
    return this.mountOffsets.map((offset, i) => {
      return [offset[0], -1, offset[2] + (i % 2 === 1 ? -this.l1 : this.l1)]
    })
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
        this.invMountRot[0][0] * px + this.invMountRot[0][1] * py + this.invMountRot[0][2] * pz
      const ly =
        this.invMountRot[1][0] * px + this.invMountRot[1][1] * py + this.invMountRot[1][2] * pz
      const lz =
        this.invMountRot[2][0] * px + this.invMountRot[2][1] * py + this.invMountRot[2][2] * pz

      const xLocal = i % 2 === 1 ? -lx : lx
      return this.legIK(xLocal, ly, lz)
    })
  }

  private legIK(x: number, y: number, z: number): [number, number, number] {
    const F = sqrt(max(0, x * x + y * y - this.l1 * this.l1))
    const G = F - this.l2
    const H = sqrt(G * G + z * z)
    const t1 = -atan2(y, x) - atan2(F, -this.l1)
    const D = (H * H - this.l3 * this.l3 - this.l4 * this.l4) / (2 * this.l3 * this.l4)
    const t3 = acos(max(-1, min(1, D)))
    const t2 = atan2(z, G) - atan2(this.l4 * sin(t3), this.l3 + this.l4 * cos(t3))
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
