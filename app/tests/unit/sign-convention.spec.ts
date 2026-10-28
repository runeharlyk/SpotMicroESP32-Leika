// @vitest-environment node
// The app's side of the shared sign convention: the firmware's motion code it runs in its simulation
// (firmware/motion.ts) and the Python simulation's gait it also runs (pico-gait.ts), each from the
// controller message on, as the app's sticks reach them. The same file the firmware's host test
// (esp32/test/host/sign_convention_test.cpp) and the simulation's test_sign_convention.py read;
// docs/conventions.md derives the signs.
import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { ControllerData, ModesEnum } from '../../src/lib/platform_shared/message'
import { FirmwareMotion } from '../../src/lib/simulation/firmware/motion'
import {
    BodyState,
    bodyRotation,
    inverseKinematics
} from '../../src/lib/simulation/firmware/kinematics'
import {
    kinConfig,
    type KinConfig,
    type Variant
} from '../../src/lib/simulation/firmware/kin-config'
import { DEFAULT_FEET, LEG_NAMES, PicoController } from '../../src/lib/simulation/pico-gait'
import { simulationSticks } from '../../src/lib/simulation/controls'

type Stick = { lx: number; ly: number; rx: number; ry?: number }
type Signs = number[]

const fixture: {
    legs: { name: string; side: Signs }[]
    attitude: {
        zero_rad: number
        poses: {
            name: string
            degrees: { omega: number; phi: number; psi: number }
            rotation_sign: Signs
        }[]
    }
    walk: {
        dt_s: number
        settle_ticks: number
        ticks: number
        zero_m_per_tick: number
        commands: { name: string; stick: Stick; stance_foot_sign: Signs[] }[]
    }
    stand: {
        settle_ticks: number
        zero_m: number
        zero_rad: number
        sticks: { name: string; stick: Required<Stick>; shift_sign: Signs; rotation_sign: Signs }[]
    }
} = JSON.parse(
    readFileSync(resolve(__dirname, '../../../platform_shared/sign_convention.json'), 'utf8')
)
const { legs, walk, stand } = fixture
const VARIANTS: Variant[] = ['SPOTMICRO_ESP32', 'SPOTMICRO_ESP32_MINI', 'SPOTMICRO_YERTLE']
// odometry.h's LIFTED_M: a foot this far above the lowest one is in the air. The stance dips a
// foot by the step depth (2 mm), so a planted foot stays within it.
const LIFTED_M = 0.003

const sign = (value: number, zero: number) =>
    value > zero ? 1
    : value < -zero ? -1
    : 0

const controller = ({ lx, ly, rx, ry = 0 }: Stick) =>
    ControllerData.create({
        left: { x: lx, y: ly },
        right: { x: rx, y: ry },
        height: 0.5,
        speed: 0.5,
        s1: 0.5
    })

// A point of the firmware's kinematics frame (x forward, y up, z left) in the fixture's
// (forward, left, up).
const rep = ([x, y, z]: number[]) => [x, z, y]

/**
 * The feet in the body frame as inverseKinematics moves them before it solves each leg, in the
 * fixture's frame. A level body at the origin standing on them gets the same legs, so they are the
 * feet the IK places, not a reading of its formula.
 */
const bodyFrameFeet = (cfg: KinConfig, body: BodyState) => {
    const rot = bodyRotation(body)
    const local = body.feet.map(foot =>
        [0, 1, 2].map(row =>
            [0, 1, 2].reduce(
                (sum, k) => sum + rot[k][row] * (foot[k] - [body.xm, body.ym, body.zm][k]),
                0
            )
        )
    )
    const level = new BodyState(cfg)
    level.ym = 0
    level.feet = local.map(foot => [...foot, 1])
    const posed = inverseKinematics(cfg, body)
    inverseKinematics(cfg, level).forEach((angle, joint) =>
        expect(Math.abs(angle - posed[joint])).toBeLessThan(1e-6)
    )
    return local.map(rep)
}

const mean = (feet: number[][], weights: number[], axis: number) =>
    feet.reduce((sum, foot, leg) => sum + (weights[leg] * foot[axis]) / 2, 0)

/**
 * The body over its feet: world up is the normal of the ground the feet span, the offset is the
 * part along that ground of the way from the feet's centre to the body's origin, and the yaw is the
 * turn that carries the feet of `rest` about their centre onto these, negated, as the feet stand
 * still while the body turns.
 */
const poseOverFeet = (feet: number[][], rest: number[][]) => {
    const axes = [0, 1, 2]
    const across = axes.map(a =>
        mean(
            feet,
            legs.map(leg => leg.side[1]),
            a
        )
    )
    const along = axes.map(a =>
        mean(
            feet,
            legs.map(leg => leg.side[0]),
            a
        )
    )
    const normal = [
        along[1] * across[2] - along[2] * across[1],
        along[2] * across[0] - along[0] * across[2],
        along[0] * across[1] - along[1] * across[0]
    ]
    const up = normal.map(v => v / Math.hypot(...normal))
    const centre = axes.map(a => mean(feet, [0.5, 0.5, 0.5, 0.5], a))
    const height = centre.reduce((sum, v, a) => sum + v * up[a], 0)
    const centred = (f: number[][]) => {
        const middle = [0, 1].map(a => f.reduce((sum, foot) => sum + foot[a] / 4, 0))
        return f.map(foot => [foot[0] - middle[0], foot[1] - middle[1]])
    }
    const [p, q] = [centred(rest), centred(feet)]
    const cross = p.reduce((sum, [x, y], leg) => sum + x * q[leg][1] - y * q[leg][0], 0)
    const dot = p.reduce((sum, [x, y], leg) => sum + x * q[leg][0] + y * q[leg][1], 0)
    return {
        offset: [0, 1].map(a => -centre[a] + height * up[a]),
        // Roll lowers the right side, so it tips world up toward body left; pitch lowers the nose,
        // so it tips world up toward body backward.
        rotation: (zero: number) => [
            sign(up[1], zero),
            sign(-up[0], zero),
            sign(-Math.atan2(cross, dot), zero)
        ]
    }
}

