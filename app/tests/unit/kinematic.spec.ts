import { describe, it, expect } from 'vitest'
import Kinematic, { type body_state_t, type KinematicParams } from '$lib/kinematic'

const params: KinematicParams = {
    coxa: 0.0605,
    coxa_offset: 0.01,
    femur: 0.1112,
    tibia: 0.1185,
    L: 0.2075,
    W: 0.078
}

const kinematic = () => new Kinematic(params)

function stance(kin: Kinematic, overrides: Partial<body_state_t> = {}): body_state_t {
    return {
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: kin.default_body_height,
        zm: 0,
        feet: kin.getDefaultFeetPos(),
        ...overrides
    }
}

const rotateY = ([x, y, z]: number[], angle: number) => [
    x * Math.cos(angle) + z * Math.sin(angle),
    y,
    -x * Math.sin(angle) + z * Math.cos(angle)
]

describe('Kinematic.getDefaultFeetPos', () => {
    it('returns a copy so callers cannot corrupt the stored stance', () => {
        const kin = kinematic()
        const first = kin.getDefaultFeetPos()
        first[0][0] = 999

        expect(kin.getDefaultFeetPos()[0][0]).not.toBe(999)
    })
})

describe('Kinematic.calcIK', () => {
    it('resolves the default stance to finite angles for all twelve joints', () => {
        const kin = kinematic()
        const angles = kin.calcIK(stance(kin))

        expect(angles).toHaveLength(12)
        angles.forEach(angle => expect(Number.isFinite(angle)).toBe(true))
    })

    // Body pose enters calcIK only as the inverse of the body transform, so shifting the body and
    // the feet by the same vector describes an identical leg geometry. A sign or index slip in
    // inv_rot / inv_trans breaks this while still producing plausible-looking angles.
    it('is invariant when body and feet translate together', () => {
        const kin = kinematic()
        const shift = [0.013, -0.021, 0.007]

        const base = kin.calcIK(stance(kin))
        const shifted = kin.calcIK(
            stance(kin, {
                xm: shift[0],
                ym: kin.default_body_height + shift[1],
                zm: shift[2],
                feet: kin
                    .getDefaultFeetPos()
                    .map(([x, y, z]) => [x + shift[0], y + shift[1], z + shift[2]])
            })
        )

        shifted.forEach((angle, i) => expect(angle).toBeCloseTo(base[i], 9))
    })

    // phi is the rotation about the vertical axis in this convention (see euler2R), matching the
    // firmware's kinematics.h.
    it('is invariant when body heading and feet rotate together', () => {
        const kin = kinematic()
        const heading = 12

        const base = kin.calcIK(stance(kin))
        const rotated = kin.calcIK(
            stance(kin, {
                phi: heading,
                feet: kin.getDefaultFeetPos().map(foot => rotateY(foot, heading * (Math.PI / 180)))
            })
        )

        rotated.forEach((angle, i) => expect(angle).toBeCloseTo(base[i], 6))
    })

    // Unreachable targets must degrade to a clamped pose: a NaN here would be forwarded to the
    // servo controller as a commanded angle.
    it('clamps unreachable targets instead of producing NaN', () => {
        const kin = kinematic()
        const farAway = kin.getDefaultFeetPos().map(([x, , z]) => [x, -10, z])

        const angles = kin.calcIK(stance(kin, { feet: farAway }))

        expect(angles).toHaveLength(12)
        angles.forEach(angle => expect(Number.isNaN(angle)).toBe(false))
    })
})
