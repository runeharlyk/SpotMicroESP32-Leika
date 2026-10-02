import { describe, it, expect } from 'vitest'
import trace from '../fixtures/pico-gait-trace.json'
import {
    BodyState,
    DEFAULT_FEET,
    PicoController,
    STAND_Z,
    inverseKinematics,
    legIk,
    type Command,
    type GaitCoef,
    type Vec3
} from '../../src/lib/simulation/pico-gait'

// The trace comes from simulation/export_gait_trace.py; regenerate it there when the Python
// controller changes, and this port must follow.
const TOLERANCE = 1e-9
const maxDifference = (a: number[], b: number[]) =>
    Math.max(...a.map((value, i) => Math.abs(value - b[i])))

describe('Pico gait port', () => {
    it('derives the same stance as the Python controller', () => {
        expect(maxDifference(DEFAULT_FEET.flat(), trace.default_feet.flat())).toBeLessThan(
            TOLERANCE
        )
        expect(Math.abs(STAND_Z - trace.stand_z)).toBeLessThan(TOLERANCE)
    })

    it('reproduces every tick of the Python gait: feet and joint targets', () => {
        const controller = new PicoController(trace.gait_coef as GaitCoef)
        for (const [index, tick] of trace.ticks.entries()) {
            const angles = controller.tick(tick.cmd as Command)
            const where = `tick ${index} (${tick.segment})`
            expect(Math.abs(controller.phase - tick.phase), where).toBeLessThan(TOLERANCE)
            expect(
                maxDifference(controller.body.feet.flat(), tick.feet.flat()),
                where
            ).toBeLessThan(TOLERANCE)
            expect(maxDifference(angles, tick.angles), where).toBeLessThan(TOLERANCE)
        }
    })

    it('shifts the feet for a body pose as the Python IK does', () => {
        const angles = inverseKinematics(new BodyState(trace.pose.body))
        expect(maxDifference(angles, trace.pose.angles)).toBeLessThan(TOLERANCE)
    })

    it('clamps an unreachable target the same way', () => {
        const { leg, target, angles } = trace.unreachable
        expect(maxDifference(legIk(leg as 'fr', target as Vec3), angles)).toBeLessThan(TOLERANCE)
    })
})