describe('sign convention: the firmware port', () => {
    it.each(VARIANTS)('puts every leg of %s on its fixture side', variant => {
        const cfg = kinConfig(variant)
        legs.forEach((leg, i) => {
            expect(
                rep(cfg.mountOffsets[i])
                    .slice(0, 2)
                    .map(v => sign(v, 0)),
                leg.name
            ).toEqual(leg.side)
            expect(
                rep(cfg.defaultFeet[i])
                    .slice(0, 2)
                    .map(v => sign(v, 0)),
                leg.name
            ).toEqual(leg.side)
        })
    })

    describe.each(VARIANTS)('%s', variant => {
        const cfg = kinConfig(variant)

        it.each(fixture.attitude.poses)('rotates the body for a $name', row => {
            const level = new BodyState(cfg)
            const rest = bodyFrameFeet(cfg, level)
            const posed = Object.assign(new BodyState(cfg), row.degrees)
            const { rotation } = poseOverFeet(bodyFrameFeet(cfg, posed), rest)
            expect(rotation(fixture.attitude.zero_rad)).toEqual(row.rotation_sign)
        })

        it.each(stand.sticks)('shifts and rotates a standing body for the $name', row => {
            const motion = new FirmwareMotion(variant)
            const hold = (stick: Stick) => {
                motion.handleInput(controller(stick))
                for (let tick = 0; tick < stand.settle_ticks; tick++)
                    motion.update(walk.dt_s, [0, 0])
                return bodyFrameFeet(cfg, motion.body)
            }
            motion.setMode(ModesEnum.STAND)
            const rest = hold({ lx: 0, ly: 0, rx: 0, ry: 0 })
            const held = hold(row.stick)
            const before = poseOverFeet(rest, rest)
            const after = poseOverFeet(held, rest)
            expect([0, 1].map(a => sign(after.offset[a] - before.offset[a], stand.zero_m))).toEqual(
                row.shift_sign
            )
            expect(after.rotation(stand.zero_rad)).toEqual(row.rotation_sign)
        })

        it.each(walk.commands)('moves each stance foot for the $name', command => {
            const motion = new FirmwareMotion(variant)
            motion.setMode(ModesEnum.STAND)
            motion.handleInput(controller({ lx: 0, ly: 0, rx: 0 }))
            for (let tick = 0; tick < walk.settle_ticks; tick++) motion.update(walk.dt_s, [0, 0])
            motion.setMode(ModesEnum.WALK)
            motion.handleInput(controller(command.stick))
            const step = () => {
                motion.update(walk.dt_s, [0, 0])
                const feet = bodyFrameFeet(cfg, motion.body)
                const ground = Math.min(...feet.map(foot => foot[2]))
                return { feet, planted: feet.map(foot => foot[2] - ground < LIFTED_M) }
            }
            for (let tick = 0; tick < walk.settle_ticks; tick++) step()
            expectStanceSigns(command, step)
        })
    })
})

// The stance feet's mean displacement per tick over the fixture's ticks, against its signs.
const expectStanceSigns = (
    command: (typeof walk.commands)[number],
    step: () => { feet: number[][]; planted: boolean[] }
) => {
    const total = legs.map(() => [0, 0])
    const planted = legs.map(() => 0)
    let before = step()
    for (let tick = 0; tick < walk.ticks; tick++) {
        const after = step()
        legs.forEach((_, i) => {
            if (!before.planted[i] || !after.planted[i]) return
            total[i][0] += after.feet[i][0] - before.feet[i][0]
            total[i][1] += after.feet[i][1] - before.feet[i][1]
            planted[i]++
        })
        before = after
    }
    legs.forEach((leg, i) => {
        expect(planted[i], leg.name).toBeGreaterThan(0)
        expect(
            total[i].map(v => sign(v / planted[i], walk.zero_m_per_tick)),
            leg.name
        ).toEqual(command.stance_foot_sign[i])
    })
}

describe('sign convention: the Python simulation gait port', () => {
    // Its base frame: +X left, +Y rear, +Z up; legs fr, fl, rr, rl.
    const fromModel = ([x, y, z]: number[]) => [-y, x, z]
    const order = legs.map(leg =>
        LEG_NAMES.indexOf(
            leg.name
                .split(' ')
                .map(word => word[0])
                .join('') as (typeof LEG_NAMES)[number]
        )
    )

    it('names and places every leg on its fixture side', () => {
        expect([...order].sort()).toEqual([0, 1, 2, 3])
        legs.forEach((leg, i) => {
            expect(
                fromModel(DEFAULT_FEET[order[i]])
                    .slice(0, 2)
                    .map(v => sign(v, 0)),
                leg.name
            ).toEqual(leg.side)
        })
    })

    it.each(walk.commands)('moves each stance foot for the $name', command => {
        const pico = new PicoController()
        const sticks = simulationSticks(controller(command.stick), ModesEnum.WALK)
        const step = () => {
            pico.tick(sticks)
            const { offset, standFrac } = pico.gait
            return {
                feet: order.map(leg => fromModel(pico.body.feet[leg])),
                planted: order.map(leg => (pico.phase + offset[leg]) % 1 <= standFrac)
            }
        }
        for (let tick = 0; tick < walk.settle_ticks; tick++) step()
        expectStanceSigns(command, step)
    })
})
