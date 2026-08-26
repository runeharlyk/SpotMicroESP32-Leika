import { describe, it, expect } from 'vitest'
import { get } from 'svelte/store'
import { BezierState } from '$lib/gait'
import { currentKinematic } from '$lib/stores/featureFlags'
import { ControllerData } from '$lib/platform_shared/message'
import type { body_state_t } from '$lib/kinematic'

const command = (over: Partial<ControllerData> = {}) =>
    ControllerData.create({
        left: { x: 0, y: 0 },
        right: { x: 0, y: 0 },
        height: 0.5,
        speed: 1,
        s1: 0.5,
        ...over
    })

function freshBodyState(): body_state_t {
    const kin = get(currentKinematic)
    return {
        omega: 0,
        phi: 0,
        psi: 0,
        xm: 0,
        ym: kin.default_body_height,
        zm: 0,
        feet: kin.getDefaultFeetPos(),
        cumulative_x: 0,
        cumulative_y: 0,
        cumulative_z: 0,
        cumulative_roll: 0,
        cumulative_pitch: 0,
        cumulative_yaw: 0
    }
}

const radialComponent = (foot: number[], radius: number[]) => {
    const dx = foot[0] - radius[0]
    const dz = foot[2] - radius[2]
    const displacement = Math.hypot(dx, dz)
    if (displacement === 0) return 0
    return (dx * radius[0] + dz * radius[2]) / (displacement * Math.hypot(radius[0], radius[2]))
}

// A pure yaw command is a rigid-body rotation about the body centre, so every foot's horizontal
// stroke must stay tangential: perpendicular to that foot's own stance radius. These guard the
// velocity composition; the replaced yawArc also satisfied them, because update_foot_position resets
// each foot to its default before the controller runs, which zeroed yawArc's offset terms.
describe('BezierState pure yaw', () => {
    it('drives every foot tangentially to its stance radius', () => {
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = freshBodyState()
        const defaults = kin.getDefaultFeetPos()

        state.step(body, command({ right: { x: 1, y: 0 } }), 20)

        body.feet.forEach((foot, i) => {
            const dx = foot[0] - defaults[i][0]
            const dz = foot[2] - defaults[i][2]
            expect(Math.hypot(dx, dz)).toBeGreaterThan(0)
            expect(Math.abs(radialComponent(foot, defaults[i]))).toBeLessThan(1e-9)
        })
    })

    it('stays tangential after the feet have drifted over many cycles', () => {
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = freshBodyState()
        const defaults = kin.getDefaultFeetPos()
        const turn = command({ right: { x: 1, y: 0 } })

        for (let i = 0; i < 200; i++) state.step(body, turn, 20)

        body.feet.forEach((foot, i) =>
            expect(Math.abs(radialComponent(foot, defaults[i]))).toBeLessThan(1e-9)
        )
    })

    it('reverses stroke direction when the yaw command reverses', () => {
        const kin = get(currentKinematic)
        const defaults = kin.getDefaultFeetPos()

        const strokeFor = (yaw: number) => {
            const state = new BezierState()
            const body = freshBodyState()
            state.step(body, command({ right: { x: yaw, y: 0 } }), 20)
            return body.feet.map((foot, i) => [foot[0] - defaults[i][0], foot[2] - defaults[i][2]])
        }

        const positive = strokeFor(1)
        const negative = strokeFor(-1)

        positive.forEach(([dx, dz], i) => {
            expect(negative[i][0]).toBeCloseTo(-dx, 12)
            expect(negative[i][1]).toBeCloseTo(-dz, 12)
        })
    })

    // The default stance puts all four feet at the same radius, so feet sharing a gait phase offset
    // must receive strokes of identical magnitude. Feet on opposite phases legitimately differ,
    // because one is in stance while the other swings.
    it('gives feet on the same gait phase an equal stroke magnitude', () => {
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = freshBodyState()
        const defaults = kin.getDefaultFeetPos()

        state.step(body, command({ right: { x: 1, y: 0 } }), 20)

        const magnitude = (i: number) =>
            Math.hypot(body.feet[i][0] - defaults[i][0], body.feet[i][2] - defaults[i][2])
        const radius = (i: number) => Math.hypot(defaults[i][0], defaults[i][2])

        radius(0) // guards the assumption below
        defaults.forEach((_, i) => expect(radius(i)).toBeCloseTo(radius(0), 12))

        state.offset.forEach((offset, i) => {
            const peer = state.offset.findIndex((o, j) => j !== i && o === offset)
            if (peer >= 0) expect(magnitude(i)).toBeCloseTo(magnitude(peer), 12)
        })
    })
})

// The stroke direction must follow the commanded heading. The replaced code derived it from
// atan2(step_z, step_length) * 2, which is right for pure forward or pure lateral motion but skews
// diagonals: an equal-parts command came out at 70.5 degrees instead of 45.
describe('BezierState translation direction', () => {
    it('moves feet along the commanded diagonal', () => {
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = freshBodyState()
        const defaults = kin.getDefaultFeetPos()

        // left.y drives step_x, left.x drives -step_z, so this asks for equal +x and +z.
        state.step(body, command({ left: { x: -0.6, y: 0.6 } }), 20)

        body.feet.forEach((foot, i) => {
            const dx = foot[0] - defaults[i][0]
            const dz = foot[2] - defaults[i][2]
            expect(Math.hypot(dx, dz)).toBeGreaterThan(0)
            expect(dz / dx).toBeCloseTo(1, 9)
        })
    })
})

// Composing translation and rotation into one stroke means the swing profile is evaluated once.
// The replaced code ran the curve twice and summed both verticals -- and its second call contributed
// bezier height even at zero rotation length -- so every command overshot the commanded step height
// by a fifth.
describe('BezierState swing height', () => {
    it('never lifts a foot above the commanded step height', () => {
        const kin = get(currentKinematic)
        const state = new BezierState()
        const body = freshBodyState()
        const defaults = kin.getDefaultFeetPos()
        const cmd = command({ left: { x: 0, y: 1 }, right: { x: 1, y: 0 } })
        const step_height = cmd.s1 * kin.max_step_height

        let apex = 0
        for (let n = 0; n < 400; n++) {
            state.step(body, cmd, 20)
            body.feet.forEach((foot, i) => {
                apex = Math.max(apex, foot[1] - defaults[i][1])
            })
        }

        expect(apex).toBeGreaterThan(step_height * 0.9)
        expect(apex).toBeLessThanOrEqual(step_height)
    })
})
